#include "waloudb/storage/BPlusTree.h"
#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/Page.h"
#include "waloudb/storage/SlottedPage.h"
#include <cstdint>

namespace WalouDB {
BPlusTree::BPlusTree(BufferPoolManager *bpm) : m_bpm(bpm) {
  page_id_t root_page_id;
  Page *page = bpm->newPage(&root_page_id);
  LeafNode root(page->getData());
  root.Init(root_page_id, INVALID_PAGE_ID);
  bpm->unpinPage(root_page_id, true);
  m_root_page_id = root_page_id;
};

BPlusTree::BPlusTree(BufferPoolManager *bpm, page_id_t root_page_id)
    : m_bpm(bpm), m_root_page_id(root_page_id) {}

bool BPlusTree::Search(uint32_t key, RID *out_rid) const {
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
      page_id_t next_id = internal.findLeaf(key);
      m_bpm->unpinPage(current_id, false);
      current_id = next_id;
    } else {
      return false;
    }
  }
}

} // namespace WalouDB
