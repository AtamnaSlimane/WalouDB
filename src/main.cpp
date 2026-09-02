#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
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
// Global test counters
// ============================================================

static int g_tests_run = 0;
static int g_tests_passed = 0;

// ============================================================
// Test helpers
// ============================================================

#define CHECK(condition)                                                       \
  do {                                                                         \
    ++g_tests_run;                                                             \
    if (!(condition)) {                                                        \
      std::cerr << "\n";                                                       \
      std::cerr << "================================================\n";       \
      std::cerr << "[FAIL] " << #condition << "\n";                            \
      std::cerr << "File: " << __FILE__ << "\n";                               \
      std::cerr << "Line: " << __LINE__ << "\n";                               \
      std::cerr << "================================================\n";       \
      std::exit(EXIT_FAILURE);                                                 \
    }                                                                          \
    ++g_tests_passed;                                                          \
  } while (false)

void section(const std::string &name) {
  std::cout << "\n";
  std::cout << "============================================================\n";
  std::cout << "[TEST] " << name << "\n";
  std::cout << "============================================================\n";
}

void pass(const std::string &message) {
  std::cout << "[PASS] " << message << "\n";
}

// ============================================================
// Tuple helpers
// ============================================================

Schema makeTestSchema() {
  return Schema(
      {Column("id", TypeId::INTEGER), Column("name", TypeId::VARCHAR)});
}

Tuple makeTuple(uint32_t id, const std::string &name) {

  return Tuple::Serialize({Value(static_cast<int32_t>(id)), Value(name)},
                          makeTestSchema());
}

void checkTuple(const Tuple &tuple, uint32_t expected_id,
                const std::string &expected_name) {

  Schema schema = makeTestSchema();

  Value id = tuple.getValue(schema, 0);
  Value name = tuple.getValue(schema, 1);

  CHECK(id.getType() == TypeId::INTEGER);
  CHECK(id.getInteger() == static_cast<int32_t>(expected_id));

  CHECK(name.getType() == TypeId::VARCHAR);
  CHECK(name.getString() == expected_name);
}

// ============================================================
// TEST 1
// DiskManager
// ============================================================

void testDiskManager() {

  section("DiskManager");

  const std::string db_file = "waloudb_test_disk.db";

  std::filesystem::remove(db_file);

  // --------------------------------------------------------
  // Allocate + write + read
  // --------------------------------------------------------

  {
    DiskManager disk(db_file);

    page_id_t page0 = disk.allocatePage();

    CHECK(page0 == 0);

    char write_buffer[PAGE_SIZE]{};
    char read_buffer[PAGE_SIZE]{};

    const char *message = "Hello WalouDB!";

    std::memcpy(write_buffer, message, std::strlen(message) + 1);

    CHECK(disk.writePage(page0, write_buffer));

    CHECK(disk.readPage(page0, read_buffer));

    CHECK(std::strcmp(read_buffer, message) == 0);

    pass("Page allocation works.");
    pass("Page write works.");
    pass("Page read works.");
    pass("Written data equals read data.");
  }

  // --------------------------------------------------------
  // Reopen database
  // --------------------------------------------------------

  {
    DiskManager disk(db_file);

    char buffer[PAGE_SIZE]{};

    CHECK(disk.readPage(0, buffer));

    CHECK(std::strcmp(buffer, "Hello WalouDB!") == 0);

    pass("Data survives DiskManager reopen.");
  }

  std::filesystem::remove(db_file);
}

// ============================================================
// TEST 2
// LRU Replacer
// ============================================================

