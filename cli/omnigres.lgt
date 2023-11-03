:- object(omnigres).

:- use_module(library(http/http_open), [http_open/3]).
:- meta_predicate(http_open:http_open(*, *, *)).

:- public([latest_commit/1, download_commit/2, extract_commit/2]).

:- table(latest_commit/1).

github_org(yrashk).
github_repo(omnigres).
github_branch('omnigres-cli').

latest_commit(Commit) :-
 github_org(Org), github_repo(Repo), github_branch(Branch),
 atomic_list_concat(['https://api.github.com/repos', Org, Repo, commits, Branch], '/', URL),
 setup_call_cleanup(
  http_open(URL,
            Stream,
            [request_header(accept='application/vnd.github.VERSION.sha')]),
  read_string(Stream, _, Commit),
  close(Stream)).

download_commit(Commit, Path) :-
  github_org(Org), github_repo(Repo),
  atomic_list_concat(['https://github.com/', Org, '/', Repo, '/archive/',
                     Commit,'.tar.gz'], URL),
  env::application_cache_path(omnigres, Cache),
  atomic_list_concat([Cache, omnnigres], '/', OmnigresPath),
  atomic_list_concat([Cache, omnnigres, Commit], '/', Path),
  os::ensure_directory(Path),
  atomic_list_concat([OmnigresPath, '/', Commit, '.tar.gz'], ArchivePath),
  setup_call_cleanup((http_open(URL, Stream, []),
                      open(ArchivePath, write, OutStream, [type(binary)])),
                      copy_stream_data(Stream, OutStream),
                      (close(OutStream), close(Stream))).

extract_commit(Commit, Path) :-
  env::application_cache_path(omnigres, Cache),
  atomic_list_concat([Cache, omnnigres], '/', OmnigresPath),
  atomic_list_concat([OmnigresPath, Commit], '/', Path),
  atomic_list_concat([OmnigresPath, '/', Commit, '.tar.gz'], ArchivePath),
  format::format(atom(Command), 'tar -xzf ~w -C ~w --strip-components=1', [ArchivePath, Path]),
  os::shell(Command).


:- end_object.