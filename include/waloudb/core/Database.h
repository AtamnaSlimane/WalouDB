#pragma once

#include "Table.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace WalouDB {
class Database {
public:
  const std::unordered_map<std::string, std::unique_ptr<Table>> &
  getAllTables() const {
    return tables;
  };

  bool createTable(const std::string &);
  bool hasTable(const std::string &) const;
  Table &getTable(const std::string &);
  bool dropTable(const std::string &);
  void listTables() const;

private:
  std::unordered_map<std::string, std::unique_ptr<Table>> tables;
};

} // namespace WalouDB
