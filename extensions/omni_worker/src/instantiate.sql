create function instantiate(schema regnamespace default 'omni_worker') returns void
    language plpgsql
as
$$
begin
    -- Set the search path to target schema and public
    perform
        set_config('search_path', schema::text || ',public', true);


    create table handlers
    (
        library text not null,
        name    text not null,
        primary key (library, name)
    );

    insert
    into
        handlers
    values ('MODULE_PATHNAME', 'sql');

    create function reload_handlers() returns trigger
        language c as
    'MODULE_PATHNAME';

    create trigger reload_handlers
        after insert or update or truncate or delete
        on handlers
        for each statement
    execute function reload_handlers();

    create function sql(stmt text, wait_ms int default null) returns bool
        language c as
    'MODULE_PATHNAME';

    create table io_handlers
    (
        library text not null,
        name    text not null,
        primary key (library, name)
    );

    insert
    into io_handlers
    values ('MODULE_PATHNAME', 'timer');

    create function reload_io_handlers() returns trigger
        language c as
    'MODULE_PATHNAME';

    create trigger reload_io_handlers
        after insert or update or truncate or delete
        on io_handlers
        for each statement
    execute function reload_io_handlers();

end;
$$;
