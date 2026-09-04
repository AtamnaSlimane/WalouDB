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
  auto old_page_id = page->getPageId();
  SlottedPage sp(page->getData());
  if (sp.insertTuple(tuple, out_rid)) {
    m_bpm->unpinPage(old_page_id, true);
    return true;
  }

  page_id_t new_page_id;
  auto new_page = m_bpm->newPage(&new_page_id);
  if (new_page == nullptr) {
    m_bpm->unpinPage(old_page_id, false);
    return false;
  }
  SlottedPage nsp(new_page->getData());
  nsp.Init(new_page_id);
  sp.setNextPageId(new_page_id);
  if (!nsp.insertTuple(tuple, out_rid)) {
    m_bpm->unpinPage(old_page_id, true);
    m_bpm->unpinPage(new_page_id, false);
    return false;
  }

  m_bpm->unpinPage(old_page_id, true);
  m_bpm->unpinPage(new_page_id, true);
  m_last_page_id = new_page_id;
  return true;
}
bool TableHeap::getTuple(RID rid, Tuple *out_tuple) const {
  auto page = m_bpm->fetchPage(rid.page_id);
  if (page == nullptr) {
    return false;
  }
  SlottedPage sp(page->getData());
  auto tuple = sp.getTuple(rid.slot_num);
  if (!tuple.has_value()) {
    m_bpm->unpinPage(rid.page_id, false);
    return false;
  }
  *out_tuple = *tuple;
  m_bpm->unpinPage(rid.page_id, false);
  return true;
};

bool TableHeap::deleteTuple(RID rid) {

  auto page = m_bpm->fetchPage(rid.page_id);
  if (page == nullptr) {
    return false;
  }
  SlottedPage sp(page->getData());
  bool deleted = sp.deleteTuple(rid.slot_num);
  if (!deleted) {
    m_bpm->unpinPage(rid.page_id, false);
    return false;
  }
  m_bpm->unpinPage(rid.page_id, true);
  return true;
}
} // namespace WalouDB
