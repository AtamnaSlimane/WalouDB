#include "waloudb/storage/DiskManager.h"
#include "waloudb/common/Types.h"
#include "waloudb/core/Log.h"
#include "waloudb/storage/Page.h"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace {

constexpr const char *TEST_DB = "test_disk_manager.db";
constexpr const char *UNWRITTEN_DB = "test_unwritten_page.db";
constexpr const char *PERSISTENCE_DB = "test_persistence.db";

int passed = 0;
int failed = 0;

// ============================================================
// Test helper
// ============================================================

bool check(bool condition, const char *message) {
  if (!condition) {
    WALOU_ERROR("    FAIL: {}", message);
    return false;
  }

  WALOU_INFO("    PASS: {}", message);
  return true;
}

void removeTestFiles() {
  std::filesystem::remove(TEST_DB);
  std::filesystem::remove(UNWRITTEN_DB);
  std::filesystem::remove(PERSISTENCE_DB);
}

// ============================================================
// Page tests
// ============================================================

bool testPageGettersAndSetters() {
  WALOU_INFO("--------------------------------------------------");
  WALOU_INFO("TEST: Page getters and setters");
  WALOU_INFO("--------------------------------------------------");

  WalouDB::Page page;

  // Initial state.
  WALOU_INFO("Initial Page state:");
  WALOU_INFO("  Page ID    : {}", page.getPageId());
  WALOU_INFO("  Dirty      : {}", page.isDirty());
  WALOU_INFO("  Pin count  : {}", page.getPinCount());

  bool success = true;

  success &= check(page.getPageId() == WalouDB::INVALID_PAGE_ID,
                   "Initial page ID is INVALID_PAGE_ID");

  success &= check(!page.isDirty(), "Initial dirty flag is false");

  success &= check(page.getPinCount() == 0, "Initial pin count is 0");

  // setPageId / getPageId.
  page.setPageId(42);

  WALOU_INFO("After setPageId(42): page ID = {}", page.getPageId());

  success &= check(page.getPageId() == 42, "setPageId() / getPageId()");

  // setDirty / isDirty.
  page.setDirty(true);

  WALOU_INFO("After setDirty(true): dirty = {}", page.isDirty());

  success &= check(page.isDirty(), "setDirty(true) / isDirty()");

  page.setDirty(false);

  WALOU_INFO("After setDirty(false): dirty = {}", page.isDirty());

  success &= check(!page.isDirty(), "setDirty(false) / isDirty()");

  // pin / getPinCount / unpin.
  page.pin();

  WALOU_INFO("After pin(): pin count = {}", page.getPinCount());

  success &= check(page.getPinCount() == 1, "pin() increments pin count");

  page.pin();

  WALOU_INFO("After second pin(): pin count = {}", page.getPinCount());

  success &= check(page.getPinCount() == 2,
                   "Multiple pin() calls increment correctly");

  page.unpin();

  WALOU_INFO("After unpin(): pin count = {}", page.getPinCount());

  success &= check(page.getPinCount() == 1, "unpin() decrements pin count");

  page.unpin();

  WALOU_INFO("After second unpin(): pin count = {}", page.getPinCount());

  success &= check(page.getPinCount() == 0, "Pin count returns to 0");

  // Test unpin at zero.
  page.unpin();

  WALOU_INFO("After unpin() at zero: pin count = {}", page.getPinCount());

  success &= check(page.getPinCount() == 0,
                   "unpin() does not produce a negative pin count");

  if (success) {
    WALOU_INFO("RESULT: Page getters/setters PASSED");
  } else {
    WALOU_ERROR("RESULT: Page getters/setters FAILED");
  }

  return success;
}

