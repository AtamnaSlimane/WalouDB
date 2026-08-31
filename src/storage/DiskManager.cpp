#include "waloudb/storage/DiskManager.h"
#include <cstring>
#include <filesystem>
#include <ios>

namespace WalouDB {
DiskManager::DiskManager(const std::string &db_file)
    : m_db_file(db_file),
      m_next_page_id(static_cast<page_id_t>(
          std::filesystem::exists(db_file)
              ? std::filesystem::file_size(db_file) / PAGE_SIZE
              : 0)) {
  m_db_io.open(db_file, std::ios::binary | std::ios::in | std::ios::out);
  if (!m_db_io.is_open()) {
    m_db_io.clear();
    m_db_io.open(db_file, std::ios::binary | std::ios::trunc | std::ios::out);
    m_db_io.close();
    m_db_io.open(db_file, std::ios::binary | std::ios::in | std::ios::out);
  }
}
bool DiskManager::readPage(page_id_t page_id, char *page_data) {
  std::lock_guard<std::mutex> lock(m_db_io_latch);
  if (page_id < 0) {
    return false;
  }
  m_db_io.seekg(static_cast<std::streamoff>(page_id) * PAGE_SIZE,
                std::ios::beg);

  if (!m_db_io) {
    return false;
  }

  m_db_io.read(page_data, PAGE_SIZE);
  std::streamsize bytes_read = m_db_io.gcount();

  if (bytes_read < PAGE_SIZE) {
    std::memset(page_data + bytes_read, 0,
                static_cast<size_t>(PAGE_SIZE - bytes_read));
    m_db_io.clear();
  }

  return true;
};

bool DiskManager::writePage(page_id_t page_id, const char *page_data) {
  std::lock_guard<std::mutex> lock(m_db_io_latch);

  m_db_io.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE,
                std::ios::beg);

  if (!m_db_io) {
    return false;
  }

  m_db_io.write(page_data, PAGE_SIZE);
  m_db_io.flush();
  return m_db_io.good();
};

DiskManager::~DiskManager() {
  if (m_db_io.is_open()) {
    m_db_io.close();
  }
}
} // namespace WalouDB
