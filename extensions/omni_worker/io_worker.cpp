#include "io_worker.hpp"

io_worker_service::io_worker_service(boost::asio::execution_context &ctx, bool running)
    : boost::asio::execution_context::service::service(ctx), running_(running) {}

void io_worker_service::shutdown() {}

boost::asio::execution_context::id io_worker_service::id;

bool io_worker_service::is_running() const { return running_; }
