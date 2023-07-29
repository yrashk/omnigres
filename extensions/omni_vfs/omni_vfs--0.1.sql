--- API surface
create type file_kind as enum ('dir', 'file');

create type file as
(
    name text,
    kind file_kind
);

create type file_info as
(
    size        bigint,
    created_at  timestamp,
    accessed_at timestamp,
    modified_at timestamp,
    kind        file_kind
);

create function is_valid_fs(type regtype) returns boolean
as
$$
declare
    result record;
begin
    select into result
    from
        pg_proc
        inner join pg_namespace on pg_namespace.nspname = 'omni_vfs' and pg_proc.pronamespace = pg_namespace.oid
    where
        pg_proc.proname = 'list' and
        pg_proc.proargtypes[0] = type::oid and
        pg_proc.proargtypes[1] = 'text'::regtype and
        pg_proc.proargtypes[2] = 'bool'::regtype and
        pg_proc.proretset and
        pg_proc.prorettype = 'omni_vfs.file'::regtype;
    if not found then
        raise warning '%s does not define valid `omni_vfs.list` function', type::text;
        return false;
    end if;

    select into result
    from
        pg_proc
        inner join pg_namespace on pg_namespace.nspname = 'omni_vfs' and pg_proc.pronamespace = pg_namespace.oid
    where
        pg_proc.proname = 'file_info' and
        pg_proc.proargtypes[0] = type::oid and
        pg_proc.proargtypes[1] = 'text'::regtype and
        not pg_proc.proretset and
        pg_proc.prorettype = 'omni_vfs.file_info'::regtype;
    if not found then
        raise warning '%s does not define valid `omni_vfs.file_info` function', type::text;
        return false;
    end if;

    select into result
    from
        pg_proc
        inner join pg_namespace on pg_namespace.nspname = 'omni_vfs' and pg_proc.pronamespace = pg_namespace.oid
    where
        pg_proc.proname = 'read' and
        pg_proc.proargtypes[0] = type::oid and
        pg_proc.proargtypes[1] = 'text'::regtype and
        pg_proc.proargtypes[2] = 'bigint'::regtype and
        pg_proc.proargtypes[3] = 'int'::regtype and
        not pg_proc.proretset and
        pg_proc.prorettype = 'bytea'::regtype;
    if not found then
        raise warning '%s does not define valid `omni_vfs.read` function', type::text;
        return false;
    end if;
    return true;
end;
$$ language plpgsql;

--- local_fs
create table local_fs_mounts
(
    id    integer primary key generated always as identity,
    mount text not null unique
);

alter table local_fs_mounts
    enable row level security;

create type local_fs as
(
    id integer
);

create function local_fs(mount text) returns local_fs as
'MODULE_PATHNAME' language c;

create function list(fs local_fs, path text, fail_unpermitted boolean default true) returns setof file as
'MODULE_PATHNAME',
'local_fs_list' language c;

create function file_info(fs local_fs, path text) returns file_info as
'MODULE_PATHNAME',
'local_fs_file_info' language c;

create function read(fs local_fs, path text, file_offset bigint default 0,
                     chunk_size int default null) returns bytea as
'MODULE_PATHNAME',
'local_fs_read' language c;

-- Helpers

create function list_recursively(fs anyelement, path text, max bigint default null) returns setof file as
$$
with
    recursive
    directory_tree as (select
                           file
                       from
                           lateral omni_vfs.list(fs, path) as file -- Top directory
                       union all
                       select
                           row ((directory_tree.file).name || '/' || sub_file.name,
                               sub_file.kind)::omni_vfs.file
                       from
                           directory_tree,
                           lateral omni_vfs.list(fs, (directory_tree.file).name, fail_unpermitted => false) as sub_file
                       where
                           (directory_tree.file).name != sub_file.name)
select *
from
    directory_tree
limit max;
$$
    language sql;

-- Checks

do
$$
    begin
        if not is_valid_fs('local_fs') then
            raise exception 'local_fs is not a valid vfs';
        end if;
    end;
$$ language plpgsql