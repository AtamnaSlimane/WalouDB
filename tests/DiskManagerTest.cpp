#include "waloudb/storage/DiskManager.h"
#include "waloudb/core/Log.h"
#include "waloudb/storage/Page.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

constexpr const char *DB_FILE = "test.db";

bool testReadWriteCycle() {
  WALOU_INFO("Test: ReadWriteCycle");

  std::filesystem::remove(DB_FILE);

  try {
    WalouDB::DiskManager disk_manager(DB_FILE);

    WalouDB::page_id_t page_id = disk_manager.allocatePage();

    WALOU_INFO("Allocated page ID: {}", page_id);

    WalouDB::Page page;
    page.setPageId(page_id);

    const char *message = "Hello from WalouDB! "
                          "This data was written to disk.";

    char *data = page.getData();

    std::memset(data, 0, WalouDB::PAGE_SIZE);

    std::memcpy(data, message, std::strlen(message) + 1);

    if (!disk_manager.writePage(page_id, page.getData())) {
      WALOU_ERROR("Failed to write page {}", page_id);
      return false;
    }

    WalouDB::Page recovered_page;
    recovered_page.setPageId(page_id);

    if (!disk_manager.readPage(page_id, recovered_page.getData())) {
      WALOU_ERROR("Failed to read page {}", page_id);
      return false;
    }

    if (std::memcmp(page.getData(), recovered_page.getData(),
                    WalouDB::PAGE_SIZE) != 0) {

      WALOU_ERROR("Read/write verification failed for page {}", page_id);

      return false;
    }

    WALOU_INFO("Read/write verification passed for page {}", page_id);

    return true;

  } catch (const std::exception &e) {
    WALOU_ERROR("ReadWriteCycle threw exception: {}", e.what());

    return false;
  }
}

bool testReadUnwrittenPageIsZeroed() {
  WALOU_INFO("Test: ReadUnwrittenPageIsZeroed");

  const char *db_file = "test_unwritten.db";

  std::filesystem::remove(db_file);

  try {
    WalouDB::DiskManager disk_manager(db_file);

    WalouDB::page_id_t page_id = disk_manager.allocatePage();

    WALOU_INFO("Allocated unwritten page {}", page_id);

    WalouDB::Page page;

    // Fill with non-zero data first.
    std::memset(page.getData(), 0xFF, WalouDB::PAGE_SIZE);

    if (!disk_manager.readPage(page_id, page.getData())) {

      WALOU_ERROR("Failed to read unwritten page {}", page_id);

      std::filesystem::remove(db_file);
      return false;
    }

    // The unwritten page should now be completely zeroed.
    for (std::size_t i = 0; i < WalouDB::PAGE_SIZE; ++i) {

      if (page.getData()[i] != 0) {
        WALOU_ERROR("Unwritten page {} is not zeroed "
                    "(non-zero byte at offset {})",
                    page_id, i);

        std::filesystem::remove(db_file);
        return false;
      }
    }

    WALOU_INFO("Unwritten page {} is correctly zeroed", page_id);

    std::filesystem::remove(db_file);

    return true;

  } catch (const std::exception &e) {
    WALOU_ERROR("ReadUnwrittenPageIsZeroed threw exception: {}", e.what());

    std::filesystem::remove(db_file);
    return false;
  }
}

