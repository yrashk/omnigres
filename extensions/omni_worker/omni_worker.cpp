// #define BOOST_ASIO_ENABLE_HANDLER_TRACKING

#include <any>
#include <map>

#include "oink.hpp"

#include <boost/asio.hpp>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>

extern "C" {
#include <dlfcn.h>
}
#include <boost/container/string.hpp>
#include <boost/function.hpp>

#include "io_worker.hpp"

#include <cppgres.hpp>

extern "C" {
PG_MODULE_MAGIC;
#include "commands/dbcommands.h"
#include <omni/omni_v0.h>
OMNI_MAGIC;

OMNI_MODULE_INFO(.name = "omni_worker", .version = EXT_VERSION,
                 .identity = "60d8114c-bce4-4e72-a278-694067a9ffb5");
}

#include "omni_worker.h"

static const omni_handle *backend_handle = nullptr;

template <class... Ts> struct overload : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overload(Ts...) -> overload<Ts...>;

struct io_handler {
  using signature = void(boost::asio::io_context &io_ctx);
  io_handler(const std::string &library, const std::string &name)
      : dl(dlopen(library.c_str(), RTLD_LAZY), dlclose),
        ptr(reinterpret_cast<void *(*)(const char *)>(
            dlsym(dl.get(), "omni_io_worker_handler_init"))(name.c_str())),
        handler_(reinterpret_cast<signature *>(ptr)) {}

  void operator()(boost::asio::io_context &io_ctx) { handler_(io_ctx); }

private:
  std::shared_ptr<void> dl;

public:
  void *ptr;
  boost::function<signature> handler_;
};

template <typename T>
concept message = oink::message<T> || std::same_as<T, std::any>;

template <message T> struct omni_pool_handler {
  using signature = bool(const T *, omni_worker::handle *);
  omni_pool_handler(const std::string &library, const std::string &name)
      : dl(dlopen(library.c_str(), RTLD_LAZY), dlclose),
        ptr(reinterpret_cast<void *(*)(const char *, std::size_t *hash)>(
            dlsym(dl.get(), "omni_worker_handler"))(name.c_str(), &hash)),
        handler(reinterpret_cast<signature *>(ptr)) {}

  bool operator()(const T &t, omni_worker::handle &handle) { return handler(t, handle); }

private:
  std::shared_ptr<void> dl;

public:
  void *ptr;
  boost::function<signature> handler;
  std::size_t hash;
};

std::string arena_name() {
  return cppgres::fmt::format("omni_worker_v1_{}_{}", cppgres::ffi_guard{::GetSystemIdentifier}(),
                              MyDatabaseId);
}
std::size_t arena_size() { return 1024 * 1024; }
std::string mq_name() {
  return cppgres::fmt::format("omni_worker_v1_{}_{}_mq",
                              cppgres::ffi_guard{::GetSystemIdentifier}(), MyDatabaseId);
}

namespace bc = boost::container;
namespace bip = boost::interprocess;

struct reload {
  static const char *name() { return "omni_worker:reload"; }
};

struct reload_io {
  static const char *name() { return "omni_worker:reload_io"; }
};

static bool reload_upon_commit = false;
static bool reload_io_upon_commit = false;

postgres_function(reload_handlers, ([]() -> cppgres::value {
                    if (CALLED_AS_TRIGGER(
                            cppgres::current_postgres_function::call_info().value())) {
                      reload_upon_commit = true;

                      return cppgres::value(cppgres::nullable_datum(0),
                                            cppgres::type{.oid = TRIGGEROID});
                    }
                    throw std::runtime_error("must be called as a trigger");
                  }));

postgres_function(reload_io_handlers, ([]() -> cppgres::value {
                    if (CALLED_AS_TRIGGER(
                            cppgres::current_postgres_function::call_info().value())) {
                      reload_io_upon_commit = true;

                      return cppgres::value(cppgres::nullable_datum(0),
                                            cppgres::type{.oid = TRIGGEROID});
                    }
                    throw std::runtime_error("must be called as a trigger");
                  }));

struct background_workers_handle {
  std::atomic<bool> io_worker_started;
  background_workers_handle() : io_worker_started(false) {}
};

