#pragma once

#include "Column.h"
#include <memory>
#include <string>
#include <unordered_map>
namespace WalouDB {

class Table {

public:
  explicit Table(const std::string &name) : m_name(name) {}

  const std::string &getName() const;
  bool createColumn(const std::string &name, const ColumnType &type);
  Column &getColumn(const std::string &name);
  bool hasColumn(const std::string &name) const;

private:
  std::string m_name;
  std::unordered_map<std::string, std::unique_ptr<Column>> columns;
};

} // namespace WalouDB
