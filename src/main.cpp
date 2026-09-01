#include "waloudb/storage/DiskManager.h"
#include "waloudb/storage/Page.h"
#include "waloudb/storage/SlottedPage.h"
#include "waloudb/storage/Tuple.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace WalouDB;

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
  int value;

  while (true) {
    std::cout << prompt;

    if (std::cin >> value) {
      clearInput();
      return value;
    }

    std::cout << "\nInvalid number. Please try again.\n\n";
    clearInput();
  }
}

std::string readString(const std::string &prompt) {
  std::string value;

  std::cout << prompt;
  std::getline(std::cin, value);

  return value;
}

// ============================================================
// ASCII helpers
// ============================================================

constexpr int VISUAL_WIDTH = 70;

void printLine(char c = '=') {
  for (int i = 0; i < VISUAL_WIDTH; ++i) {
    std::cout << c;
  }

  std::cout << "\n";
}

void printTitle(const std::string &title) {
  std::cout << "\n";
  printLine('=');
  std::cout << "  " << title << "\n";
  printLine('=');
}

// ============================================================
// Page visualizer
// ============================================================

void printBorder() {
  std::cout
      << "+--------------------------------------------------------------+\n";
}

void printRow(const std::string &text) {
  std::cout << "| " << std::left << std::setw(60) << text << " |\n";
}

