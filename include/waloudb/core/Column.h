#pragma once
#include "ColumnType.h"
#include <cstddef>

namespace WalouDB {
class Column {

public:
  explicit Column(const std::string &name, const ColumnType &type)
      : m_name(name), m_type(type) {};

  const std::string &getName() const { return m_name; };
  ColumnType getType() const { return m_type; };

private:
  std::string m_name;
  ColumnType m_type;
};
} // namespace WalouDB
