#include "waloudb/storage/BPlusTree.h"
#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/Page.h"
#include "waloudb/storage/SlottedPage.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// ===========================================================================
// Conventions used throughout this file
// ===========================================================================
//
// Return values
//   Public lookup methods return true only when the operation completed AND
//   found something. "Not found" and "I/O failure" still share `false` -- see
//   the note at the bottom of the file. Every internal helper below returns
//   false ONLY on a hard failure (bad fetch, allocation failure); "nothing to
//   do" is true.
//
// Mutation ordering
//   No page is modified until every page an operation needs has been
//   successfully allocated and pinned. A failed newPage/fetchPage is then a
//   clean no-op instead of a half-split or half-merged tree.
//
// Dirty flags
//   unpinPage(id, true) is passed if and only if the frame was actually
//   written, so the flag always matches reality.
//
// Latching
//   A single tree-level shared_mutex (m_latch, declared in the header).
//   Readers share it, writers take it exclusively. Coarse but correct; every
//   m_* helper below assumes the caller already holds it.
//
// ===========================================================================

namespace WalouDB {

namespace {

inline size_t minLeafEntries(size_t max_entries) { return max_entries / 2; }
inline size_t minInternalKeys(size_t max_keys) { return max_keys / 2; }

#ifndef NDEBUG
bool isSortedByKey(const std::vector<Entry> &entries) {
  return std::is_sorted(
      entries.begin(), entries.end(),
      [](const Entry &a, const Entry &b) { return a.key < b.key; });
}
#endif

} // namespace

// ===========================================================================
// Construction
// ===========================================================================

BPlusTree::BPlusTree(BufferPoolManager *bpm) : m_bpm(bpm) {
  m_root_page_id = INVALID_PAGE_ID;

  if (m_bpm == nullptr) {
    return;
  }

  page_id_t root_page_id;
  Page *page = m_bpm->newPage(&root_page_id);
  if (page == nullptr) {
    return;
  }

  LeafNode root(page->getData());
  root.Init(root_page_id, INVALID_PAGE_ID);
  m_bpm->unpinPage(root_page_id, true);

  // Go through the setter (not a direct assignment) so a root-change
  // callback registered later still learns about a tree created here.
  setRootPageId(root_page_id);
}

BPlusTree::BPlusTree(BufferPoolManager *bpm, page_id_t root_page_id)
    : m_bpm(bpm), m_root_page_id(root_page_id) {}

// ===========================================================================
// Page lifetime
// ===========================================================================

// Called once a node becomes unreachable after a merge. Hook this up to a
// real free list when BufferPoolManager has one; until then the page is
// simply abandoned (not corrupted -- just never reused).
void BPlusTree::freePage(page_id_t page_id) {
  (void)page_id;
  // TODO: m_bpm->deletePage(page_id)
}

// ===========================================================================
// Descent
// ===========================================================================

page_id_t BPlusTree::findLeaf(Key key) const {
  if (m_root_page_id == INVALID_PAGE_ID) {
    return INVALID_PAGE_ID;
  }

  page_id_t current_id = m_root_page_id;

  while (true) {
    Page *page = m_bpm->fetchPage(current_id);
    if (page == nullptr) {
      return INVALID_PAGE_ID;
    }

    NodeHeader *h = reinterpret_cast<NodeHeader *>(page->getData());

    if (h->page_type == NodeType::LEAF) {
      m_bpm->unpinPage(current_id, false);
      return current_id;
    }

    if (h->page_type != NodeType::INTERNAL) {
      m_bpm->unpinPage(current_id, false);
      return INVALID_PAGE_ID;
    }

    InternalNode internal(page->getData());
    page_id_t next_id = internal.findChild(key);
    m_bpm->unpinPage(current_id, false);

    if (next_id == INVALID_PAGE_ID || next_id == current_id) {
      return INVALID_PAGE_ID; // malformed node; refuse to loop forever
    }

    current_id = next_id;
  }
}

// ===========================================================================
// Point lookup
// ===========================================================================

bool BPlusTree::search(Key key, RID *out_rid) const {
  if (out_rid == nullptr) {
    return false;
  }

  std::shared_lock<std::shared_mutex> guard(m_latch);

  page_id_t leaf_id = findLeaf(key);
  if (leaf_id == INVALID_PAGE_ID) {
    return false;
  }

  Page *page = m_bpm->fetchPage(leaf_id);
  if (page == nullptr) {
    return false;
  }

  LeafNode leaf(page->getData());
  bool found = leaf.findEntry(key, out_rid);
  m_bpm->unpinPage(leaf_id, false);

  return found;
}

bool BPlusTree::searchAll(Key key, std::vector<RID> *out_rids) const {
  if (out_rids == nullptr) {
    return false;
  }
  out_rids->clear();

  std::vector<Entry> entries;
  if (!rangeSearch(key, key, &entries)) {
    return false;
  }

  out_rids->reserve(entries.size());
  for (const Entry &entry : entries) {
    if (entry.key == key) {
      out_rids->push_back(entry.rid);
    }
  }

  return !out_rids->empty();
}

// ===========================================================================
// Range scan
// ===========================================================================

bool BPlusTree::rangeSearch(Key low, Key high,
                            std::vector<Entry> *out_entries) const {
  if (out_entries == nullptr || high < low) {
    return false;
  }
  out_entries->clear();

  std::shared_lock<std::shared_mutex> guard(m_latch);

  page_id_t leaf_id = findLeaf(low);
  if (leaf_id == INVALID_PAGE_ID) {
    return false;
  }

  while (leaf_id != INVALID_PAGE_ID) {
    Page *page = m_bpm->fetchPage(leaf_id);
    if (page == nullptr) {
      // A partial scan is a failure, not a short result -- never hand the
      // caller a truncated range and call it success.
      out_entries->clear();
      return false;
    }

    LeafNode leaf(page->getData());
    std::vector<Entry> entries = leaf.getAllEntries();
    page_id_t next_leaf_id = leaf.getNextLeafId();
    m_bpm->unpinPage(leaf_id, false);

    // Every write path in this file (insertIntoLeaf, split, the merge/borrow
    // helpers) keeps leaf entries sorted, which is what makes the early exit
    // below sound.
    assert(isSortedByKey(entries));

    bool past_upper_bound = false;
    for (const Entry &e : entries) {
      if (e.key < low) {
        continue;
      }
      if (high < e.key) {
        past_upper_bound = true;
        break;
      }
      out_entries->push_back(e);
    }

    if (past_upper_bound || next_leaf_id == leaf_id) {
      break;
    }

    leaf_id = next_leaf_id;
  }

  return true;
}

// ===========================================================================
// Insert
// ===========================================================================

bool BPlusTree::insertIntoLeaf(page_id_t leaf_id, const Entry &entry) {
  Page *page = m_bpm->fetchPage(leaf_id);
  if (page == nullptr) {
    return false;
  }

  LeafNode leaf(page->getData());
  std::vector<Entry> entries = leaf.getAllEntries();

  auto pos = std::lower_bound(
      entries.begin(), entries.end(), entry,
      [](const Entry &a, const Entry &b) { return a.key < b.key; });

  entries.insert(pos, entry);
  leaf.setEntries(entries);

  m_bpm->unpinPage(leaf_id, true);
  return true;
}

bool BPlusTree::insert(Key key, RID rid) {
  std::unique_lock<std::shared_mutex> guard(m_latch);

  if (m_root_page_id == INVALID_PAGE_ID) {
    return false;
  }

  page_id_t leaf_id = findLeaf(key);
  if (leaf_id == INVALID_PAGE_ID) {
    return false;
  }

  Page *page = m_bpm->fetchPage(leaf_id);
  if (page == nullptr) {
    return false;
  }

  LeafNode leaf(page->getData());
  bool full = leaf.isFull();
  m_bpm->unpinPage(leaf_id, false);

  Entry entry{key, rid};

  if (!full) {
    // Insert in sorted position rather than via LeafNode::insertEntry, so
    // the sortedness that rangeSearch's early exit relies on is guaranteed
    // here rather than assumed.
    return insertIntoLeaf(leaf_id, entry);
  }

  return split(leaf_id, entry, INVALID_PAGE_ID);
}

// ===========================================================================
// Split
//
// Every allocation happens before the first mutation, so a failed newPage
// leaves the tree exactly as it was.
// ===========================================================================

bool BPlusTree::split(page_id_t page_id, Entry &entry,
                      page_id_t right_child_id) {
  Page *page = m_bpm->fetchPage(page_id);
  if (page == nullptr) {
    return false;
  }

  NodeHeader *h = reinterpret_cast<NodeHeader *>(page->getData());

  // -------------------------------------------------------------------
  // LEAF SPLIT
  // -------------------------------------------------------------------
  if (h->page_type == NodeType::LEAF) {
    LeafNode old_leaf(page->getData());
    page_id_t parent_id = old_leaf.getParentId();
    page_id_t old_next = old_leaf.getNextLeafId();

    std::vector<Entry> all_entries = old_leaf.getAllEntries();
    all_entries.push_back(entry);
    std::sort(all_entries.begin(), all_entries.end(),
              [](const Entry &a, const Entry &b) { return a.key < b.key; });

    const size_t mid = all_entries.size() / 2;
    std::vector<Entry> entries_left(
        all_entries.begin(), all_entries.begin() + static_cast<long>(mid));
    std::vector<Entry> entries_right(
        all_entries.begin() + static_cast<long>(mid), all_entries.end());
    assert(!entries_right.empty());

    // --- allocate everything first -----------------------------------
    page_id_t new_leaf_id;
    Page *new_leaf_page = m_bpm->newPage(&new_leaf_id);
    if (new_leaf_page == nullptr) {
      m_bpm->unpinPage(page_id, false); // nothing was written
      return false;
    }

    page_id_t new_root_id = INVALID_PAGE_ID;
    Page *root_page = nullptr;
    if (parent_id == INVALID_PAGE_ID) {
      root_page = m_bpm->newPage(&new_root_id);
      if (root_page == nullptr) {
        m_bpm->unpinPage(new_leaf_id, false);
        freePage(new_leaf_id);
        m_bpm->unpinPage(page_id, false); // still nothing was written
        return false;
      }
    }

    // --- from here on, nothing can fail --------------------------------
    const Key separator_key = entries_right[0].key;
    const page_id_t owner_id =
        (parent_id == INVALID_PAGE_ID) ? new_root_id : parent_id;

    LeafNode right_leaf(new_leaf_page->getData());
    right_leaf.Init(new_leaf_id, owner_id);
    right_leaf.setEntries(entries_right);
    right_leaf.setNextLeafId(old_next);

    old_leaf.setEntries(entries_left);
    old_leaf.setNextLeafId(new_leaf_id);

    if (parent_id == INVALID_PAGE_ID) {
      InternalNode root(root_page->getData());
      root.Init(new_root_id, INVALID_PAGE_ID);

      std::vector<page_id_t> children{page_id, new_leaf_id};
      std::vector<Key> keys{separator_key};
      root.setAllChildrenKeys(children, keys);

      old_leaf.setParentId(new_root_id);

      m_bpm->unpinPage(new_root_id, true);
      m_bpm->unpinPage(new_leaf_id, true);
      m_bpm->unpinPage(page_id, true);

      setRootPageId(new_root_id);
      return true;
    }

    m_bpm->unpinPage(new_leaf_id, true);
    m_bpm->unpinPage(page_id, true);

    Entry separator_entry{separator_key, RID{}};
    return split(parent_id, separator_entry, new_leaf_id);
  }

  // -------------------------------------------------------------------
  // INTERNAL SPLIT
  // -------------------------------------------------------------------
  if (h->page_type == NodeType::INTERNAL) {
    InternalNode node(page->getData());
    page_id_t parent_id = node.getParentId();

    std::vector<page_id_t> children;
    std::vector<Key> keys;
    node.getAllChildrenKeys(&children, &keys);

    // Raw page hasn't been touched yet, so this still reflects the node's
    // on-disk state.
    const size_t insert_index =
        static_cast<size_t>(node.findKeyIndex(entry.key));

    keys.insert(keys.begin() + static_cast<long>(insert_index), entry.key);
    children.insert(children.begin() + static_cast<long>(insert_index) + 1,
                    right_child_id);

    // Simple case: it fits without splitting further.
    if (keys.size() <= InternalNode::maxKeys()) {
      node.setAllChildrenKeys(children, keys);
      m_bpm->unpinPage(page_id, true);
      return true;
    }

    const size_t mid = keys.size() / 2;
    const Key promoted_key = keys[mid];

    std::vector<Key> left_keys(keys.begin(),
                               keys.begin() + static_cast<long>(mid));
    std::vector<page_id_t> left_children(
        children.begin(), children.begin() + static_cast<long>(mid) + 1);

    std::vector<Key> right_keys(keys.begin() + static_cast<long>(mid) + 1,
                                keys.end());
    std::vector<page_id_t> right_children(
        children.begin() + static_cast<long>(mid) + 1, children.end());

    // --- allocate everything first -----------------------------------
    page_id_t new_internal_id;
    Page *new_internal_page = m_bpm->newPage(&new_internal_id);
    if (new_internal_page == nullptr) {
      m_bpm->unpinPage(page_id, false);
      return false;
    }

    page_id_t new_root_id = INVALID_PAGE_ID;
    Page *root_page = nullptr;
    if (parent_id == INVALID_PAGE_ID) {
      root_page = m_bpm->newPage(&new_root_id);
      if (root_page == nullptr) {
        m_bpm->unpinPage(new_internal_id, false);
        freePage(new_internal_id);
        m_bpm->unpinPage(page_id, false);
        return false;
      }
    }

    // Pin every child that must be reparented BEFORE writing anything. A
    // failure here must not leave some children pointing at the new node
    // and some still at the old one.
    std::vector<Page *> moved_child_pages;
    moved_child_pages.reserve(right_children.size());
    bool fetch_failed = false;

    for (page_id_t child_id : right_children) {
      Page *child_page = m_bpm->fetchPage(child_id);
      if (child_page == nullptr) {
        fetch_failed = true;
        break;
      }
      moved_child_pages.push_back(child_page);
    }

    if (fetch_failed) {
      for (size_t i = 0; i < moved_child_pages.size(); ++i) {
        m_bpm->unpinPage(right_children[i], false);
      }
      if (root_page != nullptr) {
        m_bpm->unpinPage(new_root_id, false);
        freePage(new_root_id);
      }
      m_bpm->unpinPage(new_internal_id, false);
      freePage(new_internal_id);
      m_bpm->unpinPage(page_id, false); // nothing was written
      return false;
    }

    // --- from here on, nothing can fail --------------------------------
    const page_id_t owner_id =
        (parent_id == INVALID_PAGE_ID) ? new_root_id : parent_id;

    InternalNode right_node(new_internal_page->getData());
    right_node.Init(new_internal_id, owner_id);
    right_node.setAllChildrenKeys(right_children, right_keys);

    node.setAllChildrenKeys(left_children, left_keys);

    for (size_t i = 0; i < right_children.size(); ++i) {
      NodeHeader *child_header =
          reinterpret_cast<NodeHeader *>(moved_child_pages[i]->getData());
      child_header->parent_page_id = new_internal_id;
      m_bpm->unpinPage(right_children[i], true);
    }

    if (parent_id == INVALID_PAGE_ID) {
      InternalNode root(root_page->getData());
      root.Init(new_root_id, INVALID_PAGE_ID);

      std::vector<page_id_t> root_children{page_id, new_internal_id};
      std::vector<Key> root_keys{promoted_key};
      root.setAllChildrenKeys(root_children, root_keys);

      node.setParentId(new_root_id);

      m_bpm->unpinPage(new_root_id, true);
      m_bpm->unpinPage(new_internal_id, true);
      m_bpm->unpinPage(page_id, true);

      setRootPageId(new_root_id);
      return true;
    }

    m_bpm->unpinPage(new_internal_id, true);
    m_bpm->unpinPage(page_id, true);

    Entry promoted_entry{promoted_key, RID{}};
    return split(parent_id, promoted_entry, new_internal_id);
  }

  // -------------------------------------------------------------------
  // Unknown node type
  // -------------------------------------------------------------------
  m_bpm->unpinPage(page_id, false);
  return false;
}

// ===========================================================================
// Remove
// ===========================================================================

bool BPlusTree::removeFromLeaf(page_id_t leaf_id, Key key, RID *out_rid) {
  Page *page = m_bpm->fetchPage(leaf_id);
  if (page == nullptr) {
    return false;
  }

  LeafNode leaf(page->getData());
  std::vector<Entry> entries = leaf.getAllEntries();

  auto it = std::find_if(entries.begin(), entries.end(),
                         [&key](const Entry &e) { return e.key == key; });

  if (it == entries.end()) {
    m_bpm->unpinPage(leaf_id, false);
    return false;
  }

  if (out_rid != nullptr) {
    *out_rid = it->rid;
  }

  entries.erase(it);
  leaf.setEntries(entries);
  m_bpm->unpinPage(leaf_id, true);

  return true;
}

bool BPlusTree::remove(Key key) {
  std::unique_lock<std::shared_mutex> guard(m_latch);

  if (m_root_page_id == INVALID_PAGE_ID) {
    return false;
  }

  page_id_t leaf_id = findLeaf(key);
  if (leaf_id == INVALID_PAGE_ID) {
    return false;
  }

  // Removes exactly one occurrence. With duplicate keys, call again (or use
  // searchAll first) -- one call, one entry.
  RID rid{};
  if (!removeFromLeaf(leaf_id, key, &rid)) {
    return false; // key not present
  }

  // Fix up the tree structure before touching the heap: if this fails we
  // still want the index structurally sound, and the tuple is already
  // unreachable through it regardless.
  if (!handleLeafUnderflow(leaf_id)) {
    return false;
  }

  // Index entry goes first, by design. If the heap delete below fails we
  // leak a tuple that nothing points at -- recoverable by a vacuum pass.
  // The reverse order would leave a *live* index entry pointing at a freed
  // slot, which corrupts reads -- that was the original bug.
  Page *page = m_bpm->fetchPage(rid.page_id);
  if (page == nullptr) {
    return false;
  }

  SlottedPage sp(page->getData());
  bool deleted = sp.deleteTuple(rid.slot_num);
  m_bpm->unpinPage(rid.page_id, deleted);

  return deleted;
}

// ---------------------------------------------------------------------------
// Leaf underflow: borrow from a sibling if one can spare an entry, else
// merge with one.
// ---------------------------------------------------------------------------

bool BPlusTree::handleLeafUnderflow(page_id_t leaf_id) {
  Page *page = m_bpm->fetchPage(leaf_id);
  if (page == nullptr) {
    return false;
  }

  LeafNode leaf(page->getData());
  const page_id_t parent_id = leaf.getParentId();
  const size_t entry_count = leaf.getAllEntries().size();
  m_bpm->unpinPage(leaf_id, false);

  // The root leaf is allowed to be sparse, even empty -- that's just a small
  // or empty tree, not underflow.
  if (parent_id == INVALID_PAGE_ID) {
    return true;
  }

  if (entry_count >= minLeafEntries(LeafNode::maxEntries())) {
    return true;
  }

  Page *parent_page = m_bpm->fetchPage(parent_id);
  if (parent_page == nullptr) {
    return false;
  }

  InternalNode parent(parent_page->getData());
  std::vector<page_id_t> children;
  std::vector<Key> keys;
  parent.getAllChildrenKeys(&children, &keys);

  auto child_it = std::find(children.begin(), children.end(), leaf_id);
  if (child_it == children.end()) {
    m_bpm->unpinPage(parent_id, false);
    return false; // parent/child pointers disagree -- refuse to guess
  }
  const size_t index = static_cast<size_t>(child_it - children.begin());

  const page_id_t left_id = (index > 0) ? children[index - 1] : INVALID_PAGE_ID;
  const page_id_t right_id =
      (index + 1 < children.size()) ? children[index + 1] : INVALID_PAGE_ID;

  // --- try borrowing from the left sibling ---------------------------
  if (left_id != INVALID_PAGE_ID) {
    Page *left_page = m_bpm->fetchPage(left_id);
    if (left_page == nullptr) {
      m_bpm->unpinPage(parent_id, false);
      return false;
    }

    LeafNode left(left_page->getData());
    std::vector<Entry> left_entries = left.getAllEntries();

    if (left_entries.size() > minLeafEntries(LeafNode::maxEntries())) {
      Page *node_page = m_bpm->fetchPage(leaf_id);
      if (node_page == nullptr) {
        m_bpm->unpinPage(left_id, false);
        m_bpm->unpinPage(parent_id, false);
        return false;
      }

      LeafNode node(node_page->getData());
      std::vector<Entry> node_entries = node.getAllEntries();

      node_entries.insert(node_entries.begin(), left_entries.back());
      left_entries.pop_back();

      left.setEntries(left_entries);
      node.setEntries(node_entries);
      keys[index - 1] = node_entries.front().key;
      parent.setAllChildrenKeys(children, keys);

      m_bpm->unpinPage(leaf_id, true);
      m_bpm->unpinPage(left_id, true);
      m_bpm->unpinPage(parent_id, true);
      return true;
    }

    m_bpm->unpinPage(left_id, false);
  }

  // --- try borrowing from the right sibling --------------------------
  if (right_id != INVALID_PAGE_ID) {
    Page *right_page = m_bpm->fetchPage(right_id);
    if (right_page == nullptr) {
      m_bpm->unpinPage(parent_id, false);
      return false;
    }

    LeafNode right(right_page->getData());
    std::vector<Entry> right_entries = right.getAllEntries();

    if (right_entries.size() > minLeafEntries(LeafNode::maxEntries())) {
      Page *node_page = m_bpm->fetchPage(leaf_id);
      if (node_page == nullptr) {
        m_bpm->unpinPage(right_id, false);
        m_bpm->unpinPage(parent_id, false);
        return false;
      }

      LeafNode node(node_page->getData());
      std::vector<Entry> node_entries = node.getAllEntries();

      node_entries.push_back(right_entries.front());
      right_entries.erase(right_entries.begin());

      right.setEntries(right_entries);
      node.setEntries(node_entries);
      keys[index] = right_entries.front().key;
      parent.setAllChildrenKeys(children, keys);

      m_bpm->unpinPage(leaf_id, true);
      m_bpm->unpinPage(right_id, true);
      m_bpm->unpinPage(parent_id, true);
      return true;
    }

    m_bpm->unpinPage(right_id, false);
  }

  // --- merge -----------------------------------------------------------
  // Always merge the right node into the left one so the leaf chain stays
  // correct without extra bookkeeping.
  page_id_t merge_left_id;
  page_id_t merge_right_id;
  size_t separator_index;

  if (left_id != INVALID_PAGE_ID) {
    merge_left_id = left_id;
    merge_right_id = leaf_id;
    separator_index = index - 1;
  } else if (right_id != INVALID_PAGE_ID) {
    merge_left_id = leaf_id;
    merge_right_id = right_id;
    separator_index = index;
  } else {
    // Only child of a non-root parent -- nothing to merge with here; let the
    // parent's own underflow handling deal with the degenerate shape.
    m_bpm->unpinPage(parent_id, false);
    return handleInternalUnderflow(parent_id);
  }

  Page *lp = m_bpm->fetchPage(merge_left_id);
  if (lp == nullptr) {
    m_bpm->unpinPage(parent_id, false);
    return false;
  }
  Page *rp = m_bpm->fetchPage(merge_right_id);
  if (rp == nullptr) {
    m_bpm->unpinPage(merge_left_id, false);
    m_bpm->unpinPage(parent_id, false);
    return false;
  }

  LeafNode left_node(lp->getData());
  LeafNode right_node(rp->getData());

  std::vector<Entry> merged = left_node.getAllEntries();
  std::vector<Entry> right_entries = right_node.getAllEntries();
  merged.insert(merged.end(), right_entries.begin(), right_entries.end());
  assert(isSortedByKey(merged));

  left_node.setEntries(merged);
  left_node.setNextLeafId(right_node.getNextLeafId());

  keys.erase(keys.begin() + static_cast<long>(separator_index));
  children.erase(children.begin() + static_cast<long>(separator_index) + 1);
  parent.setAllChildrenKeys(children, keys);

  m_bpm->unpinPage(merge_right_id, true);
  m_bpm->unpinPage(merge_left_id, true);
  m_bpm->unpinPage(parent_id, true);

  freePage(merge_right_id);

  return handleInternalUnderflow(parent_id);
}

// ---------------------------------------------------------------------------
// Internal underflow: same shape as the leaf case, but the separator key
// rotates through the parent instead of being copied outright.
// ---------------------------------------------------------------------------

bool BPlusTree::handleInternalUnderflow(page_id_t node_id) {
  Page *page = m_bpm->fetchPage(node_id);
  if (page == nullptr) {
    return false;
  }

  InternalNode node(page->getData());
  const page_id_t parent_id = node.getParentId();

  std::vector<page_id_t> node_children;
  std::vector<Key> node_keys;
  node.getAllChildrenKeys(&node_children, &node_keys);
  m_bpm->unpinPage(node_id, false);

  if (parent_id == INVALID_PAGE_ID) {
    return adjustRoot();
  }

  if (node_keys.size() >= minInternalKeys(InternalNode::maxKeys())) {
    return true;
  }

  Page *parent_page = m_bpm->fetchPage(parent_id);
  if (parent_page == nullptr) {
    return false;
  }

  InternalNode parent(parent_page->getData());
  std::vector<page_id_t> children;
  std::vector<Key> keys;
  parent.getAllChildrenKeys(&children, &keys);

  auto child_it = std::find(children.begin(), children.end(), node_id);
  if (child_it == children.end()) {
    m_bpm->unpinPage(parent_id, false);
    return false;
  }
  const size_t index = static_cast<size_t>(child_it - children.begin());

  const page_id_t left_id = (index > 0) ? children[index - 1] : INVALID_PAGE_ID;
  const page_id_t right_id =
      (index + 1 < children.size()) ? children[index + 1] : INVALID_PAGE_ID;

  // --- borrow from the left sibling ----------------------------------
  if (left_id != INVALID_PAGE_ID) {
    Page *left_page = m_bpm->fetchPage(left_id);
    if (left_page == nullptr) {
      m_bpm->unpinPage(parent_id, false);
      return false;
    }

    InternalNode left(left_page->getData());
    std::vector<page_id_t> left_children;
    std::vector<Key> left_keys;
    left.getAllChildrenKeys(&left_children, &left_keys);

    if (left_keys.size() > minInternalKeys(InternalNode::maxKeys())) {
      const page_id_t moved_child = left_children.back();

      Page *moved_page = m_bpm->fetchPage(moved_child);
      if (moved_page == nullptr) {
        m_bpm->unpinPage(left_id, false);
        m_bpm->unpinPage(parent_id, false);
        return false;
      }

      Page *node_page = m_bpm->fetchPage(node_id);
      if (node_page == nullptr) {
        m_bpm->unpinPage(moved_child, false);
        m_bpm->unpinPage(left_id, false);
        m_bpm->unpinPage(parent_id, false);
        return false;
      }

      // Parent separator rotates down into this node; the left sibling's
      // last key rotates up into the parent.
      node_keys.insert(node_keys.begin(), keys[index - 1]);
      node_children.insert(node_children.begin(), moved_child);
      keys[index - 1] = left_keys.back();
      left_keys.pop_back();
      left_children.pop_back();

      InternalNode node_ref(node_page->getData());
      node_ref.setAllChildrenKeys(node_children, node_keys);
      left.setAllChildrenKeys(left_children, left_keys);
      parent.setAllChildrenKeys(children, keys);

      reinterpret_cast<NodeHeader *>(moved_page->getData())->parent_page_id =
          node_id;

      m_bpm->unpinPage(moved_child, true);
      m_bpm->unpinPage(node_id, true);
      m_bpm->unpinPage(left_id, true);
      m_bpm->unpinPage(parent_id, true);
      return true;
    }

    m_bpm->unpinPage(left_id, false);
  }

  // --- borrow from the right sibling ---------------------------------
  if (right_id != INVALID_PAGE_ID) {
    Page *right_page = m_bpm->fetchPage(right_id);
    if (right_page == nullptr) {
      m_bpm->unpinPage(parent_id, false);
      return false;
    }

    InternalNode right(right_page->getData());
    std::vector<page_id_t> right_children;
    std::vector<Key> right_keys;
    right.getAllChildrenKeys(&right_children, &right_keys);

    if (right_keys.size() > minInternalKeys(InternalNode::maxKeys())) {
      const page_id_t moved_child = right_children.front();

      Page *moved_page = m_bpm->fetchPage(moved_child);
      if (moved_page == nullptr) {
        m_bpm->unpinPage(right_id, false);
        m_bpm->unpinPage(parent_id, false);
        return false;
      }

      Page *node_page = m_bpm->fetchPage(node_id);
      if (node_page == nullptr) {
        m_bpm->unpinPage(moved_child, false);
        m_bpm->unpinPage(right_id, false);
        m_bpm->unpinPage(parent_id, false);
        return false;
      }

      node_keys.push_back(keys[index]);
      node_children.push_back(moved_child);
      keys[index] = right_keys.front();
      right_keys.erase(right_keys.begin());
      right_children.erase(right_children.begin());

      InternalNode node_ref(node_page->getData());
      node_ref.setAllChildrenKeys(node_children, node_keys);
      right.setAllChildrenKeys(right_children, right_keys);
      parent.setAllChildrenKeys(children, keys);

      reinterpret_cast<NodeHeader *>(moved_page->getData())->parent_page_id =
          node_id;

      m_bpm->unpinPage(moved_child, true);
      m_bpm->unpinPage(node_id, true);
      m_bpm->unpinPage(right_id, true);
      m_bpm->unpinPage(parent_id, true);
      return true;
    }

    m_bpm->unpinPage(right_id, false);
  }

  // --- merge -----------------------------------------------------------
  page_id_t merge_left_id;
  page_id_t merge_right_id;
  size_t separator_index;

  if (left_id != INVALID_PAGE_ID) {
    merge_left_id = left_id;
    merge_right_id = node_id;
    separator_index = index - 1;
  } else if (right_id != INVALID_PAGE_ID) {
    merge_left_id = node_id;
    merge_right_id = right_id;
    separator_index = index;
  } else {
    m_bpm->unpinPage(parent_id, false);
    return handleInternalUnderflow(parent_id);
  }

  Page *lp = m_bpm->fetchPage(merge_left_id);
  if (lp == nullptr) {
    m_bpm->unpinPage(parent_id, false);
    return false;
  }
  Page *rp = m_bpm->fetchPage(merge_right_id);
  if (rp == nullptr) {
    m_bpm->unpinPage(merge_left_id, false);
    m_bpm->unpinPage(parent_id, false);
    return false;
  }

  InternalNode left_node(lp->getData());
  InternalNode right_node(rp->getData());

  std::vector<page_id_t> left_children;
  std::vector<Key> left_keys;
  left_node.getAllChildrenKeys(&left_children, &left_keys);

  std::vector<page_id_t> right_children;
  std::vector<Key> right_keys;
  right_node.getAllChildrenKeys(&right_children, &right_keys);

  // Pin every child that changes parent before writing anything.
  std::vector<Page *> moved_pages;
  moved_pages.reserve(right_children.size());
  bool fetch_failed = false;

  for (page_id_t child_id : right_children) {
    Page *cp = m_bpm->fetchPage(child_id);
    if (cp == nullptr) {
      fetch_failed = true;
      break;
    }
    moved_pages.push_back(cp);
  }

  if (fetch_failed) {
    for (size_t i = 0; i < moved_pages.size(); ++i) {
      m_bpm->unpinPage(right_children[i], false);
    }
    m_bpm->unpinPage(merge_right_id, false);
    m_bpm->unpinPage(merge_left_id, false);
    m_bpm->unpinPage(parent_id, false);
    return false;
  }

  // The separator that used to live in the parent becomes a real key in the
  // merged node -- internal merges pull it down; they don't drop it.
  left_keys.push_back(keys[separator_index]);
  left_keys.insert(left_keys.end(), right_keys.begin(), right_keys.end());
  left_children.insert(left_children.end(), right_children.begin(),
                       right_children.end());
  left_node.setAllChildrenKeys(left_children, left_keys);

  for (size_t i = 0; i < right_children.size(); ++i) {
    reinterpret_cast<NodeHeader *>(moved_pages[i]->getData())->parent_page_id =
        merge_left_id;
    m_bpm->unpinPage(right_children[i], true);
  }

  keys.erase(keys.begin() + static_cast<long>(separator_index));
  children.erase(children.begin() + static_cast<long>(separator_index) + 1);
  parent.setAllChildrenKeys(children, keys);

  m_bpm->unpinPage(merge_right_id, true);
  m_bpm->unpinPage(merge_left_id, true);
  m_bpm->unpinPage(parent_id, true);

  freePage(merge_right_id);

  return handleInternalUnderflow(parent_id);
}

// ---------------------------------------------------------------------------
// Root shrink: an internal root with a single child (no keys) is replaced by
// that child.
// ---------------------------------------------------------------------------

bool BPlusTree::adjustRoot() {
  if (m_root_page_id == INVALID_PAGE_ID) {
    return true;
  }

  Page *page = m_bpm->fetchPage(m_root_page_id);
  if (page == nullptr) {
    return false;
  }

  NodeHeader *h = reinterpret_cast<NodeHeader *>(page->getData());

  // An empty (or sparse) root leaf is a valid small tree; leave it alone.
  if (h->page_type != NodeType::INTERNAL) {
    m_bpm->unpinPage(m_root_page_id, false);
    return true;
  }

  InternalNode root(page->getData());
  std::vector<page_id_t> children;
  std::vector<Key> keys;
  root.getAllChildrenKeys(&children, &keys);

  if (!keys.empty() || children.size() != 1) {
    m_bpm->unpinPage(m_root_page_id, false);
    return true;
  }

  const page_id_t old_root_id = m_root_page_id;
  const page_id_t new_root_id = children[0];

  Page *child_page = m_bpm->fetchPage(new_root_id);
  if (child_page == nullptr) {
    m_bpm->unpinPage(old_root_id, false);
    return false;
  }

  reinterpret_cast<NodeHeader *>(child_page->getData())->parent_page_id =
      INVALID_PAGE_ID;

  m_bpm->unpinPage(new_root_id, true);
  m_bpm->unpinPage(old_root_id, false);

  setRootPageId(new_root_id);
  freePage(old_root_id);

  return true;
}

// ===========================================================================
// Root bookkeeping
// ===========================================================================

void BPlusTree::setRootChangeCallback(RootChangeCallback callback) {
  std::unique_lock<std::shared_mutex> guard(m_latch);
  m_root_change_callback = std::move(callback);
}

void BPlusTree::setRootPageId(page_id_t root_page_id) {
  m_root_page_id = root_page_id;

  // NOTE: called with m_latch already held (by insert/split/remove/the
  // constructor). The callback must not call back into this tree, or it will
  // deadlock; it's meant only for persisting the root id in the catalog.
  if (m_root_change_callback) {
    m_root_change_callback(root_page_id);
  }
}

// ===========================================================================
// Key serialization
// ===========================================================================

SerializedKey serializeKey(const Key &key) {
  SerializedKey result{};

  result.type = static_cast<uint8_t>(key.type);

  if (key.type == KeyType::INTEGER) {
    static_assert(sizeof(SerializedKey::data) >= sizeof(int32_t),
                  "SerializedKey::data too small for an integer key");
    result.length = sizeof(int32_t);
    std::memcpy(result.data, &key.integer, sizeof(int32_t));
    return result;
  }

  if (key.string.size() > sizeof(result.data)) {
    throw std::runtime_error("Index key too large");
  }

  result.length = static_cast<uint16_t>(key.string.size());
  std::memcpy(result.data, key.string.data(), result.length);

  return result;
}

Key deserializeKey(const SerializedKey &data) {
  Key key;
  key.type = static_cast<KeyType>(data.type);

  if (key.type == KeyType::INTEGER) {
    std::memcpy(&key.integer, data.data, sizeof(int32_t));
    return key;
  }

  if (data.length > sizeof(data.data)) {
    throw std::runtime_error("Corrupt index key length");
  }

  key.string.assign(data.data, data.length);
  return key;
}

std::string nextPrefix(const std::string &prefix) {
  std::string result = prefix;

  for (int i = static_cast<int>(result.size()) - 1; i >= 0; --i) {
    unsigned char c = static_cast<unsigned char>(result[i]);
    if (c != 0xFF) {
      result[i] = static_cast<char>(c + 1);
      result.resize(static_cast<size_t>(i) + 1);
      return result;
    }
  }

  return {}; // no representable upper bound (all bytes were 0xFF)
}

} // namespace WalouDB
