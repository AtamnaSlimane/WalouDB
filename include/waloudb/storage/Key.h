#pragma once

#include "iostream"
#include "waloudb/storage/Value.h"
#include <string>

namespace WalouDB {

enum class KeyType : uint8_t { INTEGER, VARCHAR };

struct Key {
  KeyType type{KeyType::INTEGER};

  int32_t integer{};
  std::string string;

  static Key Integer(int32_t value) {
    Key key;
    key.type = KeyType::INTEGER;
    key.integer = value;
    return key;
  }

  static Key Varchar(const std::string &value) {
    Key key;
    key.type = KeyType::VARCHAR;
    key.string = value;
    return key;
  }
  std::string toString() const {
    if (type == KeyType::INTEGER) {
      return std::to_string(integer);
    }

    return string;
  }
  bool operator==(const Key &other) const {
    if (type != other.type)
      return false;

    if (type == KeyType::INTEGER)
      return integer == other.integer;

    return string == other.string;
  }

  bool operator!=(const Key &other) const { return !(*this == other); }

  bool operator<(const Key &other) const {
    if (type != other.type) {
      return static_cast<uint8_t>(type) < static_cast<uint8_t>(other.type);
    }

    if (type == KeyType::INTEGER)
      return integer < other.integer;

    return string < other.string;
  }

  bool operator<=(const Key &other) const {
    return *this < other || *this == other;
  }

  bool operator>(const Key &other) const { return other < *this; }

  bool operator>=(const Key &other) const { return !(*this < other); }
};
} // namespace WalouDB
