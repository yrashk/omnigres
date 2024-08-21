/**
 * @file http_worker.c
 * @brief HTTP worker
 *
 * This file contains code that runs in omni_httpd http workers that serve requests. These
 * workers are started by the master worker.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <h2o.h>
#include <h2o/http1.h>
#include <h2o/http2.h>
#include <h2o/websocket.h>

// clang-format off
#include <postgres.h>
#include <fmgr.h>
// clang-format on

#include <access/xact.h>
#include <catalog/namespace.h>
#include <catalog/pg_authid.h>
#include <executor/spi.h>
#include <funcapi.h>
#include <miscadmin.h>
#include <postmaster/bgworker.h>
#include <storage/latch.h>
#include <storage/lmgr.h>
#include <tcop/utility.h>
#include <utils/builtins.h>
#include <utils/inet.h>
#include <utils/lsyscache.h>
#include <utils/snapmgr.h>
#include <utils/syscache.h>
#include <utils/timestamp.h>
#if PG_MAJORVERSION_NUM >= 13
#include <postmaster/interrupt.h>
#endif
#include <parser/parse_func.h>
#include <tcop/pquery.h>
#include <utils/uuid.h>

#include <metalang99.h>

#include <libpgaug.h>
#include <omni_sql.h>
#include <sum_type.h>

#include "event_loop.h"
#include "fd.h"
#include "http_worker.h"
#include "omni_httpd.h"

#if H2O_USE_LIBUV == 1
#error "only evloop is supported, ensure H2O_USE_LIBUV is not set to 1"
#endif

h2o_multithread_receiver_t handler_receiver;
h2o_multithread_queue_t *handler_queue;

static clist_listener_contexts listener_contexts = {NULL};

static Oid handler_oid = InvalidOid;
static Oid websocket_handler_oid = InvalidOid;
static Oid websocket_on_open_oid = InvalidOid;
static Oid websocket_on_close_oid = InvalidOid;
static Oid websocket_on_message_text_oid = InvalidOid;
static Oid websocket_on_message_binary_oid = InvalidOid;

static TupleDesc request_tupdesc;
enum http_method {
  http_method_GET,
  http_method_HEAD,
  http_method_POST,
  http_method_PUT,
  http_method_DELETE,
  http_method_CONNECT,
  http_method_OPTIONS,
  http_method_TRACE,
  http_method_PATCH,
  http_method_last
};
static Oid http_method_oids[http_method_last] = {InvalidOid};
static char *http_method_names[http_method_last] = {"GET",     "HEAD",    "POST",  "PUT",  "DELETE",
                                                    "CONNECT", "OPTIONS", "TRACE", "PATCH"};

/**
 * Portal that we re-use in the handler
 *
 * It's kind of a "fake portal" in a sense that it only exists so that the check in
 * `ForgetPortalSnapshots()` doesn't fail with this upon commit/rollback in SPI:
 *
 * ```
 * portal snapshots (0) did not account for all active snapshots (1)
 * ```
 *
 * This is happening because we do have an anticipated snapshot when we call in non-atomic
 * fashion. In case when a languages is capable of handling no snapshot, that's all good –
 * PL/pgSQL, for example, works. However, if we call, say, an SQL function, it does not expect
 * to be non-atomic and hence needs a snapshot to operate in.
 */
static Portal execution_portal;
/**
 * Call context for all handler calls
 */
static CallContext *non_atomic_call_context;

/**
 * Handler context
 */
static MemoryContext HandlerContext;

static void on_message(h2o_multithread_receiver_t *receiver, h2o_linklist_t *messages) {
  while (!h2o_linklist_is_empty(messages)) {
    h2o_multithread_message_t *message =
        H2O_STRUCT_FROM_MEMBER(h2o_multithread_message_t, link, messages->next);

    handler_message_t *msg = (handler_message_t *)messages->next;
    h2o_linklist_unlink(&message->link);

    switch (msg->type) {
    case handler_message_http: {
      request_message_t *request_msg = &msg->payload.http.msg;

      pthread_mutex_t *mutex = &request_msg->mutex;
      pthread_mutex_lock(mutex);
      handler(msg);
      pthread_mutex_unlock(mutex);
      break;
    }
    default:
      handler(msg);
      switch (msg->type) {
      case handler_message_websocket_open:
        break;
      case handler_message_websocket_message:
        free(msg->payload.websocket_message.message);
        break;
      case handler_message_websocket_close:
        break;
      default:
        Assert(false); // shouldn't be here
      }
      free(msg);
      break;
    }
  }
}

static void sigusr2() {
  atomic_store(&worker_reload, true);
  h2o_multithread_send_message(&event_loop_receiver, NULL);
  h2o_multithread_send_message(&handler_receiver, NULL);
}

static void sigterm() {
  atomic_store(&worker_running, false);
  h2o_multithread_send_message(&event_loop_receiver, NULL);
  h2o_multithread_send_message(&handler_receiver, NULL);
}

