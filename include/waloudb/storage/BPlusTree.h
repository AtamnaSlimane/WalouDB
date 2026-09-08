#pragma once

#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/SlottedPage.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
namespace WalouDB {

enum class NodeType : uint8_t { INVALID = 0, LEAF, INTERNAL };

struct NodeHeader {
  NodeType page_type;
  uint16_t key_count{0};    // how many keys currently stored
  page_id_t parent_page_id; // INVALID_PAGE_ID for the root
  page_id_t page_id; // this page's own id — same self-check idea as PageHeader
};

struct Entry {
  uint32_t key;
  RID rid{};
};
class LeafNode {
public:
  explicit LeafNode(char *raw_data) : m_data(raw_data) {}
  // for ram
  void Init(page_id_t page_id, page_id_t parent_page_id) {
    NodeHeader *h = getHeader();
    h->page_id = page_id;
    h->parent_page_id = parent_page_id;
    h->page_type = NodeType::LEAF;
    h->key_count = 0;
    getLeafHeader()->next_leaf_page_id = INVALID_PAGE_ID;
  };
  bool findEntry(uint32_t key, RID *out_rid) {
    int idx = findKeyIndex(key);
    if (idx >= 0 && getEntry(idx)->key == key) {
      *out_rid = getEntry(idx)->rid;
      return true;
    }
    return false;
  }

  page_id_t getNextLeafId() { return getLeafHeader()->next_leaf_page_id; }
  void setNextLeafId(page_id_t id) { getLeafHeader()->next_leaf_page_id = id; }

  bool isFull() { return getHeader()->key_count >= maxEntries(); }
  page_id_t getId() { return getHeader()->page_id; }
  page_id_t getParentId() { return getHeader()->parent_page_id; }
  uint16_t getKeyCount() { return getHeader()->key_count; }

private:
  // linked list style leaves
  struct LeafHeader : NodeHeader {
    page_id_t next_leaf_page_id;
  };

  char *m_data;

  NodeHeader *getHeader() { return reinterpret_cast<NodeHeader *>(m_data); }
  const NodeHeader *getHeader() const {
    return reinterpret_cast<const NodeHeader *>(m_data);
  }

  LeafHeader *getLeafHeader() { return reinterpret_cast<LeafHeader *>(m_data); }
  const LeafHeader *getLeafHeader() const {
    return reinterpret_cast<const LeafHeader *>(m_data);
  }

  Entry *getEntry(int idx) {
    return reinterpret_cast<Entry *>(m_data + sizeof(LeafHeader) +
                                     idx * sizeof(Entry));
  }
  const Entry *getEntry(int idx) const {
    return reinterpret_cast<const Entry *>(m_data + sizeof(LeafHeader) +
                                           idx * sizeof(Entry));
  }

  int findKeyIndex(uint32_t key) {
    int left = 0;
    int right = getHeader()->key_count;
    while (left < right) {
      int mid = (left + right) / 2;
      if (getEntry(mid)->key < key) {
        left = mid + 1;
      } else {
        right = mid;
      }
    }
    return left;
  }

  static constexpr size_t maxEntries() {
    return (PAGE_SIZE - sizeof(LeafHeader)) / sizeof(Entry);
  }
};

class InternalNode {
public:
  explicit InternalNode(char *raw_data) : m_data(raw_data) {}
  void Init(page_id_t page_id, page_id_t parent_page_id) {
    NodeHeader *h = getHeader();
    h->page_id = page_id;
    h->parent_page_id = parent_page_id;
    h->page_type = NodeType::INTERNAL;
    h->key_count = 0;
  }
  page_id_t findLeaf(uint32_t key) {
    auto index = findFirstGreater(key);
    return *getChild(index);
  }

  bool isFull() { return getHeader()->key_count >= maxKeys(); }
  page_id_t getId() { return getHeader()->page_id; }
  page_id_t getParentId() { return getHeader()->parent_page_id; }
  uint16_t getKeyCount() { return getHeader()->key_count; }

private:
  struct InternalHeader : NodeHeader {
    page_id_t first_child_page_id;
  };

  char *m_data;

  NodeHeader *getHeader() { return reinterpret_cast<NodeHeader *>(m_data); }
  const NodeHeader *getHeader() const {
    return reinterpret_cast<const NodeHeader *>(m_data);
  }

  InternalHeader *getInternallHeader() {
    return reinterpret_cast<InternalHeader *>(m_data);
  }
  const InternalHeader *getInternalHeader() const {
    return reinterpret_cast<const InternalHeader *>(m_data);
  }

  uint32_t *getKey(int idx) {
    return reinterpret_cast<uint32_t *>(m_data + sizeof(InternalHeader) +
                                        (sizeof(page_id_t) + sizeof(uint32_t)) *
                                            idx);
  }

  page_id_t *getFirstChild() {
    return reinterpret_cast<page_id_t *>(m_data + sizeof(NodeHeader));
  }

  page_id_t *getChild(int idx) {
    if (idx == 0) {
      return getFirstChild();
    }
    char *data = m_data + sizeof(InternalHeader) +
                 (idx - 1) * (sizeof(page_id_t) + sizeof(uint32_t)) +
                 sizeof(uint32_t);
    return reinterpret_cast<page_id_t *>(data);
  }

  int findFirstGreater(uint32_t key) {

    int left = 0, right = getHeader()->key_count;
    while (left < right) {
      int mid = (left + right) / 2;
      if (*getKey(mid) <= key) {
        left = mid + 1;
      } else {
        right = mid;
      }
    }
    return left;
  }

  int maxKeys() {
    return (PAGE_SIZE - sizeof(InternalHeader)) /
           (sizeof(uint32_t) + sizeof(page_id_t));
  }
};

class BPlusTree {
public:
  explicit BPlusTree(BufferPoolManager *bpm);
  BPlusTree(BufferPoolManager *bpm, page_id_t root_page_id);
  // bool remove(uint32_t key);
  // bool getValue(uint32_t key, RID *rid);

private:
  BufferPoolManager *m_bpm;
  page_id_t m_root_page_id;
};

} // namespace WalouDB
