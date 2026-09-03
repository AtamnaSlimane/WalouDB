#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/DiskManager.h"
#include "waloudb/storage/Page.h"
#include "waloudb/storage/Schema.h"
#include "waloudb/storage/SlottedPage.h"
#include "waloudb/storage/Tuple.h"
#include "waloudb/storage/Value.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

using namespace WalouDB;

// ============================================================
// Configuration
// ============================================================

constexpr size_t BUFFER_POOL_SIZE = 5;

const std::string DATABASE_FILE = "waloudb.db";

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

    std::cout << "\nInvalid number. Try again.\n\n";

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
// Visual helpers
// ============================================================

constexpr int VISUAL_WIDTH = 72;

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

void printBorder() {

  std::cout << "+--------------------------------------------------------------"
               "--------+\n";
}

void printRow(const std::string &text) {

  std::cout << "| " << std::left << std::setw(68) << text << " |\n";
}

void waitForEnter() {

  std::cout << "\nPress ENTER to continue...";

  std::cin.get();
}

// ============================================================
// Page helpers
// ============================================================

bool isValidPageId(page_id_t page_id) { return page_id != INVALID_PAGE_ID; }

// ============================================================
// Active page helper
// ============================================================

Page *fetchActivePage(BufferPoolManager &bpm, page_id_t active_page_id) {

  if (!isValidPageId(active_page_id)) {

    std::cout << "\nNo active page selected.\n";

    return nullptr;
  }

  Page *page = bpm.fetchPage(active_page_id);

  if (page == nullptr) {

    std::cout << "\n[FAILED] Could not fetch active page.\n";
  }

  return page;
}

// ============================================================
// Tuple printing
// ============================================================

void printTupleValues(const Tuple &tuple, const Schema &schema) {

  for (size_t i = 0; i < schema.getColumnCount(); ++i) {

    const Column &column = schema.getColumn(i);

    Value value = tuple.getValue(schema, i);

    if (column.type == TypeId::INTEGER) {

      printRow(column.name + " = " + std::to_string(value.getInteger()));
    }

    else if (column.type == TypeId::VARCHAR) {

      printRow(column.name + " = \"" + value.getString() + "\"");
    }
  }
}

// ============================================================
// Visualize single page
// ============================================================

