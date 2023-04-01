begin;
with
    listener as (insert into omni_httpd.listeners (address, port) values ('127.0.0.1', 9101) returning id),
    handler as (insert into omni_httpd.handlers (query)
        select
            omni_httpd.cascading_query(name, query order by priority desc nulls last)
        from
            (values
                 ('sleep',
                  $$select omni_httpd.http_response(pg_sleep(5)::text) from request where request.path = '/sleep'$$, 1),
                 ('other',
                  $$select omni_httpd.http_response('test')$$,
                  1)) as routes(name, query, priority)
        returning id)
insert
into
    omni_httpd.listeners_handlers (listener_id, handler_id)
select
    listener.id,
    handler.id
from
    listener,
    handler;
delete
from
    omni_httpd.configuration_reloads;
end;

call omni_httpd.wait_for_configuration_reloads(1);

-- Should complete it faster than 5*2 seconds. 7s is generously accounting for the overhead.
-- As per `timeout`'s man page:
--        124    if COMMAND times out, and --preserve-status is not specified
\! timeout 7s curl --retry-connrefused --silent --parallel --http2 http://localhost:9101/sleep http://localhost:9101/sleep ; [ $? -eq 124 ] && echo "timeout"