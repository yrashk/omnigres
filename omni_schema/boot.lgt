:- protocol(pgtable).

:- public([column/2, column/3]).

:- end_protocol.

:- protocol(pgtype).

:- public([name/1,alignment/1,storage/1]).

:- end_protocol.

:- object(bool_type, implements(pgtype)).

name(bool).
name(boolean).

alignment(char).

storage(plain).

:- end_object.

:- object(int8_type, implements(pgtype)).

name(int8).
name(bigint).

alignment(double).

storage(plain).

:- end_object.


:- object(users, implements(pgtable)).

column(id, int, primary_key).
column(name, text, [not_null, unique]).
column(password, text, not_null).

:- end_object.

:- object(table_mgr).

:- public([primary_key/3, ordering/1]).

primary_key(T, Column, Type) :-
  attr(primary_key, T, Column, Type).

attr(Attr, T, Column, Type) :-
  current_object(T), conforms_to_protocol(T, pgtable),
  T::column(Column, Type, Attr).

attr(Attr, T, Column, Type) :-
  current_object(T), conforms_to_protocol(T, pgtable),
   T::column(Column, Type, List), list::member(Attr, List).

:- end_object.

:- object(omnigres).

:- public([run/1]).


run(_) :-
  prolog.


:- end_object.