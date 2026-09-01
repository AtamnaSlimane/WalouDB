#pragma once
#include "Tuple.h"
#include "waloudb/common/Types.h"
#include <optional>
#include <utility>
namespace WalouDB {

struct PageHeader {
  page_id_t page_id;
  uint16_t lower; // end of slot array / start of free space
  uint16_t upper; // start of tuple data / end of free space
  uint16_t slot_count;
  uint16_t version;
};

struct Slot {
  uint16_t offset; // byte offset from page start where the tuple begins
  uint16_t length; // 0 == tombstoned/deleted
};

struct RID {
  page_id_t page_id;
  uint16_t slot_num;
};

class SlottedPage {

public:
  explicit SlottedPage(char *raw_data) : m_data(raw_data) {}

  void Init(page_id_t page_id);

  page_id_t getId() const { return getHeader()->page_id; }

  bool insertTuple(const Tuple &tuple, RID *out_rid);

  std::optional<Tuple> getTuple(uint16_t slot_num) const;

  bool deleteTuple(uint16_t slot_num);

  uint16_t freeSpace() const;

  uint16_t getLower() const { return getHeader()->lower; }
  uint16_t getUpper() const { return getHeader()->upper; }
  uint16_t getVersion() const { return getHeader()->version; }
  uint16_t getSlotCount() const { return getHeader()->slot_count; }
  std::optional<Slot> getSlotInfo(uint16_t idx) const {
    if (idx >= getHeader()->slot_count) {
      return std::nullopt;
    }
    return *getSlot(idx);
  }

private:
  char *m_data;
  PageHeader *getHeader() { return reinterpret_cast<PageHeader *>(m_data); }

  const PageHeader *getHeader() const {
    return reinterpret_cast<const PageHeader *>(m_data);
  }
  Slot *getSlot(uint16_t);
  const Slot *getSlot(uint16_t) const;

  int findTombstonedSlot() const;
};

} // namespace WalouDB
