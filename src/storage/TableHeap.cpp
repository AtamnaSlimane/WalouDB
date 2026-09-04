#include "waloudb/storage/TableHeap.h"
#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/SlottedPage.h"
#include "waloudb/storage/Tuple.h"
#include <exception>
#include <stdexcept>

namespace WalouDB {
TableHeap::TableHeap(BufferPoolManager *bpm) : m_bpm(bpm) {

  page_id_t page_id;
  auto page = bpm->newPage(&page_id);
  if (page == nullptr) {
    throw std::runtime_error("new page is null");
  }
  SlottedPage sp(page->getData());
  sp.Init(page_id);
  bpm->unpinPage(page_id, true);
  m_first_page_id = page_id;
  m_last_page_id = page_id;
}
bool TableHeap::insertTuple(const Tuple &tuple, RID *out_rid) {
  auto page = m_bpm->fetchPage(m_last_page_id);
  if (page == nullptr) {
    return false;
  }
  auto page_id = page->getPageId();
  SlottedPage sp(page->getData());
  if (sp.insertTuple(tuple, out_rid)) {
    m_bpm->unpinPage(page_id, true);
    return true;
  }
  m_bpm->unpinPage(page_id, false);
  auto new_page = m_bpm->newPage(&page_id);
  if (new_page == nullptr) {
    return false;
  }
  SlottedPage nsp(new_page->getData());
  nsp.Init(page_id);
  if (!nsp.insertTuple(tuple, out_rid)) {
    m_bpm->unpinPage(page_id, false);
    return false;
  }

  m_bpm->unpinPage(page_id, true);
  m_last_page_id = page_id;
  return true;
}
bool TableHeap::getTuple(RID rid, Tuple *out_tuple) const {

};
} // namespace WalouDB
