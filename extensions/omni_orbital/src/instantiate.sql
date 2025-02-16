create or replace function instantiate(schema name default 'omni_orbital')
    returns void
    language plpgsql
as
$instantiate$
declare
    fs_name name              := 'omni_orbital';
    fs      omni_vfs.table_fs := omni_vfs.table_fs(fs_name);
begin
    perform set_config('search_path', schema::text || ',public', true);

    perform omni_httpd.instantiate_static_file_handler(schema => current_schema);


    create table static_file_router
    (
        like omni_httpd.urlpattern_router,
        pathname text,
        fs omni_vfs.table_fs
    );

    create function static_handler(req omni_httpd.http_request, router static_file_router) returns omni_httpd.http_outcome
    return static_file_handler(req, router.fs, path => router.pathname);

    perform omni_vfs.write(fs, 'index.html', $_____content$/*{% include "../frontend/index.html" %}*/$_____content$,
                           create_file => true);

    insert into static_file_router (match, handler, pathname, fs)
    with files as (select * from unnest('{/,/orb/:orb}'::text[]) t(file))
    select omni_httpd.urlpattern(file), 'static_handler'::regproc, '/index.html', fs
    from files;

    create table api_router
    (
        like omni_httpd.urlpattern_router
    );

    create function orbs_handler(req omni_httpd.http_request) returns omni_httpd.http_outcome
    return omni_httpd.http_response((select json_agg(jsonb_build_object('name', d.datname,
                                                                        'size', pg_database_size(d.datname),
                                                                        'size_pretty',
                                                                        pg_size_pretty(pg_database_size(d.datname)),
                                                                        'numbackends', st.numbackends,
                                                                        'xact_commit', st.xact_commit,
                                                                        'xact_rollback', st.xact_rollback))
                                     from pg_database d
                                              inner join pg_stat_database st on st.datid = d.oid
                                     where d.datname != 'postgres'
                                       and not d.datistemplate));

    insert into api_router (match, handler) values (omni_httpd.urlpattern('/orbs.json'), 'orbs_handler'::regproc);


end;
$instantiate$;