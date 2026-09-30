#pragma once
#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/DiskManager.h"
#include <cstdint>
#include <string>
namespace WalouDB {

struct ToastPointer {
  page_id_t first_page_id{INVALID_PAGE_ID};
  uint32_t total_length{0}; // what to read "post compression if true"
  uint32_t raw_length{0};   // pre compression
  bool isCompressed{false};
};
class ToastManager {
public:
  ToastManager(const std::string &toast_file_path, size_t pool_size);

  bool insertToast(const char *data, uint32_t length, ToastPointer *out_ptr);
  bool readToast(const ToastPointer &ptr, std::vector<char> *out_data) const;
  bool removeToast(const ToastPointer &ptr);

private:
  std::unique_ptr<DiskManager> m_disk_manager; // "database.toast" — one per DB
  std::unique_ptr<BufferPoolManager> m_bpm;
};

} // namespace WalouDB
