#pragma once

#include "Column.h"
#include "Row.h"
#include <memory>
#include <string>
#include <unordered_map>
namespace WalouDB {

class Table {

public:
  explicit Table(const std::string &name) : m_name(name) {}

  const std::string &getName() const;
  bool createColumn(const std::string &name, const ColumnType &type);
  bool insertRow(Row rows);
  Column &getColumn(const std::string &name);
  bool hasColumn(const std::string &name) const;

private:
  std::string m_name;
  std::vector<Column> m_columns;
  std::vector<Row> m_rows;
  std::unordered_map<std::string, size_t> m_columnIndices;
};

} // namespace WalouDB
