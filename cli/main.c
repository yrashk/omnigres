#include <SWI-Prolog.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define PL_require(x) if(!x) return FALSE

term_t Logtalk(term_t receiver, char *predicate_name, int arity, term_t arg) {
  // create term for the message
  functor_t functor = PL_new_functor(PL_new_atom(predicate_name), arity);
  term_t pred = PL_new_term_ref();
  PL_require(PL_cons_functor(pred, functor, arg));

  // term for ::(receiver, message)
  functor_t send_functor = PL_new_functor(PL_new_atom("::"), 2);
  term_t goal = PL_new_term_ref();
  PL_require(PL_cons_functor(goal, send_functor, receiver, pred));

  return goal;
}

term_t Logtalk_named(char *object_name, char *predicate_name, int arity, term_t arg) {
  // create term for the receiver of the message
  term_t receiver = PL_new_term_ref();
  PL_put_atom_chars(receiver, object_name);

  return Logtalk(receiver, predicate_name, arity, arg);
}

predicate_t call_predicate() {
  static predicate_t pred = NULL;
  if (pred == NULL) {
    pred = PL_predicate("call", 1, NULL);
  }
  return pred;
}

int main(int argc, char **argv) {
  char *program = argv[0];
  char *plav[2];
  int rc = 0;

  /* make the argument vector for Prolog */
  plav[0] = program;
  plav[1] = NULL;

  /* initialise Prolog */
  if (!PL_initialise(1, plav)) {
    PL_halt(1);
  }

  term_t arglist = PL_new_nil_ref();
  term_t tail = PL_new_nil_ref();

  for (int i = 1; i < argc; ++i) {
    term_t a = PL_new_term_ref();
    PL_put_atom_chars(a, argv[i]);  // Create an atom from the string
    PL_require(PL_cons_list(arglist, a, tail));  // Append the atom to the list
  }

  {
    term_t run = Logtalk_named("cli", "run", 1, arglist);
    qid_t qid = PL_open_query(NULL, PL_Q_NORMAL, call_predicate(), run);
    while (PL_next_solution(qid)) {
    }
    PL_close_query(qid);
  }

  PL_cleanup(0);

  return rc;
}