#include "waloudb/storage/Lrur.h"
#include "waloudb/common/Types.h"
#include <algorithm>

namespace WalouDB {

bool Lrur::Victim(frame_id_t *frame_id) {
  std::lock_guard<std::mutex> lock(m_latch);
  if (m_lru_list.empty()) {
    return false;
  }
  *frame_id = m_lru_list.back();
  m_position.erase(*frame_id);
  m_lru_list.pop_back();
  return true;
}
void Lrur::Pin(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(m_latch);
  auto it = m_position.find(frame_id);
  if (it != m_position.end()) {
    m_lru_list.erase(it->second); // iterator to node smthng
    m_position.erase(it);
  }
}
void Lrur::Unpin(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(m_latch);

  if (m_position.find(frame_id) != m_position.end())
    return;

  m_lru_list.push_front(frame_id);
  m_position[frame_id] = m_lru_list.begin();
}

} // namespace WalouDB