void visualizePage(const SlottedPage &page, const Schema &schema) {

  const uint16_t lower = page.getLower();

  const uint16_t upper = page.getUpper();

  const uint16_t slot_count = page.getSlotCount();

  const uint16_t free_space = page.freeSpace();

  // ----------------------------------------------------------
  // Statistics
  // ----------------------------------------------------------

  uint32_t live_tuple_bytes = 0;

  uint16_t active_slots = 0;
  uint16_t deleted_slots = 0;

  for (uint16_t i = 0; i < slot_count; ++i) {

    auto slot_opt = page.getSlotInfo(i);

    if (!slot_opt.has_value()) {
      continue;
    }

    const Slot &slot = *slot_opt;

    if (slot.deleted || slot.length == 0) {

      ++deleted_slots;

    } else {

      ++active_slots;

      live_tuple_bytes += slot.length;
    }
  }

  const uint32_t header_bytes = sizeof(PageHeader);

  const uint32_t slot_directory_bytes = slot_count * sizeof(Slot);

  // ==========================================================
  // Title
  // ==========================================================

  printTitle("WALOUDB PAGE VISUALIZER - PAGE " +
             std::to_string(page.getPageId()));

  // ==========================================================
  // Header
  // ==========================================================

  printBorder();

  printRow("PAGE HEADER");

  printRow("page_id    = " + std::to_string(page.getPageId()));

  printRow("lower      = " + std::to_string(lower));

  printRow("upper      = " + std::to_string(upper));

  printRow("slot_count = " + std::to_string(slot_count));

  printBorder();

  // ==========================================================
  // Statistics
  // ==========================================================

  std::cout << "\n";

  printBorder();

  printRow("PAGE STATISTICS");

  printRow("page size = " + std::to_string(PAGE_SIZE) + " bytes");

  printRow("header size = " + std::to_string(header_bytes) + " bytes");

  printRow("slot directory size = " + std::to_string(slot_directory_bytes) +
           " bytes");

  printRow("active slots = " + std::to_string(active_slots));

  printRow("deleted slots = " + std::to_string(deleted_slots));

  printRow("live tuple data = " + std::to_string(live_tuple_bytes) + " bytes");

  printRow("contiguous free space = " + std::to_string(free_space) + " bytes");

  printBorder();

  // ==========================================================
  // Memory layout
  // ==========================================================

  std::cout << "\n";

  printBorder();

  printRow("LOGICAL MEMORY LAYOUT");

  printRow("OFFSET 0");

  printRow("  +-- PAGE HEADER");

  printRow("  |   size = " + std::to_string(header_bytes) + " bytes");

  printRow("  +-- SLOT DIRECTORY");

  printRow("  |   size = " + std::to_string(slot_directory_bytes) + " bytes");

  printRow("  +-- lower = " + std::to_string(lower));

  printRow("  |");

  printRow("  |      FREE SPACE");

  printRow("  |      " + std::to_string(free_space) + " bytes");

  printRow("  |");

  printRow("  +-- upper = " + std::to_string(upper));

  printRow("  +-- TUPLE STORAGE");

  printRow("  |   live data = " + std::to_string(live_tuple_bytes) + " bytes");

  printRow("OFFSET " + std::to_string(PAGE_SIZE));

  printBorder();

  // ==========================================================
  // Slot directory
  // ==========================================================

  std::cout << "\n";

  printTitle("SLOT DIRECTORY");

  for (uint16_t i = 0; i < slot_count; ++i) {

    auto slot_opt = page.getSlotInfo(i);

    if (!slot_opt.has_value()) {
      continue;
    }

    const Slot slot = *slot_opt;

    printBorder();

    printRow("SLOT " + std::to_string(i));

    printRow("offset = " + std::to_string(slot.offset));

    printRow("length = " + std::to_string(slot.length) + " bytes");

    if (slot.deleted || slot.length == 0) {

      printRow("status = TOMBSTONED / DELETED");

    } else {

      printRow("status = ACTIVE");
    }

    printBorder();
  }

  if (slot_count == 0) {

    std::cout << "\nNo slots exist yet.\n";
  }

  // ==========================================================
  // Free space
  // ==========================================================

  std::cout << "\n";

  printBorder();

  printRow("FREE SPACE");

  printRow("from offset = " + std::to_string(lower));

  printRow("to offset   = " + std::to_string(upper));

  printRow("total       = " + std::to_string(free_space) + " bytes");

  printBorder();

  // ==========================================================
  // Tuple data
  // ==========================================================

  std::cout << "\n";

  printTitle("TUPLE DATA");

  struct TupleInfo {

    uint16_t slot_num;

    Slot slot;
  };

  std::vector<TupleInfo> tuples;

  for (uint16_t i = 0; i < slot_count; ++i) {

    auto slot_opt = page.getSlotInfo(i);

    if (!slot_opt.has_value()) {
      continue;
    }

    tuples.push_back({
        i,
        *slot_opt,
    });
  }

  std::sort(tuples.begin(), tuples.end(),

            [](const TupleInfo &a, const TupleInfo &b) {
              return a.slot.offset < b.slot.offset;
            });

  // ----------------------------------------------------------
  // Print tuples
  // ----------------------------------------------------------

  for (const TupleInfo &info : tuples) {

    uint16_t slot_num = info.slot_num;

    const Slot &slot = info.slot;

    printBorder();

    if (slot.deleted || slot.length == 0) {

      printRow("TUPLE SLOT " + std::to_string(slot_num) + " - DELETED");

      printRow("RID = (" + std::to_string(page.getPageId()) + ", " +
               std::to_string(slot_num) + ")");

      printRow("offset = " + std::to_string(slot.offset));

      printRow("length = 0 bytes");

      printRow("status = TOMBSTONED");

      printBorder();

      continue;
    }

    auto tuple_opt = page.getTuple(slot_num);

    if (!tuple_opt.has_value()) {

      printRow("ERROR: Slot marked active but tuple unavailable.");

      printBorder();

      continue;
    }

    const Tuple &tuple = *tuple_opt;

    printRow("TUPLE " + std::to_string(slot_num));

    printRow("RID = (" + std::to_string(page.getPageId()) + ", " +
             std::to_string(slot_num) + ")");

    printRow("offset = " + std::to_string(slot.offset));

    printRow("length = " + std::to_string(slot.length) + " bytes");

    printRow("status = ACTIVE");

    printTupleValues(tuple, schema);

    printBorder();
  }

  if (active_slots == 0 && deleted_slots == 0) {

    std::cout << "\nNo tuples stored in this page.\n";
  }
}

