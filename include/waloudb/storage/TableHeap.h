#pragma once
#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/SlottedPage.h"
#include "waloudb/storage/Tuple.h"
#include <string>
namespace WalouDB {

class TableHeap {
public:
  TableHeap(BufferPoolManager *bpm, page_id_t first_page_id);
  TableHeap(BufferPoolManager *bpm);
  bool insertTuple(const Tuple &tuple, RID *out_rid);
  bool updateTuple(RID rid, const Tuple &tuple);
  bool getTuple(RID rid, Tuple *out_tuple) const;
  bool deleteTuple(RID rid);

  page_id_t getFirstPageId() const { return m_first_page_id; }
  class Iterator {
  public:
    Iterator(BufferPoolManager *bpm, page_id_t page_id, uint16_t slot_num)
        : m_bpm(bpm), m_page_id(page_id), m_slot_num(slot_num) {
      advanceToValid();
    }

    bool operator!=(const Iterator &other) const {
      return m_page_id != other.m_page_id || m_slot_num != other.m_slot_num;
    }
    bool operator==(const Iterator &other) const { return !(*this != other); }

    Tuple operator*() const {
      Page *page = m_bpm->fetchPage(m_page_id);
      SlottedPage sp(page->getData());
      Tuple t =
          *sp.getTuple(m_slot_num); // guaranteed valid by advanceToValid()
      m_bpm->unpinPage(m_page_id, false);
      return t;
    }

    RID getRID() const { return RID{m_page_id, m_slot_num}; }

    Iterator &operator++() {
      ++m_slot_num;
      advanceToValid();
      return *this;
    }

  private:
    // Moves (m_page_id, m_slot_num) forward until it lands on a real,
    // non-tombstoned tuple, or on end() (m_page_id == INVALID_PAGE_ID).
    void advanceToValid() {
      while (m_page_id != INVALID_PAGE_ID) {
        Page *page = m_bpm->fetchPage(m_page_id);
        if (page == nullptr) {
          m_page_id = INVALID_PAGE_ID;
          return;
        }
        SlottedPage sp(page->getData());
        uint16_t slot_count = sp.getSlotCount();
        page_id_t next_id = sp.getNextPageId();

        while (m_slot_num < slot_count) {
          auto slot_info = sp.getSlotInfo(m_slot_num);
          if (slot_info.has_value() && !slot_info->deleted &&
              slot_info->length > 0) {
            m_bpm->unpinPage(m_page_id, false);
            return; // found a live tuple — stop here
          }
          ++m_slot_num;
        }

        // exhausted this page — move to the next one in the chain
        m_bpm->unpinPage(m_page_id, false);
        m_page_id = next_id;
        m_slot_num = 0;
      }
    }

    BufferPoolManager *m_bpm;
    page_id_t m_page_id;
    uint16_t m_slot_num;
  };

  Iterator begin() { return Iterator(m_bpm, m_first_page_id, 0); }
  Iterator end() { return Iterator(m_bpm, INVALID_PAGE_ID, 0); }

private:
  BufferPoolManager *m_bpm;
  page_id_t m_first_page_id{INVALID_PAGE_ID};
  page_id_t m_last_page_id{INVALID_PAGE_ID};
};

} // namespace WalouDB
