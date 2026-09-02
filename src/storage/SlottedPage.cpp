#include "waloudb/storage/SlottedPage.h"
#include "waloudb/common/Types.h"
#include <cstring>

namespace WalouDB {
void SlottedPage::Init(page_id_t page_id) {
  PageHeader *h = getHeader();

  h->page_id = page_id;
  h->lower = sizeof(PageHeader);
  h->upper = static_cast<uint16_t>(PAGE_SIZE);
  h->slot_count = 0;
}
bool SlottedPage::insertTuple(const Tuple &tuple, RID *out_rid) {
  PageHeader *h = getHeader();

  uint16_t tuple_len = tuple.getLength();

  uint16_t required = tuple_len + sizeof(Slot);

  if (freeSpace() < required) {
    return false;
  }

  h->upper -= tuple_len;
  std::memcpy(m_data + h->upper, tuple.getData(), tuple_len);
  uint16_t slot_idx = h->slot_count;

  h->slot_count++;
  h->lower += sizeof(Slot);

  Slot *slot = getSlot(slot_idx);

  slot->offset = h->upper;
  slot->length = tuple_len;
  slot->deleted = false;

  out_rid->page_id = h->page_id;
  out_rid->slot_num = slot_idx;

  return true;
}

uint16_t SlottedPage::freeSpace() const {
  const PageHeader *h = getHeader();

  return h->upper - h->lower;
}

Slot *SlottedPage::getSlot(uint16_t idx) {
  return reinterpret_cast<Slot *>(m_data + sizeof(PageHeader) +
                                  idx * sizeof(Slot));
}

const Slot *SlottedPage::getSlot(uint16_t idx) const {
  return reinterpret_cast<const Slot *>(m_data + sizeof(PageHeader) +
                                        idx * sizeof(Slot));
}

std::optional<Tuple> SlottedPage::getTuple(uint16_t slot_num) const {
  const PageHeader *h = getHeader();

  if (slot_num >= h->slot_count) {
    return std::nullopt;
  }

  const Slot *slot = getSlot(slot_num);

  if (slot->deleted == true || slot->length == 0) {
    return std::nullopt;
  }

  return Tuple::fromRawData(m_data + slot->offset, slot->length);
}

bool SlottedPage::deleteTuple(uint16_t slot_num) {
  PageHeader *h = getHeader();

  if (slot_num >= h->slot_count) {
    return false;
  }

  Slot *slot = getSlot(slot_num);

  if (slot->length == 0) {
    return false;
  }

  slot->length = 0;
  slot->deleted = true;

  return true;
}

int SlottedPage::findTombstonedSlot() const {
  const PageHeader *h = getHeader();
  for (uint16_t i = 0; i < h->slot_count; i++) {
    if (getSlot(i)->deleted == true) {
      return i;
    }
  }
  return -1;
}
} // namespace WalouDB