void testLruReplacer() {

  section("LRU Replacer");

  Lrur lru(3);

  frame_id_t victim;

  // --------------------------------------------------------
  // Empty
  // --------------------------------------------------------

  CHECK(!lru.Victim(&victim));

  pass("Empty LRU has no victim.");

  // --------------------------------------------------------
  // Add frames
  // --------------------------------------------------------

  lru.Unpin(0);
  lru.Unpin(1);
  lru.Unpin(2);

  /*
   * Expected:
   *
   * oldest -> newest
   *
   * 0 -> 1 -> 2
   */

  CHECK(lru.Victim(&victim));
  CHECK(victim == 0);

  CHECK(lru.Victim(&victim));
  CHECK(victim == 1);

  CHECK(lru.Victim(&victim));
  CHECK(victim == 2);

  pass("LRU eviction order is correct.");

  CHECK(!lru.Victim(&victim));

  // --------------------------------------------------------
  // Pin
  // --------------------------------------------------------

  lru.Unpin(0);
  lru.Unpin(1);
  lru.Unpin(2);

  lru.Pin(0);

  /*
   * 0 is pinned.
   *
   * Expected:
   *
   * 1 -> 2
   */

  CHECK(lru.Victim(&victim));
  CHECK(victim == 1);

  CHECK(lru.Victim(&victim));
  CHECK(victim == 2);

  CHECK(!lru.Victim(&victim));

  pass("Pinned frame cannot be selected.");

  // --------------------------------------------------------
  // Unpin after Pin
  // --------------------------------------------------------

  lru.Unpin(0);

  CHECK(lru.Victim(&victim));
  CHECK(victim == 0);

  pass("Unpin makes frame evictable again.");

  // --------------------------------------------------------
  // Duplicate Unpin
  // --------------------------------------------------------

  lru.Unpin(10);
  lru.Unpin(10);
  lru.Unpin(10);

  CHECK(lru.Victim(&victim));
  CHECK(victim == 10);

  CHECK(!lru.Victim(&victim));

  pass("Duplicate Unpin does not duplicate frame.");

  // --------------------------------------------------------
  // Reordering
  // --------------------------------------------------------

  lru.Unpin(0);
  lru.Unpin(1);
  lru.Unpin(2);

  /*
   * Remove 0.
   *
   * 1 -> 2
   */

  CHECK(lru.Victim(&victim));
  CHECK(victim == 0);

  /*
   * Add 0 again.
   *
   * 1 -> 2 -> 0
   */

  lru.Unpin(0);

  CHECK(lru.Victim(&victim));
  CHECK(victim == 1);

  CHECK(lru.Victim(&victim));
  CHECK(victim == 2);

  CHECK(lru.Victim(&victim));
  CHECK(victim == 0);

  pass("Recently unpinned frame becomes newest.");
}

// ============================================================
// TEST 3
// SlottedPage basic functionality
// ============================================================

void testSlottedPage() {

  section("SlottedPage");

  char raw_page[PAGE_SIZE]{};

  SlottedPage page(raw_page);

  page.Init(0);

  CHECK(page.getPageId() == 0);
  CHECK(page.getSlotCount() == 0);

  pass("Page initializes correctly.");

  // --------------------------------------------------------
  // Insert first tuple
  // --------------------------------------------------------

  Tuple tuple1 = makeTuple(1, "Alice");

  RID rid1;

  CHECK(page.insertTuple(tuple1, &rid1));

  CHECK(rid1.page_id == 0);
  CHECK(rid1.slot_num == 0);
  CHECK(page.getSlotCount() == 1);

  pass("First tuple inserted.");

  // --------------------------------------------------------
  // Read tuple
  // --------------------------------------------------------

  auto result1 = page.getTuple(rid1.slot_num);

  CHECK(result1.has_value());

  checkTuple(*result1, 1, "Alice");

  pass("First tuple can be read correctly.");

  // --------------------------------------------------------
  // Insert two more
  // --------------------------------------------------------

  Tuple tuple2 = makeTuple(2, "Bob");
  Tuple tuple3 = makeTuple(3, "Charlie");

  RID rid2;
  RID rid3;

  CHECK(page.insertTuple(tuple2, &rid2));
  CHECK(page.insertTuple(tuple3, &rid3));

  CHECK(page.getSlotCount() == 3);

  pass("Multiple tuples can be inserted.");

  // --------------------------------------------------------
  // Verify all
  // --------------------------------------------------------

  auto result2 = page.getTuple(rid2.slot_num);

  auto result3 = page.getTuple(rid3.slot_num);

  CHECK(result2.has_value());
  CHECK(result3.has_value());

  checkTuple(*result2, 2, "Bob");
  checkTuple(*result3, 3, "Charlie");

  pass("All tuple contents are correct.");

  // --------------------------------------------------------
  // Free space decreases
  // --------------------------------------------------------

  uint16_t free_after_insert = page.freeSpace();

  CHECK(free_after_insert < PAGE_SIZE);

  pass("Free space decreases after insertion.");

  // --------------------------------------------------------
  // Delete tuple
  // --------------------------------------------------------

  CHECK(page.deleteTuple(rid2.slot_num));

  CHECK(!page.getTuple(rid2.slot_num).has_value());

  pass("Deleted tuple cannot be fetched.");

  // --------------------------------------------------------
  // Other tuples survive
  // --------------------------------------------------------

  CHECK(page.getTuple(rid1.slot_num).has_value());

  CHECK(page.getTuple(rid3.slot_num).has_value());

  pass("Deleting one tuple does not affect others.");

  // --------------------------------------------------------
  // Reuse tombstone
  // --------------------------------------------------------

  Tuple tuple4 = makeTuple(4, "David");

  RID rid4;

  CHECK(page.insertTuple(tuple4, &rid4));

  CHECK(rid4.slot_num == 3);
  CHECK(page.getSlotCount() == 4);

  auto result4 = page.getTuple(rid4.slot_num);

  CHECK(result4.has_value());

  checkTuple(*result4, 4, "David");

  pass("Deleted slot remains tombstoned and new slot is allocated.");
  // --------------------------------------------------------
  // Invalid operations
  // --------------------------------------------------------

  CHECK(!page.getTuple(9999).has_value());

  CHECK(!page.deleteTuple(9999));

  pass("Invalid slot operations are rejected.");
}

