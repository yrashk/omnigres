/*
 * This work is inspired by postgres-sqlite by Michel Pelletier, licensed under BSD 3-Clause
 * License, but is a significant rewrite in a different language (C -> C++).
 */

#ifndef OMNI_SQLITE_H
#define OMNI_SQLITE_H

extern "C" {
#include <sqlite3.h>
}

#ifdef __cplusplus

#include <cppgres.hpp>

struct sqlite {

  sqlite();

  operator sqlite3 *() const;

  std::size_t flat_size();

  void flatten_into(std::span<std::byte> buffer);
  static cppgres::type type();

  static sqlite restore_from(std::span<std::byte> buffer);

private:
  std::shared_ptr<sqlite3> db;
  sqlite3_int64 _flat_size = 0;
};
#endif

extern "C" {
int sqlite3_db_dump(sqlite3 *db, const char *zSchema, const char *zTable,
                    int (*xCallback)(const char *, void *), void *pArg);
}

struct postgres_vtab {
  postgres_vtab(sqlite3 *db, std::string_view table_name, std::string_view query) {
    cppgres::spi_executor spi;
    auto plan = spi.plan(query);
    auto res =
        spi.query<std::vector<cppgres::value>>(plan, cppgres::spi_executor::options(true, 0));
    auto td = res.get_tuple_descriptor();

    std::string table_def = cppgres::fmt::format("create table {} (", table_name);
    for (int i = 0; i < td.attributes(); i++) {
      names.push_back(std::string(td.get_name(i)));
      types.push_back(td.get_type(i));
      table_def.append(td.get_name(i));
      table_def.append(" ");
      table_def.append(" text");
      if (i < td.attributes() - 1) {
        table_def.append(", ");
      }
    }
    table_def.append(")");
    sqlite3_declare_vtab(db, table_def.c_str());
    cppgres::report(NOTICE, "%s", table_def.c_str());
  }

private:
  sqlite3_vtab base = {0};
  std::vector<cppgres::type> types;
  std::vector<std::string> names;
};

extern sqlite3_module postgres_module;

#endif /* OMNI_SQLITE_H */
