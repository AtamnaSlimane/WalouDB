#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/DiskManager.h"
#include "waloudb/storage/Page.h"
#include "waloudb/storage/Schema.h"
#include "waloudb/storage/SlottedPage.h"
#include "waloudb/storage/TableHeap.h"
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
  for (int i = 0; i < VISUAL_WIDTH; ++i)
    std::cout << c;
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
               "----------+\n";
}
void printRow(const std::string &text) {
  std::cout << "| " << std::left << std::setw(72) << text << " |\n";
}

// ============================================================
// Page helpers
// ============================================================
bool isValidPageId(page_id_t page_id) { return page_id != INVALID_PAGE_ID; }

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
    } else if (column.type == TypeId::VARCHAR) {
      printRow(column.name + " = \"" + value.getString() + "\"");
    }
  }
}

// ============================================================
// Visualize single page (unchanged)
// ============================================================
void visualizePage(const SlottedPage &page, const Schema &schema) {
  const uint16_t lower = page.getLower();
  const uint16_t upper = page.getUpper();
  const uint16_t slot_count = page.getSlotCount();
  const uint16_t free_space = page.freeSpace();

  uint32_t live_tuple_bytes = 0;
  uint16_t active_slots = 0;
  uint16_t deleted_slots = 0;
  for (uint16_t i = 0; i < slot_count; ++i) {
    auto slot_opt = page.getSlotInfo(i);
    if (!slot_opt.has_value())
      continue;
    const Slot &slot = *slot_opt;
    if (slot.deleted || slot.length == 0)
      ++deleted_slots;
    else {
      ++active_slots;
      live_tuple_bytes += slot.length;
    }
  }
  const uint32_t header_bytes = sizeof(PageHeader);
  const uint32_t slot_directory_bytes = slot_count * sizeof(Slot);

  printTitle("WALOUDB PAGE VISUALIZER - PAGE " +
             std::to_string(page.getPageId()));
  printBorder();
  printRow("PAGE HEADER");
  printRow("page_id    = " + std::to_string(page.getPageId()));
  printRow("next_page_id = " + (page.getNextPageId() == INVALID_PAGE_ID
                                    ? std::string("(end of chain)")
                                    : std::to_string(page.getNextPageId())));
  printRow("lower      = " + std::to_string(lower));
  printRow("upper      = " + std::to_string(upper));
  printRow("slot_count = " + std::to_string(slot_count));
  printBorder();

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

  std::cout << "\n";
  printTitle("SLOT DIRECTORY");
  for (uint16_t i = 0; i < slot_count; ++i) {
    auto slot_opt = page.getSlotInfo(i);
    if (!slot_opt.has_value())
      continue;
    const Slot slot = *slot_opt;
    printBorder();
    printRow("SLOT " + std::to_string(i));
    printRow("offset = " + std::to_string(slot.offset));
    printRow("length = " + std::to_string(slot.length) + " bytes");
    printRow(std::string("status = ") + ((slot.deleted || slot.length == 0)
                                             ? "TOMBSTONED / DELETED"
                                             : "ACTIVE"));
    printBorder();
  }
  if (slot_count == 0)
    std::cout << "\nNo slots exist yet.\n";

  std::cout << "\n";
  printBorder();
  printRow("FREE SPACE");
  printRow("from offset = " + std::to_string(lower));
  printRow("to offset   = " + std::to_string(upper));
  printRow("total       = " + std::to_string(free_space) + " bytes");
  printBorder();

  std::cout << "\n";
  printTitle("TUPLE DATA");
  struct TupleInfo {
    uint16_t slot_num;
    Slot slot;
  };
  std::vector<TupleInfo> tuples;
  for (uint16_t i = 0; i < slot_count; ++i) {
    auto slot_opt = page.getSlotInfo(i);
    if (!slot_opt.has_value())
      continue;
    tuples.push_back({i, *slot_opt});
  }
  std::sort(tuples.begin(), tuples.end(),
            [](const TupleInfo &a, const TupleInfo &b) {
              return a.slot.offset < b.slot.offset;
            });

  for (const TupleInfo &info : tuples) {
    printBorder();
    if (info.slot.deleted || info.slot.length == 0) {
      printRow("TUPLE SLOT " + std::to_string(info.slot_num) + " - DELETED");
      printRow("RID = (" + std::to_string(page.getPageId()) + ", " +
               std::to_string(info.slot_num) + ")");
      printRow("status = TOMBSTONED");
      printBorder();
      continue;
    }
    auto tuple_opt = page.getTuple(info.slot_num);
    if (!tuple_opt.has_value()) {
      printRow("ERROR: Slot marked active but tuple unavailable.");
      printBorder();
      continue;
    }
    printRow("TUPLE " + std::to_string(info.slot_num));
    printRow("RID = (" + std::to_string(page.getPageId()) + ", " +
             std::to_string(info.slot_num) + ")");
    printRow("length = " + std::to_string(info.slot.length) + " bytes");
    printRow("status = ACTIVE");
    printTupleValues(*tuple_opt, schema);
    printBorder();
  }
  if (active_slots == 0 && deleted_slots == 0)
    std::cout << "\nNo tuples stored in this page.\n";
}

