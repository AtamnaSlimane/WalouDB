#include "waloudb/storage/BPlusTree.h"
#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/Page.h"
#include "waloudb/storage/SlottedPage.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace WalouDB {
BPlusTree::BPlusTree(BufferPoolManager *bpm) : m_bpm(bpm) {
  page_id_t root_page_id;
  Page *page = bpm->newPage(&root_page_id);
  if (page == nullptr) {
    m_root_page_id = INVALID_PAGE_ID;
    return;
  }
  LeafNode root(page->getData());
  root.Init(root_page_id, INVALID_PAGE_ID);
  bpm->unpinPage(root_page_id, true);
  m_root_page_id = root_page_id;
};

BPlusTree::BPlusTree(BufferPoolManager *bpm, page_id_t root_page_id)
    : m_bpm(bpm), m_root_page_id(root_page_id) {}

bool BPlusTree::search(uint32_t key, RID *out_rid) const {
  page_id_t current_id = m_root_page_id;
  while (true) {
    Page *page = m_bpm->fetchPage(current_id);
    if (page == nullptr) {
      return false;
    }
    NodeHeader *h = reinterpret_cast<NodeHeader *>(page->getData());
    if (h->page_type == NodeType::LEAF) {
      LeafNode leaf(page->getData());
      bool found = leaf.findEntry(key, out_rid);
      m_bpm->unpinPage(current_id, false);
      return found;
    } else if (h->page_type == NodeType::INTERNAL) {
      InternalNode internal(page->getData());
      page_id_t next_id = internal.findChild(key);

      m_bpm->unpinPage(current_id, false);

      if (next_id == INVALID_PAGE_ID) {
        return false;
      }

      current_id = next_id;
    } else {
      m_bpm->unpinPage(current_id, false);
      return false;
    }
  }
}

bool BPlusTree::rangeSearch(uint32_t low, uint32_t high,
                            std::vector<Entry> *out_entries) const {
  if (out_entries == nullptr) {
    return false;
  }
  if (low > high) {
    return false;
  }
  out_entries->clear();

  page_id_t current_id = m_root_page_id;
  while (true) {
    Page *page = m_bpm->fetchPage(current_id);
    if (page == nullptr) {
      return false;
    }
    NodeHeader *h = reinterpret_cast<NodeHeader *>(page->getData());
    if (h->page_type == NodeType::LEAF) {
      m_bpm->unpinPage(current_id, false);
      break;
    } else if (h->page_type == NodeType::INTERNAL) {
      InternalNode internal(page->getData());

      page_id_t next_id = internal.findChild(low);

      m_bpm->unpinPage(current_id, false);

      if (next_id == INVALID_PAGE_ID) {
        return false;
      }

      current_id = next_id;
    } else {
      m_bpm->unpinPage(current_id, false);
      return false;
    }
  }
  page_id_t leaf_id = current_id;

  while (leaf_id != INVALID_PAGE_ID) {

    Page *page = m_bpm->fetchPage(leaf_id);

    if (page == nullptr) {
      return !out_entries->empty();
    }

    LeafNode leaf(page->getData());

    auto entries = leaf.getAllEntries();

    page_id_t next_leaf_id = leaf.getNextLeafId();

    m_bpm->unpinPage(leaf_id, false);

    // Entries within a leaf are kept sorted by key (see split()),
    // so we can stop scanning this leaf as soon as we pass high_key.

    bool exceeded_upper_bound = false;

    for (const Entry &e : entries) {

      if (e.key < low) {
        continue;
      }

      if (e.key > high) {
        exceeded_upper_bound = true;
        break;
      }

      out_entries->push_back(e);
    }

    if (exceeded_upper_bound) {
      break;
    }

    leaf_id = next_leaf_id;
  }
  return true;
};

