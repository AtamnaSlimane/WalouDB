#pragma once
#include "ColumnType.h"

namespace WalouDB {
class Column {

public:
  explicit Column(const std::string &name, const ColumnType &type)
      : m_name(name), m_type(type) {};

  const std::string &getName() const;
  ColumnType getType() const;

private:
  std::string m_name;
  ColumnType m_type;
};
} // namespace WalouDB
