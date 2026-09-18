#include "faker-cxx/faker.h"
#include "faker-cxx/person.h"
#include "waloudb/common/Types.h"
#include "waloudb/storage/BPlusTree.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/Catalog.h"
#include "waloudb/storage/DiskManager.h"
#include "waloudb/storage/Page.h"
#include "waloudb/storage/Schema.h"
#include "waloudb/storage/SlottedPage.h"
#include "waloudb/storage/TableHeap.h"
#include "waloudb/storage/Tuple.h"
#include "waloudb/storage/Value.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <strings.h>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace WalouDB;

// ============================================================
// Configuration
// ============================================================

constexpr size_t BUFFER_POOL_SIZE = 4096 * 10;
constexpr const char *DATABASE_FILE = "waloudb.db";

// ============================================================
// Runtime table state
// ============================================================

struct TableCreationInfo {
  Schema schema;
  std::string primary_column;
};

// ============================================================
// Input helpers
// ============================================================

void clearInput() {
  std::cin.clear();
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

int readInt(const std::string &prompt) {
  while (true) {
    std::cout << prompt;

    int value{};

    if (std::cin >> value) {
      clearInput();
      return value;
    }

    std::cout << "Invalid input. Please enter a number.\n";
    clearInput();
  }
}

std::string readString(const std::string &prompt) {
  std::cout << prompt;

  std::string value;
  std::getline(std::cin >> std::ws, value);

  return value;
}

// ============================================================
// Visual helpers
// ============================================================

void printLine(char c = '-', int width = 70) {
  std::cout << std::string(width, c) << '\n';
}

void printTitle(const std::string &title) {
  std::cout << '\n';
  printLine('=');
  std::cout << "  " << title << '\n';
  printLine('=');
}

void printBorder() { printLine('-'); }

// ============================================================
// Schema helpers
// ============================================================

Schema schemaFromMetadata(const TableMetadata &meta) {
  return Schema(meta.columns);
}

bool findColumnIndex(const Schema &schema, const std::string &name,
                     size_t &index) {
  for (size_t i = 0; i < schema.getColumnCount(); ++i) {
    if (schema.getColumn(i).name == name) {
      index = i;
      return true;
    }
  }

  return false;
}

bool isIntegerPrimaryColumn(const Schema &schema,
                            const std::string &primary_column) {
  size_t index{};

  if (!findColumnIndex(schema, primary_column, index)) {
    return false;
  }

  return schema.getColumn(index).type == TypeId::INTEGER;
}

// ============================================================
// Tuple printing
// ============================================================

void printTupleValues(const Tuple &tuple, const Schema &schema) {
  for (size_t i = 0; i < schema.getColumnCount(); ++i) {
    const Column &column = schema.getColumn(i);

    std::cout << std::left << std::setw(18) << column.name << ": ";

    Value value = tuple.getValue(schema, i);

    if (value.getType() == TypeId::INTEGER) {
      std::cout << value.getInteger();
    } else if (value.getType() == TypeId::VARCHAR) {
      std::cout << value.getString();
    } else {
      std::cout << "NULL";
    }

    std::cout << '\n';
  }
}

// ============================================================
// Buffer pool visualization
// ============================================================

void printBufferPool(BufferPoolManager &bpm) {
  printTitle("BUFFER POOL");

  std::cout << std::left << std::setw(10) << "Frame" << std::setw(12)
            << "Page ID" << std::setw(12) << "Pin Count" << std::setw(10)
            << "Dirty" << '\n';

  printLine();

  for (size_t i = 0; i < bpm.getPoolSize(); ++i) {
    frame_id_t frame_id = static_cast<frame_id_t>(i);

    std::cout << std::left << std::setw(10) << frame_id << std::setw(12)
              << bpm.getFramePageId(frame_id) << std::setw(12)
              << bpm.getFramePinCount(frame_id) << std::setw(10)
              << (bpm.getFrameDirty(frame_id) ? "yes" : "no") << '\n';
  }
}

void printLRU(BufferPoolManager &bpm) {
  printTitle("LRU REPLACER");

  auto frames = bpm.getReplacer().getFrames();

  if (frames.empty()) {
    std::cout << "LRU is empty.\n";
    return;
  }

  std::cout << "LRU order (victim -> newest):\n";

  for (frame_id_t frame : frames) {
    std::cout << "  Frame " << frame << " -> Page " << bpm.getFramePageId(frame)
              << '\n';
  }
}

// ============================================================
// Flush
// ============================================================

void flushAllPages(BufferPoolManager &bpm) {
  printTitle("FLUSH ALL PAGES");

  if (bpm.flushAllPages()) {
    std::cout << "All dirty pages flushed to disk.\n";
  } else {
    std::cout << "Some pages could not be flushed.\n";
  }
}

// ============================================================
// Primary-index helpers
// ============================================================

bool getPrimaryKeyFromTuple(const Tuple &tuple, const Schema &schema,
                            const std::string &primary_column, int32_t &key) {
  size_t column_index{};

  if (!findColumnIndex(schema, primary_column, column_index)) {
    return false;
  }

  Value value = tuple.getValue(schema, column_index);

  if (value.getType() != TypeId::INTEGER) {
    return false;
  }

  key = value.getInteger();
  return true;
}

void buildIndex(TableHeap &table, const Schema &schema, BPlusTree &index,
                size_t column_index) {
  int indexed = 0;
  int duplicates = 0;
  int failed = 0;

  for (auto it = table.begin(); it != table.end(); ++it) {
    Tuple tuple = *it;
    RID rid = it.getRID();

    Value value = tuple.getValue(schema, column_index);

    bool inserted = false;

    if (value.getType() == TypeId::INTEGER) {
      inserted = index.insert(Key::Integer(value.getInteger()), rid);
    } else if (value.getType() == TypeId::VARCHAR) {
      inserted = index.insert(Key::Varchar(value.getString()), rid);
    } else {
      ++failed;
      continue;
    }

    if (inserted) {
      ++indexed;
    } else {
      ++duplicates;
    }
  }

  std::cout << "Indexed rows : " << indexed << '\n';
  std::cout << "Duplicates   : " << duplicates << '\n';

  if (failed > 0) {
    std::cout << "Unsupported   : " << failed << '\n';
  }
}

bool buildPrimaryIndex(TableHeap &table, const Schema &schema,
                       BPlusTree &primary_index,
                       const std::string &primary_column) {
  size_t column_index{};

  if (!findColumnIndex(schema, primary_column, column_index)) {
    std::cout << "[FAILED] Primary column not found.\n";
    return false;
  }

  if (schema.getColumn(column_index).type != TypeId::INTEGER) {
    std::cout << "[FAILED] Primary key must currently be INTEGER.\n";
    return false;
  }

  int indexed = 0;
  int duplicates = 0;
  int invalid = 0;

  for (auto it = table.begin(); it != table.end(); ++it) {
    Tuple tuple = *it;
    RID rid = it.getRID();

    Value value = tuple.getValue(schema, column_index);

    if (value.getType() != TypeId::INTEGER) {
      ++invalid;
      continue;
    }

    if (primary_index.insert(Key::Integer(value.getInteger()), rid)) {
      ++indexed;
    } else {
      ++duplicates;
    }
  }

  std::cout << "Indexed rows : " << indexed << '\n';
  std::cout << "Duplicates   : " << duplicates << '\n';

  if (invalid > 0) {
    std::cout << "Invalid rows : " << invalid << '\n';
  }

  return duplicates == 0 && invalid == 0;
}

// ============================================================
// Table creation
// ============================================================

TableCreationInfo createTableSchemaInteractive() {
  std::vector<Column> columns;

  printTitle("CREATE TABLE SCHEMA");

  int column_count{};

  while (true) {
    column_count = readInt("Number of columns: ");

    if (column_count > 0) {
      break;
    }

    std::cout << "[FAILED] Table must have at least one column.\n";
  }

  for (int i = 0; i < column_count; ++i) {
    std::cout << "\nColumn " << (i + 1) << '\n';

    std::string name;

    while (true) {
      name = readString("Column name: ");

      if (name.empty()) {
        std::cout << "[FAILED] Column name cannot be empty.\n";
        continue;
      }

      bool duplicate = false;

      for (const Column &column : columns) {
        if (column.name == name) {
          duplicate = true;
          break;
        }
      }

      if (duplicate) {
        std::cout << "[FAILED] Column already exists.\n";
        continue;
      }

      break;
    }

    std::cout << "\n";
    std::cout << "  1. INTEGER\n";
    std::cout << "  2. VARCHAR\n";

    int type_choice{};

    while (true) {
      type_choice = readInt("Type: ");

      if (type_choice == 1 || type_choice == 2) {
        break;
      }

      std::cout << "[FAILED] Invalid type.\n";
    }

    TypeId type = type_choice == 1 ? TypeId::INTEGER : TypeId::VARCHAR;

    columns.push_back({name, type});
  }

  std::cout << "\nSchema:\n";

  for (const Column &column : columns) {
    std::cout << "  " << column.name << ' ';

    if (column.type == TypeId::INTEGER) {
      std::cout << "INTEGER";
    } else if (column.type == TypeId::VARCHAR) {
      std::cout << "VARCHAR";
    }

    std::cout << '\n';
  }

  std::cout << "\nPrimary key column:\n";

  for (size_t i = 0; i < columns.size(); ++i) {
    std::cout << "  " << (i + 1) << ". " << columns[i].name << ' ';

    if (columns[i].type == TypeId::INTEGER) {
      std::cout << "INTEGER";
    } else {
      std::cout << "VARCHAR";
    }

    std::cout << '\n';
  }

  int primary_choice{};

  while (true) {
    primary_choice = readInt("Choose primary key column: ");

    if (primary_choice >= 1 &&
        primary_choice <= static_cast<int>(columns.size())) {
      break;
    }

    std::cout << "[FAILED] Invalid column.\n";
  }

  const Column &primary_column = columns[primary_choice - 1];

  if (primary_column.type != TypeId::INTEGER) {
    std::cout << "\n[FAILED] Primary key must currently be INTEGER.\n";
    return createTableSchemaInteractive();
  }

  return {Schema(columns), primary_column.name};
}

// ============================================================
// Open a table and all of its indexes
// ============================================================

bool openTable(Catalog &catalog, BufferPoolManager &bpm,
               const std::string &table_name, TableMetadata *&current_meta,
               std::unique_ptr<TableHeap> &current_table,
               Schema &current_schema, std::string &current_primary_column,
               std::unique_ptr<BPlusTree> &current_primary_index,
               std::unordered_map<std::string, std::unique_ptr<BPlusTree>>
                   &current_secondary_indexes) {

  TableMetadata *meta = catalog.getTable(table_name);

  if (meta == nullptr) {
    std::cout << "\n[FAILED] Table '" << table_name << "' does not exist.\n";
    return false;
  }

  current_meta = meta;
  current_schema = schemaFromMetadata(*meta);

  const std::string primary_index_name = table_name + "_pk";
  IndexMetadata *primary_meta = catalog.getIndex(primary_index_name);

  // ----------------------------------------------------------
  // Find the primary column from persisted metadata.
  // ----------------------------------------------------------

  if (primary_meta != nullptr && !primary_meta->column_name.empty()) {
    current_primary_column = primary_meta->column_name;
  } else {
    // Compatibility fallback for older databases.
    current_primary_column = "id";

    if (!isIntegerPrimaryColumn(current_schema, current_primary_column)) {
      std::cout << "[FAILED] Could not determine the primary key column.\n";
      return false;
    }
  }

  if (!isIntegerPrimaryColumn(current_schema, current_primary_column)) {
    std::cout << "[FAILED] Primary key column '" << current_primary_column
              << "' is missing or is not INTEGER.\n";
    return false;
  }

  // ----------------------------------------------------------
  // Table heap
  // ----------------------------------------------------------

  current_table = std::make_unique<TableHeap>(&bpm, meta->first_page_id);

  // ----------------------------------------------------------
  // Primary index
  // ----------------------------------------------------------

  if (primary_meta != nullptr &&
      primary_meta->root_page_id != INVALID_PAGE_ID) {

    current_primary_index =
        std::make_unique<BPlusTree>(&bpm, primary_meta->root_page_id);

  } else {
    current_primary_index = std::make_unique<BPlusTree>(&bpm);

    std::cout << "\nBuilding primary index '" << primary_index_name << "'...\n";

    buildPrimaryIndex(*current_table, current_schema, *current_primary_index,
                      current_primary_column);

    if (primary_meta == nullptr) {
      primary_meta = catalog.createIndex(primary_index_name, table_name,
                                         current_primary_column,
                                         current_primary_index->getRootId());

      if (primary_meta == nullptr) {
        std::cerr << "[ERROR] Could not create primary index metadata.\n";
        current_primary_index.reset();
        current_table.reset();
        current_meta = nullptr;
        return false;
      }
    } else {
      if (!catalog.updateIndexRoot(primary_index_name,
                                   current_primary_index->getRootId())) {
        std::cerr << "[ERROR] Could not persist primary index root.\n";
        return false;
      }
    }
  }

  // ----------------------------------------------------------
  // Root persistence callback
  // ----------------------------------------------------------

  current_primary_index->setRootChangeCallback(
      [&catalog, primary_index_name](page_id_t new_root_id) {
        if (!catalog.updateIndexRoot(primary_index_name, new_root_id)) {
          std::cerr << "[ERROR] Failed to persist root for index '"
                    << primary_index_name << "'.\n";
        }
      });

  // ----------------------------------------------------------
  // Secondary indexes
  // ----------------------------------------------------------

  current_secondary_indexes.clear();

  const std::vector<IndexMetadata *> indexes =
      catalog.getIndexesForTable(table_name);

  for (IndexMetadata *index_meta : indexes) {
    if (index_meta == nullptr) {
      continue;
    }

    if (index_meta->name == primary_index_name) {
      continue;
    }

    if (index_meta->root_page_id == INVALID_PAGE_ID) {
      continue;
    }

    if (index_meta->column_name.empty()) {
      continue;
    }

    size_t column_index{};

    if (!findColumnIndex(current_schema, index_meta->column_name,
                         column_index)) {
      std::cerr << "[WARNING] Ignoring index '" << index_meta->name
                << "': column no longer exists.\n";
      continue;
    }

    auto index = std::make_unique<BPlusTree>(&bpm, index_meta->root_page_id);

    const std::string index_name = index_meta->name;
    const std::string column_name = index_meta->column_name;

    index->setRootChangeCallback([&catalog, index_name](page_id_t new_root_id) {
      if (!catalog.updateIndexRoot(index_name, new_root_id)) {
        std::cerr << "[ERROR] Failed to persist root for index '" << index_name
                  << "'.\n";
      }
    });

    current_secondary_indexes[column_name] = std::move(index);

    std::cout << "[INDEX] Loaded '" << index_name << "' on column '"
              << column_name << "'.\n";
  }

  return true;
}

// ============================================================
// Rebuild primary index
// ============================================================

bool rebuildPrimaryIndex(Catalog &catalog, BufferPoolManager &bpm,
                         TableMetadata *meta, TableHeap &table,
                         const Schema &schema,
                         const std::string &primary_column,
                         std::unique_ptr<BPlusTree> &primary_index) {

  if (meta == nullptr) {
    return false;
  }

  if (!isIntegerPrimaryColumn(schema, primary_column)) {
    std::cout << "[FAILED] Primary key must be an INTEGER column.\n";
    return false;
  }

  printTitle("REBUILD PRIMARY INDEX");

  auto rebuilt = std::make_unique<BPlusTree>(&bpm);

  const std::string index_name = meta->name + "_pk";

  if (!buildPrimaryIndex(table, schema, *rebuilt, primary_column)) {
    std::cout << "[FAILED] Index rebuild encountered invalid or duplicate "
                 "keys.\n";
    return false;
  }

  if (!catalog.updateIndexRoot(index_name, rebuilt->getRootId())) {
    std::cerr << "[ERROR] Failed to persist rebuilt index root.\n";
    return false;
  }

  rebuilt->setRootChangeCallback([&catalog, index_name](page_id_t new_root_id) {
    if (!catalog.updateIndexRoot(index_name, new_root_id)) {
      std::cerr << "[ERROR] Failed to persist root for index '" << index_name
                << "'.\n";
    }
  });

  primary_index = std::move(rebuilt);

  std::cout << "\n[SUCCESS] Primary index rebuilt.\n";
  std::cout << "New root page: " << primary_index->getRootId() << '\n';

  return true;
}

// ============================================================
// Table insert
// ============================================================

bool insertIntoTable(TableHeap &table, const Schema &schema,
                     BPlusTree &primary_index,
                     const std::string &primary_column) {
  printTitle("TABLE INSERT");

  std::vector<Value> values;
  values.reserve(schema.getColumnCount());

  int32_t primary_key{};

  for (size_t i = 0; i < schema.getColumnCount(); ++i) {
    const Column &column = schema.getColumn(i);

    std::cout << "\nColumn: " << column.name << '\n';

    if (column.type == TypeId::INTEGER) {
      int32_t value = static_cast<int32_t>(readInt("Enter INTEGER value: "));

      if (column.name == primary_column) {
        primary_key = value;
      }

      values.emplace_back(value);
    } else if (column.type == TypeId::VARCHAR) {
      values.emplace_back(readString("Enter VARCHAR value: "));
    } else {
      std::cout << "[FAILED] Unsupported column type.\n";
      return false;
    }
  }

  RID existing_rid{};

  if (primary_index.search(Key::Integer(primary_key), &existing_rid)) {
    Tuple existing_tuple;

    if (table.getTuple(existing_rid, &existing_tuple)) {
      std::cout << "\n[FAILED] Primary key already exists.\n";
      return false;
    }

    std::cout << "\n[WARNING] Found a stale primary-index entry. "
                 "Rebuild the index before inserting this key.\n";
    return false;
  }

  Tuple tuple = Tuple::Serialize(values, schema);

  RID rid{};

  if (!table.insertTuple(tuple, &rid)) {
    std::cout << "\n[FAILED] Table insertion failed.\n";
    return false;
  }

  if (!primary_index.insert(Key::Integer(primary_key), rid)) {
    std::cout << "\n[FAILED] Primary index insertion failed.\n";
    std::cout << "The tuple was inserted, but is not indexed.\n";
    return false;
  }

  std::cout << "\n[SUCCESS]\n";
  std::cout << "Primary key = " << primary_key << '\n';
  std::cout << "RID = (" << rid.page_id << ", " << rid.slot_num << ")\n";

  return true;
}

// ============================================================
// Get tuple by RID
// ============================================================

bool getFromTable(TableHeap &table, const Schema &schema) {
  printTitle("TABLE GET");

  int page_id = readInt("Enter page ID: ");
  int slot_num = readInt("Enter slot number: ");

  if (page_id < 0 || slot_num < 0 ||
      slot_num > std::numeric_limits<uint16_t>::max()) {
    std::cout << "[FAILED] Invalid RID.\n";
    return false;
  }

  RID rid{
      static_cast<page_id_t>(page_id),
      static_cast<uint16_t>(slot_num),
  };

  Tuple tuple;

  if (!table.getTuple(rid, &tuple)) {
    std::cout << "\n[NOT FOUND]\n";
    return false;
  }

  printBorder();
  std::cout << "RID = (" << rid.page_id << ", " << rid.slot_num << ")\n";
  printBorder();

  printTupleValues(tuple, schema);

  printBorder();

  return true;
}

// ============================================================
// Generic table update
// ============================================================

bool updateInTable(TableHeap &table, const Schema &schema,
                   BPlusTree &primary_index,
                   const std::string &primary_column) {
  printTitle("TABLE UPDATE");

  int id = readInt("Enter primary key: ");

  if (id < 0) {
    std::cout << "[FAILED] Invalid primary key.\n";
    return false;
  }

  RID rid{};

  if (!primary_index.search(Key::Integer(id), &rid)) {
    std::cout << "\n[NOT FOUND] Primary key does not exist.\n";
    return false;
  }

  Tuple old_tuple;

  if (!table.getTuple(rid, &old_tuple)) {
    std::cout << "\n[FAILED] Primary index points to a missing tuple.\n";
    return false;
  }

  std::vector<Value> values;
  values.reserve(schema.getColumnCount());

  for (size_t i = 0; i < schema.getColumnCount(); ++i) {
    const Column &column = schema.getColumn(i);

    if (column.name == primary_column) {
      values.emplace_back(static_cast<int32_t>(id));
      continue;
    }

    std::cout << "\nColumn: " << column.name << '\n';

    if (column.type == TypeId::INTEGER) {
      values.emplace_back(
          static_cast<int32_t>(readInt("Enter new INTEGER value: ")));
    } else if (column.type == TypeId::VARCHAR) {
      values.emplace_back(readString("Enter new VARCHAR value: "));
    } else {
      std::cout << "[FAILED] Unsupported column type.\n";
      return false;
    }
  }

  Tuple new_tuple = Tuple::Serialize(values, schema);

  if (!table.updateTuple(rid, new_tuple)) {
    std::cout << "\n[FAILED] Update failed.\n";
    return false;
  }

  std::cout << "\n[SUCCESS] Tuple updated.\n";
  std::cout << "RID = (" << rid.page_id << ", " << rid.slot_num << ")\n";

  return true;
}

// ============================================================
// Delete
//
// The current BPlusTree API has no delete() operation. Therefore,
// after deleting a tuple we rebuild the primary index so it does
// not retain a stale RID.
// ============================================================

bool deleteFromTable(Catalog &catalog, BufferPoolManager &bpm,
                     TableMetadata *meta, TableHeap &table,
                     const Schema &schema, const std::string &primary_column,
                     std::unique_ptr<BPlusTree> &primary_index) {
  printTitle("TABLE DELETE");

  int id = readInt("Enter primary key: ");

  if (id < 0) {
    std::cout << "[FAILED] Invalid primary key.\n";
    return false;
  }

  RID rid{};

  if (!primary_index->search(Key::Integer(id), &rid)) {
    std::cout << "\n[NOT FOUND] Primary key does not exist.\n";
    return false;
  }

  Tuple tuple;

  if (!table.getTuple(rid, &tuple)) {
    std::cout << "\n[FAILED] Primary index contains a stale RID.\n";
    return false;
  }

  if (!primary_index->remove(Key::Integer(id))) {
    std::cout << "\n[FAILED] Table deletion failed.\n";
    return false;
  }

  std::cout << "\n[SUCCESS] Tuple deleted from table.\n";
  std::cout << "RID = (" << rid.page_id << ", " << rid.slot_num << ")\n";

  return true;
}

// ============================================================
// Dummy rows
// ============================================================

void insertDummyRows(
    TableHeap &table, const Schema &schema, BPlusTree &primary_index,
    const std::unordered_map<std::string, std::unique_ptr<BPlusTree>>
        &secondary_indexes,
    const std::string &table_name, const std::string &primary_column) {

  printTitle("INSERT DUMMY ROWS");

  int count = readInt("How many rows? ");

  if (count <= 0) {
    std::cout << "Nothing to insert.\n";
    return;
  }

  size_t primary_column_index{};

  if (!findColumnIndex(schema, primary_column, primary_column_index)) {
    std::cout << "[FAILED] Primary column does not exist.\n";
    return;
  }

  if (schema.getColumn(primary_column_index).type != TypeId::INTEGER) {
    std::cout << "[FAILED] Dummy insertion currently requires an INTEGER "
                 "primary key.\n";
    return;
  }

  // ----------------------------------------------------------
  // Cache secondary-index column positions.
  // ----------------------------------------------------------

  std::vector<std::pair<size_t, BPlusTree *>> secondary_indexes_by_column;

  for (const auto &[column_name, index] : secondary_indexes) {
    size_t column_index{};

    if (findColumnIndex(schema, column_name, column_index)) {
      secondary_indexes_by_column.push_back({column_index, index.get()});
    }
  }

  auto start = std::chrono::steady_clock::now();

  int inserted = 0;

  for (int i = 0; i < count; ++i) {
    int32_t id = i + 1;

    // Skip existing primary keys instead of creating duplicate rows.
    RID existing_rid{};

    if (primary_index.search(Key::Integer(id), &existing_rid)) {
      continue;
    }

    std::vector<Value> values;
    values.reserve(schema.getColumnCount());

    for (size_t column = 0; column < schema.getColumnCount(); ++column) {
      const Column &column_info = schema.getColumn(column);

      if (column_info.type == TypeId::INTEGER) {
        if (column == primary_column_index) {
          values.emplace_back(id);
        } else {
          values.emplace_back(static_cast<int32_t>(18 + (id % 50)));
        }
      } else if (column_info.type == TypeId::VARCHAR) {
        values.emplace_back(std::string(faker::person::firstName()));

      } else {
        values.clear();
        break;
      }
    }

    if (values.empty()) {
      std::cout << "[FAILED] Unsupported column type.\n";
      break;
    }

    Tuple tuple = Tuple::Serialize(values, schema);

    RID rid{};

    if (!table.insertTuple(tuple, &rid)) {
      std::cout << "[FAILED] table.insertTuple ID " << id << '\n';
      continue;
    }

    if (!primary_index.insert(Key::Integer(id), rid)) {
      std::cout << "[FAILED] primary_index.insert ID " << id << '\n';
      continue;
    }

    bool secondary_ok = true;

    for (const auto &[column_index, index] : secondary_indexes_by_column) {
      Value value = tuple.getValue(schema, column_index);

      bool ok = false;

      if (value.getType() == TypeId::INTEGER) {
        ok = index->insert(Key::Integer(value.getInteger()), rid);
      } else if (value.getType() == TypeId::VARCHAR) {
        ok = index->insert(Key::Varchar(value.getString()), rid);
      }

      if (!ok) {
        secondary_ok = false;

        std::cout << "[WARNING] Failed to update secondary index for "
                  << schema.getColumn(column_index).name << ", ID " << id
                  << '\n';
      }
    }

    if (secondary_ok) {
      ++inserted;
    }

    if (i % 100 == 0) {
      std::cout << "Processed: " << i << '\n';
    }
  }

  auto end = std::chrono::steady_clock::now();

  const auto elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  std::cout << "\nInserted: " << inserted << " rows.\n";
  std::cout << "Time: " << elapsed.count() << " us\n";
  std::cout << "Time: " << elapsed.count() / 1000.0 << " ms\n";
  std::cout << "Average: "
            << (inserted ? elapsed.count() / static_cast<double>(inserted)
                         : 0.0)
            << " us/row\n";
}

// ============================================================
// Table visualization
// ============================================================

void visualizeTable(TableHeap &table, const Schema &schema) {
  printTitle("TABLE VISUALIZATION");

  int count = 0;

  for (auto it = table.begin(); it != table.end(); ++it) {
    Tuple tuple = *it;
    RID rid = it.getRID();

    std::cout << '\n';
    std::cout << "Row " << count << '\n';

    printBorder();

    std::cout << "RID = (" << rid.page_id << ", " << rid.slot_num << ")\n";

    printTupleValues(tuple, schema);

    ++count;
  }

  printBorder();
  std::cout << "Total rows: " << count << '\n';
}

// ============================================================
// Catalog visualization
// ============================================================

void visualizeCatalog(Catalog &catalog) {
  printTitle("CATALOG");

  const std::vector<std::string> table_names = catalog.getAllTableNames();

  if (table_names.empty()) {
    std::cout << "No known tables.\n";
    return;
  }

  for (const std::string &name : table_names) {
    TableMetadata *meta = catalog.getTable(name);

    if (meta == nullptr) {
      continue;
    }

    std::cout << "\nTable\n";
    printBorder();

    std::cout << "ID          : " << meta->table_id << '\n';
    std::cout << "Name        : " << meta->name << '\n';
    std::cout << "First page  : " << meta->first_page_id << '\n';
    std::cout << "Columns     : " << meta->columns.size() << '\n';

    for (const Column &column : meta->columns) {
      std::cout << "  - " << column.name;

      if (column.type == TypeId::INTEGER) {
        std::cout << " INTEGER";
      } else if (column.type == TypeId::VARCHAR) {
        std::cout << " VARCHAR";
      }

      std::cout << '\n';
    }

    std::vector<IndexMetadata *> indexes = catalog.getIndexesForTable(name);

    if (indexes.empty()) {
      std::cout << "Indexes     : (none)\n";
    } else {
      std::cout << "Indexes     : " << indexes.size() << '\n';

      for (IndexMetadata *idx : indexes) {
        if (idx == nullptr) {
          continue;
        }

        std::cout << "  - " << idx->name << " column=" << idx->column_name
                  << " root=" << idx->root_page_id << '\n';
      }
    }
  }
}

// ============================================================
// Detailed table information
// ============================================================

void showAllTablesDetailed(Catalog &catalog, BufferPoolManager &bpm) {
  printTitle("ALL TABLES (DETAILED)");

  const std::vector<std::string> table_names = catalog.getAllTableNames();

  if (table_names.empty()) {
    std::cout << "No known tables.\n";
    return;
  }

  for (const std::string &name : table_names) {
    TableMetadata *meta = catalog.getTable(name);

    if (meta == nullptr) {
      continue;
    }

    std::cout << '\n';
    printBorder();
    std::cout << "TABLE: " << meta->name << '\n';
    printBorder();

    std::cout << "Table ID     : " << meta->table_id << '\n';
    std::cout << "First page   : " << meta->first_page_id << '\n';
    std::cout << "Columns      : " << meta->columns.size() << '\n';

    for (const Column &column : meta->columns) {
      std::cout << "  - " << column.name;

      if (column.type == TypeId::INTEGER) {
        std::cout << " INTEGER";
      } else if (column.type == TypeId::VARCHAR) {
        std::cout << " VARCHAR";
      }

      std::cout << '\n';
    }

    TableHeap scratch_heap(&bpm, meta->first_page_id);

    int row_count = 0;

    for (auto it = scratch_heap.begin(); it != scratch_heap.end(); ++it) {
      ++row_count;
    }

    std::cout << "Row count    : " << row_count << '\n';

    const std::string index_name = name + "_pk";
    IndexMetadata *index_meta = catalog.getIndex(index_name);

    if (index_meta != nullptr) {
      std::cout << "Primary index: " << index_name << '\n';
      std::cout << "  Column     : " << index_meta->column_name << '\n';
      std::cout << "  Root page  : " << index_meta->root_page_id << '\n';
    } else {
      std::cout << "Primary index: (none built yet)\n";
    }
  }

  std::cout << '\n';
  printBorder();
  std::cout << "Total tables: " << table_names.size() << '\n';
}

// ============================================================
// Primary-key search
// ============================================================

void searchByPrimaryKey(TableHeap &table, const Schema &schema,
                        BPlusTree &primary_index) {
  printTitle("PRIMARY KEY SEARCH");

  int id = readInt("Enter primary key: ");

  if (id < 0) {
    std::cout << "\n[FAILED] Primary key must be non-negative.\n";
    return;
  }

  RID rid{};

  auto index_start = std::chrono::steady_clock::now();

  bool found = primary_index.search(Key::Integer(id), &rid);

  auto index_end = std::chrono::steady_clock::now();

  const auto index_time = std::chrono::duration_cast<std::chrono::microseconds>(
      index_end - index_start);

  if (!found) {
    std::cout << "\n[NOT FOUND]\n";
    std::cout << "Primary key " << id << " does not exist.\n";
    std::cout << "B+Tree search time: " << index_time.count() << " us\n";
    return;
  }

  std::cout << "\n[FOUND]\n";
  std::cout << "Primary key = " << id << '\n';
  std::cout << "RID = (" << rid.page_id << ", " << rid.slot_num << ")\n";

  Tuple tuple;

  auto table_start = std::chrono::steady_clock::now();

  bool tuple_found = table.getTuple(rid, &tuple);

  auto table_end = std::chrono::steady_clock::now();

  const auto table_time = std::chrono::duration_cast<std::chrono::microseconds>(
      table_end - table_start);

  if (!tuple_found) {
    std::cout << "\n[ERROR] Index points to a missing tuple.\n";
    std::cout << "B+Tree search time: " << index_time.count() << " us\n";
    std::cout << "Table lookup time: " << table_time.count() << " us\n";
    return;
  }

  printBorder();
  printTupleValues(tuple, schema);
  printBorder();

  std::cout << "\nB+Tree search time : " << index_time.count() << " us\n";
  std::cout << "Table lookup time  : " << table_time.count() << " us\n";
  std::cout << "Total time         : " << (index_time + table_time).count()
            << " us\n";
}

// ============================================================
// Primary-key range search
// ============================================================

void rangeSearchByPrimaryKey(TableHeap &table, const Schema &schema,
                             BPlusTree &primary_index) {
  printTitle("PRIMARY KEY RANGE SEARCH");

  int low = readInt("Enter low key (inclusive): ");
  int high = readInt("Enter high key (inclusive): ");

  if (low < 0 || high < 0 || low > high) {
    std::cout << "\n[FAILED] Invalid range.\n";
    return;
  }

  std::vector<Entry> entries;

  auto start = std::chrono::steady_clock::now();

  bool ok = primary_index.rangeSearch(Key::Integer(low), Key::Integer(high),
                                      &entries);

  auto end = std::chrono::steady_clock::now();

  const auto elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  if (!ok) {
    std::cout << "\n[FAILED] Range search failed.\n";
    return;
  }

  std::cout << "\n[FOUND] " << entries.size() << " matching row(s).\n";

  printBorder();

  for (const Entry &entry : entries) {
    Tuple tuple;

    if (!table.getTuple(entry.rid, &tuple)) {
      std::cout << "[WARNING] Could not fetch RID (" << entry.rid.page_id
                << ", " << entry.rid.slot_num << ")\n";
      continue;
    }

    std::cout << "Key ";

    if (entry.key.type == KeyType::INTEGER) {
      std::cout << entry.key.integer;
    } else {
      std::cout << '"' << entry.key.string << '"';
    }

    std::cout << " -> RID(" << entry.rid.page_id << ", " << entry.rid.slot_num
              << ")\n";

    printTupleValues(tuple, schema);
    printBorder();
  }

  std::cout << "\nRange scan time: " << elapsed.count() << " us\n";
}

// ============================================================
// LIKE helper
// ============================================================

bool likeMatch(const std::string &text, const std::string &pattern) {
  size_t text_pos = 0;
  size_t pattern_pos = 0;

  size_t star_pos = std::string::npos;
  size_t match_pos = 0;

  while (text_pos < text.size()) {
    if (pattern_pos < pattern.size() &&
        (pattern[pattern_pos] == '_' ||
         pattern[pattern_pos] == text[text_pos])) {
      ++text_pos;
      ++pattern_pos;
      continue;
    }

    if (pattern_pos < pattern.size() && pattern[pattern_pos] == '%') {
      star_pos = pattern_pos;
      match_pos = text_pos;
      ++pattern_pos;
      continue;
    }

    if (star_pos != std::string::npos) {
      pattern_pos = star_pos + 1;
      ++match_pos;
      text_pos = match_pos;
      continue;
    }

    return false;
  }

  while (pattern_pos < pattern.size() && pattern[pattern_pos] == '%') {
    ++pattern_pos;
  }

  return pattern_pos == pattern.size();
}

// ============================================================
// VARCHAR search using secondary index
// ============================================================

void searchVarchar(
    TableHeap &table, const Schema &schema,
    const std::unordered_map<std::string, std::unique_ptr<BPlusTree>>
        &indexes) {

  printTitle("VARCHAR SEARCH");

  std::string column_name = readString("Column name: ");

  auto index_it = indexes.find(column_name);

  if (index_it == indexes.end()) {
    std::cout << "\n[FAILED] No secondary index on '" << column_name << "'.\n";
    return;
  }

  size_t column_index{};

  if (!findColumnIndex(schema, column_name, column_index)) {
    std::cout << "\n[FAILED] Column does not exist.\n";
    return;
  }

  if (schema.getColumn(column_index).type != TypeId::VARCHAR) {
    std::cout << "\n[FAILED] This operation requires a VARCHAR column.\n";
    return;
  }

  BPlusTree &index = *index_it->second;

  std::string value =
      readString("Value (use % for prefix search, e.g. Slim%): ");

  bool is_like = value.find('%') != std::string::npos;

  // ------------------------------------------------------------
  // EXACT SEARCH (no '%' in the value)
  // ------------------------------------------------------------
  if (!is_like) {
    auto start = std::chrono::steady_clock::now();

    std::vector<RID> rids;
    bool found = index.searchAll(Key::Varchar(value), &rids);

    auto end = std::chrono::steady_clock::now();

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "\nSearch time: " << elapsed.count() << " us\n";

    if (!found) {
      std::cout << "\n[NOT FOUND]\n";
      return;
    }
    for (const RID &rid : rids) {
      Tuple tuple;

      if (!table.getTuple(rid, &tuple)) {
        std::cout << "\n[ERROR] Index contains stale RID.\n";
        return;
      }

      std::cout << "\n[FOUND]\n";
      std::cout << "RID = (" << rid.page_id << ", " << rid.slot_num << ")\n";

      printBorder();
      printTupleValues(tuple, schema);
      printBorder();
    }
    return;
  }

  // ------------------------------------------------------------
  // PREFIX LIKE SEARCH ('%' present — must be trailing, e.g. "Slim%")
  // ------------------------------------------------------------
  if (value.back() != '%') {
    std::cout << "\n[FAILED] Prefix LIKE must end with '%'.\n";
    std::cout << "Example: Slim%\n";
    return;
  }

  std::string prefix = value.substr(0, value.size() - 1);

  if (prefix.empty()) {
    std::cout << "\n[FAILED] Prefix cannot be empty.\n";
    return;
  }

  // Build an exclusive upper bound for the byte-wise prefix range.
  std::string upper = prefix;
  bool incremented = false;

  for (int i = static_cast<int>(upper.size()) - 1; i >= 0; --i) {
    unsigned char c = static_cast<unsigned char>(upper[static_cast<size_t>(i)]);

    if (c != 0xFF) {
      upper[static_cast<size_t>(i)] = static_cast<char>(c + 1);
      upper.resize(static_cast<size_t>(i + 1));
      incremented = true;
      break;
    }
  }

  if (!incremented) {
    std::cout << "\n[FAILED] Could not construct prefix range.\n";
    return;
  }

  std::vector<Entry> entries;

  auto start = std::chrono::steady_clock::now();

  bool success =
      index.rangeSearch(Key::Varchar(prefix), Key::Varchar(upper), &entries);

  auto end = std::chrono::steady_clock::now();

  const auto elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  std::cout << "\nSearch time: " << elapsed.count() << " us\n";

  if (!success) {
    std::cout << "[FAILED] Range search failed.\n";
    return;
  }

  int displayed = 0;

  for (const Entry &entry : entries) {
    Tuple tuple;

    if (!table.getTuple(entry.rid, &tuple)) {
      std::cout << "[WARNING] Stale RID (" << entry.rid.page_id << ", "
                << entry.rid.slot_num << ")\n";
      continue;
    }

    // rangeSearch is assumed to be inclusive, so explicitly filter
    // the upper-bound key and anything else outside the prefix.
    Value indexed_value = tuple.getValue(schema, column_index);

    if (indexed_value.getType() != TypeId::VARCHAR ||
        indexed_value.getString().compare(0, prefix.size(), prefix) != 0) {
      continue;
    }

    std::cout << "RID = (" << entry.rid.page_id << ", " << entry.rid.slot_num
              << ")\n";

    printTupleValues(tuple, schema);
    printBorder();

    ++displayed;
  }

  if (displayed == 0) {
    std::cout << "[NOT FOUND]\n";
  } else {
    std::cout << "Displayed: " << displayed << " row(s).\n";
  }
}
// ============================================================
// Secondary-index range search
// ============================================================

void rangeSearchSecondary(
    TableHeap &table, const Schema &schema,
    const std::unordered_map<std::string, std::unique_ptr<BPlusTree>>
        &indexes) {

  printTitle("SECONDARY INDEX RANGE SEARCH");

  std::string column_name = readString("Indexed column name: ");

  auto index_it = indexes.find(column_name);

  if (index_it == indexes.end()) {
    std::cout << "[FAILED] No secondary index on '" << column_name << "'.\n";
    return;
  }

  size_t column_index{};

  if (!findColumnIndex(schema, column_name, column_index)) {
    std::cout << "[FAILED] Column does not exist.\n";
    return;
  }

  const Column &column = schema.getColumn(column_index);

  int32_t low_int{};
  int32_t high_int{};

  std::vector<Entry> entries;

  if (column.type == TypeId::INTEGER) {
    low_int = static_cast<int32_t>(readInt("Enter minimum: "));
    high_int = static_cast<int32_t>(readInt("Enter maximum: "));

    if (low_int > high_int) {
      std::cout << "[FAILED] Minimum cannot be greater than maximum.\n";
      return;
    }

    auto start = std::chrono::steady_clock::now();

    bool success = index_it->second->rangeSearch(
        Key::Integer(low_int), Key::Integer(high_int), &entries);

    auto end = std::chrono::steady_clock::now();

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "\nSearch time: " << elapsed.count() << " us\n";

    if (!success) {
      std::cout << "[FAILED] Range search failed.\n";
      return;
    }
  } else if (column.type == TypeId::VARCHAR) {
    std::string low = readString("Enter lower string: ");
    std::string high = readString("Enter upper string: ");

    if (low > high) {
      std::cout << "[FAILED] Lower bound cannot be greater than upper "
                   "bound.\n";
      return;
    }

    auto start = std::chrono::steady_clock::now();

    bool success = index_it->second->rangeSearch(Key::Varchar(low),
                                                 Key::Varchar(high), &entries);

    auto end = std::chrono::steady_clock::now();

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "\nSearch time: " << elapsed.count() << " us\n";

    if (!success) {
      std::cout << "[FAILED] Range search failed.\n";
      return;
    }
  } else {
    std::cout << "[FAILED] Unsupported column type.\n";
    return;
  }

  std::cout << "Index returned " << entries.size() << " entries.\n\n";

  for (const Entry &entry : entries) {
    Tuple tuple;

    if (!table.getTuple(entry.rid, &tuple)) {
      std::cout << "[WARNING] Could not fetch RID (" << entry.rid.page_id
                << ", " << entry.rid.slot_num << ")\n";
      continue;
    }

    std::cout << "RID: (" << entry.rid.page_id << ", " << entry.rid.slot_num
              << ")\n";

    printTupleValues(tuple, schema);
    printBorder();
  }
}

// ============================================================
// Secondary-index creation
// ============================================================

bool createSecondaryIndex(
    Catalog &catalog, BufferPoolManager &bpm, TableMetadata *meta,
    TableHeap &table, const Schema &schema,
    std::unordered_map<std::string, std::unique_ptr<BPlusTree>>
        &secondary_indexes) {

  if (meta == nullptr) {
    return false;
  }

  printTitle("CREATE SECONDARY INDEX");

  std::string column_name = readString("Enter column name to index: ");

  size_t column_index{};

  if (!findColumnIndex(schema, column_name, column_index)) {
    std::cout << "[FAILED] Column '" << column_name << "' not found.\n";
    return false;
  }

  const std::string index_name = meta->name + "_" + column_name + "_idx";

  if (catalog.getIndex(index_name) != nullptr) {
    std::cout << "[FAILED] Index '" << index_name << "' already exists.\n";
    return false;
  }

  auto index = std::make_unique<BPlusTree>(&bpm);

  buildIndex(table, schema, *index, column_index);

  IndexMetadata *index_meta = catalog.createIndex(
      index_name, meta->name, column_name, index->getRootId());

  if (index_meta == nullptr) {
    std::cout << "[FAILED] Could not create index metadata.\n";
    return false;
  }

  index->setRootChangeCallback([&catalog, index_name](page_id_t new_root_id) {
    if (!catalog.updateIndexRoot(index_name, new_root_id)) {
      std::cerr << "[ERROR] Failed to persist root for index '" << index_name
                << "'.\n";
    }
  });

  secondary_indexes[column_name] = std::move(index);

  std::cout << "\n[SUCCESS] Created secondary index.\n";
  std::cout << "Index  : " << index_name << '\n';
  std::cout << "Column : " << column_name << '\n';
  std::cout << "Root   : " << index_meta->root_page_id << '\n';

  return true;
}

// ============================================================
// Menu
// ============================================================

void printMenu() {
  std::cout << '\n';

  printLine('=');
  std::cout << "                    WALOUDB\n";
  printLine('=');

  std::cout << "\nBUFFER POOL\n";
  std::cout << " 10. Show buffer pool\n";
  std::cout << " 11. Show LRU\n";

  std::cout << "\nDISK\n";
  std::cout << " 13. Flush known pages\n";

  std::cout << "\nTABLE\n";
  std::cout << " 14. Insert into table\n";
  std::cout << " 15. Get tuple by RID\n";
  std::cout << " 16. Update tuple\n";
  std::cout << " 17. Delete tuple\n";
  std::cout << " 18. Insert dummy rows\n";
  std::cout << " 19. Visualize table\n";
  std::cout << " 20. Create / switch table\n";
  std::cout << " 21. Visualize catalog\n";

  std::cout << "\nINDEX\n";
  std::cout << " 22. Search by primary key\n";
  std::cout << " 23. Rebuild primary index\n";
  std::cout << " 24. Show all tables (detailed)\n";
  std::cout << " 25. Range search by primary key\n";
  std::cout << " 26. Make a secondary index\n";
  std::cout << " 27. Range search by secondary key\n";
  std::cout << " 28. Search VARCHAR (= / LIKE)\n";

  std::cout << "\n";
  std::cout << "  0. Exit\n";

  printLine('=');
}

// ============================================================
// Main
// ============================================================

int main() {
  std::cout << R"(
============================================================
                     WALOUDB
              C++ Database Playground
============================================================
)";

  // ----------------------------------------------------------
  // Core database components
  // ----------------------------------------------------------

  DiskManager disk_manager(DATABASE_FILE);
  BufferPoolManager bpm(BUFFER_POOL_SIZE, &disk_manager);
  Catalog catalog(&bpm);

  // ----------------------------------------------------------
  // Current table state
  // ----------------------------------------------------------

  TableMetadata *current_meta = nullptr;

  std::unique_ptr<TableHeap> current_table;
  Schema current_schema;

  std::string current_primary_column;

  std::unique_ptr<BPlusTree> current_primary_index;

  std::unordered_map<std::string, std::unique_ptr<BPlusTree>>
      current_secondary_indexes;

  // ----------------------------------------------------------
  // Default table.
  //
  // If this database is new, create a simple users table.
  // Existing databases keep their persisted schema.
  // ----------------------------------------------------------

  TableMetadata *users_meta = catalog.getTable("users");

  if (users_meta == nullptr) {
    std::cout << "\nCreating default table 'users'...\n";

    Schema default_schema({
        {"id", TypeId::INTEGER},
        {"name", TypeId::VARCHAR},
    });

    users_meta = catalog.createTable("users", default_schema);

    if (users_meta == nullptr) {
      std::cerr << "\nFatal error: could not create users table.\n";
      return 1;
    }

    std::cout << "[SUCCESS] users table created.\n";
  }

  // ----------------------------------------------------------
  // Open users.
  // ----------------------------------------------------------

  if (!openTable(catalog, bpm, "users", current_meta, current_table,
                 current_schema, current_primary_column, current_primary_index,
                 current_secondary_indexes)) {
    std::cerr << "\nFatal error: could not open users table.\n";
    return 1;
  }

  // ----------------------------------------------------------
  // Main loop
  // ----------------------------------------------------------

  bool running = true;

  while (running) {
    printMenu();

    std::cout << '\n';
    printLine('-');

    if (current_meta != nullptr) {
      std::cout << "Current table : " << current_meta->name << '\n';
      std::cout << "Table ID      : " << current_meta->table_id << '\n';
      std::cout << "First page    : " << current_meta->first_page_id << '\n';
      std::cout << "Primary key   : " << current_primary_column << '\n';
      std::cout << "Primary index : " << current_meta->name << "_pk\n";
    } else {
      std::cout << "Current table : (none)\n";
    }

    printLine('-');

    int choice = readInt("WalouDB >> ");

    switch (choice) {
      // ========================================================
      // EXIT
      // ========================================================

    case 0:
      running = false;
      break;

      // ========================================================
      // BUFFER POOL
      // ========================================================

    case 10:
      printBufferPool(bpm);
      break;

    case 11:
      printLRU(bpm);
      break;

      // ========================================================
      // DISK
      // ========================================================

    case 13:
      flushAllPages(bpm);
      break;

      // ========================================================
      // TABLE
      // ========================================================

    case 14:
      if (current_table && current_primary_index) {
        insertIntoTable(*current_table, current_schema, *current_primary_index,
                        current_primary_column);
      }
      break;

    case 15:
      if (current_table) {
        getFromTable(*current_table, current_schema);
      }
      break;

    case 16:
      if (current_table && current_primary_index) {
        updateInTable(*current_table, current_schema, *current_primary_index,
                      current_primary_column);
      }
      break;

    case 17:
      if (current_table && current_primary_index) {
        deleteFromTable(catalog, bpm, current_meta, *current_table,
                        current_schema, current_primary_column,
                        current_primary_index);
      }
      break;

    case 18:
      if (current_table && current_primary_index) {
        insertDummyRows(*current_table, current_schema, *current_primary_index,
                        current_secondary_indexes, current_meta->name,
                        current_primary_column);
      }
      break;

    case 19:
      if (current_table) {
        visualizeTable(*current_table, current_schema);
      }
      break;

      // ========================================================
      // CREATE / SWITCH TABLE
      // ========================================================

    case 20: {
      std::string table_name = readString("Enter table name: ");

      if (table_name.empty()) {
        std::cout << "[FAILED] Table name cannot be empty.\n";
        break;
      }

      if (catalog.getTable(table_name) != nullptr) {
        // Existing table: switch to it.
        if (openTable(catalog, bpm, table_name, current_meta, current_table,
                      current_schema, current_primary_column,
                      current_primary_index, current_secondary_indexes)) {
          std::cout << "\n[SUCCESS] Switched to table '" << table_name
                    << "'.\n";
        }

        break;
      }

      // New table.
      TableCreationInfo creation = createTableSchemaInteractive();

      TableMetadata *meta = catalog.createTable(table_name, creation.schema);

      if (meta == nullptr) {
        std::cout << "[FAILED] Could not create table.\n";
        break;
      }

      auto table = std::make_unique<TableHeap>(&bpm, meta->first_page_id);

      auto primary_index = std::make_unique<BPlusTree>(&bpm);

      std::cout << "\nBuilding primary index...\n";

      if (!buildPrimaryIndex(*table, creation.schema, *primary_index,
                             creation.primary_column)) {
        std::cout << "[FAILED] Could not build primary index.\n";
        break;
      }

      const std::string index_name = table_name + "_pk";

      IndexMetadata *primary_meta =
          catalog.createIndex(index_name, table_name, creation.primary_column,
                              primary_index->getRootId());

      if (primary_meta == nullptr) {
        std::cout << "[FAILED] Could not create primary index metadata.\n";
        break;
      }

      primary_index->setRootChangeCallback(
          [&catalog, index_name](page_id_t new_root_id) {
            if (!catalog.updateIndexRoot(index_name, new_root_id)) {
              std::cerr << "[ERROR] Failed to persist root for index '"
                        << index_name << "'.\n";
            }
          });

      current_meta = meta;
      current_schema = creation.schema;
      current_primary_column = creation.primary_column;
      current_table = std::move(table);
      current_primary_index = std::move(primary_index);
      current_secondary_indexes.clear();

      std::cout << "\n[SUCCESS] Created and opened table '" << table_name
                << "'.\n";
      break;
    }

      // ========================================================
      // CATALOG
      // ========================================================

    case 21:
      visualizeCatalog(catalog);
      break;

      // ========================================================
      // PRIMARY INDEX
      // ========================================================

    case 22:
      if (current_table && current_primary_index) {
        searchByPrimaryKey(*current_table, current_schema,
                           *current_primary_index);
      }
      break;

    case 23:
      if (current_table && current_primary_index) {
        rebuildPrimaryIndex(catalog, bpm, current_meta, *current_table,
                            current_schema, current_primary_column,
                            current_primary_index);
      }
      break;

    case 24:
      showAllTablesDetailed(catalog, bpm);
      break;

    case 25:
      if (current_table && current_primary_index) {
        rangeSearchByPrimaryKey(*current_table, current_schema,
                                *current_primary_index);
      }
      break;

    case 26:
      if (current_table) {
        createSecondaryIndex(catalog, bpm, current_meta, *current_table,
                             current_schema, current_secondary_indexes);
      }
      break;

    case 27:
      if (current_table) {
        rangeSearchSecondary(*current_table, current_schema,
                             current_secondary_indexes);
      }
      break;

    case 28:
      if (current_table) {
        searchVarchar(*current_table, current_schema,
                      current_secondary_indexes);
      }
      break;

    default:
      std::cout << "\nUnknown command.\n";
      break;
    }
  }

  // ----------------------------------------------------------
  // Shutdown
  // ----------------------------------------------------------

  std::cout << '\n';
  printLine('=');
  std::cout << "Shutting down WalouDB...\n";
  printLine('=');

  flushAllPages(bpm);

  std::cout << "\nGoodbye.\n";

  return 0;
}
