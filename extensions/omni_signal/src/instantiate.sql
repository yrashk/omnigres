create function instantiate(schema regnamespace default 'omni_signal') returns void
    language plpgsql
as
$$
declare
    old_path text := current_setting('search_path');
begin
    -- Set the search path to target schema and public
    perform
        set_config('search_path', schema::text || ',public,pg_catalog', true);

    create table signal
    (
        relation regclass,
        query
            unique (relation)
    );

end;
$$;
