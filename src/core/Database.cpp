#include "waloudb/core/Database.h"
#include <cstddef>
#include <iostream>
#include <string>

namespace WalouDB {

bool Database::createTable(const std::string &name) {
  if (tables.contains(name)) {
    std::cout << "table " << name << "already exists";
    return false;
  } else {
    tables[name] = std::make_unique<Table>(name);
    return true;
  }
}

void Database::listTables() const {
  for (const auto &[name, table] : tables) {
    std::cout << name << "\n";
  }
}

bool Database::hasTable(const std::string &name) const {
  return tables.contains(name);
}

Table &Database::getTable(const std::string &name) {
  if (!this->hasTable(name)) {
    throw std::runtime_error("table " + name + " not found");
  }

  return *tables.at(name);
}
bool Database::dropTable(const std::string &name) {
  return tables.erase(name) > 0;
}

} // namespace WalouDB