/**
 * HTTP worker entry point
 *
 * This is where everything starts. The worker is responsible for accepting connections (when
 * applicable) and handling requests. The worker handles requests for a single database.
 * @param db_oid Database OID
 */
void http_worker(Datum db_oid) {
  atomic_store(&worker_running, true);
  atomic_store(&worker_reload, true);

  // We call this before we unblock the signals as necessitated by the implementation
  setup_server();

  // Block signals except for SIGUSR2 and SIGTERM
  pqsignal(SIGUSR2, sigusr2); // used to reload configuration
  pqsignal(SIGTERM, sigterm); // used to terminate the worker

  // Start thread that will be servicing `worker_event_loop` and handling all
  // communication with the outside world. Current thread will be responsible for
  // configuration reloads and calling handlers.
  pthread_t event_loop_thread;
  event_loop_suspended = true;

  // This MUST happen before starting event_loop
  // AND before unblocking signals as signals use this receiver
  event_loop_register_receiver();
  pthread_create(&event_loop_thread, NULL, event_loop, NULL);

  BackgroundWorkerUnblockSignals();

  // Connect worker to the database
  BackgroundWorkerInitializeConnectionByOid(db_oid, InvalidOid, 0);

  if (semaphore == NULL) {
    // omni_httpd is being shut down
    return;
  }

  // Get necessary OIDs and tuple descriptors
  {
    SetCurrentStatementStartTimestamp();
    StartTransactionCommand();
    PushActiveSnapshot(GetTransactionSnapshot());

    // Cache these. These calls require a transaction, so we don't want to do this on demand in
    // non-atomic executions in the handler
    http_request_oid();
    http_header_oid();
    http_response_oid();
    http_outcome_oid();

    {
      // omni_httpd.handler(int,http_request)
      List *handler_func = list_make2(makeString("omni_httpd"), makeString("handler"));
      handler_oid = LookupFuncName(handler_func, -1, (Oid[2]){INT4OID, http_request_oid()}, false);
      list_free(handler_func);
    }

    {
      // omni_httpd.websocket_handler(int,uuid,http_request)
      List *websocket_handler_func =
          list_make2(makeString("omni_httpd"), makeString("websocket_handler"));
      websocket_handler_oid = LookupFuncName(websocket_handler_func, 3,
                                             (Oid[3]){INT4OID, UUIDOID, http_request_oid()}, false);
      list_free(websocket_handler_func);
    }

    {
      // omni_httpd.websocket_on_open(uuid)
      List *websocket_on_open_func =
          list_make2(makeString("omni_httpd"), makeString("websocket_on_open"));
      websocket_on_open_oid = LookupFuncName(websocket_on_open_func, 1, (Oid[1]){UUIDOID}, false);
      list_free(websocket_on_open_func);
    }

    {
      // omni_httpd.websocket_on_close(uuid)
      List *websocket_on_close_func =
          list_make2(makeString("omni_httpd"), makeString("websocket_on_close"));
      websocket_on_close_oid = LookupFuncName(websocket_on_close_func, 1, (Oid[1]){UUIDOID}, false);
      list_free(websocket_on_close_func);
    }

    {
      // omni_httpd.websocket_on_message(uuid,text)
      List *websocket_on_message_text_func =
          list_make2(makeString("omni_httpd"), makeString("websocket_on_message"));
      websocket_on_message_text_oid =
          LookupFuncName(websocket_on_message_text_func, 2, (Oid[2]){UUIDOID, TEXTOID}, false);
      list_free(websocket_on_message_text_func);
    }

    {
      // omni_httpd.websocket_on_message(uuid,bytea)
      List *websocket_on_message_binary_func =
          list_make2(makeString("omni_httpd"), makeString("websocket_on_message"));
      websocket_on_message_binary_oid =
          LookupFuncName(websocket_on_message_binary_func, 2, (Oid[2]){UUIDOID, BYTEAOID}, false);
      list_free(websocket_on_message_binary_func);
    }

    // Save omni_httpd.http_request's tupdesc in TopMemoryContext
    // to persist it
    MemoryContext old_context = MemoryContextSwitchTo(TopMemoryContext);
    request_tupdesc = TypeGetTupleDesc(http_request_oid(), NULL);
    MemoryContextSwitchTo(old_context);

    // Populate HTTP method IDs
    for (int i = 0; i < http_method_last; i++) {
      http_method_oids[i] = DirectFunctionCall2(enum_in, PointerGetDatum(http_method_names[i]),
                                                ObjectIdGetDatum(http_method_oid()));
    }

    PopActiveSnapshot();
    AbortCurrentTransaction();
  }

  {
    // Prepare the persistent portal
    execution_portal = CreatePortal("omni_httpd", true, true);
    execution_portal->resowner = NULL;
    execution_portal->visible = false;
    PortalDefineQuery(execution_portal, NULL, "(no query)", CMDTAG_UNKNOWN, false, NULL);
    PortalStart(execution_portal, NULL, 0, InvalidSnapshot);
  }

  {
    // All call contexts are non-atomic
    MemoryContext old_context = MemoryContextSwitchTo(TopMemoryContext);
    non_atomic_call_context = makeNode(CallContext);
    non_atomic_call_context->atomic = false;
    MemoryContextSwitchTo(old_context);
  }

  HandlerContext =
      AllocSetContextCreate(TopMemoryContext, "omni_httpd handler context", ALLOCSET_DEFAULT_SIZES);

  while (atomic_load(&worker_running)) {
    bool worker_reload_test = true;
    if (atomic_compare_exchange_strong(&worker_reload, &worker_reload_test, false)) {

      SetCurrentStatementStartTimestamp();
      StartTransactionCommand();
      PushActiveSnapshot(GetTransactionSnapshot());

      SPI_connect();

      Oid nspoid = get_namespace_oid("omni_httpd", false);
      Oid listenersoid = get_relname_relid("listeners", nspoid);

      // The idea here is that we get handlers sorted by listener id the same way they were
      // sorted in the master worker (`by id asc`) and since they will arrive in the same order
      // in the list of fds, we can simply get them by the index.
      //
      // This table is locked by the master worker and thus the order of listeners will not change
      if (!ConditionalLockRelationOid(listenersoid, AccessShareLock)) {
        // If we can't lock it, something is blocking us (like `drop extension` waiting on
        // master worker to complete for its AccessExclusiveLock while master worker is holding
        // an ExclusiveLock waiting for http workers to signal their readiness)
        continue;
      }

      int handlers_query_rc = SPI_execute("select listeners.id "
                                          "from omni_httpd.listeners "
                                          "order by listeners.id asc",
                                          false, 0);

      UnlockRelationOid(listenersoid, AccessShareLock);

      // Allocate handler information in this context instead of current SPI's context
      // It will get deleted later.
      MemoryContext handlers_ctx =
          AllocSetContextCreate(TopMemoryContext, "handlers_ctx", ALLOCSET_DEFAULT_SIZES);
      MemoryContext old_ctx = MemoryContextSwitchTo(handlers_ctx);

      // This is where we record an ordered list of handlers
      List *handlers = NIL;
      // (which are described using this struct)
      struct pending_handler {
        int id;
      };

      if (handlers_query_rc == SPI_OK_SELECT) {
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        SPITupleTable *tuptable = SPI_tuptable;
        for (int i = 0; i < tuptable->numvals; i++) {
          HeapTuple tuple = tuptable->vals[i];
          bool id_is_null = false;
          Datum id = SPI_getbinval(tuple, tupdesc, 1, &id_is_null);

          struct pending_handler *handler = palloc(sizeof(*handler));
          handler->id = DatumGetInt32(id);
          handlers = lappend(handlers, handler);
        }

      } else {
        ereport(WARNING, errmsg("Error fetching configuration: %s",
                                SPI_result_code_string(handlers_query_rc)));
      }
      MemoryContextSwitchTo(old_ctx);

      // Roll back the transaction as we don't want to hold onto it anymore
      SPI_finish();
      PopActiveSnapshot();
      AbortCurrentTransaction();

      // Now that we have this information, we can let the master worker commit its update and
      // release the lock on the table.

      pg_atomic_add_fetch_u32(semaphore, 1);

      // When all HTTP workers will do the same, the master worker will start serving the
      // socket list.

      cvec_fd_fd fds = accept_fds(MyBgworkerEntry->bgw_extra);
      if (cvec_fd_fd_empty(&fds)) {
        continue;
      }

      // At first, assume all fds are new (so we need to set up listeners)
      cvec_fd_fd new_fds = cvec_fd_fd_clone(fds);
      // Disposing allocated listener/query pairs and their associated data
      // (such as sockets)
      {
        clist_listener_contexts_iter iter = clist_listener_contexts_begin(&listener_contexts);
        while (iter.ref != NULL) {
          // Do we have this socket in the new packet?
          c_foreach(it, cvec_fd_fd, new_fds) {
            // Compare by their master fd
            if (it.ref->master_fd == iter.ref->master_fd) {
              // If we do, continue using the listener
              clist_listener_contexts_next(&iter);
              // close the incoming  because we aren't going to be using it
              // and it may be polluting the fd table
              close(it.ref->fd);
              // and ensure we don't set up a listener for it
              cvec_fd_fd_erase_at(&new_fds, it);
              // process the next listener
              goto next_ctx;
            } else {
            }
          }
          // Otherwise, dispose of the listener

          if (iter.ref->socket != NULL) {
            h2o_socket_t *socket = iter.ref->socket;
            h2o_socket_export_t info;
            h2o_socket_export(socket, &info);
            h2o_socket_dispose_export(&info);
          }
          h2o_context_dispose(&iter.ref->context);
          MemoryContextDelete(iter.ref->memory_context);
          iter = clist_listener_contexts_erase_at(&listener_contexts, iter);
        next_ctx: {}
        }
      }

      // Set up new listeners as necessary
      c_FOREACH(i, cvec_fd_fd, new_fds) {
        int fd = (i.ref)->fd;

        listener_ctx c = {.fd = fd,
                          .master_fd = (i.ref)->master_fd,
                          .socket = NULL,
                          .memory_context =
                              AllocSetContextCreate(TopMemoryContext, "omni_httpd_listener_context",
                                                    ALLOCSET_DEFAULT_SIZES),
                          .accept_ctx = (h2o_accept_ctx_t){.hosts = config.hosts}};

        listener_ctx *lctx = clist_listener_contexts_push(&listener_contexts, c);

      try_create_listener:
        if (create_listener(fd, lctx) == 0) {
          h2o_context_init(&(lctx->context), worker_event_loop, &config);
          lctx->accept_ctx.ctx = &lctx->context;
        } else {
          if (errno == EINTR) {
            goto try_create_listener; // retry
          }
          int e = errno;
          ereport(WARNING, errmsg("socket error: %s", strerror(e)));
        }
      }
      cvec_fd_fd_drop(&new_fds);

      SetCurrentStatementStartTimestamp();
      StartTransactionCommand();
      PushActiveSnapshot(GetTransactionSnapshot());
      SPI_connect();

      // Now we're ready to work with the results of the query we made earlier:
      {
        // Here we have to track what was the last listener id
        int last_id = 0;
        // Here we have to track what was the last index
        int index = -1;

        ListCell *lc;
        foreach (lc, handlers) {
          struct pending_handler *handler = (struct pending_handler *)lfirst(lc);

          // Figure out socket index
          if (last_id != handler->id) {
            last_id = handler->id;
            index++;
          }

          const cvec_fd_fd_value *fd = cvec_fd_fd_at(&fds, index);
          Assert(fd != NULL);
          listener_ctx *listener_ctx = NULL;
          c_foreach(iter, clist_listener_contexts, listener_contexts) {
            if (iter.ref->master_fd == fd->master_fd) {
              listener_ctx = iter.ref;
              break;
            }
          }
          Assert(listener_ctx != NULL);

          // Set listener ID
          listener_ctx->listener_id = handler->id;
        }
        // Free everything in handlers context
        MemoryContextDelete(handlers_ctx);
        handlers = NIL;
      }

      cvec_fd_fd_drop(&fds);
      SPI_finish();
      PopActiveSnapshot();
      AbortCurrentTransaction();
    }

    event_loop_suspended = false;
    pthread_mutex_lock(&event_loop_mutex);
    pthread_cond_signal(&event_loop_resume_cond);
    pthread_mutex_unlock(&event_loop_mutex);

    bool running = atomic_load(&worker_running);
    bool reload = atomic_load(&worker_reload);

    // Handle requests until shutdown or reload is requested
    while ((running = atomic_load(&worker_running)) && !(reload = atomic_load(&worker_reload)) &&
           h2o_evloop_run(handler_event_loop, INT32_MAX))
      ;

    // Ensure the event loop is suspended while we're reloading
    if (reload || !running) {
      pthread_mutex_lock(&event_loop_mutex);
      while (event_loop_resumed) {
        pthread_cond_wait(&event_loop_resume_cond_ack, &event_loop_mutex);
      }
      pthread_mutex_unlock(&event_loop_mutex);
    }
  }

  clist_listener_contexts_drop(&listener_contexts);
  PortalDrop(execution_portal, false);
}

