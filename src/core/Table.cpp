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

bool Table::deleteRowByIndex(size_t index) {
  if (m_rows.size() <= index) {
    return false;
  } else {
    m_rows.erase(m_rows.begin() + index);
    return true;
  }
};

bool Table::deleteRowByValue(std::string column_name, Value value) {

  if (!hasColumn(column_name)) {
    return false;
  } else {
    return true;
  }
};
void Table::printColumns() const {
  for (const auto &column : m_columns) {
    std::cout << column.getName() << "\n";
  }
}
void Table::printColumnIndices() const {
  std::cout << "--- Column Indices ---\n";
  for (const auto &[column_name, index] : m_columnIndices) {
    std::cout << column_name << " -> " << index << "\n";
  }
}

} // namespace WalouDB
