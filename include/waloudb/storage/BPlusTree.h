#pragma once

#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/SlottedPage.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
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
// ============================================================
// LEAF NODE LAYOUT
// ============================================================
//
// A leaf stores actual index entries:
//
//     key -> RID
//
// Physical layout inside the 4096-byte page:
//
//     +----------------------------+
//     | NodeHeader                 |
//     +----------------------------+
//     | next_leaf_page_id          |
//     +----------------------------+
//     | Entry[0]                   |
//     |   key                      |
//     |   RID                      |
//     +----------------------------+
//     | Entry[1]                   |
//     |   key                      |
//     |   RID                      |
//     +----------------------------+
//     | ...                        |
//     +----------------------------+
//
// Entry[i] is located at:
//
//     m_data + sizeof(LeafHeader) + i * sizeof(Entry)
//
// The entries are kept sorted by key.
//
// Leaves are also linked together:
//
//     Leaf A -> Leaf B -> Leaf C -> INVALID
//
// This allows efficient sequential/range scans.
//
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
    if (idx < getKeyCount() && getEntry(idx)->key == key) {
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

  std::vector<Entry> getAllEntries() {
    std::vector<Entry> output;
    uint16_t n = getKeyCount();
    output.reserve(n);
    for (uint16_t i = 0; i < n; i++) {
      output.push_back(*getEntry(i));
    }
    return output;
  }
  bool setEntries(std::vector<Entry> &entries) {
    if (entries.size() > maxEntries()) {
      return false;
    }
    for (size_t i = 0; i < entries.size(); i++) {
      *getEntry(static_cast<int>(i)) = entries[i];
    }
    getHeader()->key_count = static_cast<uint16_t>(entries.size());
    return true;
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
// ============================================================
// INTERNAL NODE LAYOUT
// ============================================================
//
// An internal node stores separator keys and child page IDs.
//
// It does NOT store RIDs or actual table records.
// The actual key -> RID entries are stored only in leaf nodes.
//
// If an internal node has N keys, it always has N + 1 children.
//
// Example:
//
//     keys = [30, 60]
//
//                    [30 | 60]
//                   /    |    \
//                 P0     P1    P2
//
//     P0 -> keys < 30
//     P1 -> 30 <= keys < 60
//     P2 -> keys >= 60
//
// The separator key belongs logically to the right child.
// Therefore, searching for a key uses upper_bound semantics.
//
// ------------------------------------------------------------
// Physical layout inside the page:
//
//     +----------------------------+
//     | NodeHeader                 |
//     +----------------------------+
//     | child[0]                   |
//     +----------------------------+
//     | key[0]                     |
//     | child[1]                   |
//     +----------------------------+
//     | key[1]                     |
//     | child[2]                   |
//     +----------------------------+
//     | key[2]                     |
//     | child[3]                   |
//     +----------------------------+
//     | ...                        |
//     +----------------------------+
//
// child[0] is stored immediately after NodeHeader.
//
// Every key is followed by the child to its right:
//
//     key[0] -> child[1]
//     key[1] -> child[2]
//     key[2] -> child[3]
//
// Therefore:
//
//     number of children = number of keys + 1
//
// ------------------------------------------------------------
// Example:
//
//     Node:
//
//             [30 | 60 | 90]
//            /    |    |    \
//          P0    P1   P2     P3
//
//     Search:
//
//          key < 30       -> P0
//          30 <= key < 60 -> P1
//          60 <= key < 90 -> P2
//          key >= 90      -> P3
//
// ------------------------------------------------------------
// Children can point to:
//
//     - another internal node
//     - a leaf node
//
// This allows the B+Tree to have multiple levels:
//
//                  Root
//                   |
//             Internal Node
//              /         \
//          Internal      Internal
//           /   \         /   \
//         Leaf Leaf     Leaf Leaf
//
// ------------------------------------------------------------
// During an internal-node split:
//
//     The middle separator key is promoted to the parent.
//
// Unlike a leaf split, the promoted key is NOT kept in either
// resulting internal node.
//
// ============================================================
class InternalNode {
public:
  explicit InternalNode(char *raw_data) : m_data(raw_data) {}
  void Init(page_id_t page_id, page_id_t parent_page_id) {
    NodeHeader *h = getHeader();
    h->page_id = page_id;
    h->parent_page_id = parent_page_id;
    h->page_type = NodeType::INTERNAL;
    h->key_count = 0;
    *getChild(0) = INVALID_PAGE_ID;
  }
  page_id_t findChild(uint32_t key) {
    auto index = findChildIndex(key);
    return *getChild(index);
  }

  bool isFull() { return getHeader()->key_count >= maxKeys(); }
  page_id_t getId() { return getHeader()->page_id; }
  page_id_t getParentId() { return getHeader()->parent_page_id; }
  uint16_t getKeyCount() { return getHeader()->key_count; }

  void getAllChildrenKeys(std::vector<page_id_t> *children,
                          std::vector<uint32_t> *keys) {
    int n = getKeyCount();
    keys->clear();
    children->clear();
    keys->reserve(n);
    children->reserve(n + 1);
    for (int i = 0; i < n; i++)
      keys->push_back(*getKey(i));

    for (int i = 0; i <= n; i++) // n+1
      children->push_back(*getChild(i));
  }
  bool setAllChildrenKeys(std::vector<page_id_t> &children,
                          std::vector<uint32_t> &keys) {

    if (children.size() != keys.size() + 1) {
      return false;
    }
    if (keys.size() > maxKeys()) {
      return false;
    }
    for (size_t i = 0; i < children.size(); i++)
      *getChild(i) = children[i];
    for (size_t i = 0; i < keys.size(); i++)
      *getKey(i) = keys[i];

    getHeader()->key_count = static_cast<uint16_t>(keys.size());
    return true;
  }

private:
  char *m_data;

  NodeHeader *getHeader() { return reinterpret_cast<NodeHeader *>(m_data); }
  const NodeHeader *getHeader() const {
    return reinterpret_cast<const NodeHeader *>(m_data);
  }

  uint32_t *getKey(int idx) {
    return reinterpret_cast<uint32_t *>(
        m_data + sizeof(NodeHeader) + sizeof(page_id_t) +
        idx * (sizeof(page_id_t) + sizeof(uint32_t)));
  }

  page_id_t *getChild(int idx) {
    char *data = m_data + sizeof(NodeHeader) +
                 idx * (sizeof(page_id_t) + sizeof(uint32_t));
    return reinterpret_cast<page_id_t *>(data);
  }

  const page_id_t *getChild(int idx) const {
    char *data = m_data + sizeof(NodeHeader) +
                 idx * (sizeof(page_id_t) + sizeof(uint32_t));
    return reinterpret_cast<const page_id_t *>(data);
  }

  int findChildIndex(uint32_t key) {

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
    return (PAGE_SIZE - sizeof(NodeHeader) - sizeof(page_id_t)) /
           (sizeof(uint32_t) + sizeof(page_id_t));
  }
};

class BPlusTree {
public:
  explicit BPlusTree(BufferPoolManager *bpm);
  BPlusTree(BufferPoolManager *bpm, page_id_t root_page_id);

  bool Search(uint32_t key, RID *out_rid) const;

  page_id_t getRootId() { return m_root_page_id; }
  // bool getValue(uint32_t key, RID *rid);

private:
  BufferPoolManager *m_bpm;
  page_id_t m_root_page_id;
};

} // namespace WalouDB
