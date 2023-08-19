

#if __APPLE__
// Working around the fact that JSC on macOS includes MacTypes.h which defines
// `Size` and `Boolean` as well.
#define Size Size_t
typedef unsigned char Boolean_t;
#define Boolean Boolean_t
#endif

#include <JavaScriptCore/JavaScript.h>

#if __APPLE__
#undef Size // Finish working around `Size` issue.
#undef Boolean
#endif

// clang-format off
#include <postgres.h>
#include <fmgr.h>
// clang-format on
#include <catalog/pg_proc.h>
#include <executor/spi.h>
#include <funcapi.h>
#include <miscadmin.h>
#include <utils/builtins.h>
#include <utils/lsyscache.h>
#include <utils/syscache.h>

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(javascript_handler);

Datum javascript_handler(PG_FUNCTION_ARGS) {
  bool isnull;
  Datum ret;

  // Fetch the function's pg_proc entry
  HeapTuple pl_tuple = SearchSysCache1(PROCOID, ObjectIdGetDatum(fcinfo->flinfo->fn_oid));
  if (!HeapTupleIsValid(pl_tuple))
    elog(ERROR, "cache lookup failed for function %u", fcinfo->flinfo->fn_oid);

  // Extract the source text of the function
  Form_pg_proc pl_struct = (Form_pg_proc)GETSTRUCT(pl_tuple);
  char *proname = pstrdup(NameStr(pl_struct->proname));
  ret = SysCacheGetAttr(PROCOID, pl_tuple, Anum_pg_proc_prosrc, &isnull);
  if (isnull)
    elog(ERROR, "could not find source text of function \"%s\"", proname);
  char *source = DatumGetCString(DirectFunctionCall1(textout, ret));

  // Extract argument types
  ret = SysCacheGetAttr(PROCOID, pl_tuple, Anum_pg_proc_proargtypes, &isnull);
  oidvector *current_argtypes = (oidvector *)DatumGetPointer(ret);

  JSGlobalContextRef context = JSGlobalContextCreate(NULL);
  char *prepared_source = psprintf("(%s)", source);
  JSStringRef scriptJS = JSStringCreateWithUTF8CString(prepared_source);

  JSValueRef exception = NULL;
  JSValueRef fun = JSEvaluateScript(context, scriptJS, NULL, NULL, 1, &exception);
  JSStringRelease(scriptJS);

  //  JSValueIsObject(context, fun);

  Oid result_type_oid = get_func_rettype(fcinfo->flinfo->fn_oid);

  JSValueRef res = JSObjectCallAsFunction(context, (JSObjectRef)fun, NULL, 0, NULL, &exception);

  JSGlobalContextRelease(context);
  PG_RETURN_NULL();
}