bool BPlusTree::insert(uint32_t key, RID rid) {
  page_id_t current_id = m_root_page_id;
  while (true) {
    Page *page = m_bpm->fetchPage(current_id);
    if (page == nullptr) {
      return false;
    }
    NodeHeader *h = reinterpret_cast<NodeHeader *>(page->getData());
    if (h->page_type == NodeType::LEAF) {
      LeafNode leaf(page->getData());
      Entry entry{key, rid};
      if (!leaf.isFull()) {
        bool inserted = leaf.insertEntry(entry);
        m_bpm->unpinPage(current_id, inserted);
        return inserted;
      }
      m_bpm->unpinPage(current_id, false);
      return split(current_id, entry, INVALID_PAGE_ID);
    } else if (h->page_type == NodeType::INTERNAL) {
      InternalNode internal(page->getData());
      page_id_t next_id = internal.findChild(key);
      m_bpm->unpinPage(current_id, false);
      if (next_id == INVALID_PAGE_ID) {
        return false;
      }
      current_id = next_id;
    } else {
      m_bpm->unpinPage(current_id, false);
      return false;
    }
    // self balancing if full
  }
}
bool BPlusTree::split(page_id_t page_id, Entry &entry,
                      page_id_t right_child_id) {

  Page *page = m_bpm->fetchPage(page_id);

  if (page == nullptr) {
    return false;
  }

  NodeHeader *h = reinterpret_cast<NodeHeader *>(page->getData());

  // ============================================================
  // LEAF SPLIT
  // ============================================================

  if (h->page_type == NodeType::LEAF) {

    LeafNode old_leaf(page->getData());

    page_id_t parent_id = old_leaf.getParentId();

    // ----------------------------------------------------------
    // Collect entries + new entry
    // ----------------------------------------------------------

    auto all_entries = old_leaf.getAllEntries();

    all_entries.push_back(entry);

    std::sort(all_entries.begin(), all_entries.end(),
              [](const Entry &a, const Entry &b) { return a.key < b.key; });

    size_t mid = all_entries.size() / 2;

    std::vector<Entry> entries_left(all_entries.begin(),
                                    all_entries.begin() + mid);

    std::vector<Entry> entries_right(all_entries.begin() + mid,
                                     all_entries.end());

    // ----------------------------------------------------------
    // Remember old leaf linkage
    // ----------------------------------------------------------

    page_id_t old_next = old_leaf.getNextLeafId();

    // ----------------------------------------------------------
    // Create right leaf
    // ----------------------------------------------------------

    page_id_t new_leaf_id;

    Page *new_leaf_page = m_bpm->newPage(&new_leaf_id);

    if (new_leaf_page == nullptr) {
      m_bpm->unpinPage(page_id, false);
      return false;
    }

    LeafNode right_leaf(new_leaf_page->getData());

    right_leaf.Init(new_leaf_id, parent_id);

    right_leaf.setEntries(entries_right);

    right_leaf.setNextLeafId(old_next);

    // ----------------------------------------------------------
    // Rewrite old leaf
    // ----------------------------------------------------------

    old_leaf.setEntries(entries_left);

    old_leaf.setNextLeafId(new_leaf_id);

    // Separator goes to parent.
    uint32_t separator_key = entries_right[0].key;

    Entry separator_entry{separator_key, RID{}};

    // ============================================================
    // ROOT LEAF SPLIT
    // ============================================================

    if (parent_id == INVALID_PAGE_ID) {

      page_id_t new_root_id;

      Page *root_page = m_bpm->newPage(&new_root_id);

      if (root_page == nullptr) {
        m_bpm->unpinPage(new_leaf_id, false);

        m_bpm->unpinPage(page_id, false);

        return false;
      }

      InternalNode root(root_page->getData());

      root.Init(new_root_id, INVALID_PAGE_ID);

      std::vector<page_id_t> children{page_id, new_leaf_id};

      std::vector<uint32_t> keys{separator_key};

      root.setAllChildrenKeys(children, keys);

      // Both leaves now belong to new root.
      old_leaf.setParentId(new_root_id);

      right_leaf.setParentId(new_root_id);

      m_bpm->unpinPage(new_root_id, true);

      m_bpm->unpinPage(new_leaf_id, true);

      m_bpm->unpinPage(page_id, true);

      setRootPageId(new_root_id);
      return true;
    }

    // ============================================================
    // LEAF HAS EXISTING PARENT
    // ============================================================

    // The new right leaf belongs to the same parent.
    right_leaf.setParentId(parent_id);

    m_bpm->unpinPage(new_leaf_id, true);

    m_bpm->unpinPage(page_id, true);

    /*
     * Propagate:
     *
     * separator_key
     * new_leaf_id
     *
     * into the parent.
     */
    return split(parent_id, separator_entry, new_leaf_id);
  }

  // ============================================================
  // INTERNAL NODE SPLIT
  // ============================================================

  if (h->page_type == NodeType::INTERNAL) {

    InternalNode node(page->getData());

    page_id_t parent_id = node.getParentId();

    // ----------------------------------------------------------
    // Get current internal node contents
    // ----------------------------------------------------------
    if (!node.isFull()) {
      std::vector<page_id_t> children;
      std::vector<uint32_t> keys;

      node.getAllChildrenKeys(&children, &keys);

      int insert_index = node.findKeyIndex(entry.key);

      keys.insert(keys.begin() + insert_index, entry.key);
      children.insert(children.begin() + insert_index + 1, right_child_id);

      node.setAllChildrenKeys(children, keys);

      m_bpm->unpinPage(page_id, true);

      return true;
    }
    std::vector<page_id_t> children;
    std::vector<uint32_t> keys;

    node.getAllChildrenKeys(&children, &keys);

    /*
     * entry.key is the separator that needs to be inserted.
     *
     * right_child_id is the new child created by the split
     * below this node.
     */

    int insert_index = node.findKeyIndex(entry.key);

    keys.insert(keys.begin() + insert_index, entry.key);

    children.insert(children.begin() + insert_index + 1, right_child_id);

    // ----------------------------------------------------------
    // Split around middle key
    // ----------------------------------------------------------

    size_t mid = keys.size() / 2;

    uint32_t promoted_key = keys[mid];

    // Left node
    std::vector<uint32_t> left_keys(keys.begin(), keys.begin() + mid);

    std::vector<page_id_t> left_children(children.begin(),
                                         children.begin() + mid + 1);

    // Right node
    std::vector<uint32_t> right_keys(keys.begin() + mid + 1, keys.end());

    std::vector<page_id_t> right_children(children.begin() + mid + 1,
                                          children.end());

    // ----------------------------------------------------------
    // Rewrite current node as LEFT
    // ----------------------------------------------------------

    node.setAllChildrenKeys(left_children, left_keys);

    // ----------------------------------------------------------
    // Create RIGHT internal node
    // ----------------------------------------------------------

    page_id_t new_internal_id;

    Page *new_internal_page = m_bpm->newPage(&new_internal_id);

    if (new_internal_page == nullptr) {
      m_bpm->unpinPage(page_id, false);

      return false;
    }

    InternalNode right_node(new_internal_page->getData());

    right_node.Init(new_internal_id, parent_id);

    right_node.setAllChildrenKeys(right_children, right_keys);

    // ----------------------------------------------------------
    // Update parent IDs of children moved to RIGHT
    // ----------------------------------------------------------

    for (page_id_t child_id : right_children) {

      Page *child_page = m_bpm->fetchPage(child_id);

      if (child_page == nullptr) {
        m_bpm->unpinPage(new_internal_id, false);

        m_bpm->unpinPage(page_id, false);

        return false;
      }

      NodeHeader *child_header =
          reinterpret_cast<NodeHeader *>(child_page->getData());

      child_header->parent_page_id = new_internal_id;

      m_bpm->unpinPage(child_id, true);
    }

    // ============================================================
    // INTERNAL NODE WAS ROOT
    // ============================================================

    if (parent_id == INVALID_PAGE_ID) {

      page_id_t new_root_id;

      Page *root_page = m_bpm->newPage(&new_root_id);

      if (root_page == nullptr) {
        m_bpm->unpinPage(new_internal_id, false);

        m_bpm->unpinPage(page_id, false);

        return false;
      }

      InternalNode root(root_page->getData());

      root.Init(new_root_id, INVALID_PAGE_ID);

      std::vector<page_id_t> root_children{page_id, new_internal_id};

      std::vector<uint32_t> root_keys{promoted_key};

      root.setAllChildrenKeys(root_children, root_keys);

      // Both internal nodes now belong to new root.
      node.setParentId(new_root_id);

      right_node.setParentId(new_root_id);

      m_bpm->unpinPage(new_root_id, true);

      m_bpm->unpinPage(new_internal_id, true);

      m_bpm->unpinPage(page_id, true);

      setRootPageId(new_root_id);
      return true;
    }

    // ============================================================
    // INTERNAL NODE HAS EXISTING PARENT
    // ============================================================

    right_node.setParentId(parent_id);

    m_bpm->unpinPage(new_internal_id, true);

    m_bpm->unpinPage(page_id, true);

    /*
     * Propagate the promoted key and new right node
     * to the parent.
     *
     * This is the important recursive part.
     */
    Entry promoted_entry{promoted_key, RID{}};

    return split(parent_id, promoted_entry, new_internal_id);
  }

  // ============================================================
  // INVALID NODE TYPE
  // ============================================================
  m_bpm->unpinPage(page_id, false);

  return false;
}
void BPlusTree::setRootChangeCallback(RootChangeCallback callback) {
  m_root_change_callback = std::move(callback);
}
void BPlusTree::setRootPageId(page_id_t root_page_id) {
  m_root_page_id = root_page_id;

  if (m_root_change_callback) {
    m_root_change_callback(root_page_id);
  }
}
} // namespace WalouDB
