alter table languages
    add column bundled boolean not null default false;

update languages
set bundled = true;

select pg_catalog.pg_extension_config_dump('languages', 'where not bundled');

alter table auxiliary_tools
    add column bundled boolean not null default false;

update auxiliary_tools
set bundled = true;

select pg_catalog.pg_extension_config_dump('auxiliary_tools', 'where not bundled');
