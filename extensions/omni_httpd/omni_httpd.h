/*! \file */
#ifndef OMNI_HTTPD_H
#define OMNI_HTTPD_H

#include <stdatomic.h>

#include <netinet/in.h>
#include <sys/socket.h>

#include <libgluepg_stc.h>

// clang-format off
#include <postgres.h>
#include <fmgr.h>
// clang-format on

Oid http_method_oid();
Oid http_response_oid();
Oid http_header_oid();
Oid http_header_array_oid();

int create_listening_socket(sa_family_t family, in_port_t port, char *address);

#define MAX_ADDRESS_SIZE sizeof("xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:255.255.255.255/128")

typedef struct {
  Latch latch;
  _Atomic TransactionId txid;
} omni_httpd_latch;

/**
 * @brief This latch is used to notify the workers that the configuration has changed.
 *
 * Used to `SetLatch` if it is not owned yet, otherwise using owner's PID to set a SIGUSR2
 * directly.
 *
 */
static const char *LATCH = "omni_httpd:latch:" EXT_VERSION;

extern int num_http_workers;

#endif //  OMNI_HTTPD_H