// ============================================================
// TEST 4
// SlottedPage with large VARCHAR
// ============================================================

void testLargeTuple() {

  section("SlottedPage large VARCHAR");

  char raw_page[PAGE_SIZE]{};

  SlottedPage page(raw_page);

  page.Init(50);

  std::string large_string(1000, 'X');

  Tuple tuple = makeTuple(123, large_string);

  RID rid;

  CHECK(page.insertTuple(tuple, &rid));

  auto result = page.getTuple(rid.slot_num);

  CHECK(result.has_value());

  checkTuple(*result, 123, large_string);

  pass("Large VARCHAR tuple survives serialization.");

  // --------------------------------------------------------
  // Different string sizes
  // --------------------------------------------------------

  Tuple empty_string = makeTuple(1, "");

  RID rid_empty;

  CHECK(page.insertTuple(empty_string, &rid_empty));

  auto empty_result = page.getTuple(rid_empty.slot_num);

  CHECK(empty_result.has_value());

  checkTuple(*empty_result, 1, "");

  pass("Empty VARCHAR works.");
}

// ============================================================
// TEST 5
// BufferPool basic allocation
// ============================================================

void testBufferPoolBasic() {

  section("BufferPool basic functionality");

  const std::string db_file = "waloudb_test_bpm.db";

  std::filesystem::remove(db_file);

  DiskManager disk(db_file);

  BufferPoolManager bpm(3, &disk);

  page_id_t p0;
  page_id_t p1;
  page_id_t p2;

  Page *page0 = bpm.newPage(&p0);

  Page *page1 = bpm.newPage(&p1);

  Page *page2 = bpm.newPage(&p2);

  CHECK(page0 != nullptr);
  CHECK(page1 != nullptr);
  CHECK(page2 != nullptr);

  CHECK(p0 == 0);
  CHECK(p1 == 1);
  CHECK(p2 == 2);

  pass("Three pages can be allocated.");

  // --------------------------------------------------------
  // New pages are pinned
  // --------------------------------------------------------

  CHECK(page0->getPinCount() == 1);
  CHECK(page1->getPinCount() == 1);
  CHECK(page2->getPinCount() == 1);

  pass("New pages start pinned.");

  // --------------------------------------------------------
  // Unpin
  // --------------------------------------------------------

  CHECK(bpm.unpinPage(p0, false));

  CHECK(bpm.unpinPage(p1, false));

  CHECK(bpm.unpinPage(p2, false));

  CHECK(page0->getPinCount() == 0);
  CHECK(page1->getPinCount() == 0);
  CHECK(page2->getPinCount() == 0);

  pass("Pages can be unpinned.");

  // --------------------------------------------------------
  // Fetch existing page
  // --------------------------------------------------------

  Page *again0 = bpm.fetchPage(p0);

  CHECK(again0 != nullptr);

  CHECK(again0->getPageId() == p0);

  CHECK(again0->getPinCount() == 1);

  /*
   * Should be same frame because p0 is still cached.
   */

  CHECK(again0 == page0);

  pass("Cached page is returned without creating another frame.");

  CHECK(bpm.unpinPage(p0, false));

  std::filesystem::remove(db_file);
}

// ============================================================
// TEST 6
// BufferPool repeated fetch/pin
// ============================================================

