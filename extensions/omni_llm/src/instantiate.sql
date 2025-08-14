create function instantiate(schema regnamespace default 'omni_llm') returns void
    language plpgsql
as
$instantiate$
declare
    old_search_path text := current_setting('search_path');
begin
    -- Set the search path to target schema and public
    perform
        set_config('search_path', schema::text || ',omni_polyfill,public', true);

    create table openai_endpoint
    (
        name  text not null primary key,
        url   text not null,
        token text not null
    );


    perform identity_type('openai_chat_completion_id', type => 'uuid', nextval => 'uuidv7()');

    create table openai_function_toolset
    (
        function    regprocedure not null,
        description text
    );

    create table openai_chat_completion
    (
        openai_chat_completion_id openai_chat_completion_id not null primary key default openai_chat_completion_id_nextval(),
        openai_endpoint           text                      not null references openai_endpoint (name),
        model                     text                      not null,
        toolset    regclass,
        created_at timestamptz not null default current_timestamp,
        updated_at timestamptz not null default current_timestamp
    );

    create table openai_chat_completion_message
    (
        openai_chat_completion_id openai_chat_completion_id references openai_chat_completion (openai_chat_completion_id),
        position     int         not null,
        role                      text not null,
        content text not null,
        tool_calls   jsonb,
        tool_call_id text,
        created_at   timestamptz not null default current_timestamp,
        unique (openai_chat_completion_id, position)
    );

    create table openai_chat_completion_response
    (
        openai_chat_completion_id openai_chat_completion_id references openai_chat_completion (openai_chat_completion_id),
        response jsonb,
        error                     text,
        created_at                timestamptz not null default current_timestamp
    );


    create table openai_chat_completion_choice
    (
        openai_chat_completion_id openai_chat_completion_id references openai_chat_completion (openai_chat_completion_id),
        finish_reason             text not null,
        index                     int  not null,
        role                      text not null,
        content                   text not null,
        reasoning                 text,
        tool_calls                jsonb,
        created_at                timestamptz not null default current_timestamp
    );

    create function openai_function_tool(proc regprocedure, proc_description text default null)
        returns json
    begin
        atomic;
        select
            json_build_object(
                    'type', 'function',
                    'function', json_build_object(
                            'name', ns.nspname || '___' || p.proname,
                            'description', coalesce(proc_description, d.description),
                            'parameters', json_build_object(
                                    'type', 'object',
                                    'properties', coalesce(
                                            (select
                                                 json_object_agg(
                                                         coalesce(arg_names[i], 'arg' || i),
                                                         json_build_object(
                                                                 'type', case
                                                                             when arg_types[i] in ('int4'::regtype, 'int8'::regtype, 'int2'::regtype)
                                                                                 then 'integer'
                                                                             when arg_types[i] in
                                                                                  ('float4'::regtype, 'float8'::regtype,
                                                                                   'numeric'::regtype) then 'number'
                                                                             when arg_types[i] in ('bool'::regtype)
                                                                                 then 'boolean'
                                                                             when arg_types[i] in
                                                                                  ('text'::regtype, 'varchar'::regtype,
                                                                                   'char'::regtype, 'name'::regtype)
                                                                                 then 'string'
                                                                             when arg_types[i] in ('json'::regtype, 'jsonb'::regtype)
                                                                                 then 'object'
                                                                             when format_type(arg_types[i], null) like '%[]'
                                                                                 then 'array'
                                                                             else 'string'
                                                             end,
                                                                 'description', coalesce(arg_names[i], 'arg' || i)
                                                         )
                                                 )
                                             from
                                                 generate_subscripts(p.proargnames, 1) as i
                                             where
                                                 p.proargmodes is null or
                                                 p.proargmodes[i] in ('i', 'b') -- input or inout parameters only
                                            ),
                                            '{}'::json
                                                  ),
                                    'required', coalesce(
                                            array_to_json(
                                                    array(
                                                            select
                                                                coalesce(arg_names[i], 'arg' || i)
                                                            from
                                                                generate_subscripts(p.proargnames, 1) as i
                                                            where
                                                                (p.proargmodes is null or p.proargmodes[i] in ('i', 'b')) and
                                                                i <= (p.pronargs - p.pronargdefaults) -- parameters without defaults
                                                    )
                                            ),
                                            '[]'::json
                                                )
                                          )
                                )
            )
        from
            pg_proc                   p
            inner join pg_namespace   ns on ns.oid = p.pronamespace
            left join  pg_description d on d.objoid = p.oid and d.objsubid = 0
            cross join lateral (
                           select
                               case
                                   when p.proargnames is not null then p.proargnames
                                   else array []::text[] end as arg_names,
                               p.proargtypes                 as arg_types
                           ) as       args
        where
            p.oid = proc;
    end;


    create function openai_chat_completion_choice() returns trigger
        language plpgsql as
    $openai_chat_completion_choice$
    declare
        rec          record;
        func_result  text;
        func_name    text;
        schema_name  text;
        proc_name    text;
        max_position int;
    begin
        select
            max(position)
        into max_position
        from
            openai_chat_completion_message
        where
            openai_chat_completion_id::text = new.openai_chat_completion_id::text; -- FIXME: use proper schema_path
        insert
        into
            openai_chat_completion_message (openai_chat_completion_id, position, role, content, tool_calls)
        values (new.openai_chat_completion_id, max_position + new.index + 1, new.role, new.content, new.tool_calls);
        select
            max(position)
        into max_position
        from
            openai_chat_completion_message
        where
            openai_chat_completion_id::text = new.openai_chat_completion_id::text; -- FIXME: use proper schema_path
        if new.finish_reason = 'tool_calls' then
            for rec in select
                           call ->> 'id'                      as call_id,
                           call -> 'function' ->> 'name'      as name,
                           call -> 'function' ->> 'arguments' as arguments
                       from
                           pg_catalog.jsonb_array_elements(new.tool_calls) t(call)
                       where
                           call ->> 'type' = 'function'
                loop
                    -- Parse schema and function name
                    if position('___' in rec.name) > 0 then
                        schema_name := split_part(rec.name, '___', 1);
                        proc_name := split_part(rec.name, '___', 2);
                        func_name := quote_ident(schema_name) || '.' || quote_ident(proc_name);
                    else
                        func_name := quote_ident(rec.name);
                    end if;

                    begin
                        -- Execute the function with JSON arguments
                        execute format('select %s(%s)',
                                       func_name,
                                       case
                                           when rec.arguments is not null and rec.arguments::text != '{}' then
                                               -- Convert JSON object to named parameters
                                               (select
                                                    string_agg(format('%I => %L', key, value), ', ')
                                                from
                                                    jsonb_each_text(rec.arguments::jsonb))
                                           else
                                               ''
                                           end
                                ) into func_result;

                        -- Insert the function call result
                        insert
                        into
                            openai_chat_completion_message (openai_chat_completion_id,
                                                            role,
                                                            content,
                                                            tool_call_id, position)
                        values
                            (new.openai_chat_completion_id,
                             'tool',
                             coalesce(func_result, 'null'),
                             rec.call_id, max_position + 1);

                        select
                            max(position)
                        into max_position
                        from
                            openai_chat_completion_message
                        where
                            openai_chat_completion_id::text = new.openai_chat_completion_id::text; -- FIXME: use proper schema_path

                        update openai_chat_completion
                        set
                            updated_at = now()
                        where
                            openai_chat_completion_id::text = openai_chat_completion_id::text;
                        -- FIXME: use proper schema_path

