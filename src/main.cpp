#include "waloudb/core/ColumnType.h"
#include "waloudb/core/Database.h"
#include "waloudb/core/Row.h"
#include "waloudb/core/Table.h"
#include "waloudb/core/Value.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

int main() {
  using namespace WalouDB;

  Database db;

  // ========================================
  // Create table
  // ========================================

  db.createTable("users");

  Table &users = db.getTable("users");

  // ========================================
  // Create columns
  // ========================================

  users.createColumn("id", ColumnType::Integer);
  users.createColumn("name", ColumnType::String);
  users.createColumn("age", ColumnType::Integer);
  users.createColumn("active", ColumnType::Boolean);
  users.createColumn("balance", ColumnType::Double);

  std::cout << "Users table created.\n\n";

  // ========================================
  // Check columns
  // ========================================

  std::cout << "Columns:\n";

  std::cout << "id      -> "
            << columnTypeToString(users.getColumn("id").getType()) << "\n";

  std::cout << "name    -> "
            << columnTypeToString(users.getColumn("name").getType()) << "\n";

  std::cout << "age     -> "
            << columnTypeToString(users.getColumn("age").getType()) << "\n";

  std::cout << "active  -> "
            << columnTypeToString(users.getColumn("active").getType()) << "\n";

  std::cout << "balance -> "
            << columnTypeToString(users.getColumn("balance").getType())
            << "\n\n";

  // ========================================
  // Valid row
  // ========================================

  Row validRow({1, std::string("Slimane"), 22, true, 1500.50});

  if (users.insertRow(std::move(validRow))) {
    std::cout << "[OK] Valid row inserted.\n";
  } else {
    std::cout << "[FAIL] Valid row rejected.\n";
  }

  // ========================================
  // Invalid row - wrong type
  // ========================================

  Row invalidTypeRow({2, std::string("Midou"),
                      std::string("twenty"), // ❌ should be Integer
                      true, 500.0});

  if (users.insertRow(std::move(invalidTypeRow))) {
    std::cout << "[FAIL] Invalid type was accepted.\n";
  } else {
    std::cout << "[OK] Invalid type rejected.\n";
  }

  // ========================================
  // Invalid row - wrong number of values
  // ========================================

  Row invalidSizeRow({
      3, std::string("Mami"), 25
      // missing active and balance
  });

  if (users.insertRow(std::move(invalidSizeRow))) {
    std::cout << "[FAIL] Invalid row size was accepted.\n";
  } else {
    std::cout << "[OK] Invalid row size rejected.\n";
  }

  // ========================================
  // Another valid row
  // ========================================

  Row secondRow({4, std::string("Alice"), 30, false, 3200.75});

  if (users.insertRow(std::move(secondRow))) {
    std::cout << "[OK] Second valid row inserted.\n";
  } else {
    std::cout << "[FAIL] Second valid row rejected.\n";
  }

  // ========================================
  // Test float vs double
  // ========================================

  Row floatTest({
      5, std::string("Bob"), 28, true,
      100.0f // ❌ Column expects Double
  });

  if (users.insertRow(std::move(floatTest))) {
    std::cout << "[FAIL] Float accepted as Double.\n";
  } else {
    std::cout << "[OK] Float rejected for Double column.\n";
  }

  // ========================================
  // Tables
  // ========================================

  std::cout << "\nTables:\n";
  db.listTables();

  return 0;
}