// ============================================================
// Raw page menu functions (unchanged behavior)
// ============================================================
bool createNewPage(BufferPoolManager &bpm, page_id_t &active_page_id,
                   std::vector<page_id_t> &known_pages) {
  printTitle("CREATE NEW PAGE");
  page_id_t new_page_id;
  Page *raw = bpm.newPage(&new_page_id);
  if (raw == nullptr) {
    std::cout << "\n[FAILED] No available frame.\n";
    return false;
  }
  SlottedPage page(raw->getData());
  page.Init(new_page_id);
  bpm.unpinPage(new_page_id, true);
  active_page_id = new_page_id;
  known_pages.push_back(new_page_id);
  std::cout << "\n[SUCCESS] New page created. Page ID: " << new_page_id << "\n";
  return true;
}

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
    std::cout << "  Page " << id << (id == active_page_id ? "  <-- ACTIVE" : "")
              << "\n";
  }
  int input = readInt("\nEnter page ID: ");
  if (input < 0) {
    std::cout << "\nInvalid page ID.\n";
    return;
  }
  page_id_t target = static_cast<page_id_t>(input);
  if (std::find(known_pages.begin(), known_pages.end(), target) ==
      known_pages.end()) {
    std::cout << "\n[FAILED] Page is not in the known page list.\n";
    return;
  }
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

bool insertTuple(BufferPoolManager &bpm, page_id_t active_page_id,
                 const Schema &schema) {
  printTitle("INSERT TUPLE (raw page)");
  Page *raw = fetchActivePage(bpm, active_page_id);
  if (raw == nullptr)
    return false;
  SlottedPage page(raw->getData());
  int id = readInt("Enter id: ");
  std::string name = readString("Enter name: ");
  Tuple tuple =
      Tuple::Serialize({Value(static_cast<int32_t>(id)), Value(name)}, schema);
  RID rid{};
  std::cout << "\nTuple size: " << tuple.getLength() << " bytes\n";
  std::cout << "Free space before: " << page.freeSpace() << " bytes\n";
  bool success = page.insertTuple(tuple, &rid);
  bpm.unpinPage(active_page_id, success);
  if (!success) {
    std::cout << "\n[FAILED] Not enough space.\n";
    return false;
  }
  std::cout << "\n[SUCCESS] RID = (" << rid.page_id << ", " << rid.slot_num
            << ")\n";
  return true;
}

void getTuple(BufferPoolManager &bpm, page_id_t active_page_id,
              const Schema &schema) {
  printTitle("GET TUPLE (raw page)");
  Page *raw = fetchActivePage(bpm, active_page_id);
  if (raw == nullptr)
    return;
  SlottedPage page(raw->getData());
  int input = readInt("Enter slot number: ");
  if (input < 0) {
    std::cout << "\nInvalid slot number.\n";
    bpm.unpinPage(active_page_id, false);
    return;
  }
  auto tuple_opt = page.getTuple(static_cast<uint16_t>(input));
  if (!tuple_opt.has_value()) {
    std::cout << "\nTuple not found or deleted.\n";
    bpm.unpinPage(active_page_id, false);
    return;
  }
  printBorder();
  printRow("TUPLE " + std::to_string(input));
  printTupleValues(*tuple_opt, schema);
  printBorder();
  bpm.unpinPage(active_page_id, false);
}

