// clang-format off
#include <postgres.h>
#include <fmgr.h>
// clang-format on

#include <omni/omni_v0.h>

PG_MODULE_MAGIC;

OMNI_MAGIC;

OMNI_MODULE_INFO(.name = "omni_guard", .version = EXT_VERSION,
                 .identity = "5f22b226-c7c6-478e-89a2-da07c49e5860");

#include "omni_guard.h"

void _Omni_init(const omni_handle *handle) {
  guard_init(handle);
}
