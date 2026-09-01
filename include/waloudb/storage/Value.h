#pragma once
#include <cstdint>

enum class TypeId : uint8_t { INVALID, INTEGER, VARCHAR };

#include <string>
class Value {
public:
  Value() = default;
  explicit Value(int32_t v) : m_type(TypeId::INTEGER), m_int_val(v) {}
  explicit Value(std::string v)
      : m_type(TypeId::VARCHAR), m_str_val(std::move(v)) {}

  TypeId getType() const { return m_type; }
  int32_t getInteger() const { return m_int_val; }
  const std::string &getString() const { return m_str_val; }

private:
  TypeId m_type{TypeId::INVALID};
  int32_t m_int_val{0};
  std::string m_str_val;
};
