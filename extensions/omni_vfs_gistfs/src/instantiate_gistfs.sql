create function instantiate_gistfs(schema name default 'omni_vfs') returns void
    language plpgsql
as
$instantiate_gistfs$
begin
    perform
        set_config('search_path', schema::text || ',public', true);

    create type gistfs as
    (
        url text
    );

    create function gistfs(url text)
        returns gistfs
        immutable
        language sql as
    $$
    select row (url)::gistfs
    $$;
    execute format('alter function gistfs set search_path to %I', schema);

    create function list(fs gistfs, path text, fail_unpermitted boolean default true) returns setof omni_vfs_types_v1.file
        language plpgsql
    as
    $$
    declare
        url      text := fs.url;
        username text;
        gist     text;
        match    text[];
        digest   text;
        resp     omni_httpc.http_response;
    begin
        match := regexp_match(url, 'https://gist.github.com/(.+)/(.+)');
        if match is not null then
            username := match[1];
            gist := match[2];
        else
            raise exception 'Unsupported gist URL format %', url;
        end if;
        digest := encode(digest(url, 'sha256'), 'hex');
        resp := omni_var.get_statement('omni_vfs.gistfs_list_' || digest, null::omni_httpc.http_response);
        if resp is null then
            select (http_execute.*) into resp from omni_httpc.http_execute(omni_httpc.http_request(url));
            perform omni_var.set_statement('omni_vfs.gistfs_list_' || digest, resp);
        end if;
        if resp.status = 200 then
            return query select
                             parts[1] || '/' || parts[2],
                             'file'::omni_vfs_types_v1.file_kind
                         from
                             regexp_matches(convert_from(resp.body, 'utf8'),
                                            '"/' || username || '/' || gist || '/raw/([a-f0-9]{40})/([^"]+)',
                                            'g') t(parts);
        end if;
        return;
    end;
    $$;
    execute format('alter function list(gistfs,text,boolean) set search_path to %I,public', schema);

    create function read(fs gistfs, path text, file_offset bigint default 0,
                         chunk_size int default null) returns bytea
        language plpgsql
    as
    $$
    declare
        url      text := fs.url;
        username text;
        gist     text;
        match    text[];
        resp     omni_httpc.http_response;
    begin
        match := regexp_match(url, 'https://gist.github.com/(.+)/(.+)');
        if match is not null then
            username := match[1];
            gist := match[2];
        else
            raise exception 'Unsupported gist URL format %', url;
        end if;
        url := url || '/raw/' || path;
        select (http_execute.*) into resp from omni_httpc.http_execute(omni_httpc.http_request(url));
        if resp.status = 200 then
            return resp.body;
        else
            raise exception 'Error retrieving file: % resulted in %', url, resp.status;
        end if;
    end;
    $$;
    execute format('alter function read(gistfs,text,bigint,int) set search_path to %I', schema);


end;
$instantiate_gistfs$;