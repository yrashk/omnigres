// clang-format off
#include <postgres.h>
#include <fmgr.h>
// clang-format on

#include <nodes/nodeFuncs.h>
#include <parser/analyze.h>
#include <utils/builtins.h>
#include <utils/lsyscache.h>

#include "omni_sql.h"

static bool rewrite_pipe_operator(Node *node, void *context) {
  if (node == NULL)
    return false;

  if (IsA(node, A_Expr)) {
    A_Expr *opexpr = (A_Expr *)node;

    // Check if the operator is the pipe-forward operator (|>)
    Node *name = (Node *)linitial(opexpr->name);
    if (IsA(name, String) && strcmp(castNode(String, name)->sval, "|>") == 0 &&
        IsA(opexpr->rexpr, FuncCall)) {

      // Recursively rewrite the left side of the pipe operator
      Node *arg1 = opexpr->lexpr;
      rewrite_pipe_operator(arg1, context);

      FuncCall *funcexpr = castNode(FuncCall, opexpr->rexpr);

      // Rewrite the expression by making the first argument of the function the left side of the
      // pipe operator
      funcexpr->args = lcons(arg1, funcexpr->args);

      StaticAssertStmt(sizeof(TypeCast) <= sizeof(A_Expr), "TypeCast must fit into A_Expr");

      memcpy(node,
             &(TypeCast){.type = T_TypeCast,
                         .arg = (Node *)funcexpr,
                         .location = funcexpr->location,
                         .typeName = makeNode(TypeName)},
             sizeof(TypeCast));
      TypeName *typeName = castNode(TypeCast, node)->typeName;
      typeName->names = list_make1(makeString("anyelement"));
    }
    return true;
  } else {
    return raw_expression_tree_walker(node, rewrite_pipe_operator, context);
  }
}

PG_FUNCTION_INFO_V1(extended_sql);

Datum extended_sql(PG_FUNCTION_ARGS) {
  if (PG_ARGISNULL(0)) {
    ereport(ERROR, errmsg("statement must not be NULL"));
  }
  text *statement = PG_GETARG_TEXT_PP(0);
  static bool pipe = false;
#define flag(name, index)                                                                          \
  if (!PG_ARGISNULL(index))                                                                        \
  name = PG_GETARG_BOOL(index)

  flag(pipe, 1);
#undef flag

  char *cstatement = text_to_cstring(statement);
  List *stmts = omni_sql_parse_statement(cstatement);

  if (pipe) {
    ListCell *lc;
    foreach (lc, stmts) {
      Node *node = (Node *)lfirst(lc);
      raw_expression_tree_walker(castNode(RawStmt, node)->stmt, rewrite_pipe_operator, NULL);
    }
  }

  char *deparsed = omni_sql_deparse_statement(stmts);
  text *deparsed_statement = cstring_to_text(deparsed);

  PG_RETURN_TEXT_P(deparsed_statement);
}
