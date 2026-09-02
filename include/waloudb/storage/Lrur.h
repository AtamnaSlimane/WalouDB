#pragma once

#include "waloudb/common/Types.h"
#include <cstddef>
#include <list>
#include <mutex>
#include <unordered_map>
namespace WalouDB {
// least recently Used Pages
class Lrur {
public:
  explicit Lrur(size_t num_frames) {};
  bool Victim(frame_id_t *frame_id);
  void Pin(frame_id_t frame_id);
  void Unpin(frame_id_t frame_id);
  bool contains(frame_id_t frame_id) const {
    std::lock_guard<std::mutex> guard(m_latch);
    return m_position.count(frame_id) > 0;
  }

  size_t size() const {
    std::lock_guard<std::mutex> guard(m_latch);
    return m_lru_list.size();
  }

private:
  std::list<frame_id_t> m_lru_list; // candidates for a Victim at the back
  std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> m_position;
  mutable std::mutex m_latch;
};
} // namespace WalouDB
