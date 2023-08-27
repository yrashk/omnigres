--- Common functionality
create function canonicalize_path(path text, absolute bool default false) returns text
    strict
    language c
as
'MODULE_PATHNAME',
'canonicalize_path_pg' immutable;

create function basename(path text) returns text
    strict
    language c
as
'MODULE_PATHNAME',
'file_basename' immutable;

create function dirname(path text) returns text
    strict
    language c
as
'MODULE_PATHNAME',
'file_dirname' immutable;

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

create function list(fs local_fs, path text, fail_unpermitted boolean default true) returns setof omni_vfs_types_v1.file as
'MODULE_PATHNAME',
'local_fs_list' language c;

create function file_info(fs local_fs, path text) returns omni_vfs_types_v1.file_info as
'MODULE_PATHNAME',
'local_fs_file_info' language c;

create function read(fs local_fs, path text, file_offset bigint default 0,
                     chunk_size int default null) returns bytea as
'MODULE_PATHNAME',
'local_fs_read' language c;

-- table_fs
create table table_fs_filesystems
(
    id   integer primary key generated always as identity,
    name text not null unique
);

create table table_fs_files
(
    id            bigint primary key generated always as identity,
    filesystem_id integer                not null references table_fs_filesystems (id),
    filename      text[]                 not null,
    created_at    timestamp,
    accessed_at   timestamp,
    modified_at   timestamp,
    kind          omni_vfs_types_v1.file_kind not null,
    unique (filesystem_id, filename)
);

create index table_fs_files_filename on table_fs_files using gin (filename);

create or replace function table_fs_files_trigger() returns trigger as
$$
begin
    new.filename := string_to_array(omni_vfs.canonicalize_path(array_to_string(new.filename, '/')), '/');
    return new;
end;
$$ language plpgsql;

create trigger table_fs_files_trigger
    before insert or update
    on table_fs_files
    for each row
execute function table_fs_files_trigger();


-- TODO: refactor this into [sparse] [reusable] chunks
create table table_fs_file_data
(
    id   bigint primary key generated always as identity,
    file bigint not null references table_fs_files (id),
    data bytea  not null
);

create type table_fs as
(
    id integer
);

create function table_fs(filesystem_name text) returns omni_vfs.table_fs
    language plpgsql
as
$$
declare
    result_id integer;
begin
    select
        id
    from
        omni_vfs.table_fs_filesystems
    where
        table_fs_filesystems.name = filesystem_name
    into result_id;
    if not found then
        insert
        into
            omni_vfs.table_fs_filesystems (name)
        values (filesystem_name)
        returning id into result_id;
    end if;
    return row (result_id);
end
$$;

create function list(fs table_fs, path text, fail_unpermitted boolean default true) returns setof omni_vfs_types_v1.file
    stable
    language sql
as
$$
with
    filepath(path) as (select
                           string_to_array(omni_vfs.canonicalize_path(path, absolute => true), '/')),
    directory_entries as (select
                              filesystem_id,
                              filename[array_length(filepath.path, 1) + 1] as entry,
                              kind
                          from
                              omni_vfs.table_fs_files,
                                       filepath
                          where
                              (case
                                   when array_length(filepath.path, 1) is null then true -- root directory
                                   else filepath.path <@ filename
                                  end) and
                              array_length(filename, 1) = coalesce(array_length(filepath.path, 1), 0) + 1)
select distinct
    row (entry, kind)::omni_vfs_types_v1.file
from
    directory_entries
where
    filesystem_id = fs.id
$$;

/*
create function file_info(fs table_fs, path text) returns omni_vfs_types_v1.file_info as
'MODULE_PATHNAME',
'local_fs_file_info' language c;

create function read(fs table_fs, path text, file_offset bigint default 0,
                     chunk_size int default null) returns bytea as
'MODULE_PATHNAME',
'local_fs_read' language c;
*/

-- Helpers

create function list_recursively(fs anyelement, path text, max bigint default null) returns setof omni_vfs_types_v1.file as
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
                               sub_file.kind)::omni_vfs_types_v1.file
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
        if not omni_vfs_types_v1.is_valid_fs('local_fs') then
            raise exception 'local_fs is not a valid vfs';
        end if;
    end;
$$ language plpgsql
