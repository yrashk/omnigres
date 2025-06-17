#include <boost/asio.hpp>

struct io_worker_service : public boost::asio::execution_context::service {
  using boost::asio::execution_context::service::service;

  io_worker_service(boost::asio::execution_context &ctx, bool running);
  void shutdown() override;

  bool is_running() const;

  static boost::asio::execution_context::id id;

private:
  bool running_;
};