static inline int listener_ctx_cmp(const listener_ctx *l, const listener_ctx *r) {
  return (l->listener_id == r->listener_id && l->socket == r->socket && l->fd == r->fd) ? 0 : -1;
}

h2o_accept_ctx_t *listener_accept_ctx(h2o_socket_t *listener) {
  return &((listener_ctx *)listener->data)->accept_ctx;
}

/**
 * @brief Create a listening socket
 *
 * @param family
 * @param port
 * @param address
 * @return int
 */
int create_listening_socket(sa_family_t family, in_port_t port, char *address,
                            in_port_t *out_port) {
  struct sockaddr_in addr;
  struct sockaddr_in6 addr6;
  void *sockaddr;
  socklen_t socksize;
  int fd, reuseaddr_flag = 1;

  if (family == AF_INET) {
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, address, &addr.sin_addr);
    addr.sin_port = htons(port);
    sockaddr = &addr;
    socksize = sizeof(addr);
  } else if (family == AF_INET6) {
    memset(&addr6, 0, sizeof(addr6));
    addr6.sin6_family = AF_INET;
    inet_pton(AF_INET6, address, &addr6.sin6_addr);
    addr6.sin6_port = htons(port);
    sockaddr = &addr6;
    socksize = sizeof(addr6);
  } else {
    return -1;
  }

  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ||
      setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_flag, sizeof(reuseaddr_flag)) != 0 ||
      bind(fd, (struct sockaddr *)sockaddr, socksize) != 0 || listen(fd, SOMAXCONN) != 0) {
    return -1;
  }

  if (out_port != NULL) {
    if (getsockname(fd, sockaddr, &socksize) == -1) {
      int e = errno;
      ereport(WARNING, errmsg("getsockname failed with: %s", strerror(e)));
    }
    if (family == AF_INET) {
      Assert(addr.sin_family == AF_INET);
      *out_port = ntohs(addr.sin_port);
    } else if (family == AF_INET6) {
      Assert(addr.sin_family == AF_INET6);
      *out_port = ntohs(addr6.sin6_port);
    } else {
      return -1;
    }
  }

  return fd;
}

