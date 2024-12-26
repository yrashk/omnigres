create or replace function _postgrest_function_call_arguments (fn regproc, passed_arguments text[])
    returns text immutable
    language sql
    as $$
    select
        string_agg(format('%1$I => $%2$s :: %3$I', name, idx, t.typname), ', ')
    from
        pg_proc p,
        unnest(proargnames, proargtypes, proargmodes)
    with ordinality as _ (name, type, mode, idx)
    join pg_type t on t.oid = type
where
    type is not null
    and p.oid = fn
    and name::text = any (passed_arguments)
$$;

create procedure postgrest_rpc (request omni_httpd.http_request, outcome inout omni_httpd.http_outcome, settings postgrest_settings default postgrest_settings ())
language plpgsql
as $$
declare
    function_reference regproc;
    query text;
    namespace text;
    result jsonb;
    arguments_definition text;
    argument_values jsonb;
    passed_arguments text[];
begin
    if outcome is distinct from null then
        return;
    end if;
    if request.method in ('GET', 'POST') then
        -- FIXME: write guard clause checking for function
        if split_part(request.path, '/', 2) <> 'rpc' then
            return;
        end if;
        call omni_rest._postgrest_function (request, function_reference, namespace, settings);
        if function_reference is null then
            return;
            -- terminate
        end if;
    else
        return;
        -- terminate;
    end if;
    select
        case request.method
        when 'GET' then
        (
            select
                array_agg(name)
            from
                unnest(omni_web.parse_query_string (request.query_string))
                with ordinality as _ (name, i)
            where
                i % 2 = 1)
        when 'POST' then
        (
            select
                array_agg(jsonb_object_keys)
            from
                jsonb_object_keys(convert_from(request.body, 'utf-8')::jsonb))
        end into passed_arguments;
    arguments_definition := coalesce(omni_rest._postgrest_function_call_arguments (function_reference, passed_arguments), '');
    query := format('select %1$s(%2$s) as result', function_reference, arguments_definition);
    select
        case request.method
        when 'GET' then
        (
            select
                jsonb_agg(value::text)
            from
                unnest(omni_web.parse_query_string (request.query_string))
                with ordinality as _ (value, i)
            where
                i % 2 = 0)
        when 'POST' then
        (
            select
                jsonb_agg(v)
            from
                jsonb_each_text(convert_from(request.body, 'utf-8')::jsonb) as _ (k,
                    v))
        end into argument_values;
    -- Run it
    declare message text;
    detail text;
    hint text;
    begin
        select
            omni_sql.execute (query, coalesce(argument_values, '[]'::jsonb)) -> 'result' into result;
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

