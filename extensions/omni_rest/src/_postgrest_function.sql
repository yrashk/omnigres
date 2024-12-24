create procedure _postgrest_function (request omni_httpd.http_request, function_name inout regproc, namespace inout text, settings postgrest_settings default postgrest_settings ())
language plpgsql
as $$
declare
    saved_search_path text := current_setting('search_path');
begin
    if request.method = 'GET' then
        namespace := omni_http.http_header_get (request.headers, 'accept-profile');
    end if;
    if request.method = 'POST' then
        namespace := omni_http.http_header_get (request.headers, 'content-profile');
    end if;
    if namespace is null and cardinality(settings.schemas) > 0 then
        namespace := settings.schemas[1]::text;
    end if;
    if namespace is null then
        return;
    end if;
    if not namespace::name = any (settings.schemas) then
        function_name := null;
        namespace := null;
        return;
    end if;
    perform
        set_config('search_path', namespace, true);
    function_name := to_regproc (split_part(request.path, '/', 3));
    if function_name is null or function_name::text like 'pg_%' then
        function_name := null;
        return;
    end if;
    perform
        set_config('search_path', saved_search_path, false);
end;
$$;