--                         raise log 'Function % called with arguments % returned: %',
--                             rec.name, rec.arguments, func_result;

                    exception
                        when others then
                            -- Handle function call errors
                            insert
                            into
                                omni_llm.openai_chat_completion_message (openai_chat_completion_id,
                                                                         role,
                                                                         content,
                                                                         tool_call_id, position)
                            values
                                (new.openai_chat_completion_id,
                                 'tool',
                                 format('Error calling function %s: %s', rec.name, sqlerrm),
                                 rec.call_id, max_position + 1);

--                             raise notice 'Error calling function %: %', rec.name, sqlerrm;
                    end;
                end loop;
        end if;
        return new;
    end;
    $openai_chat_completion_choice$;
    execute format('alter function openai_chat_completion_choice set search_path to %I', schema);

    create trigger openai_chat_completion_choice
        after insert
        on openai_chat_completion_choice
        for each row
    execute function openai_chat_completion_choice();

    create function openai_chat_completion_response() returns trigger
        language plpgsql as
    $openai_chat_completion_response$
    declare
    begin
        insert
        into
            openai_chat_completion_choice (openai_chat_completion_id, finish_reason, index, role, content, reasoning,
                                           tool_calls)
        select
            new.openai_chat_completion_id,
            choice ->> 'finish_reason',
            (choice ->> 'index')::int,
            choice -> 'message' ->> 'role',
            choice -> 'message' ->> 'content',
            choice -> 'message' ->> 'reasoning',
            (choice -> 'message' -> 'tool_calls')::jsonb
        from
            jsonb_array_elements(new.response -> 'choices') t(choice);
        return new;
    end;
    $openai_chat_completion_response$;
    execute format('alter function openai_chat_completion_response set search_path to %I', schema);

    create trigger openai_chat_completion_response
        after insert
        on openai_chat_completion_response
        for each row
    execute function openai_chat_completion_response();

    create function openai_chat_completion_insertion() returns trigger
        language plpgsql
    as
    $openai_chat_completion_insertion$
    begin
        perform omni_worker.sql(format($sql$
         with r as (
           select omni_httpc.http_execute_with_options(omni_httpc.http_execute_options(first_byte_timeout => 1000*60),omni_httpc.http_request(url || '/chat/completions', method => 'POST',
           headers => array[omni_http.http_header('content-type','application/json'),
                            omni_http.http_header('authorization','Bearer ' || token)],
           body => convert_to(json_build_object(
            'model', %2$L,
            'tools', (select json_agg(%1$I.openai_function_tool(function, description)) from %5$I),
            'messages', (select json_agg(json_build_object('role',m.role, 'content', m.content, 'tool_call_id', m.tool_call_id)) from %1$I.openai_chat_completion_message m where m.openai_chat_completion_id = %4$L)
           )::text, 'utf8'))) response
           from %1$I.openai_endpoint ep
           where ep.name = %3$L)
         insert into %1$I.openai_chat_completion_response (openai_chat_completion_id, response, error)
         select %4$L, convert_from((response).body,'utf-8')::jsonb, (response).error from r
         $sql$, current_setting('omni_llm.schema'), new.model, new.openai_endpoint, new.openai_chat_completion_id,
                                       coalesce(new.toolset,
                                                (current_setting('omni_llm.schema') || '.openai_function_toolset') :: regclass)));
        return new;
    end;
    $openai_chat_completion_insertion$;
    execute format($$alter function openai_chat_completion_insertion set omni_llm.schema = %I$$,
                   schema);

    create constraint trigger openai_chat_completion_insertion
        after insert or update
        on openai_chat_completion
        deferrable initially deferred
        for each row
    execute function openai_chat_completion_insertion();


    -- Restore the path
    perform set_config('search_path', old_search_path, true);
end
$instantiate$;
