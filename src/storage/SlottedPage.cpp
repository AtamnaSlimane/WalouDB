#include "waloudb/storage/SlottedPage.h"
#include "waloudb/common/Types.h"
#include "waloudb/storage/Tuple.h"
#include <cstdint>
#include <cstring>
#include <spdlog/common.h>

namespace WalouDB {
void SlottedPage::Init(page_id_t page_id) {
  auto h = getHeader();

  h->page_id = page_id;

  h->next_page_id = INVALID_PAGE_ID;
  h->lower = sizeof(PageHeader);
  h->upper = static_cast<uint16_t>(PAGE_SIZE);
  h->slot_count = 0;
}
bool SlottedPage::insertTuple(const Tuple &tuple, RID *out_rid) {
  auto h = getHeader();

  uint16_t tuple_len = tuple.getLength();

  uint16_t required = tuple_len + sizeof(Slot);

  if (freeSpace() < required) {
    compact();
    if (freeSpace() < required) {
      return false;
    }
  }

  h->upper -= tuple_len;
  std::memcpy(m_data + h->upper, tuple.getData(), tuple_len);
  uint16_t slot_idx = h->slot_count;

  h->slot_count++;
  h->lower += sizeof(Slot);

  Slot *slot = getSlot(slot_idx);

  slot->offset = h->upper;
  slot->length = tuple_len;

  out_rid->page_id = h->page_id;
  out_rid->slot_num = slot_idx;

  return true;
}

bool SlottedPage::updateTuple(uint16_t slot_num, const Tuple &tuple) {
  uint16_t tuple_len = tuple.getLength();
  auto old_tuple = getTuple(slot_num);
  if (!old_tuple.has_value()) {
    return false;
  }
  auto old_len = old_tuple->getLength();

  auto slot = getSlot(slot_num);
  if (tuple_len <= old_len) {
    memcpy(m_data + slot->offset, tuple.getData(), tuple_len);
    slot->length = tuple_len;
    return true;
  }

  if (tuple_len > freeSpace() + old_len) {
    compact();
    slot = getSlot(slot_num);
    if (tuple_len > freeSpace() + old_len) {
      return false;
    }
  }

  slot->length = 0;
  slot->deleted = true;
  compact();

  slot = getSlot(slot_num);
  auto h = getHeader();

  h->upper -= tuple_len;

  memcpy(m_data + h->upper, tuple.getData(), tuple_len);

  slot->offset = h->upper;
  slot->length = tuple_len;
  slot->deleted = false;

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
  auto h = getHeader();

  if (slot_num >= h->slot_count) {
    return false;
  }

  Slot *slot = getSlot(slot_num);

  if (slot->length == 0 || slot->deleted) {
    return false;
  }

  slot->length = 0;
  slot->deleted = true;

  return true;
}

void SlottedPage::compact() {
  auto h = getHeader();

  char tmp[PAGE_SIZE]{};

  std::vector<uint16_t> offsets(h->slot_count, 0);

  uint16_t tmp_upper = PAGE_SIZE;
  for (uint16_t i = 0; i < h->slot_count; i++) {
    auto slot = getSlot(i);
    if (slot->deleted) {
      continue;
    }
    tmp_upper -= slot->length;
    memcpy(tmp + tmp_upper, m_data + slot->offset, slot->length);
    offsets[i] = tmp_upper;
  }
  memset(m_data + h->lower, 0, PAGE_SIZE - h->lower);

  memcpy(m_data + tmp_upper, tmp + tmp_upper, PAGE_SIZE - tmp_upper);

  for (uint16_t i = 0; i < h->slot_count; i++) {
    auto slot = getSlot(i);
    if (slot->deleted) {
      slot->offset = 0;
      slot->length = 0;
      continue;
    }
    slot->offset = offsets[i];
  }

  h->upper = tmp_upper;
}

} // namespace WalouDB
