create table listeners
(
    id             integer primary key generated always as identity,
    address        inet          not null default '127.0.0.1',
    port           port          not null default 80,
    effective_port port          not null default 0,
    protocol       http_protocol not null default 'http'
);

create table configuration_reloads
(
    id          integer primary key generated always as identity,
    happened_at timestamp not null default now()
);

-- Wait for the number of configuration reloads to be `n` or greater
-- Useful for testing
create procedure wait_for_configuration_reloads(n int) as
$$
declare
    c  int = 0;
    n_ int = n;
begin
    loop
        with
            reloads as (select
                            id
                        from
                            omni_httpd.configuration_reloads
                        order by happened_at asc
                        limit n_)
        delete
        from
            omni_httpd.configuration_reloads
        where
            id in (select id from reloads);
        declare
            rowc int;
        begin
            get diagnostics rowc = row_count;
            n_ = n_ - rowc;
            c = c + rowc;
        end;
        exit when c >= n;
    end loop;
end;
$$ language plpgsql;

create function reload_configuration_trigger() returns trigger
as
'MODULE_PATHNAME',
'reload_configuration'
    language c;

create function reload_configuration() returns bool
as
'MODULE_PATHNAME',
'reload_configuration'
    language c;

create trigger listeners_updated
    after update or delete or insert
    on listeners
execute function reload_configuration_trigger();