// ============================================================
// Create new page
// ============================================================

bool createNewPage(BufferPoolManager &bpm, page_id_t &active_page_id,
                   std::vector<page_id_t> &known_pages) {

  printTitle("CREATE NEW PAGE");

  page_id_t new_page_id;

  Page *raw = bpm.newPage(&new_page_id);

  if (raw == nullptr) {

    std::cout << "\n[FAILED] No available frame.\n";

    std::cout << "All buffer pool frames may be pinned.\n";

    return false;
  }

  // ----------------------------------------------------------
  // Initialize SlottedPage
  // ----------------------------------------------------------

  SlottedPage page(raw->getData());

  page.Init(new_page_id);

  // Mark dirty and unpin.
  bpm.unpinPage(new_page_id, true);

  active_page_id = new_page_id;

  known_pages.push_back(new_page_id);

  std::cout << "\n[SUCCESS] New page created.\n";

  std::cout << "Page ID: " << new_page_id << "\n";

  std::cout << "Active page switched to: " << active_page_id << "\n";

  return true;
}

// ============================================================
// Switch active page
// ============================================================

void switchActivePage(page_id_t &active_page_id,
                      const std::vector<page_id_t> &known_pages,
                      BufferPoolManager &bpm) {

  printTitle("SWITCH ACTIVE PAGE");

  if (known_pages.empty()) {

    std::cout << "\nNo known pages.\n";

    return;
  }

  std::cout << "\nKnown pages:\n\n";

  for (page_id_t id : known_pages) {

    std::cout << "  Page " << id;

    if (id == active_page_id) {

      std::cout << "  <-- ACTIVE";
    }

    std::cout << "\n";
  }

  std::cout << "\n";

  int input = readInt("Enter page ID: ");

  if (input < 0) {

    std::cout << "\nInvalid page ID.\n";

    return;
  }

  page_id_t target = static_cast<page_id_t>(input);

  // Verify that we know about it.
  bool found = std::find(known_pages.begin(), known_pages.end(), target) !=
               known_pages.end();

  if (!found) {

    std::cout << "\n[FAILED] Page is not in the known page list.\n";

    return;
  }

  // Try fetching it.
  Page *page = bpm.fetchPage(target);

  if (page == nullptr) {

    std::cout << "\n[FAILED] Could not fetch page.\n";

    return;
  }

  bpm.unpinPage(target, false);

  active_page_id = target;

  std::cout << "\n[SUCCESS] Active page switched to " << active_page_id
            << ".\n";
}

// ============================================================
// Insert tuple
// ============================================================

bool insertTuple(BufferPoolManager &bpm, page_id_t active_page_id,
                 const Schema &schema) {

  printTitle("INSERT TUPLE");

  Page *raw = fetchActivePage(bpm, active_page_id);

  if (raw == nullptr) {
    return false;
  }

  SlottedPage page(raw->getData());

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

  // ----------------------------------------------------------
  // Always release fetched page
  // ----------------------------------------------------------

  bpm.unpinPage(active_page_id, success);

  if (!success) {

    std::cout << "\n[FAILED] Not enough space.\n";

    return false;
  }

  std::cout << "\n[SUCCESS] Tuple inserted.\n";

  std::cout << "RID = (" << rid.page_id << ", " << rid.slot_num << ")\n";

  return true;
}

// ============================================================
// Get tuple
// ============================================================

void getTuple(BufferPoolManager &bpm, page_id_t active_page_id,
              const Schema &schema) {

  printTitle("GET TUPLE");

  Page *raw = fetchActivePage(bpm, active_page_id);

  if (raw == nullptr) {
    return;
  }

  SlottedPage page(raw->getData());

  int input = readInt("Enter slot number: ");

  if (input < 0) {

    std::cout << "\nInvalid slot number.\n";

    bpm.unpinPage(active_page_id, false);

    return;
  }

  uint16_t slot_num = static_cast<uint16_t>(input);

  auto tuple_opt = page.getTuple(slot_num);

  if (!tuple_opt.has_value()) {

    std::cout << "\nTuple not found or deleted.\n";

    bpm.unpinPage(active_page_id, false);

    return;
  }

  printBorder();

  printRow("TUPLE " + std::to_string(slot_num));

  printRow("RID = (" + std::to_string(active_page_id) + ", " +
           std::to_string(slot_num) + ")");

  printTupleValues(*tuple_opt, schema);

  printBorder();

  bpm.unpinPage(active_page_id, false);
}

