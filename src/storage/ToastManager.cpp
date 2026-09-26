#include "waloudb/storage/ToastManager.h"
#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/DiskManager.h"
#include "waloudb/storage/OverflowPage.h"
#include <memory>
namespace WalouDB {
ToastManager::ToastManager(const std::string &toast_file_path, size_t pool_size)
    : m_disk_manager(std::make_unique<DiskManager>(toast_file_path)),
      m_bpm(std::make_unique<BufferPoolManager>(pool_size,
                                                m_disk_manager.get())) {}

bool ToastManager::insertToast(const char *data, uint32_t length,
                               ToastPointer *out_ptr) {
  if (data == nullptr || length == 0 || out_ptr == nullptr) {
    return false;
  }
  page_id_t first_page_id;
  page_id_t previous_page_id;

  uint32_t remaining = length;
  uint32_t offset = 0;

  while (remaining > 0) {
    page_id_t page_id;

    Page *page = m_bpm->newPage(&page_id);

    if (page == nullptr) {
      return false;
    }

    OverflowPage overflow(page->getData());
    overflow.Init(page_id);

    uint16_t amount = static_cast<uint16_t>(
        std::min<uint32_t>(remaining, OverflowPage::capacity()));

    std::memcpy(overflow.payload(), data + offset, amount);

    overflow.setDataLength(amount);

    page->setDirty(true);

    if (first_page_id == INVALID_PAGE_ID) {
      first_page_id = page_id;
    }

    if (previous_page_id != INVALID_PAGE_ID) {
      Page *previous_page = m_bpm->fetchPage(previous_page_id);

      if (previous_page == nullptr) {
        m_bpm->unpinPage(page_id, true);
        return false;
      }

      OverflowPage previous_overflow(previous_page->getData());

      previous_overflow.setNextPageId(page_id);

      m_bpm->unpinPage(previous_page_id, true);
    }

    previous_page_id = page_id;

    m_bpm->unpinPage(page_id, true);

    offset += amount;
    remaining -= amount;
  }

  out_ptr->page_id = first_page_id;

  return true;
};
} // namespace WalouDB
