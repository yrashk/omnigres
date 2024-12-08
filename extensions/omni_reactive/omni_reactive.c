/**
 * @file omni_ledger.c
 *
 */

// clang-format off
#include <postgres.h>
#include <fmgr.h>
// clang-format on
#include <catalog/namespace.h>
#include <executor/spi.h>

#include <omni/omni_v0.h>

PG_MODULE_MAGIC;

OMNI_MAGIC;

OMNI_MODULE_INFO(.name = "omni_reactive", .version = EXT_VERSION,
                 .identity = "17e98900-5df7-4e16-9d4b-edcd5fab33fc");

static void omni_reactive_hook(omni_hook_handle *handle, QueryDesc *queryDesc) {
  Oid relid = InvalidOid;
  switch (queryDesc->operation) {
  case CMD_INSERT:
  case CMD_UPDATE:
#if PG_MAJORVERSION_NUM >= 15
  case CMD_MERGE:
#endif
  case CMD_DELETE: {
    struct Plan *plan = queryDesc->plannedstmt->planTree;
    switch (nodeTag(plan)) {
    case T_ModifyTable:
      relid = list_nth_node(RangeTblEntry, queryDesc->plannedstmt->rtable, 0)->relid;
      break;
    default:
      break;
    }
    break;
  }
  default:
    break;
  }
  static bool busy = false;
  if (OidIsValid(relid) && !busy) {
    busy = true;
    SPI_connect();
    int rc = SPI_execute_with_args(
        "select query from omni_reactive.reactive_queries where $1 = any(relations)", 1,
        (Oid[1]){REGCLASSOID}, (Datum[1]){ObjectIdGetDatum(relid)}, (char[1]){' '}, true, 0);
    if (rc == SPI_OK_SELECT) {
      for (int i = 0; i < SPI_tuptable->numvals; i++) {
        char *query = SPI_getvalue(SPI_tuptable->vals[i], SPI_tuptable->tupdesc, 1);
        SPI_connect();
        SPI_exec(query, 0);
        SPI_finish();
      }
    }
    SPI_finish();
    busy = false;
  }
}

void _Omni_init(const omni_handle *handle) {
  omni_hook xact_hook = {.type = omni_hook_executor_finish,
                         .name = "omni_reactive executor hook",
                         .fn = {.executor_finish = omni_reactive_hook}};
  handle->register_hook(handle, &xact_hook);
}