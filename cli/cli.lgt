handle_int(_) :-
  halt.

:- object(cli).

:- public([run/1]).

:- use_module(library(optparse)).
:- use_module(system, [thread_self/1, thread_send_message/2, thread_get_message/1]).
:- use_module(library(yaml), [yaml_read/2, yaml_write/2]).

:- use_module(library(broadcast), [listen/3, unlisten/1, broadcast/1]).

opt_spec([]).

run(Args) :-
  opt_spec(Spec),
  opt_parse(Spec, Args, Opts, PositionalArgs),
  once(run(PositionalArgs, Opts)).

run([init|_], _Opts) :-
  format::format("Fetching Postgres versions... ", []),
  flush_output,
  postgres::latest_major_version(MajVer),
  postgres::latest_version((MajVer, MinVer)),
  format::format("done.~n", []),
  listen(L, postgres(notice(Notice), _Ver), (format::format("~w, ", [Notice]), flush_output)),
  listen(L, postgres(started(Activity), _Ver), (format::format("Postgres: ~w... ", [Activity]), flush_output)),
  listen(L, postgres(finished(_), _Ver), (format::format("done.~n", []), flush_output)),
  postgres::procure((MajVer, MinVer)),
  unlisten(L),
  %% Omnigres provisioning (before we have proper package management)
  omnigres::latest_commit(C),
  omnigres::download_commit(C, _),
  omnigres::extract_commit(C, CommitPath),
  env::application_cache_path(omnigres, Cache),
  % Record the latest fetched version of Omnigres
  atomic_list_concat([Cache, omnnigres, 'omnigres.yml'], '/', OmnigresYml),
  (os::file_exists(OmnigresYml) -> yaml_read(OmnigresYml, OmnigresCfg) ; OmnigresCfg = yaml{}),
  OmnigresCfg1 = OmnigresCfg.put(latest, C),
  setup_call_cleanup(open(OmnigresYml, write, Stream), yaml_write(Stream, OmnigresCfg1), close(Stream)),
  % Load Omnigres' own loader script
  atomic_list_concat([CommitPath, '_cli', 'loader.lgt'], '/', Loader),
  (os::file_exists(Loader) ->
    logtalk_load([Loader])
  ; true),
  %% and then emit some kind of event? we don't know how the CLI there is structured
  % what kind tho?
  broadcast(omnigres_cli(init)),
  !.


run([new|_], _Opts) :-
  \+ postgres::installed_version((MajVer, MinVer), _Path),
  format::format("No Postgres installation found, please run `omnigres init` first~n", []),
  !.

run([new|_], _Opts) :-
  postgres::installed_version((MajVer, MinVer), _Path),
  format::format("Using Postgres ~w.~w~n", [MajVer, MinVer]).

run([run|_], _Opts) :-
  postgres::installed_version((MajVer, MinVer), _Path),
  postgres::init((MajVer, MinVer), 'data'),
  {on_signal(int, _, handle_int)},
  postgres::start((MajVer, MinVer), 'data', Port),
  at_halt(postgres::stop((MajVer, MinVer), 'data')),
  postgres::connect(Port, _Stream, "omnigres", "postgres"),
  postgres::query(_Stream, "select 1", Row),
  !.

run(_, _) :-
  writeln("Usage: omnigres <command>").

:- end_object.