bool updateTuple(BufferPoolManager &bpm, page_id_t active_page_id,
                 const Schema &schema) {
  printTitle("UPDATE TUPLE (raw page)");
  Page *raw = fetchActivePage(bpm, active_page_id);
  if (raw == nullptr)
    return false;
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
  std::cout << "\nCurrent tuple:\n";
  printBorder();
  printTupleValues(*old_tuple, schema);
  printBorder();
  int id = readInt("New id: ");
  std::string name = readString("New name: ");
  Tuple new_tuple =
      Tuple::Serialize({Value(static_cast<int32_t>(id)), Value(name)}, schema);
  bool success = page.updateTuple(slot_num, new_tuple);
  bpm.unpinPage(active_page_id, success);
  std::cout << (success ? "\n[SUCCESS] Updated.\n"
                        : "\n[FAILED] Not enough space.\n");
  return success;
}

bool deleteTuple(BufferPoolManager &bpm, page_id_t active_page_id) {
  printTitle("DELETE TUPLE (raw page)");
  Page *raw = fetchActivePage(bpm, active_page_id);
  if (raw == nullptr)
    return false;
  SlottedPage page(raw->getData());
  int input = readInt("Enter slot number: ");
  bool success = input >= 0 && page.deleteTuple(static_cast<uint16_t>(input));
  bpm.unpinPage(active_page_id, success);
  std::cout << (success ? "\n[SUCCESS] Tombstoned.\n"
                        : "\n[FAILED] Not found or already deleted.\n");
  return success;
}

bool compactActivePage(BufferPoolManager &bpm, page_id_t active_page_id) {
  printTitle("COMPACT ACTIVE PAGE");
  Page *raw = fetchActivePage(bpm, active_page_id);
  if (raw == nullptr)
    return false;
  SlottedPage page(raw->getData());
  uint16_t before = page.freeSpace();
  page.compact();
  uint16_t after = page.freeSpace();
  bpm.unpinPage(active_page_id, true);
  std::cout << "\n[SUCCESS] Free space: " << before << " -> " << after
            << " bytes (recovered " << (after - before) << ")\n";
  return true;
}

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

void visualizeAllPages(BufferPoolManager &bpm,
                       const std::vector<page_id_t> &known_pages,
                       page_id_t active_page_id, const Schema &schema) {
  printTitle("ALL PAGES");
  if (known_pages.empty()) {
    std::cout << "\nNo pages created yet.\n";
    return;
  }
  for (page_id_t page_id : known_pages) {
    std::cout << "\n";
    printLine('-');
    std::cout << "PAGE " << page_id
              << (page_id == active_page_id ? "  <-- ACTIVE" : "") << "\n";
    printLine('-');
    Page *raw = bpm.fetchPage(page_id);
    if (raw == nullptr) {
      std::cout << "[FAILED] Could not fetch page.\n";
      continue;
    }
    SlottedPage page(raw->getData());
    std::cout << "Slots: " << page.getSlotCount()
              << "   Free space: " << page.freeSpace() << " bytes\n";
    bpm.unpinPage(page_id, false);
  }
}

// ============================================================
// Buffer pool / LRU / flush (unchanged)
// ============================================================
void visualizeBufferPool(const BufferPoolManager &bpm,
                         page_id_t active_page_id) {
  printTitle("BUFFER POOL VISUALIZER");
  const size_t pool_size = bpm.getPoolSize();
  std::cout
      << "\n+--------+------------+------------+----------+------------+\n";
  std::cout << "| FRAME  | PAGE ID    | PIN COUNT  | DIRTY    | STATUS     |\n";
  std::cout << "+--------+------------+------------+----------+------------+\n";
  for (frame_id_t frame_id = 0; frame_id < static_cast<frame_id_t>(pool_size);
       ++frame_id) {
    page_id_t page_id = bpm.getFramePageId(frame_id);
    int pin_count = bpm.getFramePinCount(frame_id);
    bool dirty = bpm.getFrameDirty(frame_id);
    std::string page_text =
        (page_id == INVALID_PAGE_ID) ? "EMPTY" : std::to_string(page_id);
    std::string status;
    if (page_id == INVALID_PAGE_ID)
      status = "FREE";
    else if (page_id == active_page_id)
      status = "ACTIVE PAGE";
    else if (pin_count > 0)
      status = "PINNED";
    else
      status = "EVICTABLE";
    std::cout << "| " << std::setw(6) << std::left << frame_id << "| "
              << std::setw(11) << page_text << "| " << std::setw(11)
              << pin_count << "| " << std::setw(9) << (dirty ? "YES" : "NO")
              << "| " << std::setw(11) << status << "|\n";
  }
  std::cout << "+--------+------------+------------+----------+------------+\n";
}

