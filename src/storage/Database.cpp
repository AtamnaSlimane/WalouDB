#include "waloudb/storage/Database.h"
#include "waloudb/common/Types.h"
#include "waloudb/storage/Catalog.h"
#include "waloudb/storage/DiskManager.h"
#include <memory>
#include <string>

namespace WalouDB {
Database::Database(const std::string &db_file) {
  m_disk_manager = std::make_unique<DiskManager>(db_file);
  m_bpm = std::make_unique<BufferPoolManager>(BUFFER_POOL_SIZE,
                                              m_disk_manager.get());
  m_catalog = std::make_unique<Catalog>(m_bpm.get());
}
Database::~Database() {
  if (m_bpm) {
    m_bpm->flushAllPages();
  }
}
} // namespace WalouDB
