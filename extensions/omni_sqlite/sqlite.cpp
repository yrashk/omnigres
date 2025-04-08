#include "sqlite.hpp"

std::size_t sqlite::flat_size() {
  /* Use cached value if already computed */
  if (_flat_size) {
    return _flat_size;
  }

  unsigned char *v = sqlite3_serialize(db.get(), "main", &_flat_size, 0);
  if (v == nullptr) {
    throw std::runtime_error(
        cppgres::fmt::format("Failed to serialize SQLite: {}", sqlite3_errmsg(db.get())));
  }

  return static_cast<std::size_t>(_flat_size);
}

void sqlite::flatten_into(std::span<std::byte> buffer) {
  auto ptr = sqlite3_serialize(db.get(), "main", &_flat_size, 0);
  if (ptr == nullptr) {
    throw std::runtime_error(
        cppgres::fmt::format("Failed to serialize SQLite: {}", sqlite3_errmsg(db.get())));
  }
  std::span<std::byte> bytes(reinterpret_cast<std::byte *>(ptr),
                             static_cast<std::size_t>(_flat_size));
  std::copy(bytes.begin(), bytes.end(), buffer.begin());
}

cppgres::type sqlite::type() { return cppgres::named_type("omni_sqlite", "sqlite"); }

sqlite sqlite::restore_from(std::span<std::byte> buffer) {
  sqlite sql;
  std::byte *sqlite3_alloc = reinterpret_cast<std::byte *>(sqlite3_malloc64(buffer.size_bytes()));
  std::copy(buffer.begin(), buffer.end(), sqlite3_alloc);
  if (sqlite3_deserialize(sql, "main", reinterpret_cast<unsigned char *>(sqlite3_alloc),
                          buffer.size_bytes(), buffer.size_bytes(),
                          SQLITE_DESERIALIZE_RESIZEABLE | SQLITE_DESERIALIZE_FREEONCLOSE) !=
      SQLITE_OK) {
    throw std::runtime_error(
        cppgres::fmt::format("can't deserialize SQLite: {}", sqlite3_errmsg(sql)));
  }
  return sql;
}

sqlite::sqlite()
    : db([]() {
        sqlite3 *db;
        if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
          throw std::runtime_error(
              cppgres::fmt::format("can't create a new SQLite database: {}", sqlite3_errmsg(db)));
        }
        if (sqlite3_create_module(db, "postgres", &postgres_module, 0) != SQLITE_OK) {
          throw std::runtime_error(
              cppgres::fmt::format("can't register Postgres module: {}", sqlite3_errmsg(db)));
        }
        return std::shared_ptr<sqlite3>(db, sqlite3_close);
      }()) {}

sqlite::operator sqlite3 *() const { return db.get(); }

sqlite3_module postgres_module = {
    .iVersion = 4,
    .xCreate =
        [](sqlite3 *db, void *pAux, int argc, const char *const *argv, sqlite3_vtab **ppVTab,
           char **pzErr) {
          auto vtab_ptr = cppgres::memory_context().alloc<postgres_vtab>();
          auto vtab = new (vtab_ptr)
              postgres_vtab(db, argv[2], std::string_view(argv[3] + 1, strlen(argv[3]) - 2));
          *ppVTab = (sqlite3_vtab *)vtab;
          return SQLITE_OK;
        },
    .xConnect =
        [](sqlite3 *db, void *pAux, int argc, const char *const *argv, sqlite3_vtab **ppVTab,
           char **pzErr) { return postgres_module.xCreate(db, pAux, argc, argv, ppVTab, pzErr); },
    .xBestIndex = [](sqlite3_vtab *pVTab, sqlite3_index_info *) { return SQLITE_OK; },
    .xDisconnect = [](sqlite3_vtab *pVTab) { return SQLITE_OK; },
    .xDestroy = [](sqlite3_vtab *pVTab) { return SQLITE_OK; },
    .xOpen = [](sqlite3_vtab *pVTab, sqlite3_vtab_cursor **ppCursor) { return SQLITE_OK; },
    .xClose = [](sqlite3_vtab_cursor *) { return SQLITE_OK; },
    .xFilter = [](sqlite3_vtab_cursor *, int idxNum, const char *idxStr, int argc,
                  sqlite3_value **argv) { return SQLITE_OK; },
    .xNext = [](sqlite3_vtab_cursor *) { return SQLITE_OK; },
    .xEof = [](sqlite3_vtab_cursor *) { return 0; },
    .xColumn = [](sqlite3_vtab_cursor *, sqlite3_context *, int) { return SQLITE_OK; },
    /*
         .xRowid = ,
         .xUpdate = ,
         .xBegin = ,
         .xSync = ,
         .xCommit = ,
         .xRollback = ,
         .xFindFunction = ,
         .xRename = ,
         .xSavepoint = ,
         .xRelease = ,
         .xRollbackTo = nullptr,
         .xShadowName = nullptr,
         .xIntegrity = nullptr*/
};