void visualizePage(const SlottedPage &page, const Schema &schema) {

  const uint16_t lower = page.getLower();
  const uint16_t upper = page.getUpper();
  const uint16_t slot_count = page.getSlotCount();
  const uint16_t free_space = page.freeSpace();

  // ==========================================================
  // Title
  // ==========================================================

  std::cout << "\n";
  std::cout << "====================================================\n\n";

  std::cout << "                 WALOUDB PAGE " << page.getId() << "\n\n";

  std::cout << "====================================================\n\n\n";

  // ==========================================================
  // Beginning of memory
  // ==========================================================

  std::cout << "MEMORY OFFSET 0\n";

  std::cout << "     |\n";

  std::cout << "     v\n\n";

  // ==========================================================
  // Page Header
  // ==========================================================

  printBorder();

  printRow("PAGE HEADER");

  printRow("page_id    = " + std::to_string(page.getId()));

  printRow("lower      = " + std::to_string(lower));

  printRow("upper      = " + std::to_string(upper));

  printRow("slot_count = " + std::to_string(slot_count));

  printRow("version    = " + std::to_string(page.getVersion()));

  printBorder();

  // ==========================================================
  // Slot Directory
  // ==========================================================

  for (uint16_t i = 0; i < slot_count; ++i) {

    auto slot_opt = page.getSlotInfo(i);

    if (!slot_opt.has_value()) {
      continue;
    }

    const Slot slot = *slot_opt;

    printRow("SLOT " + std::to_string(i));

    printRow("offset : " + std::to_string(slot.offset));

    printRow("length : " + std::to_string(slot.length) + " bytes");

    if (slot.length == 0) {

      printRow("status : TOMBSTONED / DELETED");

    } else {

      printRow("status : ACTIVE");
    }

    printBorder();
  }

  // ==========================================================
  // Free Space
  // ==========================================================

  std::cout << "\n";

  std::cout << "              <--------- FREE SPACE --------->\n\n";

  printBorder();

  printRow("FREE SPACE");

  printRow("from offset " + std::to_string(lower));

  printRow("to offset   " + std::to_string(upper));

  printRow("total       " + std::to_string(free_space) + " bytes");

  printBorder();

  // ==========================================================
  // Tuple Data
  // ==========================================================

  std::cout << "\n";

  std::cout << "                    TUPLE DATA\n\n";

  // We print tuples from lowest physical offset
  // to highest physical offset.
  //
  // Since tuple data grows downward from PAGE_SIZE,
  // the newest tuple usually appears first.

  struct TupleInfo {
    uint16_t slot_num;
    Slot slot;
  };

  std::vector<TupleInfo> tuples;

  // Collect all slots.

  for (uint16_t i = 0; i < slot_count; ++i) {

    auto slot_opt = page.getSlotInfo(i);

    if (!slot_opt.has_value()) {
      continue;
    }

    tuples.push_back({i, *slot_opt});
  }

  // Sort by physical memory offset.

  std::sort(tuples.begin(), tuples.end(),

            [](const TupleInfo &a, const TupleInfo &b) {
              return a.slot.offset < b.slot.offset;
            });

  // ==========================================================
  // Print each tuple
  // ==========================================================

  for (const TupleInfo &info : tuples) {

    const uint16_t slot_num = info.slot_num;

    const Slot slot = info.slot;

    // --------------------------------------------------------
    // Tombstoned tuple
    // --------------------------------------------------------

    if (slot.length == 0) {

      printBorder();

      printRow("TUPLE " + std::to_string(slot_num) + " - DELETED");

      printRow("RID = (" + std::to_string(page.getId()) + ", " +
               std::to_string(slot_num) + ")");

      printRow("offset = " + std::to_string(slot.offset));

      printRow("length = 0 bytes");

      printRow("status = TOMBSTONED");

      printBorder();

      continue;
    }

    // --------------------------------------------------------
    // Active tuple
    // --------------------------------------------------------

    auto tuple_opt = page.getTuple(slot_num);

    if (!tuple_opt.has_value()) {
      continue;
    }

    const Tuple &tuple = *tuple_opt;

    printBorder();

    printRow("TUPLE " + std::to_string(slot_num));

    printRow("RID = (" + std::to_string(page.getId()) + ", " +
             std::to_string(slot_num) + ")");

    printRow("offset = " + std::to_string(slot.offset));

    printRow("length = " + std::to_string(slot.length) + " bytes");

    // --------------------------------------------------------
    // Print actual tuple values
    // --------------------------------------------------------

    for (size_t col_idx = 0; col_idx < schema.getColumnCount(); ++col_idx) {

      const Column &column = schema.getColumn(col_idx);

      Value value = tuple.getValue(schema, col_idx);

      // INTEGER

      if (column.type == TypeId::INTEGER) {

        printRow(column.name + " = " + std::to_string(value.getInteger()));

      }

      // VARCHAR

      else if (column.type == TypeId::VARCHAR) {

        printRow(column.name + " = \"" + value.getString() + "\"");
      }
    }

    printBorder();
  }

  // ==========================================================
  // End of memory
  // ==========================================================

  std::cout << "\n";

  std::cout << "     ^\n";

  std::cout << "     |\n";

  std::cout << "MEMORY OFFSET " << PAGE_SIZE << "\n\n";
}

// ============================================================
// Insert
// ============================================================

bool insertTuple(SlottedPage &page, const Schema &schema) {
  printTitle("INSERT TUPLE");

  int id = readInt("Enter id: ");

  std::string name = readString("Enter name: ");

  Tuple tuple = Tuple::Serialize(
      {
          Value(static_cast<int32_t>(id)),
          Value(name),
      },
      schema);

  RID rid{};

  std::cout << "\nTuple size: " << tuple.getLength() << " bytes\n";

  std::cout << "Free space before: " << page.freeSpace() << " bytes\n";

  bool success = page.insertTuple(tuple, &rid);

  if (!success) {
    std::cout << "\n[FAILED] Not enough free space.\n";
    return false;
  }

  std::cout << "\n[SUCCESS] Tuple inserted.\n";

  std::cout << "RID = (" << rid.page_id << ", " << rid.slot_num << ")\n";

  std::cout << "Free space after: " << page.freeSpace() << " bytes\n";

  return true;
}

// ============================================================
// Delete
// ============================================================