void visualizeLru(const BufferPoolManager &bpm) {
  printTitle("LRU REPLACER VISUALIZER");
  const Lrur &replacer = bpm.getReplacer();
  std::vector<frame_id_t> frames = replacer.getFrames();
  if (frames.empty()) {
    std::cout << "\nLRU replacer is empty.\n";
    return;
  }
  std::cout << "\nOLDEST";
  for (frame_id_t frame : frames)
    std::cout << "  ->  [Frame " << frame << "]";
  std::cout << "  ->  NEWEST\n";
}

void flushActivePage(BufferPoolManager &bpm, page_id_t active_page_id) {
  printTitle("FLUSH ACTIVE PAGE");
  if (!isValidPageId(active_page_id)) {
    std::cout << "\nNo active page.\n";
    return;
  }
  std::cout << (bpm.flushPage(active_page_id) ? "\n[SUCCESS] Flushed.\n"
                                              : "\n[FAILED]\n");
}

void flushAllPages(BufferPoolManager &bpm) {
  printTitle("FLUSH ALL BUFFER POOL PAGES");
  size_t flushed = 0;
  for (frame_id_t frame_id = 0;
       frame_id < static_cast<frame_id_t>(bpm.getPoolSize()); ++frame_id) {
    page_id_t page_id = bpm.getFramePageId(frame_id);
    if (page_id == INVALID_PAGE_ID)
      continue;
    if (bpm.flushPage(page_id)) {
      ++flushed;
      std::cout << "[FLUSHED] Page " << page_id << "\n";
    }
  }
  std::cout << "\nTotal flushed: " << flushed << "\n";
}

// ============================================================
// NEW: TableHeap operations
// ============================================================
bool insertIntoTable(TableHeap &table, const Schema &schema) {
  printTitle("TABLE INSERT");
  int id = readInt("Enter id: ");
  std::string name = readString("Enter name: ");
  Tuple tuple =
      Tuple::Serialize({Value(static_cast<int32_t>(id)), Value(name)}, schema);
  RID rid{};
  bool success = table.insertTuple(tuple, &rid);
  if (!success) {
    std::cout
        << "\n[FAILED] Insert failed (pool exhausted or allocation failed).\n";
    return false;
  }
  std::cout << "\n[SUCCESS] RID = (" << rid.page_id << ", " << rid.slot_num
            << ")\n";
  std::cout << "(TableHeap picked this page automatically.)\n";
  return true;
}

void getFromTable(TableHeap &table, const Schema &schema) {
  printTitle("TABLE GET BY RID");
  int pid = readInt("Enter page_id: ");
  int slot = readInt("Enter slot_num: ");
  RID rid{static_cast<page_id_t>(pid), static_cast<uint16_t>(slot)};
  Tuple tuple;
  if (!table.getTuple(rid, &tuple)) {
    std::cout << "\n[FAILED] Not found or deleted.\n";
    return;
  }
  printBorder();
  printRow("RID = (" + std::to_string(rid.page_id) + ", " +
           std::to_string(rid.slot_num) + ")");
  printTupleValues(tuple, schema);
  printBorder();
}

bool updateInTable(TableHeap &table, const Schema &schema) {
  printTitle("TABLE UPDATE BY RID");
  int pid = readInt("Enter page_id: ");
  int slot = readInt("Enter slot_num: ");
  RID rid{static_cast<page_id_t>(pid), static_cast<uint16_t>(slot)};
  int id = readInt("New id: ");
  std::string name = readString("New name: ");
  Tuple tuple =
      Tuple::Serialize({Value(static_cast<int32_t>(id)), Value(name)}, schema);
  bool success = table.updateTuple(rid, tuple);
  std::cout << (success ? "\n[SUCCESS] Updated.\n"
                        : "\n[FAILED] Not enough space or not found.\n");
  return success;
}

bool deleteFromTable(TableHeap &table) {
  printTitle("TABLE DELETE BY RID");
  int pid = readInt("Enter page_id: ");
  int slot = readInt("Enter slot_num: ");
  RID rid{static_cast<page_id_t>(pid), static_cast<uint16_t>(slot)};
  bool success = table.deleteTuple(rid);
  std::cout << (success ? "\n[SUCCESS] Tombstoned.\n"
                        : "\n[FAILED] Not found or already deleted.\n");
  return success;
}