// ============================================================
// Update tuple
// ============================================================

bool updateTuple(BufferPoolManager &bpm, page_id_t active_page_id,
                 const Schema &schema) {

  printTitle("UPDATE TUPLE");

  Page *raw = fetchActivePage(bpm, active_page_id);

  if (raw == nullptr) {
    return false;
  }

  SlottedPage page(raw->getData());

  int input = readInt("Enter slot number: ");

  if (input < 0) {

    bpm.unpinPage(active_page_id, false);

    return false;
  }

  uint16_t slot_num = static_cast<uint16_t>(input);

  auto old_tuple = page.getTuple(slot_num);

  if (!old_tuple.has_value()) {

    std::cout << "\n[FAILED] Tuple does not exist or is deleted.\n";

    bpm.unpinPage(active_page_id, false);

    return false;
  }

  // ----------------------------------------------------------
  // Show old tuple
  // ----------------------------------------------------------

  std::cout << "\nCurrent tuple:\n";

  printBorder();

  printTupleValues(*old_tuple, schema);

  printBorder();

  // ----------------------------------------------------------
  // New values
  // ----------------------------------------------------------

  std::cout << "\nEnter new values:\n";

  int id = readInt("New id: ");

  std::string name = readString("New name: ");

  Tuple new_tuple = Tuple::Serialize(
      {
          Value(static_cast<int32_t>(id)),
          Value(name),
      },
      schema);

  auto slot_info = page.getSlotInfo(slot_num);

  if (slot_info.has_value()) {

    std::cout << "\nOld tuple size: " << slot_info->length << " bytes\n";

    std::cout << "New tuple size: " << new_tuple.getLength() << " bytes\n";
  }

  // ----------------------------------------------------------
  // Update
  // ----------------------------------------------------------

  bool success = page.updateTuple(slot_num, new_tuple);

  bpm.unpinPage(active_page_id, success);

  if (!success) {

    std::cout << "\n[FAILED] Not enough space.\n";

    return false;
  }

  std::cout << "\n[SUCCESS] Tuple updated.\n";

  std::cout << "RID remains = (" << active_page_id << ", " << slot_num << ")\n";

  return true;
}

// ============================================================
// Delete tuple
// ============================================================

bool deleteTuple(BufferPoolManager &bpm, page_id_t active_page_id) {

  printTitle("DELETE TUPLE");

  Page *raw = fetchActivePage(bpm, active_page_id);

  if (raw == nullptr) {
    return false;
  }

  SlottedPage page(raw->getData());

  int input = readInt("Enter slot number: ");

  if (input < 0) {

    bpm.unpinPage(active_page_id, false);

    return false;
  }

  bool success = page.deleteTuple(static_cast<uint16_t>(input));

  bpm.unpinPage(active_page_id, success);

  if (success) {

    std::cout << "\n[SUCCESS] Tuple deleted.\n";

    std::cout << "Slot is now tombstoned.\n";

  } else {

    std::cout << "\n[FAILED] Tuple not found or already deleted.\n";
  }

  return success;
}

// ============================================================
// Compact active page
// ============================================================

bool compactActivePage(BufferPoolManager &bpm, page_id_t active_page_id) {

  printTitle("COMPACT ACTIVE PAGE");

  Page *raw = fetchActivePage(bpm, active_page_id);

  if (raw == nullptr) {
    return false;
  }

  SlottedPage page(raw->getData());

  uint16_t before = page.freeSpace();

  std::cout << "\nFree space before: " << before << " bytes\n";

  page.compact();

  uint16_t after = page.freeSpace();

  bpm.unpinPage(active_page_id, true);

  std::cout << "\n[SUCCESS] Page compacted.\n";

  std::cout << "Free space after: " << after << " bytes\n";

  std::cout << "Recovered: " << (after - before) << " bytes\n";

  return true;
}

// ============================================================
// Visualize active page
// ============================================================

