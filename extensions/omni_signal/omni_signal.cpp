#include <cppgres.hpp>

extern "C" {
PG_MODULE_MAGIC;
#include <omni/omni_v0.h>
OMNI_MAGIC;

OMNI_MODULE_INFO(.name = "omni_rules", .version = EXT_VERSION,
                 .identity = "ccec012f-d90f-47e6-a13a-d710c96692cb");
}