void insertDummyRows(TableHeap &table, const Schema &schema) {
  printTitle("INSERT DUMMY ROWS INTO TABLE");
  int count = readInt("How many dummy rows? ");
  if (count <= 0) {
    std::cout << "\nInvalid count.\n";
    return;
  }

  int inserted = 0;
  std::vector<page_id_t> pages_touched;
  for (int i = 0; i < count; ++i) {
    int32_t id = static_cast<int32_t>(i + 1);
    std::string name = "Dummy_" + std::to_string(id);
    Tuple tuple = Tuple::Serialize({Value(id), Value(name)}, schema);
    RID rid{};
    if (!table.insertTuple(tuple, &rid)) {
      std::cout << "[STOPPED] Insert failed after " << inserted << " rows.\n";
      break;
    }
    ++inserted;
    if (std::find(pages_touched.begin(), pages_touched.end(), rid.page_id) ==
        pages_touched.end()) {
      pages_touched.push_back(rid.page_id);
    }
  }

  std::cout << "\n";
  printBorder();
  printRow("DUMMY INSERT SUMMARY");
  printRow("Requested = " + std::to_string(count));
  printRow("Inserted  = " + std::to_string(inserted));
  std::string pages_str;
  for (page_id_t p : pages_touched)
    pages_str += std::to_string(p) + " ";
  printRow("Pages spanned = " + pages_str + "(" +
           std::to_string(pages_touched.size()) + " total)");
  printBorder();
}

// ============================================================
// NEW: Table visualizer — walks first_page_id -> next_page_id chain
// ============================================================
void visualizeTable(BufferPoolManager &bpm, TableHeap &table,
                    const Schema &schema) {
  printTitle("TABLE VISUALIZER (PAGE CHAIN)");

  page_id_t current = table.getFirstPageId();
  int page_index = 0;
  uint32_t total_active = 0, total_deleted = 0;

  std::cout << "\nFirst page: " << current << "\n";

  while (current != INVALID_PAGE_ID) {
    Page *raw = bpm.fetchPage(current);
    if (raw == nullptr) {
      std::cout << "\n[FAILED] Could not fetch page " << current
                << " — chain broken.\n";
      break;
    }
    SlottedPage page(raw->getData());

    uint16_t active = 0, deleted = 0;
    for (uint16_t i = 0; i < page.getSlotCount(); ++i) {
      auto slot = page.getSlotInfo(i);
      if (!slot.has_value())
        continue;
      if (slot->deleted || slot->length == 0)
        ++deleted;
      else
        ++active;
    }
    total_active += active;
    total_deleted += deleted;

    std::cout << "\n";
    printLine('-');
    std::cout << "PAGE #" << page_index << "  (page_id = " << current << ")\n";
    printLine('-');
    std::cout << "  slot_count   = " << page.getSlotCount() << "\n";
    std::cout << "  active rows  = " << active << "\n";
    std::cout << "  tombstones   = " << deleted << "\n";
    std::cout << "  free space   = " << page.freeSpace() << " bytes\n";
    std::cout << "  next_page_id = "
              << (page.getNextPageId() == INVALID_PAGE_ID
                      ? "(end of chain)"
                      : std::to_string(page.getNextPageId()))
              << "\n";

    for (uint16_t i = 0; i < page.getSlotCount(); ++i) {
      auto slot = page.getSlotInfo(i);
      if (!slot.has_value() || slot->deleted || slot->length == 0)
        continue;
      auto tuple_opt = page.getTuple(i);
      if (!tuple_opt.has_value())
        continue;
      std::cout << "    RID(" << current << "," << i << "): ";
      for (size_t c = 0; c < schema.getColumnCount(); ++c) {
        Value v = tuple_opt->getValue(schema, c);
        if (schema.getColumn(c).type == TypeId::INTEGER)
          std::cout << schema.getColumn(c).name << "=" << v.getInteger() << " ";
        else
          std::cout << schema.getColumn(c).name << "=\"" << v.getString()
                    << "\" ";
      }
      std::cout << "\n";
    }

    page_id_t next = page.getNextPageId();
    bpm.unpinPage(current, false);
    current = next;
    ++page_index;
  }

  std::cout << "\n";
  printBorder();
  printRow("TABLE SUMMARY");
  printRow("Total pages = " + std::to_string(page_index));
  printRow("Active rows = " + std::to_string(total_active));
  printRow("Tombstoned  = " + std::to_string(total_deleted));
  printBorder();
}

