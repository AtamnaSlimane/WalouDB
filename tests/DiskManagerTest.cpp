#include "waloudb/storage/DiskManager.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>

using namespace WalouDB;

void testReadWriteCycle() {
  std::remove("test.db");

  DiskManager dm("test.db");

  char write_buf[PAGE_SIZE];
  char read_buf[PAGE_SIZE];

  std::memset(write_buf, 'A', PAGE_SIZE);
  std::memset(read_buf, 0, PAGE_SIZE);

  page_id_t pid = dm.allocatePage();

  assert(dm.writePage(pid, write_buf));
  assert(dm.readPage(pid, read_buf));

  assert(std::memcmp(write_buf, read_buf, PAGE_SIZE) == 0);

  std::remove("test.db");

  std::cout << "testReadWriteCycle: PASSED\n";
}

void testReadUnwrittenPageIsZeroed() {
  std::remove("test2.db");

  DiskManager dm("test2.db");

  page_id_t pid = dm.allocatePage();

  char buf[PAGE_SIZE];
  std::memset(buf, 0xFF, PAGE_SIZE);

  assert(dm.readPage(pid, buf));

  for (char c : buf) {
    assert(c == 0);
  }

  std::remove("test2.db");

  std::cout << "testReadUnwrittenPageIsZeroed: PASSED\n";
}

void testPersistsAcrossReopen() {
  std::remove("test3.db");

  {
    DiskManager dm("test3.db");

    char buf[PAGE_SIZE];
    std::memset(buf, 'Z', PAGE_SIZE);

    assert(dm.writePage(0, buf));
  }

  {
    DiskManager dm("test3.db");

    char buf[PAGE_SIZE];
    std::memset(buf, 0, PAGE_SIZE);

    assert(dm.readPage(0, buf));

    assert(buf[0] == 'Z');

    // Better: verify the entire page.
    for (char c : buf) {
      assert(c == 'Z');
    }
  }

  std::remove("test3.db");

  std::cout << "testPersistsAcrossReopen: PASSED\n";
}

int main() {
  testReadWriteCycle();
  testReadUnwrittenPageIsZeroed();
  testPersistsAcrossReopen();

  std::cout << "\nAll tests passed!\n";

  return 0;
}
