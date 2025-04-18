create function schema_revisions(fs anyelement, revisions_path text, leafs_only boolean default false)
    returns table
            (
                revision revision_id,
                parents  revision_id[],
                metadata jsonb
            )
    language plpgsql
as
$$
begin
    if leafs_only then
        return query
            with
                revisions as materialized (select * from schema_revisions(fs, revisions_path, leafs_only => false)),
                parent as materialized (select distinct unnest(revisions.parents) as parent_id from revisions)
            select
                r0.*
            from
                revisions r0
                left join parent on parent.parent_id = r0.revision
            where
                parent is not distinct from null;
    else
        return query with
                         recursive
                         revdb
                             as (select omni_sqlite.sqlite_deserialize(omni_vfs.read(fs, revisions_path || '/' || name)) as db
                                 from
                                     omni_vfs.list_recursively(fs, revisions_path)
                                 where omni_vfs.basename(name) = 'revision.db'),
                         revisions
                             as (select id             as revision,
                                        coalesce(array_agg(parent_id) filter (where parent_id is not null),
                                                 '{}') as parents,
                                        '{}'::jsonb    as metadata
                                 from revdb
                                          inner join lateral (select id::uuid::revision_id
                                                              from omni_sqlite.sqlite_query(db,
                                                                                            'select id from revision') as t (id text)) ids
                                                     on true
                                          left join lateral (select id::uuid::revision_id as parent_id
                                                              from omni_sqlite.sqlite_query(db,
                                                                                            'select id from parent') as t (id text)) parents
                                                     on true
                                 group by ids.id),
                         all_ancestors as (select
                                               r.revision,
                                               unnest(r.parents) as ancestor
                                           from
                                               revisions r
                                           union all
                                           select
                                               aa.revision,
                                               unnest(r.parents) as ancestor
                                           from
                                               all_ancestors  aa
                                               join revisions r
                                                    on aa.ancestor = r.revision),
                         descendant_counts as (select
                                                   ancestor                    as parent,
                                                   count(distinct aa.revision) as descendant_count
                                               from
                                                   all_ancestors aa
                                               group by ancestor)
                     select
                         r.revision as revision,
                         r.parents  as parents,
                         r.metadata as metadata
                     from
                         revisions                   r
                         left join descendant_counts dc
                                   on dc.parent = r.revision
                     order by coalesce(dc.descendant_count, 0) desc;
    end if;
end;
$$;