// ============================================================
// Menu
// ============================================================
void printMenu(page_id_t active_page_id, page_id_t table_first_page) {
  printTitle("WALOUDB STORAGE ENGINE");
  std::cout << "\nACTIVE (raw) PAGE: "
            << (isValidPageId(active_page_id) ? std::to_string(active_page_id)
                                              : "NONE")
            << "   |   TABLE first_page_id: " << table_first_page << "\n";

  std::cout << "\n  RAW PAGE MANAGEMENT\n  "
               "------------------------------------------------\n";
  std::cout << "  1. Create new raw page\n";
  std::cout << "  2. Switch active raw page\n";
  std::cout << "  3. Insert tuple into active page (raw)\n";
  std::cout << "  4. Get tuple from active page (raw)\n";
  std::cout << "  5. Update tuple in active page (raw)\n";
  std::cout << "  6. Delete tuple from active page (raw)\n";
  std::cout << "  7. Compact active page\n";
  std::cout << "  8. Visualize active page\n";
  std::cout << "  9. Visualize all known raw pages\n";

  std::cout << "\n  TABLE (TableHeap — multi-page, auto-chaining)\n  "
               "------------------------------------------------\n";
  std::cout << " 14. Insert row into table\n";
  std::cout << " 15. Get row from table (by RID)\n";
  std::cout << " 16. Update row in table (by RID)\n";
  std::cout << " 17. Delete row from table (by RID)\n";
  std::cout << " 18. Visualize table (walk full page chain)\n";
  std::cout << " 19. Insert N dummy rows into table\n";

  std::cout << "\n  BUFFER POOL / CACHE\n  "
               "------------------------------------------------\n";
  std::cout << " 10. Visualize Buffer Pool\n";
  std::cout << " 11. Visualize LRU Replacer\n";

  std::cout << "\n  DISK\n  ------------------------------------------------\n";
  std::cout << " 12. Flush active raw page\n";
  std::cout << " 13. Flush all pages\n";

  std::cout << "\n   0. Exit\n\n";
}

// ============================================================
// Main
// ============================================================
int main() {
  std::cout
      << "\n############################################################\n";
  std::cout << "#              WALOUDB STORAGE PLAYGROUND                  #\n";
  std::cout << "############################################################\n";

  DiskManager disk_manager(DATABASE_FILE);
  BufferPoolManager bpm(BUFFER_POOL_SIZE, &disk_manager);
  Schema schema = createSchema();

  page_id_t active_page_id = INVALID_PAGE_ID;
  std::vector<page_id_t> known_pages;

  // NOTE: TableHeap's current constructor always allocates a fresh
  // first page — it can't yet reopen an existing table across runs
  // (that needs a catalog, not built yet). Fine for this playground.
  TableHeap table(&bpm);
  std::cout << "\nCreated table. first_page_id = " << table.getFirstPageId()
            << "\n";

  bool running = true;
  while (running) {
    printMenu(active_page_id, table.getFirstPageId());
    int choice = readInt("Choose an option: ");

    switch (choice) {
    case 1:
      createNewPage(bpm, active_page_id, known_pages);
      break;
    case 2:
      switchActivePage(active_page_id, known_pages, bpm);
      break;
    case 3:
      insertTuple(bpm, active_page_id, schema);
      break;
    case 4:
      getTuple(bpm, active_page_id, schema);
      break;
    case 5:
      updateTuple(bpm, active_page_id, schema);
      break;
    case 6:
      deleteTuple(bpm, active_page_id);
      break;
    case 7:
      compactActivePage(bpm, active_page_id);
      break;
    case 8:
      visualizeActivePage(bpm, active_page_id, schema);
      break;
    case 9:
      visualizeAllPages(bpm, known_pages, active_page_id, schema);
      break;

    case 10:
      visualizeBufferPool(bpm, active_page_id);
      break;
    case 11:
      visualizeLru(bpm);
      break;
    case 12:
      flushActivePage(bpm, active_page_id);
      break;
    case 13:
      flushAllPages(bpm);
      break;

    case 14:
      insertIntoTable(table, schema);
      break;
    case 15:
      getFromTable(table, schema);
      break;
    case 16:
      updateInTable(table, schema);
      break;
    case 17:
      deleteFromTable(table);
      break;
    case 18:
      visualizeTable(bpm, table, schema);
      break;
    case 19:
      insertDummyRows(table, schema);
      break;

    case 0:
      printTitle("SHUTTING DOWN");
      flushAllPages(bpm);
      std::cout << "\nGoodbye.\n";
      running = false;
      break;

    default:
      std::cout << "\nInvalid option.\n";
      break;
    }
  }
  return 0;
}
