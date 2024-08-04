// clang-format off
#include <postgres.h>
#include <fmgr.h>
// clang-format on

#include <stdint.h>

#define DatumGetInt8(X) ((int8_t)(X))

#define PG_GETARG_INT8(n) DatumGetInt8(PG_GETARG_DATUM(n))
#define PG_RETURN_INT8(x) return Int8GetDatum(x)
#define PG_GETARG_UINT8(n) DatumGetUInt8(PG_GETARG_DATUM(n))
#define PG_RETURN_UINT8(x) return UInt8GetDatum(x)