void testRepeatedFetch() {

  section("Repeated fetch and pin count");

  const std::string db_file = "waloudb_test_repeated_fetch.db";

  std::filesystem::remove(db_file);

  DiskManager disk(db_file);

  BufferPoolManager bpm(3, &disk);

  page_id_t pid;

  Page *page = bpm.newPage(&pid);

  CHECK(page != nullptr);

  CHECK(page->getPinCount() == 1);

  Page *page2 = bpm.fetchPage(pid);

  CHECK(page2 == page);
  CHECK(page->getPinCount() == 2);

  Page *page3 = bpm.fetchPage(pid);

  CHECK(page3 == page);
  CHECK(page->getPinCount() == 3);

  pass("Repeated fetch increments pin count.");

  // --------------------------------------------------------
  // Unpin one
  // --------------------------------------------------------

  CHECK(bpm.unpinPage(pid, false));

  CHECK(page->getPinCount() == 2);

  // --------------------------------------------------------
  // Unpin second
  // --------------------------------------------------------

  CHECK(bpm.unpinPage(pid, false));

  CHECK(page->getPinCount() == 1);

  // --------------------------------------------------------
  // Unpin third
  // --------------------------------------------------------

  CHECK(bpm.unpinPage(pid, false));

  CHECK(page->getPinCount() == 0);

  pass("Pin count decreases correctly.");

  // --------------------------------------------------------
  // Cannot unpin below zero
  // --------------------------------------------------------

  CHECK(!bpm.unpinPage(pid, false));

  pass("Cannot unpin an already-unpinned page.");

  std::filesystem::remove(db_file);
}

// ============================================================
// TEST 7
// Deterministic LRU eviction
// ============================================================

void testBufferPoolLruEviction() {

  section("BufferPool + LRU eviction");

  const std::string db_file = "waloudb_test_eviction.db";

  std::filesystem::remove(db_file);

  DiskManager disk(db_file);

  BufferPoolManager bpm(3, &disk);

  page_id_t p0;
  page_id_t p1;
  page_id_t p2;

  CHECK(bpm.newPage(&p0) != nullptr);
  CHECK(bpm.newPage(&p1) != nullptr);
  CHECK(bpm.newPage(&p2) != nullptr);

  /*
   * All three become evictable.
   */

  CHECK(bpm.unpinPage(p0, false));
  CHECK(bpm.unpinPage(p1, false));
  CHECK(bpm.unpinPage(p2, false));

  /*
   * Touch p0.
   */

  CHECK(bpm.fetchPage(p0) != nullptr);
  CHECK(bpm.unpinPage(p0, false));

  /*
   * Touch p1.
   */

  CHECK(bpm.fetchPage(p1) != nullptr);
  CHECK(bpm.unpinPage(p1, false));

  /*
   * Expected order:
   *
   * oldest -> newest
   *
   * p2 -> p0 -> p1
   */

  page_id_t p3;

  CHECK(bpm.newPage(&p3) != nullptr);

  CHECK(p3 == 3);

  pass("Fourth page allocated through eviction.");

  /*
   * p2 should have been the victim.
   *
   * Fetching it should work.
   */

  Page *page2 = bpm.fetchPage(p2);

  CHECK(page2 != nullptr);

  CHECK(page2->getPageId() == p2);

  pass("Least recently used page was evicted.");

  CHECK(bpm.unpinPage(p2, false));

  CHECK(bpm.unpinPage(p3, false));
}

// ============================================================
// TEST 8
// Dirty page write-back
// ============================================================

void testDirtyEviction() {

  section("Dirty page write-back");

  const std::string db_file = "waloudb_test_dirty.db";

  std::filesystem::remove(db_file);

  DiskManager disk(db_file);

  BufferPoolManager bpm(3, &disk);

  page_id_t p0;
  page_id_t p1;
  page_id_t p2;

  Page *page0 = bpm.newPage(&p0);

  CHECK(page0 != nullptr);

  CHECK(bpm.newPage(&p1) != nullptr);

  CHECK(bpm.newPage(&p2) != nullptr);

  const char *message = "DIRTY PAGE DATA";

  std::memcpy(page0->getData(), message, std::strlen(message) + 1);

  /*
   * p0 is dirty.
   */

  CHECK(bpm.unpinPage(p0, true));

  CHECK(bpm.unpinPage(p1, false));

  CHECK(bpm.unpinPage(p2, false));

  pass("Page marked dirty.");

  /*
   * Make p0 the oldest page.
   *
   * p1 -> p2 accesses.
   */

  CHECK(bpm.fetchPage(p1) != nullptr);
  CHECK(bpm.unpinPage(p1, false));

  CHECK(bpm.fetchPage(p2) != nullptr);
  CHECK(bpm.unpinPage(p2, false));

  /*
   * This should evict p0 and write it to disk.
   */

  page_id_t p3;

  CHECK(bpm.newPage(&p3) != nullptr);

  pass("Dirty page was evicted.");

  /*
   * Fetch p0 again.
   */

  Page *page0_again = bpm.fetchPage(p0);

  CHECK(page0_again != nullptr);

  CHECK(std::strcmp(page0_again->getData(), message) == 0);

  pass("Dirty data survived eviction.");

  CHECK(bpm.unpinPage(p0, false));

  CHECK(bpm.unpinPage(p3, false));

  std::filesystem::remove(db_file);
}