void visualizeActivePage(BufferPoolManager &bpm, page_id_t active_page_id,
                         const Schema &schema) {

  if (!isValidPageId(active_page_id)) {

    std::cout << "\nNo active page.\n";

    return;
  }

  Page *raw = bpm.fetchPage(active_page_id);

  if (raw == nullptr) {

    std::cout << "\n[FAILED] Could not fetch page.\n";

    return;
  }

  SlottedPage page(raw->getData());

  visualizePage(page, schema);

  bpm.unpinPage(active_page_id, false);
}

// ============================================================
// Visualize all pages
// ============================================================

void visualizeAllPages(BufferPoolManager &bpm,
                       const std::vector<page_id_t> &known_pages,
                       page_id_t active_page_id, const Schema &schema) {

  printTitle("ALL PAGES");

  if (known_pages.empty()) {

    std::cout << "\nNo pages created yet.\n";

    return;
  }

  std::cout << "\nKnown page count: " << known_pages.size() << "\n";

  for (page_id_t page_id : known_pages) {

    std::cout << "\n";

    printLine('-');

    std::cout << "PAGE " << page_id;

    if (page_id == active_page_id) {

      std::cout << "  <-- ACTIVE";
    }

    std::cout << "\n";

    printLine('-');

    Page *raw = bpm.fetchPage(page_id);

    if (raw == nullptr) {

      std::cout << "[FAILED] Could not fetch page.\n";

      continue;
    }

    SlottedPage page(raw->getData());

    std::cout << "Slots: " << page.getSlotCount() << "\n";

    std::cout << "Free space: " << page.freeSpace() << " bytes\n";

    std::cout << "lower: " << page.getLower() << "\n";

    std::cout << "upper: " << page.getUpper() << "\n";

    // --------------------------------------------------------
    // Count active/deleted
    // --------------------------------------------------------

    uint16_t active = 0;
    uint16_t deleted = 0;

    for (uint16_t i = 0; i < page.getSlotCount(); ++i) {

      auto slot = page.getSlotInfo(i);

      if (!slot.has_value()) {
        continue;
      }

      if (slot->deleted || slot->length == 0) {

        ++deleted;

      } else {

        ++active;
      }
    }

    std::cout << "Active tuples: " << active << "\n";

    std::cout << "Deleted tuples: " << deleted << "\n";

    bpm.unpinPage(page_id, false);
  }
}

// ============================================================
// Visualize Buffer Pool
// ============================================================

void visualizeBufferPool(const BufferPoolManager &bpm,
                         page_id_t active_page_id) {

  printTitle("BUFFER POOL VISUALIZER");

  const size_t pool_size = bpm.getPoolSize();

  std::cout << "\n";

  printBorder();

  printRow("BUFFER POOL SIZE = " + std::to_string(pool_size));

  printBorder();

  std::cout << "\n";

  std::cout << "+--------+------------+------------+----------+------------+\n";

  std::cout << "| FRAME  "
            << "| PAGE ID    "
            << "| PIN COUNT  "
            << "| DIRTY    "
            << "| STATUS     |\n";

  std::cout << "+--------+------------+------------+----------+------------+\n";

  for (frame_id_t frame_id = 0; frame_id < static_cast<frame_id_t>(pool_size);
       ++frame_id) {

    page_id_t page_id = bpm.getFramePageId(frame_id);

    int pin_count = bpm.getFramePinCount(frame_id);

    bool dirty = bpm.getFrameDirty(frame_id);

    std::string page_text;

    std::string status;

    if (page_id == INVALID_PAGE_ID) {

      page_text = "EMPTY";

      status = "FREE";

    } else {

      page_text = std::to_string(page_id);

      if (page_id == active_page_id) {

        status = "ACTIVE PAGE";

      } else if (pin_count > 0) {

        status = "PINNED";

      } else {

        status = "EVICTABLE";
      }
    }

    std::cout << "| " << std::setw(6) << std::left << frame_id

              << "| " << std::setw(11) << page_text

              << "| " << std::setw(11) << pin_count

              << "| " << std::setw(9) << (dirty ? "YES" : "NO")

              << "| " << std::setw(11) << status

              << "|\n";
  }

  std::cout << "+--------+------------+------------+----------+------------+\n";

  // ----------------------------------------------------------
  // Summary
  // ----------------------------------------------------------

  size_t used_frames = 0;
  size_t pinned_frames = 0;
  size_t dirty_frames = 0;

  for (frame_id_t frame_id = 0; frame_id < static_cast<frame_id_t>(pool_size);
       ++frame_id) {

    page_id_t page_id = bpm.getFramePageId(frame_id);

    if (page_id != INVALID_PAGE_ID) {
      ++used_frames;
    }

    if (bpm.getFramePinCount(frame_id) > 0) {
      ++pinned_frames;
    }

    if (bpm.getFrameDirty(frame_id)) {
      ++dirty_frames;
    }
  }

  std::cout << "\nUsed frames: " << used_frames << " / " << pool_size << "\n";

  std::cout << "Pinned frames: " << pinned_frames << "\n";

  std::cout << "Dirty frames: " << dirty_frames << "\n";
}

