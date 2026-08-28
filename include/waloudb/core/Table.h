#pragma once

#include <string>
namespace WalouDB {

class Table {

public:
  explicit Table(const std::string &name) : m_name(name) {}

  const std::string &getName() const;

private:
  std::string m_name;
};

} // namespace WalouDB
