#pragma once

#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/Catalog.h"
#include "waloudb/storage/DiskManager.h"
namespace WalouDB {
class Database {
public:
  explicit Database(const std::string &db_file);
  ~Database();
  BufferPoolManager *getBufferPoolManager() { return m_bpm.get(); }
  Catalog *getCatalog() { return m_catalog.get(); }

private:
  std::unique_ptr<DiskManager> m_disk_manager;
  std::unique_ptr<BufferPoolManager> m_bpm;
  std::unique_ptr<Catalog> m_catalog;
};
} // namespace WalouDB
