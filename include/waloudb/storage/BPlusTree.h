#pragma once

#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/Key.h"
#include "waloudb/storage/SlottedPage.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
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

struct SerializedKey {
  uint8_t type;
  uint16_t length;
  char data[MAX_INDEX_KEY_SIZE];
};

struct Entry {
  Key key;
  RID rid{};
};
SerializedKey serializeKey(const Key &key);
Key deserializeKey(const SerializedKey &data);
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
  }

  bool findEntry(Key key, RID *out_rid) {
    int idx = findKeyIndex(key);

    if (idx >= getKeyCount()) {
      return false;
    }

    SerializedKey serialized{};
    std::memcpy(&serialized, entryData(idx), sizeof(SerializedKey));

    Key stored_key = deserializeKey(serialized);

    if (stored_key != key) {
      return false;
    }

    RID rid{};
    std::memcpy(&rid, entryData(idx) + sizeof(SerializedKey), sizeof(RID));

    if (out_rid != nullptr) {
      *out_rid = rid;
    }

    return true;
  }

  page_id_t getNextLeafId() { return getLeafHeader()->next_leaf_page_id; }

  void setNextLeafId(page_id_t id) { getLeafHeader()->next_leaf_page_id = id; }

  bool isFull() { return getHeader()->key_count >= maxEntries(); }

  page_id_t getId() { return getHeader()->page_id; }

  page_id_t getParentId() { return getHeader()->parent_page_id; }

  uint16_t getKeyCount() { return getHeader()->key_count; }

  void setParentId(page_id_t parent_id) {
    getHeader()->parent_page_id = parent_id;
  }

  std::vector<Entry> getAllEntries() {
    std::vector<Entry> output;

    uint16_t n = getKeyCount();
    output.reserve(n);

    for (uint16_t i = 0; i < n; i++) {
      SerializedKey serialized{};

      std::memcpy(&serialized, entryData(i), sizeof(SerializedKey));

      Key key = deserializeKey(serialized);

      RID rid{};

      std::memcpy(&rid, entryData(i) + sizeof(SerializedKey), sizeof(RID));

      output.push_back(Entry{key, rid});
    }

    return output;
  }

  bool setEntries(std::vector<Entry> &entries) {
    if (entries.size() > maxEntries()) {
      return false;
    }

    for (size_t i = 0; i < entries.size(); i++) {
      SerializedKey serialized = serializeKey(entries[i].key);

      std::memcpy(entryData(static_cast<int>(i)), &serialized,
                  sizeof(SerializedKey));

      std::memcpy(entryData(static_cast<int>(i)) + sizeof(SerializedKey),
                  &entries[i].rid, sizeof(RID));
    }

    getHeader()->key_count = static_cast<uint16_t>(entries.size());

    return true;
  }

  bool insertEntry(const Entry &entry) {
    if (isFull()) {
      return false;
    }

    int idx = findKeyIndex(entry.key);
    int count = getKeyCount();

    // if (idx < count && getKey(idx) == entry.key) {
    //   return false; // duplicate key
    // }

    for (int i = count; i > idx; i--) {
      SerializedKey serialized{};

      std::memcpy(&serialized, entryData(i - 1), sizeof(SerializedKey));

      RID rid{};

      std::memcpy(&rid, entryData(i - 1) + sizeof(SerializedKey), sizeof(RID));

      std::memcpy(entryData(i), &serialized, sizeof(SerializedKey));

      std::memcpy(entryData(i) + sizeof(SerializedKey), &rid, sizeof(RID));
    }

    SerializedKey serialized = serializeKey(entry.key);

    std::memcpy(entryData(idx), &serialized, sizeof(SerializedKey));

    std::memcpy(entryData(idx) + sizeof(SerializedKey), &entry.rid,
                sizeof(RID));

    getHeader()->key_count++;

    return true;
  }

