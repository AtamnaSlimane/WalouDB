
#include "waloudb/common/Types.h"
#include <fstream>
#include <string>
namespace WalouDB {

class DiskManager {
public:
  explicit DiskManager(const std::string &db_file);
  ~DiskManager();

  bool readPage(page_id_t page_id, char *page_data);
  bool writePage(page_id_t page_id, const char *page_data);
  page_id_t allocatePage();

private:
  std::fstream m_db_io;
  std::string m_db_file;
};

} // namespace WalouDB
