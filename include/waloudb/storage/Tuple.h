#pragma once
#include "Schema.h"
#include "Value.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// data stored at the end of a slottedpage
class Tuple {
public:
  Tuple() = default;

  static Tuple Serialize(const std::vector<Value> &values,
                         const Schema &schema) {
    Tuple t;
    for (size_t i = 0; i < schema.getColumnCount(); ++i) {
      const Column &col = schema.getColumn(i);
      const Value &v = values[i];
      if (col.type == TypeId::INTEGER) {
        int32_t iv = v.getInteger();
        t.append(reinterpret_cast<const char *>(&iv), sizeof(int32_t));
      } else { // VARCHAR: length-prefixed
        uint32_t len = static_cast<uint32_t>(v.getString().size());
        t.append(reinterpret_cast<const char *>(&len), sizeof(uint32_t));
        t.append(v.getString().data(), len);
      }
    }
    return t;
  }

  // Wrap raw bytes read back from a TablePage (copies them out — the
  // SlottedPage's buffer may be evicted/overwritten later)
  static Tuple fromRawData(const char *data, uint16_t len) {
    Tuple t;
    t.m_data.assign(data, data + len);
    return t;
  }

  Value getValue(const Schema &schema, size_t col_idx) const {
    size_t offset = 0;
    for (size_t i = 0; i <= col_idx; ++i) {
      const Column &col = schema.getColumn(i);
      if (col.type == TypeId::INTEGER) {
        if (i == col_idx) {
          int32_t v;
          std::memcpy(&v, m_data.data() + offset, sizeof(int32_t));
          return Value(v);
        }
        offset += sizeof(int32_t);
      } else { // VARCHAR
        uint32_t len;
        std::memcpy(&len, m_data.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        if (i == col_idx) {
          return Value(std::string(m_data.data() + offset, len));
        }
        offset += len;
      }
    }
    return Value(); // unreachable if col_idx is valid
  }

  const char *getData() const { return m_data.data(); }
  uint16_t getLength() const { return static_cast<uint16_t>(m_data.size()); }

private:
  void append(const char *bytes, size_t len) {
    m_data.insert(m_data.end(), bytes, bytes + len);
  }
  std::vector<char> m_data;
};
