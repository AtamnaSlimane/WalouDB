#pragma once

#include "Column.h"
#include "Row.h"
#include "waloudb/core/Value.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
namespace WalouDB {

class Table {

public:
  explicit Table(const std::string &name) : m_name(name) {}

  const std::string &getName() const;
  bool createColumn(const std::string &name, const ColumnType &type);
  bool insertRow(Row rows);
  Column &getColumn(const std::string &name);

  const Row &getRowByIndex(size_t index) const { return m_rows.at(index); }
  const std::vector<Row> &getRowByValue(std::string column_name,
                                        Value value) const;

  const std::vector<Row> &getAllRows() const { return this->m_rows; };

  bool deleteRowByIndex(size_t index);
  bool deleteRowByValue(std::string column_name, Value value);

  bool hasColumn(const std::string &name) const;
  void printRow(const Row &row);
  void printRows(const std::vector<Row> &rows);
  void printColumns() const;

  void printColumnIndices() const;

private:
  std::string m_name;
  std::vector<Column> m_columns;
  std::vector<Row> m_rows;
  std::unordered_map<std::string, size_t> m_columnIndices;
};

} // namespace WalouDB
