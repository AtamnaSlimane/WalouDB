#include "waloudb/core/Column.h"
#include "waloudb/core/ColumnType.h"
#include <string>

namespace WalouDB {
const std::string &Column::getName() const { return this->m_name; }

ColumnType Column::getType() const { return this->m_type; }
} // namespace WalouDB
