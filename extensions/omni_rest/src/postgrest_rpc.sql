create procedure postgrest_rpc (request omni_httpd.http_request, outcome inout omni_httpd.http_outcome, settings postgrest_settings default postgrest_settings ())
language plpgsql
as $$
declare
    function_name regproc;
    query text;
    namespace text;
    result jsonb;
begin
    if outcome is distinct from null then
        return;
    end if;
    if request.method in ('GET', 'POST') then
        -- FIXME: write guard clause checking for function
        if split_part(request.path, '/', 2) <> 'rpc' then
            return;
        end if;
        call omni_rest._postgrest_function (request, function_name, namespace, settings);
        if function_name is null then
            return;
            -- terminate
        end if;
    else
        return;
        -- terminate;
    end if;
    query := format('select %1$s() as result', function_name);
    -- Run it
    declare message text;
    detail text;
    hint text;
    begin
        select
            omni_sql.execute (query) -> 'result' into result;
        outcome := omni_httpd.http_response (result);
    exception
        when others then
            get stacked diagnostics message = message_text,
            detail = pg_exception_detail,
            hint = pg_exception_hint;
    outcome := omni_httpd.http_response (jsonb_build_object('message', message, 'detail', detail, 'hint', hint), status => 400);
    end;
end;

$$;

