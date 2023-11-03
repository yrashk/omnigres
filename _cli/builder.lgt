:- object(omnigres_builder).

build(omnigres, Version) :-
  format::format("BUILD OMNIGRES for ~w~n", [Version]),
  true.

:- end_object.