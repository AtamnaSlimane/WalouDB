#pragma once
#include "waloudb/common/Types.h"
#include "waloudb/storage/DiskManager.h"
#include "waloudb/storage/Lrur.h"
#include "waloudb/storage/Page.h"
#include <cstddef>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>
namespace WalouDB {

class BufferPoolManager {
public:
  BufferPoolManager(size_t pool_size, DiskManager *diskmanager);

  Page *fetchPage(page_id_t page_id);
  Page *newPage(page_id_t *page_id);
  bool findFrame(frame_id_t *frame_id);
  bool unpinPage(page_id_t page_id, bool is_dirty);
  bool flushPage(page_id_t page_id);
  bool deletePage(page_id_t page_id);

  size_t getPoolSize() const { return m_pool_size; }
  page_id_t getFramePageId(frame_id_t frame_id) const {
    // debug
    if (frame_id < 0 || frame_id >= static_cast<frame_id_t>(m_pool_size)) {

      return INVALID_PAGE_ID;
    }

    return m_pages[frame_id].getPageId();
  }
  int getFramePinCount(frame_id_t frame_id) const {

    if (frame_id < 0 || frame_id >= static_cast<frame_id_t>(m_pool_size)) {

      return -1;
    }

    return m_pages[frame_id].getPinCount();
  }

  bool getFrameDirty(frame_id_t frame_id) const {

    if (frame_id < 0 || frame_id >= static_cast<frame_id_t>(m_pool_size)) {

      return false;
    }

    return m_pages[frame_id].isDirty();
  }

  const Lrur &getReplacer() const { return *m_replacer; }

private:
  DiskManager *m_diskmanager;
  size_t m_pool_size;
  std::unordered_map<page_id_t, frame_id_t> m_pages_table;
  std::vector<Page> m_pages; // the actual pool
  std::list<frame_id_t> m_free_list;
  std::unique_ptr<Lrur> m_replacer;
  mutable std::mutex m_latch;
};

} // namespace WalouDB