static void worker(cppgres::datum main) {
  cppgres::current_background_worker bgw;

  bgw.connect(cppgres::from_nullable_datum<cppgres::oid>(cppgres::nullable_datum(main), OIDOID));
  bgw.unblock_signals();

  bool bgw_handle_found;
  background_workers_handle *bgw_handle =
      reinterpret_cast<background_workers_handle *>(backend_handle->lookup_shmem(
          backend_handle, cppgres::fmt::format("omni_pool_worker_{}", MyDatabaseId).c_str(),
          &bgw_handle_found));

  bool io_worker_owner = false;

  auto const threads = 2 /* FIXME: hard-coded for now */;
  boost::asio::io_context ioc{threads};
  auto work_guard = boost::asio::make_work_guard(ioc);

  std::vector<std::thread> ioc_threads;
  ioc_threads.reserve(threads);
  if (bgw_handle_found) {
    if (!bgw_handle->io_worker_started.exchange(true)) {
      io_worker_owner = true;
      cppgres::report(LOG, "Starting %d I/O worker threads (omni_worker)", threads);
      for (auto i = 0; i < threads; i++) {
        ioc_threads.emplace_back([&ioc] {
          while (!ioc.stopped()) {
            try {
              ioc.run();
            } catch (const std::exception &e) {
              std::cout << "run() threw: " << e.what() << std::endl;
            }
          }
          std::cout << "end thread" << std::endl;
        });
      }
    }
  }
  auto &worker_service = boost::asio::make_service<io_worker_service>(ioc, io_worker_owner);

  std::map<std::uint64_t, omni_pool_handler<std::any>> handlers;
  struct handler {
    std::string library;
    std::string name;
  };
  auto do_reload = [&]() {
    handlers.clear();
    cppgres::transaction tx(false);
    cppgres::spi_executor spi;
    for (const auto &h : spi.query<handler>("select library, name from omni_worker.handlers")) {
      omni_pool_handler<std::any> hndl(h.library, h.name);
      handlers.insert({hndl.hash, std::move(hndl)});
    }
  };
  do_reload();

  auto do_reload_io = [&]() {
    //    cppgres::transaction tx(false);
    //    cppgres::spi_executor spi;
    //    for (const auto &h : spi.query<handler>("select library, name from
    //    omni_worker.io_handlers")) {
    //      io_handler hndl(h.library, h.name);
    //      hndl(ioc);
    //    }
  };
  if (io_worker_owner) {
    do_reload_io();
  }

  oink::arena arena(arena_name().c_str(), arena_size());
  oink::receiver rcvr(arena, mq_name().c_str(), 8192);
  cppgres::worker worker;

  std::thread receiver([&]() {
    while (true) {
      rcvr.receive<reload, reload_io>(
          overload{[&](reload &) {
                     cppgres::report(LOG, "Reloading omni_worker handlers");
                     do_reload();
                   },
                   [&](reload_io &) {
                     if (io_worker_owner) {
                       cppgres::report(LOG, "Reloading omni_worker I/O handlers");
                       do_reload_io();
                       return true;
                     }
                     return false;
                   },
                   [&](oink::endpoint::msg &msg) {
                     auto it = handlers.find(msg.hash);
                     omni_worker::handle handle{.io_ctx = ioc};
                     if (it != handlers.end()) {
                       return worker
                           .post([&]() {
                             return reinterpret_cast<bool (*)(const void *, omni_worker::handle *)>(
                                 it->second.ptr)(
                                 static_cast<char *>(arena.get_address()) + msg.offset, &handle);
                           })
                           .get();
                     }
                     // if there's no consumer for it, drop it. FIXME: or not?
                     return true;
                   }});
    }
  });

  worker.run();
}

extern "C" {

void omni_worker_bgw(::Datum main) { cppgres::exception_guard{worker}(cppgres::datum(main)); }

void _Omni_init(const omni_handle *handle) {
  cppgres::exception_guard([&]() {
    backend_handle = handle;
    bool worker_bgw_found;
    handle->allocate_shmem(
        handle, cppgres::fmt::format("omni_pool_worker_{}", MyDatabaseId).c_str(),
        sizeof(background_workers_handle),
        [](const omni_handle *handle, void *ptr, void *arg, bool allocated) {
          if (allocated) {
            std::construct_at(reinterpret_cast<background_workers_handle *>(ptr));
            int default_num_workers = 2;
#ifdef _SC_NPROCESSORS_ONLN
            default_num_workers = sysconf(_SC_NPROCESSORS_ONLN);
#endif
            omni_guc_variable guc_num_workers = {
                .name = "omni_worker.workers",
                .long_desc = "Number of omni_worker workers",
                .type = PGC_INT,
                .typed = {.int_val = {.boot_value = default_num_workers,
                                      .min_value = 0,
                                      .max_value = INT_MAX}},
                .context = PGC_SIGHUP};
            handle->declare_guc_variable(handle, &guc_num_workers);

            for (int i = 0; i < *guc_num_workers.typed.int_val.value; i++) {
              auto bgw =
                  cppgres::background_worker()
                      .name(cppgres::fmt::format(
                          "omni_worker #{} [{}]", i,
                          cppgres::ffi_guard{::get_database_name}(MyDatabaseId)))
                      .type("omni_worker")
                      .library_name(handle->get_library_name(handle))
                      .function_name("omni_worker_bgw")
                      .main_arg(cppgres::datum_conversion<cppgres::oid>::into_datum(MyDatabaseId))
                      .flags(BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION)
                      .notify_pid(MyProcPid)
                      .start_time(BgWorkerStart_RecoveryFinished);
              omni_bgworker_handle bgw_handle;
              handle->request_bgworker_start(handle, bgw, &bgw_handle,
                                             {.timing = omni_timing_after_commit});
            }
          }
        },
        nullptr, &worker_bgw_found);
    omni_hook txn_hook = {
        .type = omni_hook_xact_callback,
        .fn = {.xact_callback =
                   [](omni_hook_handle *handle, XactEvent event) {
                     if (event == XACT_EVENT_COMMIT && reload_upon_commit) {
                       oink::arena arena(arena_name().c_str(), arena_size());
                       oink::sender snd(arena, mq_name().c_str(), 8192);
                       snd.send<reload>();
                       reload_upon_commit = false;
                     }
                     if (event == XACT_EVENT_COMMIT && reload_io_upon_commit) {
                       oink::arena arena(arena_name().c_str(), arena_size());
                       oink::sender snd(arena, mq_name().c_str(), 8192);
                       snd.send<reload_io>();
                       reload_io_upon_commit = false;
                     }
                   }},
        .name = "omni_worker transaction hook",
    };
    handle->register_hook(handle, &txn_hook);
  })();
}
}
