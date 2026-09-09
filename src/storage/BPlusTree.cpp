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
      bool inserted = leaf.insertEntry(entry);
      m_bpm->unpinPage(current_id, inserted);
      return inserted;
    } else if (h->page_type == NodeType::INTERNAL) {
      InternalNode internal(page->getData());

      m_bpm->unpinPage(current_id, false);

      page_id_t next_id = internal.findChild(key);
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
bool BPlusTree::split(page_id_t page_id, Entry &entry) {
  Page *page = m_bpm->fetchPage(page_id);
  if (page == nullptr) {
    return false;
  }

  NodeHeader *h = reinterpret_cast<NodeHeader *>(page->getData());

  if (h->page_type != NodeType::LEAF) {
    m_bpm->unpinPage(page_id, false);
    return false;
  }

  LeafNode old_leaf(page->getData());

  page_id_t parent_id = old_leaf.getParentId();

  // Get all existing entries and add the new one.
  auto all_entries = old_leaf.getAllEntries();
  all_entries.push_back(entry);

  // Entries must be sorted before splitting.
  // This assumes 'entry' isn't necessarily inserted in order.
  std::sort(all_entries.begin(), all_entries.end(),
            [](const Entry &a, const Entry &b) { return a.key < b.key; });

  size_t mid = all_entries.size() / 2;

  std::vector<Entry> entries_left(all_entries.begin(),
                                  all_entries.begin() + mid);

  std::vector<Entry> entries_right(all_entries.begin() + mid,
                                   all_entries.end());

  page_id_t old_next = old_leaf.getNextLeafId();

  // Create right leaf.
  page_id_t right_leaf_id;
  Page *right_page = m_bpm->newPage(&right_leaf_id);

  if (right_page == nullptr) {
    m_bpm->unpinPage(page_id, false);
    return false;
  }

  LeafNode right_leaf(right_page->getData());

  right_leaf.Init(right_leaf_id, parent_id);
  right_leaf.setEntries(entries_right);
  right_leaf.setNextLeafId(old_next);

  old_leaf.setEntries(entries_left);
  old_leaf.setNextLeafId(right_leaf_id);

  uint32_t separator_key = entries_right[0].key;

  // --------------------------------------------------------
  // Root leaf split
  // --------------------------------------------------------

  if (parent_id == INVALID_PAGE_ID) {

    page_id_t new_root_id;
    Page *root_page = m_bpm->newPage(&new_root_id);

    if (root_page == nullptr) {
      m_bpm->unpinPage(right_leaf_id, false);
      m_bpm->unpinPage(page_id, false);
      return false;
    }

    InternalNode root(root_page->getData());

    root.Init(new_root_id, INVALID_PAGE_ID);

    std::vector<page_id_t> children = {old_leaf.getId(), right_leaf_id};

    std::vector<uint32_t> keys = {separator_key};

    root.setAllChildrenKeys(children, keys);

    // Both leaves now belong to the new root.
    // We need setters for parent_page_id.
    //
    // old_leaf.setParentId(new_root_id);
    // right_leaf.setParentId(new_root_id);

    m_root_page_id = new_root_id;
    old_leaf.setParentId(new_root_id);
    right_leaf.setParentId(new_root_id);

    m_bpm->unpinPage(new_root_id, true);
  } else {
    Page *parent_page = m_bpm->fetchPage(parent_id);
    if (parent_page == nullptr) {
      m_bpm->unpinPage(right_leaf_id, false);
      m_bpm->unpinPage(page_id, false);
      return false;
    }
    InternalNode parent_node(parent_page->getData());
    if (parent_node.isFull()) {

      // only handling true for now if more a new internal must be created
    }
    bool inserted = parent_node.insertChild(separator_key, right_leaf_id);
    if (!inserted) {
      m_bpm->unpinPage(parent_id, false);
      m_bpm->unpinPage(right_leaf_id, true);
      m_bpm->unpinPage(page_id, true);
      return false;
    }
    right_leaf.setParentId(parent_id);
    m_bpm->unpinPage(parent_id, true);
  }

  m_bpm->unpinPage(right_leaf_id, true);
  m_bpm->unpinPage(page_id, true);

  return true;
}

} // namespace WalouDB
