#pragma once
#include "waloudb/common/Types.h"
#include <cstring>
namespace WalouDB {

class Page {
public:
  char *getData() { return m_data; };
  const char *getData() const { return m_data; };
  page_id_t getPageId() const { return m_page_id; }
  bool isDirty() const { return m_dirty; }
  int getPinCount() const { return m_pin_count; }

  void setDirty(bool dirty) { m_dirty = dirty; }
  void setPageId(page_id_t page_id) { m_page_id = page_id; }
  void pin() { ++m_pin_count; }
  void unpin() {
    if (m_pin_count > 0) {
      --m_pin_count;
    }
  }
  void resetMemory() {
    std::memset(m_data, 0, PAGE_SIZE);
    m_page_id = INVALID_PAGE_ID;
    m_dirty = false;
    m_pin_count = 0;
  }

private:
  page_id_t m_page_id{INVALID_PAGE_ID};
  char m_data[PAGE_SIZE]{};
  bool m_dirty{false};
  int m_pin_count{0}; // later for buffer pool eviction;
};

} // namespace WalouDB
