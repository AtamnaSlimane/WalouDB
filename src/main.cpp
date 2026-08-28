#include "waloudb/core/ColumnType.h"
#include "waloudb/core/Database.h"
#include "waloudb/core/Table.h"
#include <iostream>
#include <string>

int main() {

  std::string input;
  WalouDB::Database db;

  db.createTable("users");
  db.createTable("users");
  db.createTable("posts");
  db.createTable("cache");
  db.getTable("cache").createColumn("name", WalouDB::ColumnType::String);
  // std::cout << db.getTable("cache").getColumn("name").getType()<<
  std::cout << "\n";
  std::cout << WalouDB::columnTypeToString(
                   db.getTable("cache").getColumn("name").getType())
            << "\n";
  std::cout << db.getTable("cache").getColumn("name").getName() << "\n";
  db.listTables();
  std::cout << db.getTable("users").getName() << "\n";
  db.dropTable("users");
  db.listTables();

  if (!db.createTable("posts")) {
    std::cout << "table already exists\n";
  }
  db.getTable("users");
  while (true) {
    std::cout << "WalouDB>> ";
    std::getline(std::cin, input);
    if (input == ".help") {
      std::cout << "Available commands: \nhelp\nexit\n";
    } else if (input == ".exit") {
      break;
    } else if (input == "create database") {

    } else {
      std::cout << "unrecognizable command\n";
    }
  }
  std::cout << "bye;\n";

  return 0;
}
