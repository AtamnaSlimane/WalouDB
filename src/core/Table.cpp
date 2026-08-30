#include "waloudb/core/Table.h"
#include "waloudb/core/ColumnType.h"
#include <iostream>
#include <memory>
#include <vector>

namespace WalouDB {
const std::string &Table::getName() const { return this->m_name; }
bool Table::createColumn(const std::string &name, const ColumnType &type) {
  if (hasColumn(name)) {
    return false;
  }

  size_t index = m_columns.size();

  m_columns.emplace_back(name, type);

  m_columnIndices[name] = index;

  return true;
}

Column &Table::getColumn(const std::string &name) {
  auto it = m_columnIndices.find(name);

  if (it == m_columnIndices.end()) {
    throw std::runtime_error("column " + name + " not found");
  }

  return m_columns[it->second];
}

bool Table::hasColumn(const std::string &name) const {
  return m_columnIndices.contains(name);
}

bool Table::insertRow(Row row) {

  // Check number of values
  if (row.size() != m_columns.size()) {
    return false;
  }
  const auto &values = row.getValues();

  for (size_t i = 0; i < m_columns.size(); ++i) {
    if (!valueMatchesColumnType(values[i], m_columns[i].getType())) {
      return false;
    }
  }

  // Everything is valid
  m_rows.push_back(std::move(row));

  return true;
}
void Table::printRow(const Row &row) {
  for (const auto &value : row.getValues()) {
    std::visit([](const auto &v) { std::cout << v << " "; }, value);
  }

  std::cout << '\n';
}
void Table::printRows(const std::vector<Row> &rows) {
  for (const auto &row : rows) {
    printRow(row);
  }
}

} // namespace WalouDB
