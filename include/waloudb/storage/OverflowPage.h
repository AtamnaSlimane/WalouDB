#pragma once
#include "waloudb/common/Types.h"
#include <cstdint>
namespace WalouDB {
struct OverflowPageHeader {
  page_id_t page_id;
  page_id_t next_page_id;
  uint16_t data_length{0};
};

// reference in slotted page
struct OverflowRef {
  page_id_t first_page_id;
  uint32_t total_length;
};
class OverflowPage {
public:
  explicit OverflowPage(char *data) : m_data(data) {};

  void Init(page_id_t page_id) {
    auto *h = getHeader();
    h->page_id = page_id;
    h->next_page_id = INVALID_PAGE_ID;
    h->data_length = 0;
  }

  static constexpr uint16_t capacity() {
    return static_cast<uint16_t>(PAGE_SIZE - sizeof(OverflowPageHeader));
  }

  // actual data in the ovfpage
  char *payload() { return m_data + sizeof(OverflowPageHeader); }

  const char *payload() const { return m_data + sizeof(OverflowPageHeader); }

  uint16_t getDataLength() const { return getHeader()->data_length; }

  void setDataLength(uint16_t length) { getHeader()->data_length = length; }

  page_id_t getPageId() const { return getHeader()->page_id; }

  page_id_t getNextPageId() const { return getHeader()->next_page_id; }

  void setNextPageId(page_id_t page_id) { getHeader()->next_page_id = page_id; }
  OverflowPageHeader *getHeader() {
    return reinterpret_cast<OverflowPageHeader *>(m_data);
  }
  const OverflowPageHeader *getHeader() const {
    return reinterpret_cast<const OverflowPageHeader *>(m_data);
  }

private:
  char *m_data;
};

} // namespace WalouDB