// ============================================================
// Visualize LRU Replacer
// ============================================================

void visualizeLru(const BufferPoolManager &bpm) {

  printTitle("LRU REPLACER VISUALIZER");

  const Lrur &replacer = bpm.getReplacer();

  std::vector<frame_id_t> frames = replacer.getFrames();

  std::cout << "\n";

  printBorder();

  printRow("EVICTABLE FRAMES = " + std::to_string(frames.size()));

  printBorder();

  if (frames.empty()) {

    std::cout << "\nLRU replacer is empty.\n";

    std::cout << "No frames are currently evictable.\n";

    return;
  }

  std::cout << "\n";

  std::cout << "LRU ORDER:\n\n";

  std::cout << "OLDEST";

  for (frame_id_t frame : frames) {

    std::cout << "  ->  [Frame " << frame << "]";
  }

  std::cout << "  ->  NEWEST\n\n";

  std::cout << "Legend:\n";

  std::cout << "  OLDEST = next likely victim\n";

  std::cout << "  NEWEST = most recently unpinned\n";
}

// ============================================================
// Flush active page
// ============================================================

void flushActivePage(BufferPoolManager &bpm, page_id_t active_page_id) {

  printTitle("FLUSH ACTIVE PAGE");

  if (!isValidPageId(active_page_id)) {

    std::cout << "\nNo active page.\n";

    return;
  }

  bool success = bpm.flushPage(active_page_id);

  if (success) {

    std::cout << "\n[SUCCESS] Page " << active_page_id << " flushed to disk.\n";

  } else {

    std::cout << "\n[FAILED] Could not flush page.\n";
  }
}

// ============================================================
// Flush all pages
// ============================================================

void flushAllPages(BufferPoolManager &bpm) {

  printTitle("FLUSH ALL BUFFER POOL PAGES");

  size_t flushed = 0;
  size_t failed = 0;

  for (frame_id_t frame_id = 0;
       frame_id < static_cast<frame_id_t>(bpm.getPoolSize()); ++frame_id) {

    page_id_t page_id = bpm.getFramePageId(frame_id);

    if (page_id == INVALID_PAGE_ID) {
      continue;
    }

    bool success = bpm.flushPage(page_id);

    if (success) {

      ++flushed;

      std::cout << "[FLUSHED] Page " << page_id << "\n";

    } else {

      ++failed;

      std::cout << "[FAILED] Page " << page_id << "\n";
    }
  }

  std::cout << "\n";

  printBorder();

  printRow("FLUSH ALL SUMMARY");

  printRow("Successfully flushed = " + std::to_string(flushed));

  printRow("Failed = " + std::to_string(failed));

  printBorder();
}

// ============================================================
// Menu
// ============================================================

void printMenu(page_id_t active_page_id) {

  printTitle("WALOUDB STORAGE ENGINE");

  if (isValidPageId(active_page_id)) {

    std::cout << "\nACTIVE PAGE: " << active_page_id << "\n";

  } else {

    std::cout << "\nACTIVE PAGE: NONE\n";
  }

  std::cout << "\n";

  std::cout << "  PAGE MANAGEMENT\n";

  std::cout << "  ------------------------------------------------\n";

  std::cout << "  1. Create new page\n";

  std::cout << "  2. Switch active page\n";

  std::cout << "\n";

  std::cout << "  TUPLE OPERATIONS\n";

  std::cout << "  ------------------------------------------------\n";

  std::cout << "  3. Insert tuple\n";

  std::cout << "  4. Get tuple\n";

  std::cout << "  5. Update tuple\n";

  std::cout << "  6. Delete tuple\n";

  std::cout << "\n";

  std::cout << "  PAGE OPERATIONS\n";

  std::cout << "  ------------------------------------------------\n";

  std::cout << "  7. Compact active page\n";

  std::cout << "  8. Visualize active page\n";

  std::cout << "  9. Visualize all pages\n";

  std::cout << "\n";

  std::cout << "  BUFFER POOL / CACHE\n";

  std::cout << "  ------------------------------------------------\n";

  std::cout << " 10. Visualize Buffer Pool\n";

  std::cout << " 11. Visualize LRU Replacer\n";

  std::cout << "\n";

  std::cout << "  DISK\n";

  std::cout << "  ------------------------------------------------\n";

  std::cout << " 12. Flush page\n";

  std::cout << " 13. Flush all pages\n";

  std::cout << "\n";

  std::cout << "   0. Exit\n";

  std::cout << "\n";
}