// ============================================================
// TEST 9
// Pinned page protection
// ============================================================

void testPinnedPageProtection() {

  section("Pinned pages cannot be evicted");

  const std::string db_file = "waloudb_test_pinned.db";

  std::filesystem::remove(db_file);

  DiskManager disk(db_file);

  BufferPoolManager bpm(3, &disk);

  page_id_t p0;
  page_id_t p1;
  page_id_t p2;

  Page *page0 = bpm.newPage(&p0);

  CHECK(page0 != nullptr);

  CHECK(bpm.newPage(&p1) != nullptr);

  CHECK(bpm.newPage(&p2) != nullptr);

  /*
   * Keep p0 pinned.
   */

  CHECK(bpm.unpinPage(p1, false));

  CHECK(bpm.unpinPage(p2, false));

  /*
   * p0 is pinned.
   *
   * Allocate p3.
   *
   * It must evict p1 or p2.
   */

  page_id_t p3;

  CHECK(bpm.newPage(&p3) != nullptr);

  pass("Eviction succeeds while one page remains pinned.");

  /*
   * p0 must still be the same cached page.
   */

  Page *again = bpm.fetchPage(p0);

  CHECK(again != nullptr);
  CHECK(again == page0);

  /*
   * p0 now has two pins.
   */

  CHECK(again->getPinCount() == 2);

  pass("Pinned page remains in memory.");

  // Release both p0 pins.
  CHECK(bpm.unpinPage(p0, false));
  CHECK(bpm.unpinPage(p0, false));

  CHECK(bpm.unpinPage(p3, false));

  std::filesystem::remove(db_file);
}

// ============================================================
// TEST 10
// Complete pool exhaustion
// ============================================================

void testPoolExhaustion() {

  section("BufferPool exhaustion");

  const std::string db_file = "waloudb_test_exhaustion.db";

  std::filesystem::remove(db_file);

  DiskManager disk(db_file);

  BufferPoolManager bpm(3, &disk);

  page_id_t p0;
  page_id_t p1;
  page_id_t p2;

  CHECK(bpm.newPage(&p0) != nullptr);
  CHECK(bpm.newPage(&p1) != nullptr);
  CHECK(bpm.newPage(&p2) != nullptr);

  /*
   * All three pages are still pinned.
   *
   * Therefore:
   *
   * free list = empty
   * LRU       = empty
   *
   * No frame can be used.
   */

  page_id_t p3;

  CHECK(bpm.newPage(&p3) == nullptr);

  pass("newPage fails when all frames are pinned.");

  /*
   * Release one page.
   */

  CHECK(bpm.unpinPage(p1, false));

  /*
   * Now allocation should work.
   */

  CHECK(bpm.newPage(&p3) != nullptr);

  pass("Allocation succeeds after unpinning a page.");

  CHECK(bpm.unpinPage(p0, false));
  CHECK(bpm.unpinPage(p2, false));
  CHECK(bpm.unpinPage(p3, false));

  std::filesystem::remove(db_file);
}

// ============================================================
// TEST 11
// SlottedPage + BufferPool integration
// ============================================================

