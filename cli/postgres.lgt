:- table(load_postgres_listing/1).

load_postgres_listing(DOM) :-
   load_html('https://ftp.postgresql.org/pub/source/', DOM, []).

:- object(postgres).

:- use_module(library(sgml), [load_html/3]).
:- meta_predicate(sgml:load_html(*, *, *)).
:- use_module(library(http/http_open), [http_open/3]).
:- meta_predicate(http_open:http_open(*, *, *)).
:- use_module(library(xpath), [xpath/3]).

:- use_module(library(process), [process_create/3, process_wait/2]).
:- use_module(library(broadcast), [broadcast/1]).

%% xpath operators
:-  op(400, fx, //).
:-  op(400, fx, /).
:-  op(200, fy, @).

:- public([available_version/2, available_version/1,
           latest_version/2, latest_version/1,
           latest_major_version/1, download/2,
           installed_version/2]).

:- table(available_version/2).

available_version((Maj, Min), URL) :-
  { load_postgres_listing(DOM) },
  xpath(DOM, //a(@href), Href),
  atom_chars(Href, ['v'|Chars]),
  list::reverse(Chars, ['/'|RChars]),
  list::reverse(RChars, VChars),
  atom_chars(VAtom, VChars),
  atom_string(VAtom, VStr),
  split_string(VStr, ".", "", Components),
  meta::maplist(atom_number, Components, [Maj, Min]),
  Maj >= 13, % we don't support anything below v13
  atomic_list_concat(['https://ftp.postgresql.org/pub/source/', Href, 'postgresql-', VAtom, '.tar.gz'],'', URL).

available_version((Maj, Min)) :-
  available_version((Maj, Min), _URL).

:- use_module(library(lists), [max_list/2]).

latest_major_version(Major) :-
  setof(Maj, Min^URL^(::available_version((Maj, Min), URL)), MajList),
  list::member(Major, MajList),
  max_list(MajList, Major).

:- table(latest_version/2).

latest_version((Maj, Min)) :-
  latest_version((Maj, Min), _URL).

latest_version((MaxMaj, MaxMin), URL) :-
  setof(Maj, Min^URL^(::available_version((Maj, Min), URL)), MajList),
  list::member(MaxMaj, MajList),
  findall(Min, ::available_version((MaxMaj, Min), URL), Mins),
  max_list(Mins, MaxMin),
  available_version((MaxMaj, MaxMin), URL).

:- public([procure/1]).

procure((Maj, Min)) :-
  env::application_cache_path(omnigres, Cache),
  atomic_list_concat([Cache, downloads], '/', Downloads),
  os::ensure_directory(Downloads),
  atomic_list_concat([Cache, postgres], '/', Builds),
  os::ensure_directory(Builds),
  atomic_list_concat([postgres, '-', Maj, '.', Min, '.tar.gz'], Filename),
  atomic_list_concat([Downloads, Filename], '/', Path),
  download((Maj, Min), Path),
  extract((Maj, Min), Path, Builds, Path1),
  build((Maj, Min), Path1, _BuildPath).

extract((Maj, Min), Path, Builds, Source) :-
 atomic_list_concat([Builds, '/postgresql-', Maj, '.', Min], Source),
 format::format(atom(Command), 'tar -xzf ~w -C ~w', [Path, Builds]),
 atomic_list_concat([Source, '.complete'], '/', Complete),
 \+ os::file_exists(Complete),
 broadcast(postgres(started(extracting), (Maj, Min))),
 os::shell(Command),
 os::ensure_file(Complete),
 broadcast(postgres(finished(extracting), (Maj, Min))).

extract((Maj, Min), Path, Builds, Source) :-
 atomic_list_concat([Builds, '/postgresql-', Maj, '.', Min], Source),
 atomic_list_concat([Source, '.complete'], '/', Complete),
 os::file_exists(Complete),
 os::directory_exists(Source).

download((Maj, Min), Path) :-
  available_version((Maj, Min), URL),
  \+ installed_version((Maj, Min), Path),
  atomic_list_concat([Path, '.complete'], CompletePath),
  \+ os::file_exists(CompletePath),
  \+ os::file_exists(Path),
  broadcast(postgres(started(downloading), (Maj, Min))),
  setup_call_cleanup(
          http_open(URL, InStream, []),
          setup_call_cleanup(
              open(Path, write, OutStream, [type(binary)]),
              copy_stream_data(InStream, OutStream),
              (close(OutStream),
               os::ensure_file(CompletePath))
          ),
          close(InStream)
      ),
  broadcast(postgres(finished(downloading), (Maj, Min))).

download(_, Path) :-
  os::file_exists(Path),
  atomic_list_concat([Path, '.complete'], CompletePath),
  os::file_exists(CompletePath).

:- public(build/3).


build((16, Min), Path, BuildPath) :-
  %% TODO: explore if we should use `ICU_CFLAGS` and `ICU_LIBS` here
  %% instead of `PKG_CONFIG_PATH`
  \+ installed_version((16, Min), _),
  env::os(macos),
  env::available_executable(brew, Brew),
  \+ pkgconfig::exists('icu-uc'),
  broadcast(postgres(started(dependencies), Version)),
  broadcast(postgres(notice('using icu4c from Homebrew'), (16, Min))),
  process_create(Brew, [ '--prefix', icu4c ], [stdout(pipe(Out)), process(BrewInfoPid)]),
  process_wait(BrewInfoPid, exit(0)),
  read_string(Out, ":", "\n\r", _, Prefix),
  atomic_list_concat([Prefix, lib, pkgconfig], '/', PkgConfigPath),
  (getenv('PKG_CONFIG_PATH', ExistingPkgConfigPath) ->
    atomic_list_concat([ExistingPkgConfigPath, PkgConfigPath], ':', NewPkgConfigPath),
    setenv('PKG_CONFIG_PATH', NewPkgConfigPath) ;
    setenv('PKG_CONFIG_PATH', PkgConfigPath)),
  broadcast(postgres(finished(dependencies), Version)),
  build_((16, Min), Path, BuildPath), !.

build((_Maj, _Min), Path, BuildPath) :-
  build_((_Maj, _Min), Path, BuildPath).

build_(Version, Path, BuildPath) :-
  \+ installed_version(Version, _),
  atomic_list_concat([Path, configure], '/', Configure),
  atomic_list_concat([Path, build], '/', BuildPath),
  broadcast(postgres(started(configuring), Version)),
  process_create(Configure, ['--prefix', BuildPath ], [cwd(Path), process(Pid)]),
  process_wait(Pid, exit(_ExitCode)),
  broadcast(postgres(finished(configuring), Version)),
  env::available_executable(make, Make),
  broadcast(postgres(started(building), Version)),
  process_create(Make, [ '-j', install ], [cwd(Path), process(BuildPid)]),
  process_wait(BuildPid, exit(_ExitCode1)),
  atomic_list_concat([BuildPath, '.complete'], '/', CompletePath),
  os::ensure_file(CompletePath),
  broadcast(postgres(finished(building), Version)).

build_(Version, Path, BuildPath) :-
  installed_version(Version, BuildPath).

installed_version((Maj, Min), Path) :-
   env::application_cache_path(omnigres, Cache),
   atomic_list_concat([Cache, postgres], '/', Builds),
   % Are there any builds?
   os::directory_exists(Builds),
   % What are the builds?
   os::directory_files(Builds, Entries),
   list::member(Entry, Entries),
   Entry \= '.', Entry \=  '..',
   atomic_list_concat([Builds, Entry], '/', SourcePath),
   os::directory_exists(SourcePath),
   atomic_list_concat([SourcePath, build], '/', Path),
   % Has the build completed?
   atomic_list_concat([Path, '.complete'], '/', PostgresBuildComplete),
   os::file_exists(PostgresBuildComplete),
   % Do we have `postgres` executable?
   atomic_list_concat([Path, bin, postgres], '/', Postgres),
   os::file_exists(Postgres),
   os::file_permission(Postgres, execute),
   % What's the version of the build?
   atom_string(Entry, EntryStr),
   split_string(EntryStr, ".-", "", [_|Components]),
   meta::maplist(atom_number, Components, [Maj, Min]).

:- public([init/2, start/2, start/3, stop/2, connect/4]).

:- use_module(socket, [tcp_socket/1, tcp_bind/2, tcp_close_socket/1, tcp_connect/3]).

init(Version, Dir) :-
  \+ os::directory_exists(Dir),
  installed_version(Version, Path),
  atomic_list_concat([Path, bin, pg_ctl], '/', PgCtl),
  process_create(PgCtl, [initdb, '-o', "-A trust -U omnigres", '-D', Dir], [process(InitDbPid)]),
  process_wait(InitDbPid, exit(_)).

init(Version, Dir) :-
  os::directory_exists(Dir).

available_tcp_port(Port) :-
  catch(
  (tcp_socket(Socket), tcp_bind(Socket, Port), tcp_close_socket(Socket)),
  _,
  false).

start(Version, Dir) :-
  start(Version, Dir, _).

start(Version, Dir, Port) :-
  installed_version(Version, Path),
  atomic_list_concat([Path, bin, pg_ctl], '/', PgCtl),
  (available_tcp_port(5432) -> Port = 5432 ; available_tcp_port(Port)),
  format::format(atom(Config), "-c port=~w", [Port]),
  process_create(PgCtl, [start, '-o', Config, '-D', Dir], [process(StartPid)]),
  process_wait(StartPid, exit(_)).

stop(Version, Dir) :-
  installed_version(Version, Path),
  atomic_list_concat([Path, bin, pg_ctl], '/', PgCtl),
  process_create(PgCtl, [stop, '-D', Dir], [process(StopPid)]),
  process_wait(StopPid, exit(_)).

:- use_module(library(aggregate), [foreach/2]).

read_packet(Stream, [Tag|Tail]) :-
  get_byte(Stream, Tag),
  list::length(N_, 4),
  meta::maplist(get_byte(Stream), N_),
  pgwire::parse(int32_be(N), N_),
  BodySz is N - 4,
  list::length(Body, BodySz),
  meta::maplist(get_byte(Stream), Body),
  list::append(N_, Body, Tail).

connect(Port, StreamPair, User, Database) :-
  tcp_connect('127.0.0.1':Port, StreamPair, []),
  pgwire::parse(startup_message([("user", User),("database", Database)]), Bytes),
  list::length(Bytes, BL),
  pgwire::parse(int32_be(4+BL), L),
  foreach(list::member(Byte, L), put_byte(StreamPair, Byte)),
  foreach(list::member(Byte, Bytes), put_byte(StreamPair, Byte)),
  flush_output(StreamPair),
  read_packet(StreamPair, BBytes),
  pgwire::parse(message(M), BBytes),
  (M = authenticationOk -> true;
  throw(error(postgres_errror('Authentication failed'), Context))).

:- public(query/3).

query(Stream, Query, Row) :-
  pgwire::parse(message(query(Query)), B),
  foreach(list::member(Byte, B), put_byte(Stream, Byte)),
  flush_output(Stream),
  query_response(Stream, Row, _).

query_response(Stream, Row, Fields) :-
  read_packet(Stream, Packet),
  (pgwire::parse(message(Message), Packet) -> true ; Message = Packet),
  query_response(Stream, Row, Fields, Message).

%query_response(Stream, Row, Fields, rowDescription(Rows)) :-
%  list::member(Row, Rows),

query_response(Stream, Row, Fields, Message) :-
  format::format("~w~n", [Message]),
  query_response(Stream, Row, Fields).


:- end_object.