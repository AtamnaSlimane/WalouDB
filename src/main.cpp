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
#include <string>
#include <vector>

using namespace WalouDB;

// ============================================================
// Configuration
// ============================================================

constexpr size_t BUFFER_POOL_SIZE = 4096 * 100;
constexpr const char *DATABASE_FILE = "waloudb.db";

// ============================================================
// Schema
// ============================================================

Schema createSchema() {
  return Schema({
      {"id", TypeId::INTEGER},
      {"name", TypeId::VARCHAR},
  });
}

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

    int value;

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
// Tuple printing
// ============================================================

void printTupleValues(const Tuple &tuple, const Schema &schema) {
  for (size_t i = 0; i < schema.getColumnCount(); ++i) {
    const Column &column = schema.getColumn(i);

    std::cout << std::left << std::setw(15) << column.name << ": ";

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
// Raw page helpers
// ============================================================

bool createNewPage(BufferPoolManager &bpm,
                   std::vector<page_id_t> &known_pages) {
  page_id_t page_id = INVALID_PAGE_ID;

  Page *page = bpm.newPage(&page_id);

  if (page == nullptr) {
    std::cout << "\n[FAILED] Could not allocate a new page.\n";
    return false;
  }

  SlottedPage slotted(page->getData());
  slotted.Init(page_id);

  bpm.unpinPage(page_id, true);

  known_pages.push_back(page_id);

  std::cout << "\n[SUCCESS] Created page " << page_id << '\n';

  return true;
}

bool switchActivePage(BufferPoolManager &bpm, page_id_t page_id) {
  Page *page = bpm.fetchPage(page_id);

  if (page == nullptr) {
    std::cout << "\n[FAILED] Could not fetch page " << page_id << ".\n";
    return false;
  }

  SlottedPage slotted(page->getData());

  std::cout << "\nPage ID     : " << slotted.getPageId() << '\n';

  std::cout << "Slot count  : " << slotted.getSlotCount() << '\n';

  std::cout << "Free space  : " << slotted.freeSpace() << " bytes\n";

  bpm.unpinPage(page_id, false);

  return true;
}

bool insertTupleRaw(BufferPoolManager &bpm, page_id_t page_id) {
  printTitle("RAW PAGE INSERT");

  Page *page = bpm.fetchPage(page_id);

  if (page == nullptr) {
    std::cout << "[FAILED] Page not found.\n";
    return false;
  }

  std::string text = readString("Enter string to insert: ");

  Schema schema({
      {"value", TypeId::VARCHAR},
  });

  Tuple tuple = Tuple::Serialize({Value(text)}, schema);

  SlottedPage slotted(page->getData());

  RID rid{};

  if (!slotted.insertTuple(tuple, &rid)) {
    std::cout << "\n[FAILED] Tuple could not be inserted.\n";

    bpm.unpinPage(page_id, false);
    return false;
  }

  bpm.unpinPage(page_id, true);

  std::cout << "\n[SUCCESS]\n";
  std::cout << "RID = (" << rid.page_id << ", " << rid.slot_num << ")\n";

  return true;
}

bool getTupleRaw(BufferPoolManager &bpm, page_id_t page_id) {
  printTitle("RAW PAGE READ");

  uint16_t slot_num = static_cast<uint16_t>(readInt("Enter slot number: "));

  Page *page = bpm.fetchPage(page_id);

  if (page == nullptr) {
    std::cout << "[FAILED] Page not found.\n";
    return false;
  }

  SlottedPage slotted(page->getData());

  auto tuple = slotted.getTuple(slot_num);

  if (!tuple.has_value()) {
    std::cout << "\n[NOT FOUND] Slot does not contain a tuple.\n";

    bpm.unpinPage(page_id, false);
    return false;
  }

  Schema schema({
      {"value", TypeId::VARCHAR},
  });

  printBorder();
  printTupleValues(*tuple, schema);
  printBorder();

  bpm.unpinPage(page_id, false);

  return true;
}

bool deleteTupleRaw(BufferPoolManager &bpm, page_id_t page_id) {
  printTitle("RAW PAGE DELETE");

  uint16_t slot_num = static_cast<uint16_t>(readInt("Enter slot number: "));

  Page *page = bpm.fetchPage(page_id);

  if (page == nullptr) {
    std::cout << "[FAILED] Page not found.\n";
    return false;
  }

  SlottedPage slotted(page->getData());

  if (!slotted.deleteTuple(slot_num)) {
    std::cout << "\n[FAILED] Could not delete tuple.\n";

    bpm.unpinPage(page_id, false);
    return false;
  }

  bpm.unpinPage(page_id, true);

  std::cout << "\n[SUCCESS] Tuple deleted.\n";

  return true;
}

bool compactActivePage(BufferPoolManager &bpm, page_id_t page_id) {
  printTitle("COMPACT PAGE");

  Page *page = bpm.fetchPage(page_id);

  if (page == nullptr) {
    std::cout << "[FAILED] Page not found.\n";
    return false;
  }

  SlottedPage slotted(page->getData());

  std::cout << "Free space before: " << slotted.freeSpace() << " bytes\n";

  slotted.compact();

  std::cout << "Free space after : " << slotted.freeSpace() << " bytes\n";

  bpm.unpinPage(page_id, true);

  return true;
}

void visualizeActivePage(BufferPoolManager &bpm, page_id_t page_id) {
  printTitle("PAGE VISUALIZATION");

  Page *page = bpm.fetchPage(page_id);

  if (page == nullptr) {
    std::cout << "[FAILED] Page not found.\n";
    return;
  }

  SlottedPage slotted(page->getData());

  std::cout << "Page ID       : " << slotted.getPageId() << '\n';

  std::cout << "Lower         : " << slotted.getLower() << '\n';

  std::cout << "Upper         : " << slotted.getUpper() << '\n';

  std::cout << "Slot count    : " << slotted.getSlotCount() << '\n';

  std::cout << "Free space    : " << slotted.freeSpace() << " bytes\n";

  std::cout << "Next page     : " << slotted.getNextPageId() << '\n';

  printBorder();

  for (uint16_t i = 0; i < slotted.getSlotCount(); ++i) {
    auto slot = slotted.getSlotInfo(i);

    if (!slot.has_value()) {
      continue;
    }

    std::cout << "Slot " << std::setw(4) << i << " | offset=" << std::setw(5)
              << slot->offset << " | length=" << std::setw(5) << slot->length
              << " | deleted=" << (slot->deleted ? "yes" : "no") << '\n';
  }

  bpm.unpinPage(page_id, false);
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
// Flush helpers
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
// Table operations
// ============================================================

bool insertIntoTable(TableHeap &table, const Schema &schema,
                     BPlusTree &primary_index) {
  printTitle("TABLE INSERT");

  int id = readInt("Enter id: ");

  if (id < 0) {
    std::cout << "\n[FAILED] Primary key must be non-negative.\n";
    return false;
  }

  std::string name = readString("Enter name: ");

  Tuple tuple = Tuple::Serialize(
      {
          Value(static_cast<int32_t>(id)),
          Value(name),
      },
      schema);

  RID rid{};

  if (!table.insertTuple(tuple, &rid)) {
    std::cout << "\n[FAILED] Table insertion failed.\n";
    return false;
  }

  if (!primary_index.insert(static_cast<uint32_t>(id), rid)) {

    std::cout << "\n[FAILED] Index insertion failed.\n";

    // The tuple has already been inserted.
    // A complete DB would need transactional rollback here.
    return false;
  }

  std::cout << "\n[SUCCESS]\n";
  std::cout << "ID  = " << id << '\n';
  std::cout << "RID = (" << rid.page_id << ", " << rid.slot_num << ")\n";

  return true;
}

bool getFromTable(TableHeap &table, const Schema &schema) {
  printTitle("TABLE GET");

  int page_id = readInt("Enter page ID: ");
  int slot_num = readInt("Enter slot number: ");

  if (page_id < 0 || slot_num < 0) {
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

bool updateInTable(TableHeap &table, const Schema &schema,
                   BPlusTree &primary_index) {
  printTitle("TABLE UPDATE");

  int id = readInt("Enter primary key: ");

  if (id < 0) {
    std::cout << "[FAILED] Invalid primary key.\n";
    return false;
  }

  RID rid{};

  if (!primary_index.search(static_cast<uint32_t>(id), &rid)) {

    std::cout << "\n[NOT FOUND] Primary key does not exist.\n";
    return false;
  }

  std::string name = readString("Enter new name: ");

  Tuple tuple = Tuple::Serialize(
      {
          Value(static_cast<int32_t>(id)),
          Value(name),
      },
      schema);

  if (!table.updateTuple(rid, tuple)) {
    std::cout << "\n[FAILED] Update failed.\n";
    return false;
  }

  std::cout << "\n[SUCCESS] Tuple updated.\n";

  return true;
}

bool deleteFromTable(TableHeap &table, BPlusTree &primary_index) {
  printTitle("TABLE DELETE");

  int id = readInt("Enter primary key: ");

  if (id < 0) {
    std::cout << "[FAILED] Invalid primary key.\n";
    return false;
  }

  RID rid{};

  if (!primary_index.search(static_cast<uint32_t>(id), &rid)) {

    std::cout << "\n[NOT FOUND] Primary key does not exist.\n";
    return false;
  }

  if (!table.deleteTuple(rid)) {
    std::cout << "\n[FAILED] Table deletion failed.\n";
    return false;
  }

  std::cout << "\n[SUCCESS] Tuple deleted from table.\n";
  std::cout << "RID = (" << rid.page_id << ", " << rid.slot_num << ")\n";

  // NOTE:
  // Current BPlusTree implementation has no delete().
  // Therefore the index entry remains.
  //
  // This means deleting a row currently leaves a stale
  // primary-index entry.
  //
  // Search detects the RID but table.getTuple() will fail.

  std::cout << "\n[WARNING] B+Tree deletion is not implemented yet.\n";
  std::cout << "The index entry for this key remains.\n";

  return true;
}

void insertDummyRows(TableHeap &table, const Schema &schema,
                     BPlusTree &primary_index) {
  printTitle("INSERT DUMMY ROWS");

  int count = readInt("How many rows? ");

  if (count <= 0) {
    std::cout << "Nothing to insert.\n";
    return;
  }

  int inserted = 0;

  for (int i = 0; i < count; ++i) {
    int id = i + 1;

    RID existing{};

    if (primary_index.search(static_cast<uint32_t>(id), &existing)) {
      continue;
    }

    std::string name = "User_" + std::to_string(id);

    Tuple tuple = Tuple::Serialize(
        {
            Value(static_cast<int32_t>(id)),
            Value(name),
        },
        schema);

    RID rid{};

    if (!table.insertTuple(tuple, &rid)) {
      std::cout << "[FAILED] Could not insert ID " << id << '\n';
      continue;
    }

    if (!primary_index.insert(static_cast<uint32_t>(id), rid)) {

      std::cout << "[FAILED] Index insertion failed for ID " << id << '\n';

      continue;
    }

    ++inserted;
  }

  std::cout << "\nInserted: " << inserted << " rows.\n";
}

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

void visualizeCatalog(Catalog &catalog,
                      const std::vector<std::string> &table_names) {
  printTitle("CATALOG");

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
      std::cout << "  - " << column.name << '\n';
    }
  }
}

// ============================================================
// Primary index
// ============================================================

void buildPrimaryIndex(TableHeap &table, const Schema &schema,
                       BPlusTree &primary_index) {
  printTitle("BUILD PRIMARY INDEX");

  int indexed = 0;
  int duplicates = 0;

  for (auto it = table.begin(); it != table.end(); ++it) {

    Tuple tuple = *it;
    RID rid = it.getRID();

    Value id_value = tuple.getValue(schema, 0);

    if (id_value.getType() != TypeId::INTEGER) {
      continue;
    }

    int32_t id = id_value.getInteger();

    if (id < 0) {
      continue;
    }

    if (!primary_index.insert(static_cast<uint32_t>(id), rid)) {

      ++duplicates;
      continue;
    }

    ++indexed;
  }

  std::cout << "Indexed rows : " << indexed << '\n';

  std::cout << "Duplicates   : " << duplicates << '\n';
}

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

  bool found = primary_index.search(static_cast<uint32_t>(id), &rid);

  auto index_end = std::chrono::steady_clock::now();

  auto index_time = std::chrono::duration_cast<std::chrono::microseconds>(
      index_end - index_start);

  if (!found) {
    std::cout << "\n[NOT FOUND]\n";

    std::cout << "Primary key " << id << " does not exist.\n";

    std::cout << "\nB+Tree search time: " << index_time.count() << " ns\n";

    return;
  }

  std::cout << "\n[FOUND]\n";

  std::cout << "Primary key = " << id << '\n';

  std::cout << "RID = (" << rid.page_id << ", " << rid.slot_num << ")\n";

  Tuple tuple;

  auto table_start = std::chrono::steady_clock::now();

  bool tuple_found = table.getTuple(rid, &tuple);

  auto table_end = std::chrono::steady_clock::now();

  auto table_time = std::chrono::duration_cast<std::chrono::microseconds>(
      table_end - table_start);

  if (!tuple_found) {
    std::cout << "\n[ERROR]\n";
    std::cout << "Index points to a missing tuple.\n";

    std::cout << "\nB+Tree search time: " << index_time.count() << " ns\n";

    std::cout << "Table lookup time: " << table_time.count() << " ns\n";

    return;
  }

  printBorder();

  printTupleValues(tuple, schema);

  printBorder();

  std::cout << "\nB+Tree search time : " << index_time.count() << " ns\n";

  std::cout << "Table lookup time  : " << table_time.count() << " ns\n";

  std::cout << "Total time         : " << (index_time + table_time).count()
            << " ns\n";
}

// ============================================================
// Table creation / opening
// ============================================================

TableMetadata *openOrCreateUsers(Catalog &catalog, const Schema &schema) {
  std::string table_name = "users";

  TableMetadata *meta = catalog.getTable(table_name);

  if (meta != nullptr) {
    return meta;
  }

  std::cout << "\nCreating table 'users'...\n";

  meta = catalog.createTable(table_name, schema);

  if (meta == nullptr) {
    std::cout << "[FAILED] Could not create users table.\n";
    return nullptr;
  }

  std::cout << "[SUCCESS] users table created.\n";

  return meta;
}

TableMetadata *createTableInteractive(Catalog &catalog) {
  printTitle("CREATE TABLE");

  std::string table_name = readString("Enter table name: ");

  if (table_name.empty()) {
    std::cout << "[FAILED] Table name cannot be empty.\n";
    return nullptr;
  }

  if (catalog.getTable(table_name) != nullptr) {
    std::cout << "[FAILED] Table already exists.\n";
    return nullptr;
  }

  std::cout << "\nFor now, tables use the default schema:\n";
  std::cout << "  id   INTEGER\n";
  std::cout << "  name VARCHAR\n";

  Schema schema = createSchema();

  TableMetadata *meta = catalog.createTable(table_name, schema);

  if (meta == nullptr) {
    std::cout << "\n[FAILED] Could not create table.\n";
    return nullptr;
  }

  std::cout << "\n[SUCCESS]\n";
  std::cout << "Table ID     : " << meta->table_id << '\n';

  std::cout << "First page   : " << meta->first_page_id << '\n';

  return meta;
}

// ============================================================
// Menu
// ============================================================

void printMenu() {
  std::cout << '\n';

  printLine('=');
  std::cout << "                    WALOUDB\n";
  printLine('=');

  std::cout << "\nRAW STORAGE\n";
  std::cout << "  1. Create new page\n";
  std::cout << "  2. Inspect page\n";
  std::cout << "  3. Insert raw tuple\n";
  std::cout << "  4. Read raw tuple\n";
  std::cout << "  5. Delete raw tuple\n";
  std::cout << "  6. Compact page\n";
  std::cout << "  7. Visualize page\n";

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
  std::cout << " 20. Create/open table\n";
  std::cout << " 21. Visualize catalog\n";

  std::cout << "\nINDEX\n";
  std::cout << " 22. Search by primary key\n";
  std::cout << " 23. Rebuild primary index\n";

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

  Schema default_schema = createSchema();

  // ----------------------------------------------------------
  // Open or create default users table
  // ----------------------------------------------------------

  TableMetadata *meta = openOrCreateUsers(catalog, default_schema);

  if (meta == nullptr) {
    std::cerr << "\nFatal error: could not open users table.\n";
    return 1;
  }

  TableHeap table(&bpm, meta->first_page_id);

  Schema schema = default_schema;

  // ----------------------------------------------------------
  // Primary B+Tree
  //
  // Current implementation creates a fresh B+Tree root.
  // Existing table rows are therefore indexed at startup.
  // ----------------------------------------------------------

  BPlusTree primary_index(&bpm);

  buildPrimaryIndex(table, schema, primary_index);

  // ----------------------------------------------------------
  // Known pages for playground visualization
  // ----------------------------------------------------------

  std::vector<page_id_t> known_pages;

  known_pages.push_back(0);
  if (meta->first_page_id != INVALID_PAGE_ID) {
    known_pages.push_back(meta->first_page_id);
  }

  // ----------------------------------------------------------
  // Known table names
  // ----------------------------------------------------------

  std::vector<std::string> known_table_names;

  known_table_names.push_back(meta->name);

  // ----------------------------------------------------------
  // Main menu loop
  // ----------------------------------------------------------

  bool running = true;

  while (running) {
    printMenu();

    int choice = readInt("WalouDB >> ");

    switch (choice) {

      // ========================================================
      // EXIT
      // ========================================================

    case 0:
      running = false;
      break;

      // ========================================================
      // RAW STORAGE
      // ========================================================

    case 1: {
      createNewPage(bpm, known_pages);
      break;
    }

    case 2: {
      int page_id = readInt("Enter page ID: ");

      if (page_id < 0) {
        std::cout << "Invalid page ID.\n";
        break;
      }

      switchActivePage(bpm, static_cast<page_id_t>(page_id));

      break;
    }

    case 3: {
      int page_id = readInt("Enter page ID: ");

      if (page_id < 0) {
        std::cout << "Invalid page ID.\n";
        break;
      }

      insertTupleRaw(bpm, static_cast<page_id_t>(page_id));

      break;
    }

    case 4: {
      int page_id = readInt("Enter page ID: ");

      if (page_id < 0) {
        std::cout << "Invalid page ID.\n";
        break;
      }

      getTupleRaw(bpm, static_cast<page_id_t>(page_id));

      break;
    }

    case 5: {
      int page_id = readInt("Enter page ID: ");

      if (page_id < 0) {
        std::cout << "Invalid page ID.\n";
        break;
      }

      deleteTupleRaw(bpm, static_cast<page_id_t>(page_id));

      break;
    }

    case 6: {
      int page_id = readInt("Enter page ID: ");

      if (page_id < 0) {
        std::cout << "Invalid page ID.\n";
        break;
      }

      compactActivePage(bpm, static_cast<page_id_t>(page_id));

      break;
    }

    case 7: {
      int page_id = readInt("Enter page ID: ");

      if (page_id < 0) {
        std::cout << "Invalid page ID.\n";
        break;
      }

      visualizeActivePage(bpm, static_cast<page_id_t>(page_id));

      break;
    }

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
      insertIntoTable(table, schema, primary_index);
      break;

    case 15:
      getFromTable(table, schema);
      break;

    case 16:
      updateInTable(table, schema, primary_index);
      break;

    case 17:
      deleteFromTable(table, primary_index);
      break;

    case 18:
      insertDummyRows(table, schema, primary_index);
      break;

    case 19:
      visualizeTable(table, schema);
      break;

    case 20: {
      TableMetadata *new_meta = createTableInteractive(catalog);

      if (new_meta != nullptr) {
        known_table_names.push_back(new_meta->name);

        std::cout << "\nNote: the playground is still operating "
                     "on the current users table.\n";
      }

      break;
    }

    case 21:
      visualizeCatalog(catalog, known_table_names);
      break;

      // ========================================================
      // PRIMARY INDEX
      // ========================================================

    case 22:
      searchByPrimaryKey(table, schema, primary_index);
      break;

    case 23:
      primary_index = BPlusTree(&bpm);

      buildPrimaryIndex(table, schema, primary_index);

      break;

      // ========================================================
      // UNKNOWN COMMAND
      // ========================================================

    default:
      std::cout << "\nUnknown command.\n";
      break;
    }
  }

  // ----------------------------------------------------------
  // Flush known pages before shutdown
  // ----------------------------------------------------------

  std::cout << "\n";
  printLine('=');
  std::cout << "Shutting down WalouDB...\n";
  printLine('=');

  flushAllPages(bpm);

  std::cout << "\nGoodbye.\n";

  return 0;
}
