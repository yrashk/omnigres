:- initialization((
        logtalk_load([edcg(loader)]),
        use_module(library(sgml)),
        use_module(library(http/http_open)),
        use_module(library(lists)),
        use_module(library(process)),
        use_module(library(socket)),
        use_module(library(system)),
        use_module(library(clpfd)),
        use_module(library(yaml)),
        use_module(library(broadcast)),
		logtalk_load([
		   meta(loader),
		   types(loader),
		   format(loader),
		   os(loader),
		   json(loader),
		   errors,
		   env,
		   pkgconfig,
		   pgwire,
		   postgres,
		   omnigres,
		   cli
		], [hook(edcg), debug(on)])
	)).