#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/DiskManager.h"
#include "waloudb/storage/Lrur.h"
#include "waloudb/storage/Page.h"
#include "waloudb/storage/Schema.h"
#include "waloudb/storage/SlottedPage.h"
#include "waloudb/storage/Tuple.h"
#include "waloudb/storage/Value.h"

using namespace WalouDB;

// ============================================================
// Configuration
// ============================================================

constexpr size_t BUFFER_POOL_SIZE = 5;

const std::string DATABASE_FILE = "waloudb_playground.db";

// ============================================================
// Schema
// ============================================================
//
// For now the playground uses:
//
// id   INTEGER
// name VARCHAR
//
// Later we can make the schema interactive.
//

Schema getDefaultSchema() {

  return Schema(
      {Column("id", TypeId::INTEGER), Column("name", TypeId::VARCHAR)});
}

// ============================================================
// Application State
// ============================================================

struct AppState {

  std::unique_ptr<DiskManager> disk;
  std::unique_ptr<BufferPoolManager> buffer_pool;

  std::vector<page_id_t> known_pages;

  page_id_t current_page_id = INVALID_PAGE_ID;
};

// ============================================================
// Utility
// ============================================================

void clearInput() {

  std::cin.clear();

  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

void waitForEnter() {

  std::cout << "\nPress ENTER to continue...";
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

void clearScreen() {

#ifdef _WIN32

  std::system("cls");

#else

  std::system("clear");

#endif
}

void printLine(char c = '=', int width = 70) {

  std::cout << std::string(width, c) << "\n";
}

void printTitle(const std::string &title) {

  printLine();

  std::cout << " " << title << "\n";

  printLine();
}

bool pageExists(const AppState &state, page_id_t page_id) {

  return std::find(state.known_pages.begin(), state.known_pages.end(),
                   page_id) != state.known_pages.end();
}

// ============================================================
// Tuple Helpers
// ============================================================

Tuple createTuple(int32_t id, const std::string &name) {

  return Tuple::Serialize({Value(id), Value(name)}, getDefaultSchema());
}

void printTupleValues(const Tuple &tuple) {

  Schema schema = getDefaultSchema();

  try {

    Value id = tuple.getValue(schema, 0);

    Value name = tuple.getValue(schema, 1);

    std::cout << "id=" << id.getInteger() << ", ";

    std::cout << "name=\"" << name.getString() << "\"";

  } catch (...) {

    std::cout << "<unable to deserialize tuple>";
  }
}

// ============================================================
// MAIN MENU
// ============================================================

void printMainMenu(const AppState &state) {

  printTitle("WALOUDB STORAGE PLAYGROUND");

  std::cout << "\n";

  if (state.current_page_id == INVALID_PAGE_ID) {

    std::cout << "Current Page: "
              << "NONE\n";

  } else {

    std::cout << "Current Page: " << state.current_page_id << "\n";
  }

  std::cout << "Known Pages: " << state.known_pages.size() << "\n";

  std::cout << "\n";

  printLine('-');

  std::cout << "\n";

  std::cout << "  1. Create New Page\n";
  std::cout << "  2. Switch / Fetch Page\n";
  std::cout << "  3. Insert Tuple\n";
  std::cout << "  4. Delete Tuple\n";
  std::cout << "  5. Visualize Current Page\n";
  std::cout << "  6. Visualize Buffer Pool\n";
  std::cout << "  7. Visualize LRU Replacer\n";
  std::cout << "  8. List All Known Pages\n";
  std::cout << "  9. Flush Current Page\n";
  std::cout << " 10. Flush All Pages\n";
  std::cout << " 11. Run Storage Overview\n";
  std::cout << "  0. Exit\n";

  std::cout << "\n";

  printLine('-');

  std::cout << "\nChoice: ";
}

// ============================================================
// CREATE PAGE
// ============================================================

void createNewPage(AppState &state) {

  printTitle("CREATE NEW PAGE");

  page_id_t page_id;

  Page *page = state.buffer_pool->newPage(&page_id);

  if (page == nullptr) {

    std::cout << "\n[ERROR] "
              << "Could not allocate a page.\n";

    std::cout << "The buffer pool may be full and "
              << "all pages may be pinned.\n";

    return;
  }

  // Initialize slotted page.
  SlottedPage slotted(page->getData());

  slotted.Init(page_id);

  state.known_pages.push_back(page_id);

  state.current_page_id = page_id;

  // Mark dirty and unpin.
  state.buffer_pool->unpinPage(page_id, true);

  std::cout << "\n";

  std::cout << "[SUCCESS] Created page " << page_id << "\n";

  std::cout << "Current page switched to " << page_id << "\n";
}

// ============================================================
// SWITCH PAGE
// ============================================================

void switchPage(AppState &state) {

  printTitle("SWITCH PAGE");

  if (state.known_pages.empty()) {

    std::cout << "\nNo pages exist yet.\n";

    return;
  }

  std::cout << "\nKnown pages:\n\n";

  for (page_id_t page_id : state.known_pages) {

    std::cout << "  Page " << page_id;

    if (page_id == state.current_page_id) {

      std::cout << "  <-- CURRENT";
    }

    std::cout << "\n";
  }

  std::cout << "\nEnter page ID: ";

  page_id_t page_id;

  std::cin >> page_id;

  if (!std::cin) {

    clearInput();

    std::cout << "\nInvalid input.\n";

    return;
  }

  if (!pageExists(state, page_id)) {

    std::cout << "\n[ERROR] Unknown page.\n";

    return;
  }

  Page *page = state.buffer_pool->fetchPage(page_id);

  if (page == nullptr) {

    std::cout << "\n[ERROR] "
              << "Could not fetch page.\n";

    return;
  }

  state.current_page_id = page_id;

  std::cout << "\n[SUCCESS] Switched to page " << page_id << "\n";

  state.buffer_pool->unpinPage(page_id, false);
}

// ============================================================
// INSERT TUPLE
// ============================================================

void insertTuple(AppState &state) {

  printTitle("INSERT TUPLE");

  if (state.current_page_id == INVALID_PAGE_ID) {

    std::cout << "\n[ERROR] "
              << "Create or switch to a page first.\n";

    return;
  }

  int32_t id;

  std::string name;

  std::cout << "\nID: ";

  std::cin >> id;

  if (!std::cin) {

    clearInput();

    std::cout << "\nInvalid ID.\n";

    return;
  }

  clearInput();

  std::cout << "Name: ";

  std::getline(std::cin, name);

  Page *raw = state.buffer_pool->fetchPage(state.current_page_id);

  if (raw == nullptr) {

    std::cout << "\n[ERROR] "
              << "Could not fetch current page.\n";

    return;
  }

  SlottedPage page(raw->getData());

  Tuple tuple = createTuple(id, name);

  RID rid;

  bool inserted = page.insertTuple(tuple, &rid);

  if (!inserted) {

    std::cout << "\n[ERROR] "
              << "Not enough space in page.\n";

    state.buffer_pool->unpinPage(state.current_page_id, false);

    return;
  }

  state.buffer_pool->unpinPage(state.current_page_id, true);

  std::cout << "\n";

  std::cout << "[SUCCESS] Tuple inserted.\n";

  std::cout << "RID:\n";

  std::cout << "  Page ID : " << rid.page_id << "\n";

  std::cout << "  Slot    : " << rid.slot_num << "\n";
}

// ============================================================
// DELETE TUPLE
// ============================================================

void deleteTuple(AppState &state) {

  printTitle("DELETE TUPLE");

  if (state.current_page_id == INVALID_PAGE_ID) {

    std::cout << "\nNo current page.\n";

    return;
  }

  uint16_t slot;

  std::cout << "\nSlot number: ";

  std::cin >> slot;

  if (!std::cin) {

    clearInput();

    std::cout << "\nInvalid slot number.\n";

    return;
  }

  Page *raw = state.buffer_pool->fetchPage(state.current_page_id);

  if (raw == nullptr) {

    std::cout << "\nCould not fetch page.\n";

    return;
  }

  SlottedPage page(raw->getData());

  bool deleted = page.deleteTuple(slot);

  state.buffer_pool->unpinPage(state.current_page_id, deleted);

  if (!deleted) {

    std::cout << "\n[ERROR] "
              << "Could not delete tuple.\n";

    return;
  }

  std::cout << "\n[SUCCESS] Tuple deleted.\n";

  std::cout << "Slot " << slot << " is now a tombstone.\n";
}

// ============================================================
// PAGE VISUALIZER
// ============================================================
//
// This visualizer displays:
//
// +------------------------------------------------------+
// | PAGE HEADER                                          |
// +------------------------------------------------------+
// | Page ID                                              |
// | Slot Count                                           |
// | Free Space                                           |
// +------------------------------------------------------+
// | SLOT DIRECTORY                                       |
// +------------------------------------------------------+
// | Slot | Status | Tuple Data                           |
// +------------------------------------------------------+
//
// For a more detailed physical visualizer we will add:
//
// Slot offset
// Tuple size
// Header size
// Free-space boundaries
//
// once we expose the slot structure.
//

void visualizePage(AppState &state) {

  printTitle("SLOTTED PAGE VISUALIZER");

  if (state.current_page_id == INVALID_PAGE_ID) {

    std::cout << "\nNo current page.\n";

    return;
  }

  Page *raw = state.buffer_pool->fetchPage(state.current_page_id);

  if (raw == nullptr) {

    std::cout << "\nCould not fetch page.\n";

    return;
  }

  SlottedPage page(raw->getData());

  uint16_t slot_count = page.getSlotCount();

  uint16_t free_space = page.freeSpace();

  // ========================================================
  // PAGE HEADER
  // ========================================================

  std::cout << "\n";

  printLine('=');

  std::cout << " PAGE HEADER\n";

  printLine('=');

  std::cout << std::left << std::setw(25) << "Page ID"
            << ": " << page.getPageId() << "\n";

  std::cout << std::left << std::setw(25) << "Page Size"
            << ": " << PAGE_SIZE << " bytes\n";

  std::cout << std::left << std::setw(25) << "Slot Count"
            << ": " << slot_count << "\n";

  std::cout << std::left << std::setw(25) << "Free Space"
            << ": " << free_space << " bytes\n";

  std::cout << std::left << std::setw(25) << "Used Space"
            << ": " << PAGE_SIZE - free_space << " bytes\n";

  // ========================================================
  // MEMORY BAR
  // ========================================================

  std::cout << "\n";

  printLine('=');

  std::cout << " PAGE SPACE OVERVIEW\n";

  printLine('=');

  constexpr int BAR_WIDTH = 50;

  double used_ratio = static_cast<double>(PAGE_SIZE - free_space) / PAGE_SIZE;

  int used_blocks = static_cast<int>(used_ratio * BAR_WIDTH);

  std::cout << "\n[";

  for (int i = 0; i < BAR_WIDTH; ++i) {

    if (i < used_blocks) {

      std::cout << '#';

    } else {

      std::cout << '.';
    }
  }

  std::cout << "]\n";

  std::cout << " # = Used\n";

  std::cout << " . = Free\n";

  // ========================================================
  // SLOT DIRECTORY
  // ========================================================

  std::cout << "\n";

  printLine('=');

  std::cout << " SLOT DIRECTORY\n";

  printLine('=');

  if (slot_count == 0) {

    std::cout << "\n<No slots>\n";

  } else {

    std::cout << "\n";

    std::cout << std::left << std::setw(10) << "SLOT"

              << std::setw(15) << "STATUS"

              << "DATA\n";

    printLine('-');

    for (uint16_t slot = 0; slot < slot_count; ++slot) {

      auto tuple = page.getTuple(slot);

      std::cout << std::left << std::setw(10) << slot;

      if (!tuple.has_value()) {

        std::cout << std::setw(15) << "TOMBSTONE"

                  << "<deleted>\n";

        continue;
      }

      std::cout << std::setw(15) << "ACTIVE";

      printTupleValues(*tuple);

      std::cout << "\n";
    }
  }

  // ========================================================
  // TUPLE VISUALIZATION
  // ========================================================

  std::cout << "\n";

  printLine('=');

  std::cout << " TUPLES\n";

  printLine('=');

  bool has_tuple = false;

  for (uint16_t slot = 0; slot < slot_count; ++slot) {

    auto tuple = page.getTuple(slot);

    if (!tuple.has_value()) {

      continue;
    }

    has_tuple = true;

    std::cout << "\n";

    std::cout << "+--------------------------------------------------+\n";

    std::cout << "| SLOT " << std::setw(43) << std::left << slot << "|\n";

    std::cout << "+--------------------------------------------------+\n";

    std::cout << "| ";

    printTupleValues(*tuple);

    std::cout << "\n";

    std::cout << "+--------------------------------------------------+\n";
  }

  if (!has_tuple) {

    std::cout << "\n<No active tuples>\n";
  }

  // ========================================================
  // PHYSICAL PAGE CONCEPT
  // ========================================================

  std::cout << "\n";

  printLine('=');

  std::cout << " PHYSICAL PAGE CONCEPT\n";

  printLine('=');

  std::cout << "\n";

  std::cout << "+----------------------------------------------------------+\n";

  std::cout << "| HEADER                                                   |\n";

  std::cout << "+----------------------------------------------------------+\n";

  std::cout << "| SLOT DIRECTORY                                           |\n";

  std::cout << "| Slots: " << slot_count << "\n";

  std::cout << "+----------------------------------------------------------+\n";

  std::cout << "| FREE SPACE                                               |\n";

  std::cout << "| " << free_space << " bytes available\n";

  std::cout << "+----------------------------------------------------------+\n";

  std::cout << "| TUPLES                                                   |\n";

  std::cout << "| Stored from the end of the page toward the front         |\n";

  std::cout << "+----------------------------------------------------------+\n";

  state.buffer_pool->unpinPage(state.current_page_id, false);
}

// ============================================================
// LIST PAGES
// ============================================================

void listPages(AppState &state) {

  printTitle("KNOWN DATABASE PAGES");

  if (state.known_pages.empty()) {

    std::cout << "\nNo pages created.\n";

    return;
  }

  std::cout << "\n";

  for (page_id_t page_id : state.known_pages) {

    std::cout << "Page " << page_id;

    if (page_id == state.current_page_id) {

      std::cout << "  <-- CURRENT";
    }

    std::cout << "\n";
  }
}

// ============================================================
// FLUSH CURRENT PAGE
// ============================================================

void flushCurrentPage(AppState &state) {

  printTitle("FLUSH CURRENT PAGE");

  if (state.current_page_id == INVALID_PAGE_ID) {

    std::cout << "\nNo current page.\n";

    return;
  }

  bool result = state.buffer_pool->flushPage(state.current_page_id);

  if (result) {

    std::cout << "\n[SUCCESS] Page " << state.current_page_id
              << " flushed to disk.\n";

  } else {

    std::cout << "\n[ERROR] "
              << "Could not flush page.\n";
  }
}

// ============================================================
// FLUSH ALL
// ============================================================

void flushAllPages(AppState &state) {

  printTitle("FLUSH ALL PAGES");

  for (page_id_t page_id : state.known_pages) {

    state.buffer_pool->flushPage(page_id);
  }

  std::cout << "\n[SUCCESS] "
            << "All known pages flushed.\n";
}

// ============================================================
// BUFFER POOL VISUALIZER
// ============================================================
//
// Requires debug methods:
//
// getPoolSize()
// getFramePageId()
// getFramePinCount()
// getFrameDirty()
//
// See additions below.
//

void visualizeBufferPool(AppState &state) {

  printTitle("BUFFER POOL VISUALIZER");

  std::cout << "\n";

  std::cout << "Buffer Pool Size: " << state.buffer_pool->getPoolSize() << "\n";

  printLine('=');

  std::cout << std::left << std::setw(10) << "FRAME"

            << std::setw(15) << "PAGE ID"

            << std::setw(15) << "PIN COUNT"

            << std::setw(15) << "DIRTY"

            << "STATUS\n";

  printLine('-');

  for (frame_id_t frame = 0; frame < state.buffer_pool->getPoolSize();
       ++frame) {

    page_id_t page_id = state.buffer_pool->getFramePageId(frame);

    if (page_id == INVALID_PAGE_ID) {

      std::cout << std::left << std::setw(10) << frame

                << std::setw(15) << "-"

                << std::setw(15) << "-"

                << std::setw(15) << "-"

                << "FREE\n";

      continue;
    }

    int pin_count = state.buffer_pool->getFramePinCount(frame);

    bool dirty = state.buffer_pool->getFrameDirty(frame);

    std::cout << std::left << std::setw(10) << frame

              << std::setw(15) << page_id

              << std::setw(15) << pin_count

              << std::setw(15) << (dirty ? "YES" : "NO");

    if (pin_count > 0) {

      std::cout << "PINNED";

    } else {

      std::cout << "EVICTABLE";
    }

    std::cout << "\n";
  }

  std::cout << "\n";

  printLine('=');

  std::cout << "Legend:\n";

  std::cout << "  PINNED    -> Cannot be evicted\n";

  std::cout << "  EVICTABLE -> Can be selected by LRU\n";

  std::cout << "  FREE      -> Unused frame\n";
}

// ============================================================
// LRU VISUALIZER
// ============================================================
//
// Requires:
//
// std::vector<frame_id_t> Lrur::getFrames() const;
//
// Returned order:
//
// oldest -> newest
//

void visualizeLru(AppState &state) {

  printTitle("LRU REPLACER VISUALIZER");

  const auto &frames = state.buffer_pool->getReplacer().getFrames();

  if (frames.empty()) {

    std::cout << "\nLRU is empty.\n";

    std::cout << "All cached pages are currently pinned "
              << "or the buffer pool is empty.\n";

    return;
  }

  std::cout << "\n";

  std::cout << "Eviction order:\n\n";

  std::cout << "OLDEST";

  for (frame_id_t frame : frames) {

    std::cout << "  --->  "
              << "[Frame " << frame << "]";
  }

  std::cout << "  --->  NEWEST\n";

  std::cout << "\n";

  std::cout << "The next victim would be Frame " << frames.front() << "\n";
}

// ============================================================
// FULL STORAGE OVERVIEW
// ============================================================

void storageOverview(AppState &state) {

  clearScreen();

  printTitle("WALOUDB STORAGE SYSTEM OVERVIEW");

  std::cout << "\n";

  std::cout << "DATABASE FILE\n";

  printLine('-');

  std::cout << "File: " << DATABASE_FILE << "\n";

  std::cout << "Known pages: " << state.known_pages.size() << "\n";

  std::cout << "Current page: ";

  if (state.current_page_id == INVALID_PAGE_ID) {

    std::cout << "NONE\n";

  } else {

    std::cout << state.current_page_id << "\n";
  }

  std::cout << "\n";

  visualizeBufferPool(state);

  std::cout << "\n";

  visualizeLru(state);
}

// ============================================================
// MAIN
// ============================================================

int main() {

  // ========================================================
  // Startup
  // ========================================================

  AppState state;

  state.disk = std::make_unique<DiskManager>(DATABASE_FILE);

  state.buffer_pool =
      std::make_unique<BufferPoolManager>(BUFFER_POOL_SIZE, state.disk.get());

  bool running = true;

  // ========================================================
  // Main loop
  // ========================================================

  while (running) {

    clearScreen();

    printMainMenu(state);

    int choice;

    std::cin >> choice;

    if (!std::cin) {

      clearInput();

      std::cout << "\nInvalid choice.";

      waitForEnter();

      continue;
    }

    clearInput();

    clearScreen();

    switch (choice) {

      // =================================================
      // CREATE PAGE
      // =================================================

    case 1:

      createNewPage(state);

      waitForEnter();

      break;

      // =================================================
      // SWITCH PAGE
      // =================================================

    case 2:

      switchPage(state);

      waitForEnter();

      break;

      // =================================================
      // INSERT
      // =================================================

    case 3:

      insertTuple(state);

      waitForEnter();

      break;

      // =================================================
      // DELETE
      // =================================================

    case 4:

      deleteTuple(state);

      waitForEnter();

      break;

      // =================================================
      // PAGE VISUALIZER
      // =================================================

    case 5:

      visualizePage(state);

      waitForEnter();

      break;

      // =================================================
      // BUFFER POOL
      // =================================================

    case 6:

      visualizeBufferPool(state);

      waitForEnter();

      break;

      // =================================================
      // LRU
      // =================================================

    case 7:

      visualizeLru(state);

      waitForEnter();

      break;

      // =================================================
      // LIST PAGES
      // =================================================

    case 8:

      listPages(state);

      waitForEnter();

      break;

      // =================================================
      // FLUSH CURRENT
      // =================================================

    case 9:

      flushCurrentPage(state);

      waitForEnter();

      break;

      // =================================================
      // FLUSH ALL
      // =================================================

    case 10:

      flushAllPages(state);

      waitForEnter();

      break;

      // =================================================
      // OVERVIEW
      // =================================================

    case 11:

      storageOverview(state);

      waitForEnter();

      break;

      // =================================================
      // EXIT
      // =================================================

    case 0:

      std::cout << "\nFlushing pages...\n";

      flushAllPages(state);

      std::cout << "\nGoodbye from WalouDB.\n";

      running = false;

      break;

      // =================================================
      // INVALID
      // =================================================

    default:

      std::cout << "\nInvalid choice.\n";

      waitForEnter();

      break;
    }
  }

  return EXIT_SUCCESS;
}
