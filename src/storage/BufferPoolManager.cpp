#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/common/Types.h"
#include "waloudb/storage/DiskManager.h"
#include "waloudb/storage/Lrur.h"
#include "waloudb/storage/Page.h"
#include <algorithm>
#include <memory>
#include <mutex>

namespace WalouDB {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *diskmanager)
    : m_pool_size(pool_size), m_diskmanager(diskmanager), m_pages(pool_size) {
  m_replacer = std::make_unique<Lrur>(pool_size);
  for (frame_id_t i = 0; i < static_cast<frame_id_t>(pool_size); ++i) {
    m_free_list.push_back(i); // every frame starts free
  }
}

Page *BufferPoolManager::fetchPage(page_id_t page_id) {
  std::lock_guard<std::mutex> guard(m_latch);
  auto it = m_pages_table.find(page_id);
  if (it != m_pages_table.end()) {
    frame_id_t frame_id = it->second;
    Page &page = m_pages[frame_id];
    page.pin();
    m_replacer->Pin(frame_id);
    return &page;
  }
  frame_id_t frame_id;
  if (!findFrame(&frame_id)) {
    return nullptr;
  }

  Page &page = m_pages[frame_id];
  page.resetMemory();
  m_diskmanager->readPage(page_id, page.getData());
  page.setPageId(page_id);
  page.pin();
  m_pages_table[page_id] = frame_id;
  m_replacer->Pin(frame_id);
  return &page;
};

bool BufferPoolManager::findFrame(frame_id_t *frame_id) {

  if (!m_free_list.empty()) {
    *frame_id = m_free_list.front();
    m_free_list.pop_front();
    return true;
  }
  if (m_replacer->Victim(frame_id)) {
    Page &victim = m_pages[*frame_id];
    if (victim.isDirty()) {
      m_diskmanager->writePage(victim.getPageId(), victim.getData());
    }
    m_pages_table.erase(victim.getPageId());
    return true;
  }

  return false;
}

Page *BufferPoolManager::newPage(page_id_t *page_id) {
  std::lock_guard<std::mutex> guard(m_latch);
  frame_id_t frame_id;
  if (!findFrame(&frame_id)) {
    return nullptr;
  }

  page_id_t new_page_id = m_diskmanager->allocatePage();
  Page &page = m_pages[frame_id];
  page.resetMemory();
  page.setPageId(new_page_id);
  page.pin();
  m_pages_table[new_page_id] = frame_id;
  m_replacer->Pin(frame_id);
  *page_id = new_page_id;
  return &page;
}

bool BufferPoolManager::unpinPage(page_id_t page_id, bool is_dirty) {
  std::lock_guard<std::mutex> guard(m_latch);
  auto it = m_pages_table.find(page_id);
  if (it == m_pages_table.end()) {
    return false;
  }
  frame_id_t frame_id = it->second;
  Page &page = m_pages[frame_id];
  if (page.getPinCount() <= 0) {
    return false;
  }
  page.unpin();
  if (is_dirty)
    page.setDirty(true);
  if (page.getPinCount() == 0) {
    m_replacer->Unpin(frame_id);
  }
  return true;
}
bool BufferPoolManager::flushPage(page_id_t page_id) {
  std::lock_guard<std::mutex> guard(m_latch);
  auto it = m_pages_table.find(page_id);
  if (it == m_pages_table.end()) {
    return false;
  }
  Page &page = m_pages[page_id];
  m_diskmanager->writePage(page_id, page.getData());
  page.setDirty(false); // same as disk

  return true;
}

bool BufferPoolManager::deletePage(page_id_t page_id) {
  std::lock_guard<std::mutex> guard(m_latch);
  auto it = m_pages_table.find(page_id);
  if (it != m_pages_table.end()) {
    return false;
  }
  frame_id_t frame_id = it->second;
  Page &page = m_pages[frame_id];
  if (page.getPinCount() > 0)
    return false;
  m_pages_table.erase(it);
  page.resetMemory();
  m_free_list.push_back(frame_id);
  return true;
}
} // namespace WalouDB