static int create_listener(int fd, listener_ctx *listener_ctx) {
  h2o_socket_t *sock;

  if (fd == -1) {
    return -1;
  }

  sock = h2o_evloop_socket_create(worker_event_loop, fd, H2O_SOCKET_FLAG_DONT_READ);
  sock->data = listener_ctx;
  listener_ctx->socket = sock;
  h2o_socket_read_start(sock, on_accept);

  return 0;
}

static h2o_pathconf_t *register_handler(h2o_hostconf_t *hostconf, const char *path,
                                        int (*on_req)(h2o_handler_t *, h2o_req_t *)) {
  h2o_pathconf_t *pathconf = h2o_config_register_path(hostconf, path, 0);
  h2o_handler_t *request_handler = h2o_create_handler(pathconf, sizeof(h2o_handler_t));
  request_handler->on_req = on_req;
  return pathconf;
}

// This must happen BEFORE signals are unblocked because of handler_receiver setup
static void setup_server() {
  h2o_hostconf_t *hostconf;

  h2o_config_init(&config);
  config.server_name = h2o_iovec_init(H2O_STRLIT("omni_httpd-" EXT_VERSION));
  hostconf = h2o_config_register_host(&config, h2o_iovec_init(H2O_STRLIT("default")), 65535);

  // Set up event loop for HTTP event loop
  worker_event_loop = h2o_evloop_create();
  event_loop_queue = h2o_multithread_create_queue(worker_event_loop);

  // Set up event loop for request handler loop
  handler_event_loop = h2o_evloop_create();
  handler_queue = h2o_multithread_create_queue(handler_event_loop);

  // This must happen BEFORE signals are unblocked
  h2o_multithread_register_receiver(handler_queue, &handler_receiver, on_message);

  h2o_pathconf_t *pathconf = register_handler(hostconf, "/", event_loop_req_handler);
}
static cvec_fd_fd accept_fds(char *socket_name) {
  struct sockaddr_un address;
  int socket_fd;

  socket_fd = socket(PF_UNIX, SOCK_STREAM, 0);
  if (socket_fd < 0) {
    ereport(ERROR, errmsg("can't create sharing socket"));
  }
  int err = fcntl(socket_fd, F_SETFL, fcntl(socket_fd, F_GETFL, 0) | O_NONBLOCK);
  if (err != 0) {
    ereport(ERROR, errmsg("Error setting O_NONBLOCK: %s", strerror(err)));
  }

  memset(&address, 0, sizeof(struct sockaddr_un));

  address.sun_family = AF_UNIX;
  snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_name);

