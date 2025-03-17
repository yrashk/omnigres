create function instantiate(schema regnamespace default 'omni_pool') returns void
    language plpgsql
as
$$
begin
    -- Set the search path to target schema and public
    perform
        set_config('search_path', schema::text || ',public', true);

    create function test(stmt text default $s$do $ss$begin raise log 'hi'; end;$ss$ $s$) returns void
        language c as
    'MODULE_PATHNAME';

end;
$$;
