#include "waloudb/storage/Key.h"

namespace WalouDB {

bool operator<(const Key &a, const Key &b) {
  if (a.type != b.type) {
    return static_cast<uint8_t>(a.type) < static_cast<uint8_t>(b.type);
  }

  if (a.type == KeyType::INTEGER) {
    return a.integer < b.integer;
  }

  return a.string < b.string;
}

bool operator==(const Key &a, const Key &b) {
  if (a.type != b.type) {
    return false;
  }

  if (a.type == KeyType::INTEGER) {
    return a.integer == b.integer;
  }

  return a.string == b.string;
}

} // namespace WalouDB
