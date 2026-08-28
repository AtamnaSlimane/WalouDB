#pragma once

#include <stdlib.h>
#include <string>
#include <unordered_map>

namespace WalouDB {
class Database {
public:
  Database();

  void createTable(const std::string &);
  bool hasTable(const std::string &);
  void getTable(const std::string &); // void for now until table class is done
  void dropTable(const std::string &);
  void listTables();

private:
  std::unordered_map<std::string, std::string>
      tables; // string should be replaced with table class
};

} // namespace WalouDB