bool deleteTuple(SlottedPage &page) {
  printTitle("DELETE TUPLE");

  int slot_num = readInt("Enter slot number: ");

  if (slot_num < 0) {
    std::cout << "Invalid slot number.\n";
    return false;
  }

  bool success = page.deleteTuple(static_cast<uint16_t>(slot_num));

  if (success) {
    std::cout << "\n[SUCCESS] Tuple deleted.\n";
    std::cout << "The slot is now tombstoned.\n";
    return true;
  }

  std::cout << "\n[FAILED] Tuple does not exist or is already deleted.\n";

  return false;
}

// ============================================================
// Get tuple
// ============================================================

void getTuple(const SlottedPage &page, const Schema &schema) {
  printTitle("GET TUPLE");

  int slot_num = readInt("Enter slot number: ");

  if (slot_num < 0) {
    std::cout << "Invalid slot number.\n";
    return;
  }

  auto tuple_opt = page.getTuple(static_cast<uint16_t>(slot_num));

  if (!tuple_opt.has_value()) {
    std::cout << "\nTuple not found or deleted.\n";
    return;
  }

  const Tuple &tuple = *tuple_opt;

  std::cout << "\n";
  printBorder();

  printRow("TUPLE " + std::to_string(slot_num));

  for (size_t i = 0; i < schema.getColumnCount(); ++i) {
    const Column &column = schema.getColumn(i);

    Value value = tuple.getValue(schema, i);

    if (column.type == TypeId::INTEGER) {
      printRow(column.name + " = " + std::to_string(value.getInteger()));
    } else if (column.type == TypeId::VARCHAR) {
      printRow(column.name + " = \"" + value.getString() + "\"");
    }
  }

  printBorder();
}

// ============================================================
// Menu
// ============================================================

void printMenu() {
  printTitle("MENU");

  std::cout << "\n";
  std::cout << "  1. Insert tuple\n";
  std::cout << "  2. Get tuple\n";
  std::cout << "  3. Delete tuple\n";
  std::cout << "  4. Visualize page\n";
  std::cout << "  5. Reset page\n";
  std::cout << "  6. Insert dummy tuples\n";
  std::cout << "  0. Exit\n";
  std::cout << "\n";
}

// ============================================================
// Main
// ============================================================

