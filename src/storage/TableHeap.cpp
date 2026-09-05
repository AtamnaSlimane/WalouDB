#include "waloudb/storage/TableHeap.h"
#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/Page.h"
#include "waloudb/storage/SlottedPage.h"
#include "waloudb/storage/Tuple.h"
#include <exception>
#include <stdexcept>

namespace WalouDB {

TableHeap::TableHeap(BufferPoolManager *bpm) {
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
TableHeap::TableHeap(BufferPoolManager *bpm, page_id_t first_page_id)
    : m_bpm(bpm), m_first_page_id(first_page_id) {

  page_id_t current = first_page_id;
  while (true) {
    Page *page = bpm->fetchPage(current);
    if (page == nullptr) {
      throw std::runtime_error("TableHeap: could not fetch page " +
                               std::to_string(current) +
                               " while walking existing chain");
    }
    SlottedPage sp(page->getData());
    page_id_t next = sp.getNextPageId();
    bpm->unpinPage(current, false);
    if (next == INVALID_PAGE_ID)
      break;
    current = next;
  }
  m_last_page_id = current;
}
bool TableHeap::insertTuple(const Tuple &tuple, RID *out_rid) {
  // on empty heap handling
  if (m_last_page_id == INVALID_PAGE_ID) {
    page_id_t new_page_id;
    auto new_page = m_bpm->newPage(&new_page_id);
    if (new_page == nullptr) {
      return false; // Disk or Buffer Pool exhausted
    }

    SlottedPage nsp(new_page->getData());
    nsp.Init(new_page_id);

    if (!nsp.insertTuple(tuple, out_rid)) {
      m_bpm->unpinPage(new_page_id, false);
      return false; // Tuple exceeds maximum page capacity
    }

    m_first_page_id = new_page_id;
    m_last_page_id = new_page_id;
    m_bpm->unpinPage(new_page_id, true);
    return true;
  }
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
  if (!nsp.insertTuple(tuple, out_rid)) {
    m_bpm->unpinPage(old_page_id, false);
    m_bpm->unpinPage(new_page_id, false);
    return false;
  }

  sp.setNextPageId(new_page_id);
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

bool TableHeap::updateTuple(RID rid, const Tuple &tuple) {

  auto page = m_bpm->fetchPage(rid.page_id);
  if (page == nullptr) {
    return false;
  }
  SlottedPage sp(page->getData());
  bool updated = sp.updateTuple(rid.slot_num, tuple);
  m_bpm->unpinPage(rid.page_id, updated);
  return updated;
}

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
