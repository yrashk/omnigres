:- object(pkgconfig).

:- use_module(library(process), [process_create/3, process_wait/2]).

:- public([exists/1]).

exists(Package) :-
  env::available_executable('pkg-config', PkgConfig),
  process_create(PkgConfig, ['--exists', Package], [stdout(null), stderr(null), process(PID)]),
  process_wait(PID, exit(N)),
  N == 0.

:- end_object.