int main() {
  constexpr page_id_t page_id = 0;

  printTitle("WALOUDB INTERACTIVE SLOTTED PAGE");

  // ----------------------------------------------------------
  // Open database
  // ----------------------------------------------------------

  DiskManager disk_manager("test.db");

  // ----------------------------------------------------------
  // Create RAM page
  // ----------------------------------------------------------

  Page raw_page;

  // ----------------------------------------------------------
  // Try to load page from disk
  // ----------------------------------------------------------

  bool loaded = disk_manager.readPage(page_id, raw_page.getData());

  // ----------------------------------------------------------
  // Interpret RAM as a slotted page
  // ----------------------------------------------------------

  SlottedPage page(raw_page.getData());

  if (!loaded) {

    std::cout << "\nNo existing page found.\n";

    std::cout << "Creating new page...\n";

    page.Init(page_id);

    disk_manager.writePage(page_id, raw_page.getData());

    std::cout << "New page created and saved.\n";

  } else {

    std::cout << "\nExisting page loaded from disk.\n";
  }

  // ----------------------------------------------------------
  // Schema
  // ----------------------------------------------------------

  Schema schema = createSchema();

  // ----------------------------------------------------------
  // Interactive loop
  // ----------------------------------------------------------

  bool running = true;

  while (running) {

    printMenu();

    int choice = readInt("Choose an option: ");

    switch (choice) {

      // ======================================================
      // INSERT TUPLE
      // ======================================================

    case 1: {

      bool success = insertTuple(page, schema);

      if (success) {

        raw_page.setDirty(true);

        disk_manager.writePage(page_id, raw_page.getData());

        raw_page.setDirty(false);

        std::cout << "\nPage automatically "
                     "saved to disk.\n";
      }

      break;
    }

      // ======================================================
      // GET TUPLE
      // ======================================================

    case 2:

      getTuple(page, schema);

      break;

      // ======================================================
      // DELETE TUPLE
      // ======================================================

    case 3: {

      bool success = deleteTuple(page);

      if (success) {

        raw_page.setDirty(true);

        disk_manager.writePage(page_id, raw_page.getData());

        raw_page.setDirty(false);

        std::cout << "\nPage automatically "
                     "saved to disk.\n";
      }

      break;
    }

      // ======================================================
      // VISUALIZE
      // ======================================================

    case 4:

      visualizePage(page, schema);

      break;

      // ======================================================
      // RESET PAGE
      // ======================================================

    case 5: {

      printTitle("RESET PAGE");

      raw_page.resetMemory();

      page.Init(page_id);

      raw_page.setDirty(true);

      bool success = disk_manager.writePage(page_id, raw_page.getData());

      if (success) {

        raw_page.setDirty(false);

        std::cout << "\n[SUCCESS] "
                     "Page reset and saved.\n";

      } else {

        std::cout << "\n[FAILED] "
                     "Could not save page.\n";
      }

      break;
    }

      // ======================================================
      // INSERT DUMMY TUPLES
      // ======================================================

    case 6: {

      printTitle("INSERT DUMMY TUPLES");

      int count = readInt("How many dummy tuples? ");

      if (count <= 0) {

        std::cout << "\nInvalid count.\n";

        break;
      }

      int inserted = 0;

      std::cout << "\n";

      for (int i = 0; i < count; ++i) {

        // ----------------------------------------------------
        // Generate dummy data
        // ----------------------------------------------------

        int32_t id = static_cast<int32_t>(page.getSlotCount() + 1);

        std::string name = "Dummy_" + std::to_string(id);

        Tuple tuple = Tuple::Serialize(
            {
                Value(id),
                Value(name),
            },
            schema);

        // ----------------------------------------------------
        // Insert
        // ----------------------------------------------------

        RID rid{};

        bool success = page.insertTuple(tuple, &rid);

        if (!success) {

          std::cout << "[STOPPED] "
                       "Page is full.\n";

          break;
        }

        ++inserted;

        raw_page.setDirty(true);

        // ----------------------------------------------------
        // Print inserted tuple
        // ----------------------------------------------------

        std::cout << "[INSERTED] ";

        std::cout << "RID=(" << rid.page_id << ", " << rid.slot_num << ")";

        std::cout << " | id=" << id;

        std::cout << " | name=\"" << name << "\"";

        std::cout << " | size=" << tuple.getLength() << " bytes";

        std::cout << "\n";
      }

      // ------------------------------------------------------
      // Automatically save
      // ------------------------------------------------------

      if (inserted > 0) {

        bool success = disk_manager.writePage(page_id, raw_page.getData());

        if (success) {

          raw_page.setDirty(false);

          std::cout << "\n[SUCCESS] "
                       "Page saved to disk.\n";

        } else {

          std::cout << "\n[FAILED] "
                       "Could not save page.\n";
        }
      }

      // ------------------------------------------------------
      // Summary
      // ------------------------------------------------------

      std::cout << "\n";

      printBorder();

      printRow("DUMMY INSERT SUMMARY");

      printRow("Requested : " + std::to_string(count));

      printRow("Inserted  : " + std::to_string(inserted));

      printRow("Slots     : " + std::to_string(page.getSlotCount()));

      printRow("Free space: " + std::to_string(page.freeSpace()) + " bytes");

      printBorder();

      break;
    }

      // ======================================================
      // EXIT
      // ======================================================

    case 0: {

      std::cout << "\nSaving page before exit...\n";

      bool success = disk_manager.writePage(page_id, raw_page.getData());

      if (success) {

        raw_page.setDirty(false);

        std::cout << "[SUCCESS] "
                     "Page saved to disk.\n";

      } else {

        std::cout << "[FAILED] "
                     "Could not save page to disk.\n";
      }

      std::cout << "Goodbye.\n";

      running = false;

      break;
    }

      // ======================================================
      // INVALID OPTION
      // ======================================================

    default:

      std::cout << "\nInvalid option.\n";

      break;
    }
  }

  return 0;
}
