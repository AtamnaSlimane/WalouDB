#pragma once
#include <cstdint>
#include <memory.h>
namespace WalouDB {

static constexpr size_t PAGE_SIZE = 4096;
using page_id_t = int32_t;   // disk
using frame_id_t = uint32_t; // ram
static constexpr page_id_t INVALID_PAGE_ID = -1;

} // namespace WalouDB
