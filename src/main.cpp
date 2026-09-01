#include "waloudb/common/Types.h"
#include "waloudb/core/Log.h"
#include "waloudb/storage/DiskManager.h"
#include "waloudb/storage/Page.h"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

int main() {
  WalouDB::Log::Init();

  constexpr const char *DB_FILE = "test.db";

  WALOU_INFO("========================================");
  WALOU_INFO("       WalouDB DiskManager Test");
  WALOU_INFO("========================================");

  // --------------------------------------------------
  // Test configuration
  // --------------------------------------------------

  std::filesystem::remove(DB_FILE);

  WALOU_INFO("Database file : {}", DB_FILE);
  WALOU_INFO("Page size     : {} bytes", WalouDB::PAGE_SIZE);

  // ==================================================
  // PHASE 1: Create database and write a page
  // ==================================================

  WalouDB::page_id_t page_id;

  {
    WALOU_INFO("----------------------------------------");
    WALOU_INFO("PHASE 1: Create and write");
    WALOU_INFO("----------------------------------------");

    WalouDB::DiskManager disk_manager(DB_FILE);

    WALOU_INFO("DiskManager created");

    // Allocate a new page.
    page_id = disk_manager.allocatePage();

    WALOU_INFO("Allocated page ID: {}", page_id);

    // Create an in-memory page.
    WalouDB::Page page;
    page.setPageId(page_id);

    WALOU_INFO("Page object created");
    WALOU_INFO("Page ID: {}", page.getPageId());

    // Fill the page with deterministic data.
    char *data = page.getData();

    const char *message =
        "Hello from WalouDB! "
        "This data was written to disk and should survive reopening.";

    std::memcpy(data, message, std::strlen(message) + 1);

    // Fill the remaining bytes with a predictable pattern.
    for (std::size_t i = std::strlen(message) + 1; i < WalouDB::PAGE_SIZE;
         ++i) {
      data[i] = static_cast<char>(i % 256);
    }

    WALOU_INFO("Page filled with test data");

    std::cout << "\nData before writing:\n";
    std::cout << "----------------------------------------\n";
    std::cout << page.getData() << '\n';
    std::cout << "----------------------------------------\n";

    WALOU_INFO("Writing page {} to disk", page_id);

    disk_manager.writePage(page_id, page.getData());

    WALOU_INFO("Page written successfully");

    // Show file size.
    if (std::filesystem::exists(DB_FILE)) {
      const auto file_size = std::filesystem::file_size(DB_FILE);

      WALOU_INFO("Database file size: {} bytes", file_size);
      WALOU_INFO("Expected minimum size: {} bytes",
                 (static_cast<std::uintmax_t>(page_id) + 1) *
                     WalouDB::PAGE_SIZE);
    }

    WALOU_INFO("Closing DiskManager...");
  }

  // At this point the DiskManager destructor has run.
  // The file should be closed.

  WALOU_INFO("DiskManager closed");
  WALOU_INFO("Database file remains on disk");

  // ==================================================
  // PHASE 2: Reopen database
  // ==================================================

  {
    WALOU_INFO("----------------------------------------");
    WALOU_INFO("PHASE 2: Reopen and read");
    WALOU_INFO("----------------------------------------");

    WALOU_INFO("Creating a NEW DiskManager");

    WalouDB::DiskManager disk_manager(DB_FILE);

    WALOU_INFO("New DiskManager created successfully");

    // Create a fresh Page object.
    WalouDB::Page recovered_page;
    recovered_page.setPageId(page_id);

    WALOU_INFO("Created empty Page object for page {}",
               recovered_page.getPageId());

    // Read the page from disk.
    WALOU_INFO("Reading page {} from disk", page_id);

    disk_manager.readPage(page_id, recovered_page.getData());

    WALOU_INFO("Page read successfully");

    // --------------------------------------------------
    // Print recovered data
    // --------------------------------------------------

    std::cout << "\nRecovered data:\n";
    std::cout << "----------------------------------------\n";
    std::cout << recovered_page.getData() << '\n';
    std::cout << "----------------------------------------\n";

    // --------------------------------------------------
    // Verify the human-readable message
    // --------------------------------------------------

    const char *expected_message =
        "Hello from WalouDB! "
        "This data was written to disk and should survive reopening.";

    if (std::strcmp(recovered_page.getData(), expected_message) == 0) {

      WALOU_INFO("Message verification: PASS");

    } else {

      WALOU_ERROR("Message verification: FAIL");
      return 1;
    }

    // --------------------------------------------------
    // Verify every byte
    // --------------------------------------------------

    WALOU_INFO("Starting byte-for-byte verification...");

    const char *recovered_data = recovered_page.getData();

    const std::size_t message_size = std::strlen(expected_message) + 1;

    bool bytes_match = true;

    for (std::size_t i = 0; i < WalouDB::PAGE_SIZE; ++i) {

      char expected;

      if (i < message_size) {
        expected = expected_message[i];
      } else {
        expected = static_cast<char>(i % 256);
      }

      if (recovered_data[i] != expected) {
        WALOU_ERROR(
            "Byte mismatch at offset {}: expected {}, got {}", i,
            static_cast<int>(static_cast<unsigned char>(expected)),
            static_cast<int>(static_cast<unsigned char>(recovered_data[i])));

        bytes_match = false;
        break;
      }
    }

    if (!bytes_match) {
      WALOU_ERROR("Byte-for-byte verification: FAIL");
      return 1;
    }

    WALOU_INFO("Byte-for-byte verification: PASS ({} bytes)",
               WalouDB::PAGE_SIZE);

    // --------------------------------------------------
    // Display some raw bytes
    // --------------------------------------------------

    std::cout << "\nFirst 32 bytes (hex):\n";
    std::cout << "----------------------------------------\n";

    for (std::size_t i = 0; i < 32; ++i) {
      std::cout << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(
                       static_cast<unsigned char>(recovered_data[i]))
                << ' ';
    }

    std::cout << std::dec << "\n";
    std::cout << "----------------------------------------\n";

    // Last 16 bytes.
    std::cout << "\nLast 16 bytes (hex):\n";
    std::cout << "----------------------------------------\n";

    for (std::size_t i = WalouDB::PAGE_SIZE - 16; i < WalouDB::PAGE_SIZE; ++i) {

      std::cout << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(
                       static_cast<unsigned char>(recovered_data[i]))
                << ' ';
    }

    std::cout << std::dec << "\n";
    std::cout << "----------------------------------------\n";

    WALOU_INFO("All DiskManager tests passed");
  }

  // ==================================================
  // Final result
  // ==================================================

  WALOU_INFO("========================================");
  WALOU_INFO("       TEST RESULT: PASS");
  WALOU_INFO("========================================");

  return 0;
}
