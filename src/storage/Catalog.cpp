#include "waloudb/storage/Catalog.h"
#include "waloudb/storage/Tuple.h"
#include "waloudb/storage/Value.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace WalouDB {

// ============================================================
// Fixed schema for the catalog's own rows — NOT the user's table
// schema. Every catalog row looks like this, regardless of what
// tables it describes.
// ============================================================
Schema Catalog::catalogSchema() {
  return Schema({
      {"table_id", TypeId::INTEGER},
      {"name", TypeId::VARCHAR},
      {"first_page_id", TypeId::INTEGER},
      {"columns_blob",
       TypeId::VARCHAR}, // manually packed column list, see below
  });
}

// ============================================================
// Column list <-> blob encoding
//
// Format: [count: uint32]
//         repeated `count` times: [name_len: uint32][name bytes][type: uint8]
//
// This is separate from Tuple/Schema's own (de)serialization because
// Schema doesn't support a nested, variable-length list of columns
// as a single typed value — so we hand-roll a small binary format
// and store the whole thing as one VARCHAR.
// ============================================================
//
std::string Catalog::encodeColumns(const std::vector<Column> &columns) {
  std::string blob;
  uint32_t count = static_cast<uint32_t>(columns.size());
  blob.append(reinterpret_cast<const char *>(&count), sizeof(count));

  for (const Column &col : columns) {
    uint32_t name_len = static_cast<uint32_t>(col.name.size());
    blob.append(reinterpret_cast<const char *>(&name_len), sizeof(name_len));
    blob.append(col.name.data(), name_len);
    uint8_t type = static_cast<uint8_t>(col.type);
    blob.append(reinterpret_cast<const char *>(&type), sizeof(type));
  }
  return blob;
}

std::vector<Column> Catalog::decodeColumns(const std::string &blob) {
  std::vector<Column> columns;
  size_t offset = 0;

  uint32_t count;
  std::memcpy(&count, blob.data() + offset, sizeof(count));
  offset += sizeof(count);

  for (uint32_t i = 0; i < count; ++i) {
    uint32_t name_len;
    std::memcpy(&name_len, blob.data() + offset, sizeof(name_len));
    offset += sizeof(name_len);

    std::string name(blob.data() + offset, name_len);
    offset += name_len;

    uint8_t type;
    std::memcpy(&type, blob.data() + offset, sizeof(type));
    offset += sizeof(type);

    columns.push_back(Column{name, static_cast<TypeId>(type)});
  }
  return columns;
}

// ============================================================
// Construction — MUST run before any TableHeap claims page 0.
// If page 0 doesn't exist yet on disk, this creates it and
// initializes it as an empty catalog page. Either way, the
// in-memory map is then rebuilt entirely from what's on disk —
// never assumed, always recomputed.
// ============================================================
Catalog::Catalog(BufferPoolManager *bpm) : m_bpm(bpm) {
  Page *probe = bpm->fetchPage(CATALOG_PAGE_ID);
  bool needs_init = true;

  if (probe != nullptr) {
    SlottedPage sp(probe->getData());
    needs_init = (sp.getLower() == 0);
    bpm->unpinPage(CATALOG_PAGE_ID, false);
  }

  if (needs_init) {
    page_id_t allocated;
    Page *page = bpm->newPage(&allocated);
    if (page == nullptr) {
      throw std::runtime_error(
          "Catalog: could not allocate page 0 — buffer pool exhausted");
    }
    if (allocated != CATALOG_PAGE_ID) {
      throw std::runtime_error(
          "Catalog: page 0 was already claimed by something else — "
          "Catalog must be constructed before any TableHeap.");
    }
    SlottedPage sp(page->getData());
    sp.Init(CATALOG_PAGE_ID);
    bpm->unpinPage(CATALOG_PAGE_ID, true);
  }

  m_catalog_heap = std::make_unique<TableHeap>(bpm, CATALOG_PAGE_ID);
  loadFromDisk();
}

void Catalog::loadFromDisk() {
  m_tables.clear();
  m_next_table_id = 0;

  for (auto it = m_catalog_heap->begin(); it != m_catalog_heap->end(); ++it) {
    Tuple row = *it;
    Schema schema = catalogSchema();

    TableMetadata meta;
    meta.table_id = static_cast<uint32_t>(row.getValue(schema, 0).getInteger());
    meta.name = row.getValue(schema, 1).getString();
    meta.first_page_id = row.getValue(schema, 2).getInteger();
    meta.columns = decodeColumns(row.getValue(schema, 3).getString());

    m_tables[meta.name] = meta;
    m_next_table_id = std::max<uint32_t>(m_next_table_id, meta.table_id + 1);
  }
}

void Catalog::persistEntry(const TableMetadata &meta) {
  Schema schema = catalogSchema();
  std::string blob = encodeColumns(meta.columns);

  Tuple row = Tuple::Serialize(
      {
          Value(static_cast<int32_t>(meta.table_id)),
          Value(meta.name),
          Value(static_cast<int32_t>(meta.first_page_id)),
          Value(blob),
      },
      schema);

  RID rid;
  bool ok = m_catalog_heap->insertTuple(row, &rid);
  if (!ok) {
    throw std::runtime_error("Catalog: failed to persist entry for table '" +
                             meta.name + "'");
  }
}

// ============================================================
// createTable — allocates the table's own storage (a fresh
// TableHeap), then durably records it in the catalog BEFORE
// updating the in-memory cache. If persistEntry throws, the
// cache is never touched, so it can't drift from disk truth.
// ============================================================
TableMetadata *Catalog::createTable(const std::string &name,
                                    const Schema &schema) {
  if (m_tables.count(name)) {
    return nullptr; // already exists
  }

  TableHeap new_table(m_bpm); // allocates the table's real first page

  TableMetadata meta;
  meta.table_id = m_next_table_id++;
  meta.name = name;
  meta.columns.reserve(schema.getColumnCount());
  for (size_t i = 0; i < schema.getColumnCount(); ++i) {
    meta.columns.push_back(schema.getColumn(i));
  }
  meta.first_page_id = new_table.getFirstPageId();

  persistEntry(meta); // disk first

  auto [it, inserted] = m_tables.emplace(name, std::move(meta));
  return &it->second;
}

TableMetadata *Catalog::getTable(const std::string &name) {
  auto it = m_tables.find(name);
  if (it == m_tables.end())
    return nullptr;
  return &it->second;
}

// ============================================================
// dropTable — NOTE: this only removes the catalog's *record* of
// the table. It does NOT reclaim the table's own pages (no
// free-page-list mechanism exists yet — see the TableHeap
// discussion on why deleted pages currently just sit unused).
// Also: this doesn't yet remove the row from m_catalog_heap's
// underlying storage, only from the in-memory cache — a real
// implementation needs a way to tombstone/rewrite that specific
// catalog row too. Flagging honestly rather than pretending this
// is complete.
// ============================================================
bool Catalog::dropTable(const std::string &name) {
  return m_tables.erase(name) > 0;
}

} // namespace WalouDB