bool testPersistsAcrossReopen() {
  WALOU_INFO("Test: PersistsAcrossReopen");

  const char *db_file = "test_persistence.db";

  std::filesystem::remove(db_file);

  const char *expected_message = "WalouDB persistence test";

  try {
    // ----------------------------------------
    // First DiskManager
    // ----------------------------------------

    {
      WALOU_INFO("Creating first DiskManager");

      WalouDB::DiskManager disk_manager(db_file);

      WalouDB::Page page;

      std::memset(page.getData(), 0, WalouDB::PAGE_SIZE);

      std::memcpy(page.getData(), expected_message,
                  std::strlen(expected_message) + 1);

      // Write page 0.
      if (!disk_manager.writePage(0, page.getData())) {

        WALOU_ERROR("Failed to write page 0");

        std::filesystem::remove(db_file);
        return false;
      }

      WALOU_INFO("Page 0 written successfully");
    }

    // DiskManager destructor has run here.
    WALOU_INFO("First DiskManager destroyed");

    // ----------------------------------------
    // Second DiskManager
    // ----------------------------------------

    {
      WALOU_INFO("Opening second DiskManager");

      WalouDB::DiskManager disk_manager(db_file);

      WalouDB::Page recovered_page;

      std::memset(recovered_page.getData(), 0, WalouDB::PAGE_SIZE);

      if (!disk_manager.readPage(0, recovered_page.getData())) {

        WALOU_ERROR("Failed to read page 0 after reopening");

        std::filesystem::remove(db_file);
        return false;
      }

      if (std::strcmp(recovered_page.getData(), expected_message) != 0) {

        WALOU_ERROR("Persistence verification failed");

        WALOU_ERROR("Expected: {}", expected_message);

        WALOU_ERROR("Got: {}", recovered_page.getData());

        std::filesystem::remove(db_file);
        return false;
      }

      WALOU_INFO("Persistence verification passed");
    }

    std::filesystem::remove(db_file);

    return true;

  } catch (const std::exception &e) {
    WALOU_ERROR("PersistsAcrossReopen threw exception: {}", e.what());

    std::filesystem::remove(db_file);
    return false;
  }
}

bool testPageAllocation() {
  WALOU_INFO("Test: PageAllocation");

  const char *db_file = "test_allocation.db";

  std::filesystem::remove(db_file);

  try {
    WalouDB::DiskManager disk_manager(db_file);

    const auto page0 = disk_manager.allocatePage();
    const auto page1 = disk_manager.allocatePage();
    const auto page2 = disk_manager.allocatePage();

    WALOU_INFO("Allocated pages: {}, {}, {}", page0, page1, page2);

    if (page0 != 0 || page1 != 1 || page2 != 2) {

      WALOU_ERROR("Page allocation returned unexpected IDs");

      WALOU_ERROR("Expected: 0, 1, 2");

      WALOU_ERROR("Got: {}, {}, {}", page0, page1, page2);

      std::filesystem::remove(db_file);
      return false;
    }

    WALOU_INFO("Page allocation verification passed");

    std::filesystem::remove(db_file);

    return true;

  } catch (const std::exception &e) {
    WALOU_ERROR("PageAllocation threw exception: {}", e.what());

    std::filesystem::remove(db_file);
    return false;
  }
}

} // namespace

int main() {
  WalouDB::Log::Init();

  WALOU_INFO("========================================");
  WALOU_INFO("       WalouDB DiskManager Tests");
  WALOU_INFO("========================================");

  int passed = 0;
  int failed = 0;

  // ----------------------------------------
  // Test 1
  // ----------------------------------------

  if (testReadWriteCycle()) {
    WALOU_INFO("PASS: ReadWriteCycle");
    ++passed;
  } else {
    WALOU_ERROR("FAIL: ReadWriteCycle");
    ++failed;
  }

  // ----------------------------------------
  // Test 2
  // ----------------------------------------

  if (testReadUnwrittenPageIsZeroed()) {
    WALOU_INFO("PASS: ReadUnwrittenPageIsZeroed");
    ++passed;
  } else {
    WALOU_ERROR("FAIL: ReadUnwrittenPageIsZeroed");
    ++failed;
  }

  // ----------------------------------------
  // Test 3
  // ----------------------------------------

  if (testPersistsAcrossReopen()) {
    WALOU_INFO("PASS: PersistsAcrossReopen");
    ++passed;
  } else {
    WALOU_ERROR("FAIL: PersistsAcrossReopen");
    ++failed;
  }

  // ----------------------------------------
  // Test 4
  // ----------------------------------------

  if (testPageAllocation()) {
    WALOU_INFO("PASS: PageAllocation");
    ++passed;
  } else {
    WALOU_ERROR("FAIL: PageAllocation");
    ++failed;
  }

  // ----------------------------------------
  // Final result
  // ----------------------------------------

  WALOU_INFO("========================================");
  WALOU_INFO("Tests passed: {}", passed);
  WALOU_INFO("Tests failed: {}", failed);
  WALOU_INFO("========================================");

  if (failed == 0) {
    WALOU_INFO("ALL TESTS PASSED");

    return 0;
  }

  WALOU_CRITICAL("TEST SUITE FAILED");

  return 1;
}