bool testPageData() {
  WALOU_INFO("--------------------------------------------------");
  WALOU_INFO("TEST: Page data access");
  WALOU_INFO("--------------------------------------------------");

  WalouDB::Page page;

  const char *message = "Hello from WalouDB Page!";

  std::memset(page.getData(), 0, WalouDB::PAGE_SIZE);

  std::memcpy(page.getData(), message, std::strlen(message) + 1);

  WALOU_INFO("Page data: \"{}\"", page.getData());

  bool success = true;

  success &= check(std::strcmp(page.getData(), message) == 0,
                   "getData() returns writable page memory");

  success &=
      check(page.getData()[0] == 'H', "First byte of page data is correct");

  if (success) {
    WALOU_INFO("RESULT: Page data PASSED");
  } else {
    WALOU_ERROR("RESULT: Page data FAILED");
  }

  return success;
}

bool testPageResetMemory() {
  WALOU_INFO("--------------------------------------------------");
  WALOU_INFO("TEST: Page resetMemory()");
  WALOU_INFO("--------------------------------------------------");

  WalouDB::Page page;

  page.setPageId(100);
  page.setDirty(true);
  page.pin();

  std::memset(page.getData(), 'X', WalouDB::PAGE_SIZE);

  WALOU_INFO("Before resetMemory():");
  WALOU_INFO("  Page ID    : {}", page.getPageId());
  WALOU_INFO("  Dirty      : {}", page.isDirty());
  WALOU_INFO("  Pin count  : {}", page.getPinCount());
  WALOU_INFO("  First byte : {}",
             static_cast<int>(static_cast<unsigned char>(page.getData()[0])));

  page.resetMemory();

  WALOU_INFO("After resetMemory():");
  WALOU_INFO("  Page ID    : {}", page.getPageId());
  WALOU_INFO("  Dirty      : {}", page.isDirty());
  WALOU_INFO("  Pin count  : {}", page.getPinCount());
  WALOU_INFO("  First byte : {}",
             static_cast<int>(static_cast<unsigned char>(page.getData()[0])));

  bool success = true;

  success &= check(page.getPageId() == WalouDB::INVALID_PAGE_ID,
                   "resetMemory() resets page ID");

  success &= check(!page.isDirty(), "resetMemory() clears dirty flag");

  success &= check(page.getPinCount() == 0, "resetMemory() resets pin count");

  bool all_zero = true;

  for (std::size_t i = 0; i < WalouDB::PAGE_SIZE; ++i) {

    if (page.getData()[i] != 0) {
      all_zero = false;
      break;
    }
  }

  success &= check(all_zero, "resetMemory() zeroes the entire page");

  if (success) {
    WALOU_INFO("RESULT: Page resetMemory() PASSED");
  } else {
    WALOU_ERROR("RESULT: Page resetMemory() FAILED");
  }

  return success;
}

// ============================================================
// DiskManager tests
// ============================================================

bool testAllocatePage() {
  WALOU_INFO("--------------------------------------------------");
  WALOU_INFO("TEST: DiskManager allocatePage()");
  WALOU_INFO("--------------------------------------------------");

  std::filesystem::remove(TEST_DB);

  try {
    WalouDB::DiskManager disk_manager(TEST_DB);

    const auto page0 = disk_manager.allocatePage();
    const auto page1 = disk_manager.allocatePage();
    const auto page2 = disk_manager.allocatePage();

    WALOU_INFO("Allocated page IDs: {}, {}, {}", page0, page1, page2);

    bool success = true;

    success &= check(page0 == 0, "First allocated page ID is 0");

    success &= check(page1 == 1, "Second allocated page ID is 1");

    success &= check(page2 == 2, "Third allocated page ID is 2");

    if (success) {
      WALOU_INFO("RESULT: allocatePage() PASSED");
    } else {
      WALOU_ERROR("RESULT: allocatePage() FAILED");
    }

    return success;

  } catch (const std::exception &e) {
    WALOU_ERROR("allocatePage() threw exception: {}", e.what());

    return false;
  }
}

