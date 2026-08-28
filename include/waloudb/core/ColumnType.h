#pragma once

#include <string>

namespace WalouDB {
enum class ColumnType { Integer, Float, Double, Boolean, String };

constexpr std::string_view columnTypeToString(ColumnType type) noexcept {
  switch (type) {
  case ColumnType::Integer:
    return "Integer";
  case ColumnType::Float:
    return "Float";
  case ColumnType::Double:
    return "Double";
  case ColumnType::Boolean:
    return "Boolean";
  case ColumnType::String:
    return "String";
  default:
    return "Unknown";
  }
}
} // namespace WalouDB
