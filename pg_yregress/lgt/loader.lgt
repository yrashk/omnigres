:- use_module(library(yaml)).

:- initialization((
     logtalk_load([os(loader), json(loader), types(loader), meta(loader)]),
     logtalk_load([pg_yregress, test])
   )).