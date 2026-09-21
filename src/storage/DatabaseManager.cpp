#include "waloudb/storage/DatabaseManager.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace WalouDB {

DatabaseManager::DatabaseManager(const std::string &directory)
    : m_directory(directory) {

  std::filesystem::create_directories(m_directory);
}

std::string DatabaseManager::databasePath(const std::string &name) const {

  return m_directory + "/" + name + ".db";
}

bool DatabaseManager::databaseExists(const std::string &name) {

  return std::filesystem::exists(databasePath(name));
}

bool DatabaseManager::createDatabase(const std::string &name) {

  if (name.empty() || databaseExists(name)) {
    return false;
  }

  {
    Database database(databasePath(name));
  }

  return databaseExists(name);
}

bool DatabaseManager::openDatabase(const std::string &name) {

  if (!databaseExists(name)) {
    return false;
  }

  closeDatabase();

  m_current_db = std::make_unique<Database>(databasePath(name));

  m_current_db_name = name;

  return true;
}

void DatabaseManager::closeDatabase() {

  m_current_db.reset();
  m_current_db_name.clear();
}

Database *DatabaseManager::getCurrentDatabase() { return m_current_db.get(); }

BufferPoolManager *DatabaseManager::getBufferPoolManager() {

  if (!m_current_db) {
    return nullptr;
  }

  return m_current_db->getBufferPoolManager();
}

Catalog *DatabaseManager::getCatalog() {

  if (!m_current_db) {
    return nullptr;
  }

  return m_current_db->getCatalog();
}

std::vector<std::string> DatabaseManager::listDatabases() const {

  std::vector<std::string> databases;

  if (!std::filesystem::exists(m_directory)) {
    return databases;
  }

  for (const auto &entry : std::filesystem::directory_iterator(m_directory)) {

    if (!entry.is_regular_file()) {
      continue;
    }

    if (entry.path().extension() != ".db") {
      continue;
    }

    databases.push_back(entry.path().stem().string());
  }

  return databases;
}

const std::string &DatabaseManager::getCurrentDatabaseName() const {

  return m_current_db_name;
}

bool DatabaseManager::removeDatabase(const std::string &name) {

  if (name == m_current_db_name) {
    return false;
  }

  return std::filesystem::remove(databasePath(name));
}

} // namespace WalouDB
