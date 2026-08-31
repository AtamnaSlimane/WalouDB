#include "waloudb/common/Types.h"
#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
namespace WalouDB {

class DiskManager {
public:
  explicit DiskManager(const std::string &db_file);
  ~DiskManager();

  bool readPage(page_id_t page_id, char *page_data);
  bool writePage(page_id_t page_id, const char *page_data);
  page_id_t allocatePage() { return m_next_page_id.fetch_add(1); };

private:
  std::fstream m_db_io;
  std::string m_db_file;
  std::mutex m_db_io_latch;
  std::atomic<page_id_t> m_next_page_id{0};
};

} // namespace WalouDB
