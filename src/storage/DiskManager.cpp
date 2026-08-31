#include "waloudb/storage/DiskManager.h"
#include <ios>

namespace WalouDB {
DiskManager::DiskManager(const std::string &db_file) {
  m_db_io.open(db_file, std::ios::binary | std::ios::in | std::ios::out);
  if (!m_db_io.is_open()) {
    m_db_io.clear();
    m_db_io.open(db_file, std::ios::binary | std::ios::trunc | std::ios::out);
    m_db_io.close();
    m_db_io.open(db_file, std::ios::binary | std::ios::in | std::ios::out);
  }
}
} // namespace WalouDB