private:
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

  char *entryData(int idx) {
    return m_data + sizeof(LeafHeader) + idx * entrySize();
  }

  const char *entryData(int idx) const {
    return m_data + sizeof(LeafHeader) + idx * entrySize();
  }

  Key getKey(int idx) {
    SerializedKey serialized{};

    std::memcpy(&serialized, entryData(idx), sizeof(SerializedKey));

    return deserializeKey(serialized);
  }

  int findKeyIndex(Key key) {
    int left = 0;
    int right = getHeader()->key_count;

    while (left < right) {
      int mid = (left + right) / 2;

      if (getKey(mid) < key) {
        left = mid + 1;
      } else {
        right = mid;
      }
    }

    return left;
  }

  static constexpr size_t entrySize() {
    return sizeof(SerializedKey) + sizeof(RID);
  }

  static constexpr size_t maxEntries() {
    return (PAGE_SIZE - sizeof(LeafHeader)) / entrySize();
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

  page_id_t findChild(Key key) {
    auto index = findChildIndex(key);
    return *getChild(index);
  }

  bool isFull() { return getHeader()->key_count >= maxKeys(); }

  page_id_t getId() { return getHeader()->page_id; }

  page_id_t getParentId() { return getHeader()->parent_page_id; }

  uint16_t getKeyCount() { return getHeader()->key_count; }

  void setParentId(page_id_t parent_page_id) {
    getHeader()->parent_page_id = parent_page_id;
  }

  bool insertChild(Key key, page_id_t child_id) {
    if (isFull()) {
      return false;
    }

    int n = getKeyCount();
    int idx = findKeyIndex(key);

    // Shift keys right.
    for (int i = n; i > idx; i--) {
      SerializedKey serialized{};

      std::memcpy(&serialized, keyData(i - 1), sizeof(SerializedKey));

      std::memcpy(keyData(i), &serialized, sizeof(SerializedKey));
    }

    // Shift children right.
    for (int i = n + 1; i > idx + 1; i--) {
      *getChild(i) = *getChild(i - 1);
    }

    // Write new key.
    SerializedKey serialized = serializeKey(key);

    std::memcpy(keyData(idx), &serialized, sizeof(SerializedKey));

    // Write new right child.
    *getChild(idx + 1) = child_id;

    getHeader()->key_count++;

    return true;
  }

  int findKeyIndex(Key key) {
    int left = 0;
    int right = getHeader()->key_count;

    while (left < right) {
      int mid = (left + right) / 2;

      if (getKey(mid) < key) {
        left = mid + 1;
      } else {
        right = mid;
      }
    }

    return left;
  }

  void getAllChildrenKeys(std::vector<page_id_t> *children,
                          std::vector<Key> *keys) {
    int n = getKeyCount();

    keys->clear();
    children->clear();

    keys->reserve(n);
    children->reserve(n + 1);

    for (int i = 0; i < n; i++) {
      keys->push_back(getKey(i));
    }

    for (int i = 0; i <= n; i++) {
      children->push_back(*getChild(i));
    }
  }

  bool setAllChildrenKeys(const std::vector<page_id_t> &children,
                          const std::vector<Key> &keys) {
    if (children.size() != keys.size() + 1) {
      return false;
    }

    if (keys.size() > maxKeys()) {
      return false;
    }

    for (size_t i = 0; i < children.size(); i++) {
      *getChild(static_cast<int>(i)) = children[i];
    }

    for (size_t i = 0; i < keys.size(); i++) {
      SerializedKey serialized = serializeKey(keys[i]);

      std::memcpy(keyData(static_cast<int>(i)), &serialized,
                  sizeof(SerializedKey));
    }

    getHeader()->key_count = static_cast<uint16_t>(keys.size());

    return true;
  }

private:
  char *m_data;

  NodeHeader *getHeader() { return reinterpret_cast<NodeHeader *>(m_data); }

  const NodeHeader *getHeader() const {
    return reinterpret_cast<const NodeHeader *>(m_data);
  }

  Key getKey(int idx) {
    SerializedKey serialized{};

    std::memcpy(&serialized, keyData(idx), sizeof(SerializedKey));

    return deserializeKey(serialized);
  }

  char *keyData(int idx) {
    return m_data + sizeof(NodeHeader) + sizeof(page_id_t) +
           idx * (sizeof(page_id_t) + sizeof(SerializedKey));
  }

  page_id_t *getChild(int idx) {
    char *data = m_data + sizeof(NodeHeader) +
                 idx * (sizeof(page_id_t) + sizeof(SerializedKey));

    return reinterpret_cast<page_id_t *>(data);
  }

  const page_id_t *getChild(int idx) const {
    const char *data = m_data + sizeof(NodeHeader) +
                       idx * (sizeof(page_id_t) + sizeof(SerializedKey));

    return reinterpret_cast<const page_id_t *>(data);
  }

  int findChildIndex(Key key) {
    int left = 0;
    int right = getHeader()->key_count;

    while (left < right) {
      int mid = (left + right) / 2;

      if (getKey(mid) <= key) {
        left = mid + 1;
      } else {
        right = mid;
      }
    }

    return left;
  }

  static constexpr size_t maxKeys() {
    return (PAGE_SIZE - sizeof(NodeHeader) - sizeof(page_id_t)) /
           (sizeof(SerializedKey) + sizeof(page_id_t));
  }
};
class BPlusTree {
public:
  explicit BPlusTree(BufferPoolManager *bpm);
  BPlusTree(BufferPoolManager *bpm, page_id_t root_page_id);

  bool search(Key key, RID *out_rid) const;
  bool searchAll(Key key, std::vector<RID> *out_rids) const;
  bool insert(Key key, RID rid);
  bool split(page_id_t page_id, Entry &entry, page_id_t right_child_id);
  bool rangeSearch(Key low, Key high, std::vector<Entry> *out_entries) const;

  page_id_t getRootId() { return m_root_page_id; }
  // bool getValue(uint32_t key, RID *rid);

  using RootChangeCallback = std::function<void(page_id_t)>;
  void setRootChangeCallback(RootChangeCallback callback);

private:
  BufferPoolManager *m_bpm;
  page_id_t m_root_page_id{INVALID_PAGE_ID};

  RootChangeCallback m_root_change_callback;

  void setRootPageId(page_id_t root_page_id);
};

} // namespace WalouDB
