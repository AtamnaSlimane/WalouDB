#pragma once

#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/SlottedPage.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>
namespace WalouDB {

struct Node;

struct NodeHeader {
  Node *last_child{nullptr};
};

struct Entry {
  uint32_t key;
  RID rid{};

  Node *child{nullptr};
};

struct Node {
  NodeHeader header;
  Node *parent{nullptr};
  std::vector<Entry> entries;
  uint16_t capacity{0};
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