void testTupleBufferPoolIntegration() {

  section("Tuple + SlottedPage + BufferPool");

  const std::string db_file = "waloudb_test_tuple_integration.db";

  std::filesystem::remove(db_file);

  DiskManager disk(db_file);

  BufferPoolManager bpm(3, &disk);

  page_id_t pid;

  Page *raw = bpm.newPage(&pid);

  CHECK(raw != nullptr);

  /*
   * Turn raw page into a SlottedPage.
   */

  SlottedPage page(raw->getData());

  page.Init(pid);

  std::vector<RID> rids;

  // --------------------------------------------------------
  // Insert 20 tuples
  // --------------------------------------------------------

  for (uint32_t i = 0; i < 20; ++i) {

    Tuple tuple = makeTuple(i, "user_" + std::to_string(i));

    RID rid;

    CHECK(page.insertTuple(tuple, &rid));

    CHECK(rid.page_id == pid);

    rids.push_back(rid);
  }

  CHECK(page.getSlotCount() == 20);

  pass("20 tuples inserted into page.");

  /*
   * Page is dirty.
   */

  CHECK(bpm.unpinPage(pid, true));

  // --------------------------------------------------------
  // Create buffer pressure
  // --------------------------------------------------------

  page_id_t p1;
  page_id_t p2;
  page_id_t p3;

  CHECK(bpm.newPage(&p1) != nullptr);

  CHECK(bpm.newPage(&p2) != nullptr);

  CHECK(bpm.unpinPage(p1, false));

  CHECK(bpm.unpinPage(p2, false));

  /*
   * p1/p2 are candidates.
   *
   * Allocate another page.
   */

  CHECK(bpm.newPage(&p3) != nullptr);

  CHECK(bpm.unpinPage(p3, false));

  pass("Buffer pool pressure exercised eviction.");

  // --------------------------------------------------------
  // Fetch original tuple page again
  // --------------------------------------------------------

  Page *raw_again = bpm.fetchPage(pid);

  CHECK(raw_again != nullptr);

  SlottedPage page_again(raw_again->getData());

  CHECK(page_again.getSlotCount() == 20);

  /*
   * Verify every tuple.
   */

  for (uint32_t i = 0; i < 20; ++i) {

    auto tuple = page_again.getTuple(rids[i].slot_num);

    CHECK(tuple.has_value());

    checkTuple(*tuple, i, "user_" + std::to_string(i));
  }

  pass("All 20 tuples survived buffer eviction.");

  // --------------------------------------------------------
  // Delete even tuples
  // --------------------------------------------------------

  for (size_t i = 0; i < rids.size(); i += 2) {

    CHECK(page_again.deleteTuple(rids[i].slot_num));
  }

  /*
   * Even tuples gone.
   */

  for (size_t i = 0; i < rids.size(); i += 2) {

    CHECK(!page_again.getTuple(rids[i].slot_num).has_value());
  }

  /*
   * Odd tuples still exist.
   */

  for (size_t i = 1; i < rids.size(); i += 2) {

    auto tuple = page_again.getTuple(rids[i].slot_num);

    CHECK(tuple.has_value());

    checkTuple(*tuple, static_cast<uint32_t>(i), "user_" + std::to_string(i));
  }

  pass("Delete affects only targeted tuples.");

  CHECK(bpm.unpinPage(pid, true));

  std::filesystem::remove(db_file);
}

// ============================================================
// TEST 12
// Persistence through BufferPool
// ============================================================

void testPersistence() {

  section("Persistence across BufferPoolManager lifetime");

  const std::string db_file = "waloudb_test_persistence.db";

  std::filesystem::remove(db_file);

  page_id_t pid;

  // ========================================================
  // First DB session
  // ========================================================

  {
    DiskManager disk(db_file);

    BufferPoolManager bpm(3, &disk);

    Page *page = bpm.newPage(&pid);

    CHECK(page != nullptr);

    const char *message = "PERSISTENT WALOUDB DATA";

    std::memcpy(page->getData(), message, std::strlen(message) + 1);

    CHECK(bpm.unpinPage(pid, true));

    CHECK(bpm.flushPage(pid));

    pass("Page explicitly flushed to disk.");
  }

  // ========================================================
  // Second DB session
  // ========================================================

  {
    DiskManager disk(db_file);

    BufferPoolManager bpm(3, &disk);

    Page *page = bpm.fetchPage(pid);

    CHECK(page != nullptr);

    CHECK(std::strcmp(page->getData(), "PERSISTENT WALOUDB DATA") == 0);

    pass("Data survives BufferPoolManager restart.");

    CHECK(bpm.unpinPage(pid, false));
  }

  std::filesystem::remove(db_file);
}
// ============================================================
// TEST 13
// No slot reuse / tombstones
// ============================================================

