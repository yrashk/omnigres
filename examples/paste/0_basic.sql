create extension if not exists omni_httpd;
create extension if not exists omni_web;

create table pastes
(
    id         uuid primary key not null default gen_random_uuid(),
    content    text,
    created_at timestamp        not null default now()
);

create function paste(request omni_httpd.http_request) returns omni_httpd.http_response
as
$$
insert
into
    pastes (content)
values
    (omni_web.param_get(request.body, 'input'))
returning omni_httpd.http_response(status => 302,
                                   headers => array [omni_httpd.http_header('Location', '/' || id)])
$$
    language sql;

create function show(request omni_httpd.http_request) returns omni_httpd.http_response
as
$$
select
    omni_httpd.http_response(body => content,
                             headers => array [omni_httpd.http_header('content-type', 'text/plain; charset=utf-8')])
from
    pastes
where
    id::text = split_part(request.path, '/', 2)
$$ language sql;


create function form(request omni_httpd.http_request) returns omni_httpd.http_response
as
$$
select
    omni_httpd.http_response(
                    '<html>' ||
                    '<head><link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bulma@0.9.4/css/bulma.min.css"></head>' ||
                    '<body class="container"><section class="section"><h1 class="title">Paste</h1><form method="POST"><div class="field"><div class="control"><textarea class="textarea" name="input"></textarea></div></div><div class="field"><div class="control"><input type="submit" class="button is-link"></div></div></form></section></body></html>',
                    headers => array [omni_httpd.http_header('content-type', 'text/html; charset=utf-8')])
$$
    language sql;

update omni_httpd.handlers
set
    query =
        (select
             omni_httpd.cascading_query(name, query order by priority desc nulls last)
         from
             (values
                  ('paste', $$select paste(request.*) from request where request.method = 'POST'$$,
                   1),
                  ('form', $$select form(request.*) from request where request.method = 'GET' and request.path = '/' $$,
                   1),
                  ('show', $$select show(request.*) from request where request.method = 'GET'$$,
                   0),
                  ('notfound', $$select omni_httpd.http_response(status => 404)$$,
                   -1)
                  ) routes(name, query, priority));