try_connect:
  if (connect(socket_fd, (struct sockaddr *)&address, sizeof(struct sockaddr_un)) != 0) {
    int e = errno;
    if (e == EAGAIN || e == EWOULDBLOCK) {
      if (atomic_load(&worker_reload)) {
        // Don't try to get fds, roll with the reload
        return cvec_fd_fd_init();
      } else {
        goto try_connect;
      }
    }
    if (e == ECONNREFUSED) {
      goto try_connect;
    }
    ereport(ERROR, errmsg("error connecting to sharing socket: %s", strerror(e)));
  }

  cvec_fd_fd result;

  do {
    errno = 0;
    if (atomic_load(&worker_reload)) {
      result = cvec_fd_fd_init();
      break;
    }
    result = recv_fds(socket_fd);
  } while (errno == EAGAIN || errno == EWOULDBLOCK);

  close(socket_fd);

  return result;
}

static int handler(handler_message_t *msg) {
  MemoryContext memory_context = CurrentMemoryContext;
  if (msg->type == handler_message_http) {
    if (msg->payload.http.msg.req == NULL) {
      // The connection is gone
      // We can release the message
      free(msg);
      goto release;
    }
  }

  SetCurrentStatementStartTimestamp();
  StartTransactionCommand();

  ActivePortal = execution_portal;

  bool succeeded = false;

  // Execute handler
  CurrentMemoryContext = HandlerContext;

  switch (msg->type) {
  case handler_message_http: {
    h2o_req_t *req = msg->payload.http.msg.req;
    listener_ctx *lctx = H2O_STRUCT_FROM_MEMBER(listener_ctx, context, req->conn->ctx);
    bool is_websocket_upgrade = msg->payload.http.websocket_upgrade;
    bool nulls[REQUEST_PLAN_PARAMS] = {[REQUEST_PLAN_METHOD] = false,
                                       [REQUEST_PLAN_PATH] = false,
                                       [REQUEST_PLAN_QUERY_STRING] = req->query_at == SIZE_MAX,
                                       [REQUEST_PLAN_BODY] = is_websocket_upgrade,
                                       [REQUEST_PLAN_HEADERS] = false};
    Datum values[REQUEST_PLAN_PARAMS] = {
        [REQUEST_PLAN_METHOD] = ({
          PointerGetDatum(cstring_to_text_with_len(req->method.base, req->method.len));
          Datum result = InvalidOid;
          for (int i = 0; i < http_method_last; i++) {
            if (strncmp(req->method.base, http_method_names[i], req->method.len) == 0) {
              result = ObjectIdGetDatum(http_method_oids[i]);
              goto found;
            }
          }
          Assert(false);
        found:
          result;
        }),
        [REQUEST_PLAN_PATH] = PointerGetDatum(
            cstring_to_text_with_len(req->path_normalized.base, req->path_normalized.len)),
        [REQUEST_PLAN_QUERY_STRING] =
            req->query_at == SIZE_MAX
                ? PointerGetDatum(NULL)
                : PointerGetDatum(cstring_to_text_with_len(req->path.base + req->query_at + 1,
                                                           req->path.len - req->query_at - 1)),
        [REQUEST_PLAN_BODY] = ({
          bytea *result = NULL;
          if (is_websocket_upgrade) {
            goto done;
          }
          while (req->proceed_req != NULL) {
            req->proceed_req(req, NULL);
          }
          result = (bytea *)palloc(req->entity.len + VARHDRSZ);
          SET_VARSIZE(result, req->entity.len + VARHDRSZ);
          memcpy(VARDATA(result), req->entity.base, req->entity.len);
        done:
          PointerGetDatum(result);
        }),
        [REQUEST_PLAN_HEADERS] = ({
          TupleDesc header_tupledesc = TypeGetTupleDesc(http_header_oid(), NULL);
          BlessTupleDesc(header_tupledesc);

          Datum *elems = (Datum *)palloc(sizeof(Datum) * req->headers.size);
          bool *header_nulls = (bool *)palloc(sizeof(bool) * req->headers.size);
          for (int i = 0; i < req->headers.size; i++) {
            h2o_header_t header = req->headers.entries[i];
            header_nulls[i] = 0;
            HeapTuple header_tuple = heap_form_tuple(
                header_tupledesc,
                (Datum[2]){
                    PointerGetDatum(cstring_to_text_with_len(header.name->base, header.name->len)),
                    PointerGetDatum(cstring_to_text_with_len(header.value.base, header.value.len)),
                },
                (bool[2]){false, false});
            elems[i] = HeapTupleGetDatum(header_tuple);
          }
          ArrayType *result =
              construct_md_array(elems, header_nulls, 1, (int[1]){req->headers.size}, (int[1]){1},
                                 http_header_oid(), -1, false, TYPALIGN_DOUBLE);
          PointerGetDatum(result);
        })};

    HeapTuple request_tuple = heap_form_tuple(request_tupdesc, values, nulls);

    Datum outcome;
    bool isnull = false;
    PG_TRY();
    {
      FmgrInfo flinfo;

      Oid function = is_websocket_upgrade ? websocket_handler_oid : handler_oid;
      fmgr_info(function, &flinfo);

      Snapshot snapshot = GetTransactionSnapshot();
      PushActiveSnapshot(snapshot);
      execution_portal->portalSnapshot = snapshot;

      if (is_websocket_upgrade) {
        LOCAL_FCINFO(fcinfo, 3);
        InitFunctionCallInfoData(*fcinfo, &flinfo, 3, InvalidOid /* collation */, NULL, NULL);

        fcinfo->args[0].value = Int32GetDatum(lctx->listener_id);
        fcinfo->args[0].isnull = false;
        fcinfo->args[1].value = UUIDPGetDatum((pg_uuid_t *)msg->payload.http.msg.ws_uuid);
        fcinfo->args[1].isnull = false;
        fcinfo->args[2].value = HeapTupleGetDatum(request_tuple);
        fcinfo->args[2].isnull = false;
        fcinfo->context = (fmNodePtr)non_atomic_call_context;
        outcome = FunctionCallInvoke(fcinfo);
        isnull = fcinfo->isnull;
      } else {
        LOCAL_FCINFO(fcinfo, 2);
        InitFunctionCallInfoData(*fcinfo, &flinfo, 2, InvalidOid /* collation */, NULL, NULL);

        fcinfo->args[0].value = Int32GetDatum(lctx->listener_id);
        fcinfo->args[0].isnull = false;
        fcinfo->args[1].value = HeapTupleGetDatum(request_tuple);
        fcinfo->args[1].isnull = false;
        fcinfo->context = (fmNodePtr)non_atomic_call_context;
        outcome = FunctionCallInvoke(fcinfo);
        isnull = fcinfo->isnull;
      }

      PopActiveSnapshot();
      execution_portal->portalSnapshot = NULL;

      heap_freetuple(request_tuple);
    }
    PG_CATCH();
    {
      heap_freetuple(request_tuple);
      MemoryContextSwitchTo(memory_context);
      WITH_TEMP_MEMCXT {
        ErrorData *error = CopyErrorData();
        ereport(WARNING, errmsg("Error executing query"),
                errdetail("%s: %s", error->message, error->detail));
      }

      FlushErrorState();

      req->res.status = 500;
      h2o_queue_send_inline(&msg->payload.http.msg, H2O_STRLIT("Internal server error"));
      goto cleanup;
    }
    PG_END_TRY();
    switch (msg->type) {
    case handler_message_http: {
      if (is_websocket_upgrade) {
        if (isnull) {
          h2o_queue_abort(&msg->payload.http.msg);
        } else {
          if (DatumGetBool(outcome)) {
            h2o_queue_upgrade_to_websocket(&msg->payload.http.msg);
          }
        }
        succeeded = true;
        break;
      } else if (!isnull) {
        // We know that the outcome is a variable-length type
        struct varlena *outcome_value = (struct varlena *)PG_DETOAST_DATUM_PACKED(outcome);

        VarSizeVariant *variant = (VarSizeVariant *)VARDATA_ANY(outcome_value);

        switch (variant->discriminant) {
        case HTTP_OUTCOME_RESPONSE: {
          HeapTupleHeader response_tuple = (HeapTupleHeader)&variant->data;

          // Status
          req->res.status = DatumGetUInt16(
              GetAttributeByIndex(response_tuple, HTTP_RESPONSE_TUPLE_STATUS, &isnull));
          if (isnull) {
            req->res.status = 200;
          }

          bool content_length_specified = false;
          long long content_length = 0;

          // Headers
          Datum array_datum =
              GetAttributeByIndex(response_tuple, HTTP_RESPONSE_TUPLE_HEADERS, &isnull);
          if (!isnull) {
            ArrayType *headers = DatumGetArrayTypeP(array_datum);
            ArrayIterator iter = array_create_iterator(headers, 0, NULL);
            Datum header;
            while (array_iterate(iter, &header, &isnull)) {
              if (!isnull) {
                HeapTupleHeader header_tuple = DatumGetHeapTupleHeader(header);
                Datum name = GetAttributeByNum(header_tuple, 1, &isnull);
                if (!isnull) {
                  text *name_text = DatumGetTextPP(name);
                  size_t name_len = VARSIZE_ANY_EXHDR(name_text);
                  char *name_cstring = h2o_mem_alloc_pool(&req->pool, char *, name_len + 1);
                  text_to_cstring_buffer(name_text, name_cstring, name_len + 1);

                  Datum value = GetAttributeByNum(header_tuple, 2, &isnull);
                  if (!isnull) {
                    text *value_text = DatumGetTextPP(value);
                    size_t value_len = VARSIZE_ANY_EXHDR(value_text);
                    char *value_cstring = h2o_mem_alloc_pool(&req->pool, char *, value_len + 1);
                    text_to_cstring_buffer(value_text, value_cstring, value_len + 1);
                    if (name_len == sizeof("content-length") - 1 &&
                        strncasecmp(name_cstring, "content-length", name_len) == 0) {
                      // If we got content-length, we will not include it as we'll let h2o
                      // send the length.

                      // However, we'll remember the length set so that when we're processing the
                      // body, we can check its size and take action (reduce the size or send a
                      // warning if length specified is too big)

                      content_length_specified = true;
                      content_length = strtoll(value_cstring, NULL, 10);

                    } else {
                      // Otherwise, we'll just add the header
                      h2o_set_header_by_str(&req->pool, &req->res.headers, name_cstring, name_len,
                                            0, value_cstring, value_len, true);
                    }
                  }
                }
              }
            }
            array_free_iterator(iter);
          }

          Datum body = GetAttributeByIndex(response_tuple, HTTP_RESPONSE_TUPLE_BODY, &isnull);

          if (!isnull) {
            bytea *body_content = DatumGetByteaPP(body);
            size_t body_len = VARSIZE_ANY_EXHDR(body_content);
            if (content_length_specified) {
              if (body_len > content_length) {
                body_len = content_length;
              } else if (body_len < content_length) {
                ereport(WARNING, errmsg("Content-Length overflow"),
                        errdetail("Content-Length is set at %lld, but actual body is %zu",
                                  content_length, body_len));
              }
            }
            char *body_cstring = h2o_mem_alloc_pool(&req->pool, char *, body_len + 1);
            text_to_cstring_buffer(body_content, body_cstring, body_len + 1);
            // ensure we have the trailing \0 if we had to cut the response
            body_cstring[body_len] = 0;
            req->res.content_length = body_len;
            h2o_queue_send_inline(&msg->payload.http.msg, body_cstring, body_len);
          } else {
            h2o_queue_send_inline(&msg->payload.http.msg, "", 0);
          }
          break;
        }
        case HTTP_OUTCOME_ABORT: {
          h2o_queue_abort(&msg->payload.http.msg);
          break;
        }
        case HTTP_OUTCOME_PROXY: {
          HeapTupleHeader proxy_tuple = (HeapTupleHeader)&variant->data;

          // URL
          text *url =
              DatumGetTextPP(GetAttributeByIndex(proxy_tuple, HTTP_PROXY_TUPLE_URL, &isnull));
          if (isnull) {
            h2o_queue_abort(&msg->payload.http.msg);
            goto proxy_done;
          }
          // Preserve host
          int preserve_host = DatumGetBool(
              GetAttributeByIndex(proxy_tuple, HTTP_PROXY_TUPLE_PRESERVE_HOST, &isnull));
          if (isnull) {
            preserve_host = true;
          }
          size_t url_len = VARSIZE_ANY_EXHDR(url);
          char *url_cstring = h2o_mem_alloc_pool(&req->pool, char *, url_len + 1);
          text_to_cstring_buffer(url, url_cstring, url_len + 1);
          h2o_queue_proxy(&msg->payload.http.msg, url_cstring, preserve_host);
        proxy_done:
          break;
        }
        }
      } else {
        req->res.status = 204;
        h2o_queue_send_inline(&msg->payload.http.msg, "", 0);
      }
      succeeded = true;
      break;
    }
    default:
      Assert(false); // unhandled for now
      break;
    }
    break;
  }
  case handler_message_websocket_open:
  case handler_message_websocket_close:
    PG_TRY();
    {
      FmgrInfo flinfo;

      fmgr_info(msg->type == handler_message_websocket_open ? websocket_on_open_oid
                                                            : websocket_on_close_oid,
                &flinfo);
      LOCAL_FCINFO(fcinfo, 1);
      InitFunctionCallInfoData(*fcinfo, &flinfo, 1, InvalidOid /* collation */, NULL, NULL);

      fcinfo->args[0].value =
          UUIDPGetDatum((const pg_uuid_t *)(msg->type == handler_message_websocket_open
                                                ? msg->payload.websocket_open.uuid
                                                : msg->payload.websocket_close.uuid));
      fcinfo->args[0].isnull = false;
      fcinfo->context = (fmNodePtr)non_atomic_call_context;

      Snapshot snapshot = GetTransactionSnapshot();
      PushActiveSnapshot(snapshot);
      execution_portal->portalSnapshot = snapshot;

      FunctionCallInvoke(fcinfo);

      PopActiveSnapshot();
      execution_portal->portalSnapshot = NULL;
    }
    PG_CATCH();
    {
      MemoryContextSwitchTo(memory_context);
      WITH_TEMP_MEMCXT {
        ErrorData *error = CopyErrorData();
        ereport(WARNING, errmsg("Error executing omni_httpd.on_websocket_message"),
                errdetail("%s: %s", error->message, error->detail));
      }

      FlushErrorState();

      goto cleanup;
    }
    PG_END_TRY();
    succeeded = true;
    break;
  case handler_message_websocket_message:
    PG_TRY();
    {
      FmgrInfo flinfo;

      fmgr_info(msg->payload.websocket_message.opcode == WSLAY_TEXT_FRAME
                    ? websocket_on_message_text_oid
                    : websocket_on_message_binary_oid,
                &flinfo);
      LOCAL_FCINFO(fcinfo, 2);
      InitFunctionCallInfoData(*fcinfo, &flinfo, 2, InvalidOid /* collation */, NULL, NULL);

      fcinfo->args[0].value = UUIDPGetDatum((const pg_uuid_t *)msg->payload.websocket_message.uuid);
      fcinfo->args[0].isnull = false;
      fcinfo->args[1].value =
          PointerGetDatum(cstring_to_text_with_len((char *)msg->payload.websocket_message.message,
                                                   msg->payload.websocket_message.message_len));
      fcinfo->args[1].isnull = false;
      fcinfo->context = (fmNodePtr)non_atomic_call_context;

      Snapshot snapshot = GetTransactionSnapshot();
      PushActiveSnapshot(snapshot);
      execution_portal->portalSnapshot = snapshot;

      FunctionCallInvoke(fcinfo);

      PopActiveSnapshot();
      execution_portal->portalSnapshot = NULL;
    }
    PG_CATCH();
    {
      MemoryContextSwitchTo(memory_context);
      WITH_TEMP_MEMCXT {
        ErrorData *error = CopyErrorData();
        ereport(WARNING, errmsg("Error executing omni_httpd.on_websocket_message"),
                errdetail("%s: %s", error->message, error->detail));
      }

      FlushErrorState();

      goto cleanup;
    }
    PG_END_TRY();
    succeeded = true;
    break;
  default:
    Assert(false);
  }

cleanup:
  // Ensure we no longer have an active portal
  ActivePortal = false;

  if (succeeded) {
    CommitTransactionCommand();
  } else {
    AbortCurrentTransaction();
  }

  MemoryContextReset(HandlerContext);
release:

  return 0;
}