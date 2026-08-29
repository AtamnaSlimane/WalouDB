#pragma once

#include "Row.h"
#include "Value.h"
#include <vector>
namespace WalouDB {

class Row {
public:
  Row(std::vector<Value> values) : m_values(std::move(values)) {}
  const std::vector<Value> &getValues() { return m_values; }
  size_t size() const { return m_values.size(); }

private:
  std::vector<Value> m_values;
};

} // namespace WalouDB
