#pragma once

#include "waloudb/common/Types.h"
#include "waloudb/storage/BufferPoolManager.h"
#include "waloudb/storage/Schema.h"
#include "waloudb/storage/TableHeap.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
namespace WalouDB {

struct TableMetadata {
  uint32_t table_id;
  std::string name;
  std::vector<Column> columns;
  page_id_t first_page_id;
};
class Catalog {
public:
  explicit Catalog(BufferPoolManager *bpm);
  TableMetadata *createTable(const std::string &table_name,
                             const Schema &schema);

  const TableMetadata *createTable(const std::string &table_name) const;

  bool dropTable(const std::string &table_name);
  bool hasTable(std::string &table_name);
  TableMetadata *getTable(const std::string &name);

private:
  static constexpr page_id_t CATALOG_PAGE_ID = 0;
  void loadFromDisk();
  void persistEntry(const TableMetadata &meta);

  static Schema catalogSchema();
  static std::string encodeColumns(const std::vector<Column> &columns);
  static std::vector<Column> decodeColumns(const std::string &blob);

  BufferPoolManager *m_bpm;
  std::unique_ptr<TableHeap> m_catalog_heap;

  std::unordered_map<std::string, TableMetadata> m_tables;

  page_id_t m_next_table_id{0};
};

} // namespace WalouDB
