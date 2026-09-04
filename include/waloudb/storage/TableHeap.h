#pragma once
#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/SlottedPage.h"
#include "waloudb/storage/Tuple.h"
#include <string>
namespace WalouDB {

class TableHeap {
public:
  TableHeap(BufferPoolManager *bpm);

  bool insertTuple(const Tuple &tuple, RID *out_rid);
  bool getTuple(RID rid, Tuple *out_tuple) const;
  bool deleteTuple(RID rid);

  page_id_t getFirstPageId() const { return m_first_page_id; }

private:
  BufferPoolManager *m_bpm;
  page_id_t m_first_page_id{INVALID_PAGE_ID};
  page_id_t m_last_page_id{INVALID_PAGE_ID};
};

} // namespace WalouDB
