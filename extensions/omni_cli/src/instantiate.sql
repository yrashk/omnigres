create function instantiate(schema regnamespace default 'omni_cli') returns void
    language plpgsql
as
$instantiate$
declare
    old_search_path text := current_setting('search_path');
begin
    -- Set the search path to target schema and public
    perform
        set_config('search_path', schema::text || ',public', true);

    ---- Schema
    create table cli_command_attachment
    (
        command regclass not null unique,
        parent  regclass not null check (parent != command)
    );

    create view cli_command as
        select
            (regex_match(relname, 'omni_cli<(\w+)>'))[1] as name,
            d.description                                  as description,
            a.parent                                       as parent,
            pg_class.oid::regclass                       as relation,
            n.nspname                                    as schema_name,
            pg_class.relname                             as relation_name
        from
            pg_class
            inner join pg_namespace n on n.oid = pg_class.relnamespace
            left join pg_description         d on d.objoid = pg_class.oid and d.objsubid = 0
            left join cli_command_attachment a on a.command = pg_class.oid
        where
            relkind = 'r' and
            relname ~ 'omni_cli<(\w+)>';

    create view cli_command_argument as
        select
            cmd.relation                         as command_relation,
            cmd.name                             as command_name,
            att.attname as column_name,
            string_to_array(att.attname, '|')    as names,
            att.atttypid::regtype                as type,
            d.description                        as description,
            att.attnotnull and not att.atthasdef as required,
            case
                when att.attname !~ regex '^--' then row_number()
                                                     over (partition by att.attname !~ regex '^--' order by att.attnum) end
                                                 as position
        from
            pg_attribute              att
            inner join cli_command    cmd on cmd.relation = att.attrelid
            left join  pg_description d on d.objoid = att.attrelid and d.objsubid = att.attnum
        where
            att.attnum > 0 and
            att.attname !~ regex '^\(.*' and
            att.attisdropped = false
        order by position nulls first;

    --- Populate
    create table "omni_cli<version>"
    (
        "--short|-s"   boolean     not null default false,
        "(invoked_at)" timestamptz not null default now()
    );
    comment on table "omni_cli<version>" is 'Prints version information';
    comment on column "omni_cli<version>"."--short|-s" is 'Print only the version number';

    create function "omni_cli<version>"() returns trigger
        language plpgsql as
    $_$
    begin
        if new."--short|-s" then
            raise notice '{"jsonrpc": "2.0", "method": "console.print", "params": ["1.0"], "id": 1}';
        else
            raise notice '{"jsonrpc": "2.0", "method": "console.print", "params": ["[bold green]omni_cli 1.0[/bold green]"], "id": 1}';
            raise notice '{"jsonrpc": "2.0", "method": "file.list_dir", "params": ["/"], "id": 2}';
        end if;
        return new;
    end;
    $_$;

    create trigger "omni_cli<version>"
        after insert
        on "omni_cli<version>"
        for each row
    execute function "omni_cli<version>"();

    -- Restore the path
    perform set_config('search_path', old_search_path, true);
end
$instantiate$;
