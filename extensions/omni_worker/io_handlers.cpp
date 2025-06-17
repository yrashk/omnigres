#include <boost/asio.hpp>
#include <boost/container/string.hpp>
#include <oink.hpp>

#include <cppgres.hpp>

#include "io_worker.hpp"

void timer_handler_init(boost::asio::io_context &io_ctx) {
  auto timer = std::make_shared<boost::asio::steady_timer>(io_ctx);
  timer->expires_after(std::chrono::seconds(3));
  std::cout << "Timer armed!" << std::endl;
  timer->async_wait([timer](const boost::system::error_code &ec) {
    if (!ec) {
      std::cout << "Timer fired!" << std::endl;

    } else {
      std::cout << "cancelled" << std::endl;
    }
  });
}

extern "C" void *omni_io_worker_handler_init(const char *name) {
  if (std::string_view(name) == "timer") {
    return reinterpret_cast<void *>(timer_handler_init);
  }
  return nullptr;
}