bool testWriteAndReadPage() {
  WALOU_INFO("--------------------------------------------------");
  WALOU_INFO("TEST: DiskManager writePage() / readPage()");
  WALOU_INFO("--------------------------------------------------");

  std::filesystem::remove(TEST_DB);

  try {
    WalouDB::DiskManager disk_manager(TEST_DB);

    const auto page_id = disk_manager.allocatePage();

    WalouDB::Page write_page;
    write_page.setPageId(page_id);

    const char *message = "WalouDB DiskManager read/write test.";

    std::memset(write_page.getData(), 0, WalouDB::PAGE_SIZE);

    std::memcpy(write_page.getData(), message, std::strlen(message) + 1);

    WALOU_INFO("Page before writing:");
    WALOU_INFO("  Page ID   : {}", write_page.getPageId());
    WALOU_INFO("  Dirty     : {}", write_page.isDirty());
    WALOU_INFO("  Pin count : {}", write_page.getPinCount());
    WALOU_INFO("  Data      : \"{}\"", write_page.getData());

    bool success = true;

    success &= check(disk_manager.writePage(page_id, write_page.getData()),
                     "writePage() succeeds");

    WalouDB::Page read_page;
    read_page.setPageId(page_id);

    std::memset(read_page.getData(), 0, WalouDB::PAGE_SIZE);

    success &= check(disk_manager.readPage(page_id, read_page.getData()),
                     "readPage() succeeds");

    WALOU_INFO("Page after reading:");
    WALOU_INFO("  Page ID   : {}", read_page.getPageId());
    WALOU_INFO("  Dirty     : {}", read_page.isDirty());
    WALOU_INFO("  Pin count : {}", read_page.getPinCount());
    WALOU_INFO("  Data      : \"{}\"", read_page.getData());

    success &= check(std::memcmp(write_page.getData(), read_page.getData(),
                                 WalouDB::PAGE_SIZE) == 0,
                     "Written and read page data are identical");

    success &= check(std::strcmp(read_page.getData(), message) == 0,
                     "Read data matches original message");

    if (success) {
      WALOU_INFO("RESULT: writePage() / readPage() PASSED");
    } else {
      WALOU_ERROR("RESULT: writePage() / readPage() FAILED");
    }

    return success;

  } catch (const std::exception &e) {
    WALOU_ERROR("writePage/readPage threw exception: {}", e.what());

    return false;
  }
}

bool testReadUnwrittenPage() {
  WALOU_INFO("--------------------------------------------------");
  WALOU_INFO("TEST: Reading an unwritten page");
  WALOU_INFO("--------------------------------------------------");

  std::filesystem::remove(UNWRITTEN_DB);

  try {
    WalouDB::DiskManager disk_manager(UNWRITTEN_DB);

    const auto page_id = disk_manager.allocatePage();

    WalouDB::Page page;

    // Deliberately fill memory with 0xFF.
    std::memset(page.getData(), 0xFF, WalouDB::PAGE_SIZE);

    WALOU_INFO("Reading newly allocated page {}", page_id);

    bool success = true;

    success &= check(disk_manager.readPage(page_id, page.getData()),
                     "readPage() succeeds for unwritten page");

    bool all_zero = true;

    for (std::size_t i = 0; i < WalouDB::PAGE_SIZE; ++i) {

      if (page.getData()[i] != 0) {
        all_zero = false;

        WALOU_ERROR("Non-zero byte at offset {}", i);

        break;
      }
    }

    success &= check(all_zero, "Unwritten page is zero-filled");

    if (success) {
      WALOU_INFO("RESULT: Unwritten page PASSED");
    } else {
      WALOU_ERROR("RESULT: Unwritten page FAILED");
    }

    return success;

  } catch (const std::exception &e) {
    WALOU_ERROR("Reading unwritten page threw exception: {}", e.what());

    return false;
  }
}

