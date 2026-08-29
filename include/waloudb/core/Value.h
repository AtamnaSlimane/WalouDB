#pragma once

#include "ColumnType.h"
#include <string>
#include <variant>

namespace WalouDB {
using Value = std::variant<int, double, bool, std::string, float>;

inline bool valueMatchesColumnType(const Value &value, ColumnType type) {
  switch (type) {
  case ColumnType::Integer:
    return std::holds_alternative<int>(value);

  case ColumnType::Double:
    return std::holds_alternative<double>(value);

  case ColumnType::Boolean:
    return std::holds_alternative<bool>(value);

  case ColumnType::String:
    return std::holds_alternative<std::string>(value);

  case ColumnType::Float:
    return std::holds_alternative<float>(value);
  }

  return false;
}
} // namespace WalouDB
