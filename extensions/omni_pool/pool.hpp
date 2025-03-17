#pragma once

#define BOOST_DATE_TIME_NO_LIB
#include <boost/container/string.hpp>
#include <boost/container/vector.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>

#include <map>

namespace bip = boost::interprocess;
namespace bc = boost::container;

extern "C" {
#include <dlfcn.h>
}
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

#include <cppgres.hpp>

template <typename T> struct omni_pool_handler {
  friend struct omni_pool;
  using signature = void(const T *);
  explicit omni_pool_handler(const std::string &name, const std::string &library,
                             const std::string &function)
      : name(name), library(library), function(function),
        dl(dlopen(library.c_str(), RTLD_LAZY), dlclose), ptr(dlsym(dl.get(), function.c_str())),
        handler(reinterpret_cast<signature *>(ptr)) {}

  std::uint64_t hash() const {
    std::hash<std::string> hash_fn;
    return hash_fn(cppgres::fmt::format("{}/{}/{}", name, library, function));
  }

  void operator()(const T &t) { handler(t); }

private:
  std::string name;
  std::string library;
  std::string function;

  boost::shared_ptr<void> dl;
  void *ptr;
  boost::function<signature> handler;
};

struct omni_pool {
  struct job {
    std::uint64_t hash;
    bip::offset_ptr<void> msg;
  };
  using job_allocator_t = bip::allocator<job, bip::managed_shared_memory::segment_manager>;
  using job_vec = bc::vector<job, job_allocator_t>;

  explicit omni_pool()
      : segment(bip::open_or_create,
                cppgres::fmt::format("omni_pool_v1_{}_{}",
                                     cppgres::ffi_guard{::GetSystemIdentifier}(), MyDatabaseId)
                    .c_str(),
                65536),
        mq(bip::open_or_create,
           cppgres::fmt::format("omni_pool_v1_{}_{}_messge",
                                cppgres::ffi_guard{::GetSystemIdentifier}(), MyDatabaseId)
               .c_str(),
           1024, sizeof(std::size_t)),
        job_allocator(segment.get_segment_manager()) {
    segment.find_or_construct<job_vec>("job_vec")(job_allocator);
  }

  template <typename T> void register_handler(const omni_pool_handler<T> &handler) {
    using signature = void(const void *);
    handlers.emplace(handler.hash(), reinterpret_cast<signature *>(handler.ptr));
  }

  template <typename T> void send(const omni_pool_handler<T> &handler, T &&msg) {
    auto stmt = static_cast<T *>(segment.allocate(sizeof(T)));
    *stmt = msg;
    job_vec *vec = segment.find<job_vec>("job_vec").first;
    auto v = vec->emplace(vec->end(), handler.hash(), stmt);
    size_t i = vec->index_of(v);
    mq.send(&i, sizeof(i), 0);
  }

  bool receive() {
    job j;
    bip::message_queue::size_type recvd_size;
    unsigned int priority;

    std::size_t i;
    mq.receive(&i, sizeof(i), recvd_size, priority);
    job_vec *vec = segment.find<job_vec>("job_vec").first;
    if (auto j = vec->nth(i); j != vec->end()) {
      if (auto handler = handlers.find(j->hash); handler != handlers.end()) {
        handler->second(j->msg.get());
      } else {
      }
    }
    return true;
  }

  operator bip::managed_shared_memory &() { return segment; }

private:
  bip::managed_shared_memory segment;
  bip::message_queue mq;
  job_allocator_t job_allocator;

  std::map<std::uint64_t, boost::function<void(const void *)>> handlers;
};
