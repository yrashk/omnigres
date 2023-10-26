create domain port integer check (value >= 0 and value <= 65535);

create type http_request as
(
    method       omni_http.http_method,
    path         text,
    query_string text,
    body         bytea,
    headers              http_headers,
    websocket_client_key text
);

create function http_request(path text, method omni_http.http_method default 'GET', query_string text default null,
                             body bytea default null,
                             headers http_headers default array []::http_headers,
                             websocket_client_key text default null)
    returns http_request
    language sql
    immutable
as
$$
select row (method, path, query_string, body, headers, websocket_client_key)
$$;

create type http_response as
(
    body    bytea,
    status  smallint,
    headers http_headers
);

create type http_proxy as
(
    url           text,
    preserve_host boolean
);

create domain abort as omni_types.unit;

create type upgrade_to_websocket as
(
    topic      text,
    on_message text
);

select omni_types.sum_type('http_outcome', 'http_response', 'abort', 'http_proxy', 'upgrade_to_websocket');

create function abort() returns http_outcome as
$$
select omni_httpd.http_outcome_from_abort(omni_types.unit())
$$ language sql immutable;

create function http_proxy(url text, preserve_host boolean default true) returns http_outcome as
$$
select omni_httpd.http_outcome_from_http_proxy(row (url, preserve_host))
$$ language sql immutable;

create function http_response(
    body anycompatible default null,
    status int default 200,
    headers http_headers default null
)
    returns http_outcome
as
'MODULE_PATHNAME',
'http_response'
    language c immutable;

create function upgrade_to_websocket(topic text, on_message text default $$select null$$) returns http_outcome as
$$
select omni_httpd.http_outcome_from_upgrade_to_websocket(row (topic, on_message))
$$ language sql;

create type http_protocol as enum ('http', 'https');