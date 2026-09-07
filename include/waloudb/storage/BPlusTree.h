#pragma once

#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/SlottedPage.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
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
    int i = 0;
    int keys = getHeader()->key_count;
    while (i <= keys) {
      int mid = (i + keys) / 2;
      if (getEntry(i)->key < key) {
        i = mid + 1;
      } else {
        return i = mid;
      }
    }
    return i;
  }

  static constexpr size_t maxEntries() {
    return (PAGE_SIZE - sizeof(LeafHeader)) / sizeof(Entry);
  }
};

class InternalNode {
public:
  explicit InternalNode(char *raw_data) : m_data(raw_data) {}

private:
  char *m_data;
};

class BPlusTree {
public:
  BPlusTree(BufferPoolManager *bpm);
  // bool insert(uint32_t key, RID rid);
  // bool remove(uint32_t key);
  // bool getValue(uint32_t key, RID *rid);

private:
  BufferPoolManager *m_bpm;
  page_id_t m_root_page_id;
};

} // namespace WalouDB
