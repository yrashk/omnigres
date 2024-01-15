:- object(pg_yregress).

:- public(file/3).

:- use_module(yaml, [yaml_read/2, yaml_write/2]).

% Takes JSON input and produces JSON output
file(Filename, Input, Output) :-
  os::decompose_file_name(Filename, Directory, Name, _Ext),
  atomic_list_concat([Directory, 'loader.lgt'], '/', Loader),
  (os::file_exists(Loader) -> logtalk_load(Loader) ; true),
  atomic_list_concat([Directory, '/', Name, '.lgt'], FileLoader),
  (os::file_exists(FileLoader) -> logtalk_load(FileLoader) ; true),
  J = json(list, dash, atom),
  J::parse(chars(Input), JSON),
  process(JSON, JSON_),
  J::generate(chars(Output), JSON_).

process(json([tests-Tests|T]), json([tests-Tests_|T])) :-
  meta::map({Test_}/[Test, NewTests]>>(findall(Test_, pg_yregress_test(Test)::process(Test_), NewTests)), Tests, ProducedTests),
  list::flatten(ProducedTests, Tests_),
  writeq(Tests_), flush_output.

process(json([H|T]), json([H|T])) :-
  process(json(T), json(T)).

process(json([]), json([])).

:- end_object.