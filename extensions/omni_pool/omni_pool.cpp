#include "pool.hpp"

#include <cppgres.hpp>

extern "C" {
PG_MODULE_MAGIC;
#include "commands/dbcommands.h"
#include <omni/omni_v0.h>
OMNI_MAGIC;

OMNI_MODULE_INFO(.name = "omni_pool", .version = EXT_VERSION,
                 .identity = "60d8114c-bce4-4e72-a278-694067a9ffb5");
}

struct sql_message {
  bip::offset_ptr<char> stmt;
};

static const omni_handle *backend_handle = nullptr;

postgres_function(test, ([](std::string s) {
                    omni_pool_handler<sql_message> handler(
                        "sql", backend_handle->get_library_name(backend_handle), "sql_handler");
                    omni_pool pool;
                    auto stmt = static_cast<char *>(
                        pool.operator bip::managed_shared_memory &().allocate(s.size() + 1));
                    std::fill_n(stmt, s.size() + 1, 0);
                    std::copy(s.begin(), s.end(), stmt);
                    pool.send(handler, sql_message{.stmt = stmt});
                  }));

namespace cppgres {
struct background_worker {

  background_worker() {}

  background_worker &name(std::string_view name) {
    size_t n = std::min(name.size(), static_cast<size_t>(sizeof(worker.bgw_name) - 1));
    std::copy_n(name.data(), n, worker.bgw_name);
    worker.bgw_name[n] = '\0';
    return *this;
  }
  std::string_view name() { return worker.bgw_name; }

  background_worker &type(std::string_view name) {
    size_t n = std::min(name.size(), static_cast<size_t>(sizeof(worker.bgw_type) - 1));
    std::copy_n(name.data(), n, worker.bgw_type);
    worker.bgw_type[n] = '\0';
    return *this;
  }
  std::string_view type() { return worker.bgw_type; }

  background_worker &library_name(std::string_view name) {
    size_t n = std::min(name.size(), static_cast<size_t>(sizeof(worker.bgw_library_name) - 1));
    std::copy_n(name.data(), n, worker.bgw_library_name);
    worker.bgw_library_name[n] = '\0';
    return *this;
  }
  std::string_view library_name() { return worker.bgw_library_name; }

  background_worker &function_name(std::string_view name) {
    size_t n = std::min(name.size(), static_cast<size_t>(sizeof(worker.bgw_function_name) - 1));
    std::copy_n(name.data(), n, worker.bgw_function_name);
    worker.bgw_function_name[n] = '\0';
    return *this;
  }
  std::string_view function_name() { return worker.bgw_function_name; }

  background_worker &start_time(BgWorkerStartTime time) {
    worker.bgw_start_time = time;
    return *this;
  }

  BgWorkerStartTime start_time() { return worker.bgw_start_time; }

  background_worker &restart_time(int time) {
    worker.bgw_restart_time = time;
    return *this;
  }

  int restart_time() { return worker.bgw_restart_time; }

  background_worker &flags(int flags) {
    worker.bgw_flags = flags;
    return *this;
  }

  int flags() { return worker.bgw_flags; }

  background_worker &main_arg(datum datum) {
    worker.bgw_main_arg = datum;
    return *this;
  }

  datum main_arg() { return datum(worker.bgw_main_arg); }

  background_worker &extra(std::string_view name) {
    size_t n = std::min(name.size(), static_cast<size_t>(sizeof(worker.bgw_extra) - 1));
    std::copy_n(name.data(), n, worker.bgw_extra);
    worker.bgw_extra[n] = '\0';
    return *this;
  }
  std::string_view extra() { return worker.bgw_extra; }

  background_worker &notify_pid(pid_t pid) {
    worker.bgw_notify_pid = pid;
    return *this;
  }

  pid_t notify_pid() { return worker.bgw_notify_pid; }

  operator BackgroundWorker &() { return worker; }
  operator BackgroundWorker *() { return &worker; }

private:
  BackgroundWorker worker = {0};
};

struct transaction {
  transaction(bool commit = true) : commit(commit) {
    ffi_guard([]() {
      if (!::IsTransactionState()) {
        ::SetCurrentStatementStartTimestamp();
        ::StartTransactionCommand();
        ::PushActiveSnapshot(::GetTransactionSnapshot());
      }
    })();
  }

  ~transaction() {
    ffi_guard([this]() {
      ::PopActiveSnapshot();
      if (commit) {
        ::CommitTransactionCommand();
      } else {
        ::AbortCurrentTransaction();
      }
    })();
  }

private:
  bool commit;
};

} // namespace cppgres

static void worker(cppgres::datum main) {
  cppgres::ffi_guard{::BackgroundWorkerUnblockSignals}();
  cppgres::ffi_guard{::BackgroundWorkerInitializeConnectionByOid}(
      cppgres::datum_conversion<cppgres::oid>::from_datum(main, std::nullopt), InvalidOid, 0);

  cppgres::report(LOG, "pool");
  omni_pool_handler<sql_message> handler("sql", backend_handle->get_library_name(backend_handle),
                                         "sql_handler");
  omni_pool pool;
  pool.register_handler(handler);
  while (pool.receive()) {
    CHECK_FOR_INTERRUPTS();
  }
}

extern "C" {

void sql_handler(sql_message *msg) {
  cppgres::exception_guard([msg]() {
    cppgres::transaction tx;

    cppgres::spi_executor spi;
    try {
      spi.execute(std::format("{}", msg->stmt.get()));
    } catch (std::exception &e) {
      cppgres::report(WARNING, "%s", e.what());
    }
  })();
}

void omni_pool_worker(::Datum main) { cppgres::exception_guard{worker}(cppgres::datum(main)); }

void _Omni_init(const omni_handle *handle) {
  cppgres::exception_guard([&]() {
    backend_handle = handle;
    bool worker_bgw_found;
    handle->allocate_shmem(
        handle, cppgres::fmt::format("omni_pool_worker_{}", MyDatabaseId).c_str(), sizeof(void *),
        [](const omni_handle *handle, void *ptr, void *arg, bool allocated) {
          if (allocated) {
            auto bgw =
                cppgres::background_worker()
                    .name(cppgres::fmt::format(
                        "omni_pool {}", cppgres::ffi_guard{::get_database_name}(MyDatabaseId)))
                    .type("omni_pool")
                    .library_name(handle->get_library_name(handle))
                    .function_name("omni_pool_worker")
                    .main_arg(cppgres::datum_conversion<cppgres::oid>::into_datum(MyDatabaseId))
                    .flags(BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION)
                    .notify_pid(MyProcPid)
                    .start_time(BgWorkerStart_RecoveryFinished);
            omni_bgworker_handle bgw_handle;
            handle->request_bgworker_start(handle, bgw, &bgw_handle,
                                           {.timing = omni_timing_after_commit});
          }
        },
        nullptr, &worker_bgw_found);
  })();
}
}
