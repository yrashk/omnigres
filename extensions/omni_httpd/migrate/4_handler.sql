create table handlers
(
    id    integer primary key generated always as identity,
    query text not null
);

create function handlers_query_validity_trigger() returns trigger
as
'MODULE_PATHNAME',
'handlers_query_validity_trigger' language c;

create constraint trigger handlers_query_validity_trigger
    after insert or update
    on handlers
    deferrable initially deferred
    for each row
execute function handlers_query_validity_trigger();

create table listeners_handlers
(
    listener_id integer not null references listeners (id),
    handler_id  integer not null references handlers (id)
);
create index listeners_handlers_index on listeners_handlers (listener_id, handler_id);

create function update_handler() returns trigger
    language plpgsql
as
$$
declare
    query text;
begin
    -- TODO: handle listener differentiation
    select
        omni_httpd.cascading_query('handler-' || handlers.id::text, handlers.query)
    from
        omni_httpd.listeners_handlers
        inner join omni_httpd.handlers on listeners_handlers.handler_id = handlers.id
    into query;
    query := omni_sql.add_cte(query::omni_sql.statement, 'request', 'select (__request).*'::omni_sql.statement,
                              prepend => true);
    query := 'begin return (' || query || '); end;';
    execute format(
                'create or replace function omni_httpd.handler(listener omni_httpd.listeners, __request omni_httpd.http_request)' ||
                'returns omni_httpd.http_outcome language plpgsql security definer as %L',
                query
        );
    return null;
end ;
$$;

create trigger handlers_updated
    after update or delete or insert
    on handlers
    for each statement
execute function update_handler();

create trigger listeners_handlers_updated
    after update or delete or insert
    on listeners_handlers
    for each statement
execute function update_handler();

create role omni_httpd_handler;
grant usage on schema omni_httpd to omni_httpd_handler;
grant usage on schema omni_http to omni_httpd_handler;
grant usage on schema omni_types to omni_httpd_handler;
grant select on table listeners, listeners_handlers, handlers to omni_httpd_handler;
grant execute on all functions in schema omni_httpd to omni_httpd_handler;
grant execute on all functions in schema omni_http to omni_httpd_handler;

create function handler(omni_httpd.listeners, omni_httpd.http_request) returns omni_httpd.http_outcome
    language plpgsql
    security definer
as
$$
begin
    return omni_httpd.http_response(status => 204);
end
$$;

alter function handler(omni_httpd.listeners, omni_httpd.http_request) owner to omni_httpd_handler;

