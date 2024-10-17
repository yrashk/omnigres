create or replace function _is_inherited_type(child regclass, parent regclass) returns boolean
    language sql
    stable
as
$$
with recursive inherited_types as (
    -- Start with the parent type
    select parent as inhrelid
    union all
    select inhrelid
    from pg_inherits
    where inhparent = parent

    union all

    -- Recursively find all children of the parent
    select i.inhrelid
    from pg_inherits i
             inner join inherited_types it
                        on i.inhparent = it.inhrelid)
select exists (select 1
               from inherited_types
               where inhrelid = child);


$$;
