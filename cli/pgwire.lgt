:- object(pgwire).

:- use_module(clpfd, [(#=)/2, in/2,
        op(700, xfx, #=), op(700, xfx, in)
]).

:- use_module(library(dcg/basics), [string//1, remainder//1]).

int32_be(Num) -->
    [B3, B2, B1, B0],
    {
    B3 in 0..255,
    B2 in 0..255,
    B1 in 0..255,
    B0 in 0..255,
    Num #= (B3 << 24 + B2 << 16 + B1 << 8 + B0)
    }.

int16_be(Num) -->
    [B1, B0],
    {
    B1 in 0..255,
    B0 in 0..255,
    Num #= (B1 << 8 + B0)
    }.

zstring(S) -->
  { nonvar(S), !, string_codes(S, Codes) },
  string(Codes), [0].
zstring(S) -->
  string(Codes), [0],
  { string_codes(S, Codes) }.

parameter([]) --> [0].
parameter([(Name, Value)|Xs]) -->
  zstring(Name), zstring(Value),
  parameter(Xs).

startup_message(Params) -->
   int32_be(196608),
  parameter(Params).

message(authenticationOk) -->
  packet('R', int32_be(0)).

message(query(Query)) -->
 packet('Q', zstring(Query)).

message(rowDescription(Fields)) -->
 packet('T', fields(Fields)).

message(dataRow(Cols)) -->
 packet('D', dataRow(Cols)).

format(text) --> [0, 0].
format(binary) --> [0, 1].

fields([]) --> [].
fields([{name: Name, table_oid: TableOid, attribute: Att, type: TypeOid, size: Size, mod: Mod, format: Format}|Fields]) -->
  zstring(Name),
  int32_be(TableOid),
  int16_be(Att),
  int32_be(TypeOid),
  int16_be(Size),
  int32_be(Mod),
  format(Format),
  fields(Fields).

dataRow(Cols) -->
  int16_be(Num),
  { list::length(Cols, Num) },
  cols(Cols, Num).

cols([Col|Rest], Num) -->
  int32_be(L),
  { list::length(Col, L) },
  Col,
  cols(Rest, Num).
cols([], Num) --> [].

%%%%%

packet(Char, Rule) -->
 {char_code(Char, Code) },
 [Code],
 packet_length(Length),
 data(Rule, Length).

packet_length(Length) -->
  int32_be(Length).

:- meta_non_terminal(data(*, 0)).

data(Rule, Length) -->
 peek(Start),
 call(Rule),
 peek(End),
 eos,
 { diff_length(Start, End, Length0), Length is Length0 + 4 }.

peek(List, List, List).

diff_length(Start, End, Length) :-
    length(Start, StartLength),
    length(End, EndLength),
    Length is StartLength - EndLength.

:- public([parse/2]).

parse(E, B) :-
  phrase(E, B).


:- end_object.