bool testPersistence() {
  WALOU_INFO("--------------------------------------------------");
  WALOU_INFO("TEST: DiskManager persistence");
  WALOU_INFO("--------------------------------------------------");

  std::filesystem::remove(PERSISTENCE_DB);

  const char *message = "This data must survive DiskManager destruction.";

  try {

    // --------------------------------------------
    // First DiskManager
    // --------------------------------------------

    {
      WALOU_INFO("Opening first DiskManager");

      WalouDB::DiskManager disk_manager(PERSISTENCE_DB);

      WalouDB::Page page;

      std::memset(page.getData(), 0, WalouDB::PAGE_SIZE);

      std::memcpy(page.getData(), message, std::strlen(message) + 1);

      WALOU_INFO("Writing page 0: \"{}\"", page.getData());

      if (!check(disk_manager.writePage(0, page.getData()),
                 "writePage() succeeds")) {

        return false;
      }
    }

    WALOU_INFO("First DiskManager destroyed");

    // --------------------------------------------
    // Second DiskManager
    // --------------------------------------------

    {
      WALOU_INFO("Opening second DiskManager");

      WalouDB::DiskManager disk_manager(PERSISTENCE_DB);

      WalouDB::Page page;

      std::memset(page.getData(), 0, WalouDB::PAGE_SIZE);

      if (!check(disk_manager.readPage(0, page.getData()),
                 "readPage() succeeds after reopening")) {

        return false;
      }

      WALOU_INFO("Recovered data: \"{}\"", page.getData());

      if (!check(std::strcmp(page.getData(), message) == 0,
                 "Data survives DiskManager destruction and reopening")) {

        return false;
      }
    }

    WALOU_INFO("RESULT: Persistence PASSED");

    return true;

  } catch (const std::exception &e) {
    WALOU_ERROR("Persistence test threw exception: {}", e.what());

    return false;
  }
}

// ============================================================
// Test runner
// ============================================================

void runTest(const char *name, bool (*test)()) {
  WALOU_INFO("");
  WALOU_INFO("========================================");
  WALOU_INFO("Running: {}", name);
  WALOU_INFO("========================================");

  if (test()) {
    ++passed;
    WALOU_INFO("TEST PASSED: {}", name);
  } else {
    ++failed;
    WALOU_ERROR("TEST FAILED: {}", name);
  }
}

} // namespace

int main() {
  WalouDB::Log::Init();

  WALOU_INFO("");
  WALOU_INFO("==============================================");
  WALOU_INFO("          WalouDB Storage Tests");
  WALOU_INFO("==============================================");
  WALOU_INFO("Page size: {} bytes", WalouDB::PAGE_SIZE);

  removeTestFiles();

  // -----------------------------
  // Page tests
  // -----------------------------

  runTest("Page getters and setters", testPageGettersAndSetters);

  runTest("Page data access", testPageData);

  runTest("Page resetMemory", testPageResetMemory);

  // -----------------------------
  // DiskManager tests
  // -----------------------------

  runTest("DiskManager allocatePage", testAllocatePage);

  runTest("DiskManager write/read page", testWriteAndReadPage);

  runTest("DiskManager read unwritten page", testReadUnwrittenPage);

  runTest("DiskManager persistence", testPersistence);

  removeTestFiles();

  // -----------------------------
  // Final result
  // -----------------------------

  WALOU_INFO("");
  WALOU_INFO("==============================================");
  WALOU_INFO("              TEST SUMMARY");
  WALOU_INFO("==============================================");
  WALOU_INFO("Tests passed : {}", passed);
  WALOU_INFO("Tests failed : {}", failed);
  WALOU_INFO("==============================================");

  if (failed == 0) {
    WALOU_INFO("ALL TESTS PASSED");
    WALOU_INFO("==============================================");

    return 0;
  }

  WALOU_CRITICAL("{} TEST(S) FAILED", failed);

  WALOU_INFO("==============================================");

  return 1;
}
