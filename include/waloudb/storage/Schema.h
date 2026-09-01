
#pragma once
#include "Value.h"
#include <string>
#include <vector>

struct Column {
  std::string name;
  TypeId type;
};

class Schema {
public:
  explicit Schema(std::vector<Column> columns)
      : m_columns(std::move(columns)) {}
  size_t getColumnCount() const { return m_columns.size(); }
  const Column &getColumn(size_t idx) const { return m_columns[idx]; }

private:
  std::vector<Column> m_columns;
};
