#pragma once

#ifdef __cplusplus
#include <boost/asio.hpp>
using asio_context = boost::asio::io_context;

namespace omni_worker {
struct handle {
  asio_context &io_ctx;
};
} // namespace omni_worker
#else
#endif