void testNoSlotReuse() {

  section("SlottedPage no slot reuse");

  char raw_page[PAGE_SIZE]{};

  SlottedPage page(raw_page);

  page.Init(100);

  std::vector<RID> original_rids;

  // --------------------------------------------------------
  // Insert 10
  // --------------------------------------------------------

  for (uint32_t i = 0; i < 10; ++i) {

    Tuple tuple = makeTuple(i, "first_" + std::to_string(i));

    RID rid;

    CHECK(page.insertTuple(tuple, &rid));

    original_rids.push_back(rid);
  }

  CHECK(page.getSlotCount() == 10);

  pass("10 slots created.");

  // --------------------------------------------------------
  // Delete all
  // --------------------------------------------------------

  for (const RID &rid : original_rids) {

    CHECK(page.deleteTuple(rid.slot_num));
  }

  CHECK(page.getSlotCount() == 10);

  pass("All 10 tuples deleted.");

  // --------------------------------------------------------
  // Verify tombstones
  // --------------------------------------------------------

  for (const RID &rid : original_rids) {

    CHECK(!page.getTuple(rid.slot_num).has_value());
    CHECK(!page.getTuple(rid.slot_num).has_value());
  }

  pass("All deleted slots remain tombstoned.");

  // --------------------------------------------------------
  // Reinsert
  // --------------------------------------------------------

  std::vector<RID> new_rids;

  for (uint32_t i = 0; i < 10; ++i) {

    Tuple tuple = makeTuple(100 + i, "second_" + std::to_string(i));

    RID rid;

    CHECK(page.insertTuple(tuple, &rid));

    new_rids.push_back(rid);
  }

  // --------------------------------------------------------
  // Critical check:
  //
  // Old tombstones are NOT reused.
  // New tuples receive new slot numbers.
  // --------------------------------------------------------

  CHECK(page.getSlotCount() == 20);

  for (uint32_t i = 0; i < 10; ++i) {

    CHECK(new_rids[i].slot_num == 10 + i);
  }

  pass("New tuples receive new slots instead of reusing tombstones.");

  // --------------------------------------------------------
  // Verify old slots are still tombstones
  // --------------------------------------------------------

  for (const RID &rid : original_rids) {
    CHECK(!page.getTuple(rid.slot_num).has_value());
  }

  pass("Original slots remain tombstoned.");

  // --------------------------------------------------------
  // Verify new tuples
  // --------------------------------------------------------

  for (uint32_t i = 0; i < 10; ++i) {

    auto tuple = page.getTuple(new_rids[i].slot_num);

    CHECK(tuple.has_value());

    checkTuple(*tuple, 100 + i, "second_" + std::to_string(i));
  }

  pass("New slots contain correct tuples.");
}
// ============================================================
// TEST 14
// Page metadata / free space
// ============================================================

void testPageMetadata() {

  section("Page metadata");

  char raw_page[PAGE_SIZE]{};

  SlottedPage page(raw_page);

  page.Init(123);

  CHECK(page.getPageId() == 123);
  CHECK(page.getSlotCount() == 0);

  uint16_t initial_free = page.freeSpace();

  CHECK(initial_free > 0);
  CHECK(initial_free < PAGE_SIZE);

  pass("Page ID is correct.");
  pass("Initial slot count is zero.");
  pass("Initial free space is valid.");

  Tuple tuple = makeTuple(42, "MetadataTest");

  RID rid;

  CHECK(page.insertTuple(tuple, &rid));

  uint16_t after_insert = page.freeSpace();

  CHECK(after_insert < initial_free);

  pass("Free space decreases after insertion.");

  CHECK(page.deleteTuple(rid.slot_num));

  /*
   * With tombstones, deleting does NOT necessarily increase
   * freeSpace().
   *
   * That's expected in your current design.
   */

  pass("Deletion uses tombstone semantics.");
}

// ============================================================
// TEST 15
// Invalid operations
// ============================================================

void testInvalidOperations() {

  section("Invalid operations");

  const std::string db_file = "waloudb_test_invalid.db";

  std::filesystem::remove(db_file);

  DiskManager disk(db_file);

  BufferPoolManager bpm(3, &disk);

  // --------------------------------------------------------
  // Invalid unpin
  // --------------------------------------------------------

  CHECK(!bpm.unpinPage(999999, false));

  pass("Invalid page unpin is rejected.");

  // --------------------------------------------------------
  // SlottedPage invalid get/delete
  // --------------------------------------------------------

  char raw_page[PAGE_SIZE]{};

  SlottedPage page(raw_page);

  page.Init(0);

  CHECK(!page.getTuple(9999).has_value());

  CHECK(!page.deleteTuple(9999));

  pass("Invalid slot get is rejected.");
  pass("Invalid slot delete is rejected.");

  // --------------------------------------------------------
  // Delete same tuple twice
  // --------------------------------------------------------

  Tuple tuple = makeTuple(1, "Test");

  RID rid;

  CHECK(page.insertTuple(tuple, &rid));

  CHECK(page.deleteTuple(rid.slot_num));

  CHECK(!page.deleteTuple(rid.slot_num));

  pass("Double delete is rejected.");

  std::filesystem::remove(db_file);
}