// ============================================================
// Main
// ============================================================

int main() {

  std::cout << "\n";

  std::cout << "############################################################\n";

  std::cout << "#                                                          #\n";

  std::cout << "#              WALOUDB STORAGE PLAYGROUND                  #\n";

  std::cout << "#                                                          #\n";

  std::cout << "############################################################\n";

  // ----------------------------------------------------------
  // Disk Manager
  // ----------------------------------------------------------

  DiskManager disk_manager(DATABASE_FILE);

  // ----------------------------------------------------------
  // Buffer Pool Manager
  // ----------------------------------------------------------

  BufferPoolManager bpm(BUFFER_POOL_SIZE, &disk_manager);

  // ----------------------------------------------------------
  // Schema
  // ----------------------------------------------------------

  Schema schema = createSchema();

  // ----------------------------------------------------------
  // Application state
  // ----------------------------------------------------------

  page_id_t active_page_id = INVALID_PAGE_ID;

  // Pages created during this session.
  //
  // Later we can replace this with a proper database catalog.
  std::vector<page_id_t> known_pages;

  bool running = true;

  // ==========================================================
  // Main loop
  // ==========================================================

  while (running) {

    printMenu(active_page_id);

    int choice = readInt("Choose an option: ");

    switch (choice) {

      // ========================================================
      // 1. Create page
      // ========================================================

    case 1:

      createNewPage(bpm, active_page_id, known_pages);

      break;

      // ========================================================
      // 2. Switch page
      // ========================================================

    case 2:

      switchActivePage(active_page_id, known_pages, bpm);

      break;

      // ========================================================
      // 3. Insert
      // ========================================================

    case 3:

      insertTuple(bpm, active_page_id, schema);

      break;

      // ========================================================
      // 4. Get tuple
      // ========================================================

    case 4:

      getTuple(bpm, active_page_id, schema);

      break;

      // ========================================================
      // 5. Update tuple
      // ========================================================

    case 5:

      updateTuple(bpm, active_page_id, schema);

      break;

      // ========================================================
      // 6. Delete tuple
      // ========================================================

    case 6:

      deleteTuple(bpm, active_page_id);

      break;

      // ========================================================
      // 7. Compact
      // ========================================================

    case 7:

      compactActivePage(bpm, active_page_id);

      break;

      // ========================================================
      // 8. Visualize active page
      // ========================================================

    case 8:

      visualizeActivePage(bpm, active_page_id, schema);

      break;

      // ========================================================
      // 9. Visualize all pages
      // ========================================================

    case 9:

      visualizeAllPages(bpm, known_pages, active_page_id, schema);

      break;

      // ========================================================
      // 10. Buffer Pool
      // ========================================================

    case 10:

      visualizeBufferPool(bpm, active_page_id);

      break;

      // ========================================================
      // 11. LRU
      // ========================================================

    case 11:

      visualizeLru(bpm);

      break;

      // ========================================================
      // 12. Flush active page
      // ========================================================

    case 12:

      flushActivePage(bpm, active_page_id);

      break;

      // ========================================================
      // 13. Flush all
      // ========================================================

    case 13:

      flushAllPages(bpm);

      break;

      // ========================================================
      // Exit
      // ========================================================

    case 0:

      printTitle("SHUTTING DOWN");

      std::cout << "\nFlushing all pages...\n";

      flushAllPages(bpm);

      std::cout << "\nGoodbye.\n";

      running = false;

      break;

      // ========================================================
      // Invalid
      // ========================================================

    default:

      std::cout << "\nInvalid option.\n";

      break;
    }
  }

  return 0;
}
