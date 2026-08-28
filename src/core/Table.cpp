#include "waloudb/core/Table.h"
#include "waloudb/core/ColumnType.h"
#include <iostream>
#include <memory>

namespace WalouDB {
const std::string &Table::getName() const { return this->m_name; }
bool Table::createColumn(const std::string &name, const ColumnType &type) {
  if (columns.contains(name)) {
    std::cout << "column " << name << "already exists";
    return false;
  } else {
    columns[name] = std::make_unique<Column>(name, type);
    return true;
  }
}
Column &Table::getColumn(const std::string &name) {
  if (!this->hasColumn(name)) {
    throw std::runtime_error("column " + name + " not found");
  }

  return *columns.at(name);
}
bool Table::hasColumn(const std::string &name) const {
  return columns.contains(name);
}
} // namespace WalouDB