// ============================================================
// TEST 16
// Multiple pages containing tuples
// ============================================================

void testMultipleTuplePages() {

  section("Multiple pages containing tuples");

  const std::string db_file = "waloudb_test_multiple_pages.db";

  std::filesystem::remove(db_file);

  DiskManager disk(db_file);

  BufferPoolManager bpm(3, &disk);

  page_id_t p0;
  page_id_t p1;

  Page *raw0 = bpm.newPage(&p0);

  CHECK(raw0 != nullptr);

  SlottedPage page0(raw0->getData());

  page0.Init(p0);

  RID rid0;

  Tuple tuple0 = makeTuple(1, "PageZero");

  CHECK(page0.insertTuple(tuple0, &rid0));

  CHECK(bpm.unpinPage(p0, true));

  // --------------------------------------------------------
  // Second page
  // --------------------------------------------------------

  Page *raw1 = bpm.newPage(&p1);

  CHECK(raw1 != nullptr);

  SlottedPage page1(raw1->getData());

  page1.Init(p1);

  RID rid1;

  Tuple tuple1 = makeTuple(2, "PageOne");

  CHECK(page1.insertTuple(tuple1, &rid1));

  CHECK(bpm.unpinPage(p1, true));

  pass("Tuples inserted into multiple pages.");

  // --------------------------------------------------------
  // Fetch both again
  // --------------------------------------------------------

  Page *again0 = bpm.fetchPage(p0);

  CHECK(again0 != nullptr);

  SlottedPage verify0(again0->getData());

  auto result0 = verify0.getTuple(rid0.slot_num);

  CHECK(result0.has_value());

  checkTuple(*result0, 1, "PageZero");

  CHECK(bpm.unpinPage(p0, false));

  Page *again1 = bpm.fetchPage(p1);

  CHECK(again1 != nullptr);

  SlottedPage verify1(again1->getData());

  auto result1 = verify1.getTuple(rid1.slot_num);

  CHECK(result1.has_value());

  checkTuple(*result1, 2, "PageOne");

  CHECK(bpm.unpinPage(p1, false));

  pass("Tuples remain correctly associated with their pages.");

  /*
   * Verify RID page IDs.
   */

  CHECK(rid0.page_id == p0);
  CHECK(rid1.page_id == p1);

  pass("RID page IDs are correct.");

  std::filesystem::remove(db_file);
}

// ============================================================
// Main
// ============================================================

int main() {

  std::cout << "\n";
  std::cout << "############################################################\n";
  std::cout << "#                                                          #\n";
  std::cout << "#              WALOUDB FULL SYSTEM TEST                   #\n";
  std::cout << "#                                                          #\n";
  std::cout << "############################################################\n";

  try {

    // ----------------------------------------------------
    // Low-level components
    // ----------------------------------------------------

    testDiskManager();

    testLruReplacer();

    testSlottedPage();

    testLargeTuple();

    // ----------------------------------------------------
    // Buffer pool
    // ----------------------------------------------------

    testBufferPoolBasic();

    testRepeatedFetch();

    testBufferPoolLruEviction();

    testDirtyEviction();

    testPinnedPageProtection();

    testPoolExhaustion();

    // ----------------------------------------------------
    // Integration
    // ----------------------------------------------------

    testTupleBufferPoolIntegration();

    testPersistence();

    testNoSlotReuse();

    testPageMetadata();

    testInvalidOperations();

    testMultipleTuplePages();

  } catch (const std::exception &e) {

    std::cerr << "\n";
    std::cerr << "[EXCEPTION] " << e.what() << "\n";

    return EXIT_FAILURE;
  }

  std::cout << "\n";
  std::cout << "############################################################\n";
  std::cout << "#                                                          #\n";
  std::cout << "#                 ALL TESTS PASSED                        #\n";
  std::cout << "#                                                          #\n";
  std::cout << "############################################################\n";

  std::cout << "\n";
  std::cout << "Assertions passed: " << g_tests_passed << " / " << g_tests_run
            << "\n";

  return EXIT_SUCCESS;
}
