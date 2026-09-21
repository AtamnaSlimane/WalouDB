#pragma once

#include "waloudb/storage/Database.h"

#include <memory>
#include <string>
#include <vector>

namespace WalouDB {

class DatabaseManager {
public:
  explicit DatabaseManager(const std::string &directory);

  bool createDatabase(const std::string &name);
  bool databaseExists(const std::string &name);
  bool removeDatabase(const std::string &name);

  bool openDatabase(const std::string &name);
  void closeDatabase();

  Database *getCurrentDatabase();

  BufferPoolManager *getBufferPoolManager();
  Catalog *getCatalog();

  std::vector<std::string> listDatabases() const;

  const std::string &getCurrentDatabaseName() const;

private:
  std::string databasePath(const std::string &name) const;

  std::string m_directory;

  std::unique_ptr<Database> m_current_db;
  std::string m_current_db_name;
};

} // namespace WalouDB
