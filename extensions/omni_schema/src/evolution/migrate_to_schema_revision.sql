create function migrate_to_schema_revision(fs anyelement, revisions_path text,
                                           target revision_id, source_conn text,
                                           rollback boolean default true)
    returns
        table
        (
            source_schema text,
            target_schema text
        )
    language plpgsql
as
$$
declare
    host       text               := ' host=' ||
                                     current_setting('unix_socket_directories') || ' port=' ||
                                     current_setting('port') ||
                                     ' user=' || current_user;
    self_conn  text               := 'dbname=' || current_database() || host;
    source_db         name;
    revision_metadata jsonb;
    parents           revision_id[];
    diff_schema       name;
    result            jsonb;
    conn       text               := gen_random_uuid()::text;
    revdb_path text               := revisions_path || '/' || target || '/revision.db';
    revdb      omni_sqlite.sqlite := case
                                         when omni_vfs.file_info(fs, revdb_path) is distinct from null then
                                             omni_sqlite.sqlite_deserialize(omni_vfs.read(fs, revdb_path))
        end;
    schema     text;
begin
    if revdb is null then
        raise exception 'No revision.db for %', target;
    end if;
    select db into source_db from dblink(source_conn, 'select current_database()') t(db name);

    /*
    perform from dblink(source_conn, $q$select relname from pg_class c
        inner join pg_namespace ns on ns.oid = c.relnamespace and ns.nspname = 'omni_schema' where relname = 'deployed_revision'$q$) t(relname name);
     */

    /*
    if found then
        perform from dblink(source_conn, format($q$select true from omni_schema.deployed_revision where revision = %L$q$, target)) t(found bool);
        if found then
            -- Don't try to provision an existing revision
            return null;
        end if;
    end if;
     */

    -- TODO:
    -- For now, we assume the case of direct Source->Target migration but we should be able to find
    -- the path and apply this function for all steps.

    select array_agg(id)::uuid[]::revision_id[]
    into parents
    from omni_sqlite.sqlite_query(revdb, 'select id from parent') t(id);

    select content
    into schema
    from omni_sqlite.sqlite_query(revdb, 'select content from schema') as t(content);

    /*
    if omni_vfs.file_info(fs, revisions_path || '/' || target || '/metadata.yaml') is distinct from null then
        revision_metadata :=
                omni_yaml.to_json(convert_from(omni_vfs.read(fs, revisions_path || '/' || target || '/metadata.yaml'),
                                               'utf8'))::jsonb;
        parents := array((select jsonb_array_elements_text(revision_metadata -> 'parents')))::revision_id[];
    else
        raise exception 'Metadata file % is not found', revisions_path || '/' || target || '/metadata.yaml';
    end if;
     */

    --- 1. Check for pre-conditions

    --- 1.0. Make sure source database has omni_schema and omni_schema.deployed_revision


    perform progress_report(format('Preparing the database'));
    ---- Install omni_schema in the target schema to get its meta
    /*perform dblink(source_conn, format('create extension if not exists omni_schema version %L cascade',
                                       (select extversion from pg_extension where extname = 'omni_schema')));

    perform dblink(source_conn,
                   format('create table if not exists omni_schema.deployed_revision (revision omni_schema.revision_id)'));*/

    --- 1.1. The source database must be a parent of the target revision
    /*
    declare
        num_parents int;
    begin
        -- However, there's a case when this is the first revision and then everything is good
        if cardinality(parents) > 0 then
            select
                count
            into num_parents
            from
                dblink(source_conn,
                       format('select distinct count(*) from omni_schema.deployed_revision where revision = any (%L)',
                              parents)) t(count int);
            if num_parents != cardinality(parents) then
                raise exception '% database is not a parent to %', source_db, target;
            end if;
        end if;
    end;*/

    --     --- 2. Freeze source's schema meta
--
--     ---- Create a foreign server to connect to the revision
--     execute format(
--             $sql$create server %1$I foreign data wrapper postgres_fdw options(host %2$L, dbname %1$L, port %3$L)$sql$,
--             source_db, current_setting('unix_socket_directories'), current_setting('port'));
--     execute format('create user mapping for %1$I server %2$I options (user %1$L)',
--                    current_user, source_db);
--

    --- 2. Run necessary migrations
    perform dblink_connect(conn, source_conn);
    perform dblink(conn, 'begin');
    if cardinality(parents) = 0 then
        --- 2.0. Just assemble it if there are no parents (first migration)
        declare
            rec record;
        begin
            perform progress_report(format('Initializing first revision'));
            if revdb is distinct from null then
                for rec in select * from omni_sql.raw_statements(schema::cstring, true)
                    loop
                        perform dblink_exec(conn, rec.source || ';do $X$begin end;$X$;');
                    end loop;
            end if;
        end;
    else
        --- 2.1. Run migrate.sql file
        declare
            migration_path text;
            migration      text;
            rec            record;
        begin
            migration_path := revisions_path || '/' || target || '/migrate.sql';
            if omni_vfs.file_info(fs, migration_path) is distinct from null then
                perform progress_report(format('Applying migrations'));
                migration := convert_from(omni_vfs.read(fs, migration_path), 'utf8');
                for rec in select * from omni_sql.raw_statements(migration::cstring, true)
                    loop
                        case
                            when omni_sql.statement_type(rec.source::omni_sql.statement) = 'TransactionStmt'
                                then raise exception 'No transactional statements allowed' using detail =
                                        format('Line %s, Column %s: %s', rec.line, rec.col, rec.source);
                            when omni_sql.statement_type(rec.source::omni_sql.statement) = 'MultiStmt' and
                                 omni_sql.statement_type((select array_agg(source order by ordinality)
                                                          from
                                                              omni_sql.raw_statements(rec.source::cstring) with ordinality
                                                          limit 1)[1]::omni_sql.statement) =
                                 'TransactionStmt'
                                then raise exception 'No transactional statements allowed' using detail =
                                        format('Line %s, Column %s: %s', rec.line, rec.col, rec.source);
                            else null;
                            end case;
                        begin
                            perform dblink(conn, rec.source);
                        exception
                            when others then
                                perform dblink(conn, 'rollback');
                                perform dblink_disconnect(conn);
                                raise using hint = rec.source;
                        end;
                    end loop;
            end if;
        end;
        ---- Stamp new revision
--         perform dblink_exec(conn,
--                             format('insert into omni_schema.deployed_revision (revision) values (%L)',
--                                    target));

    end if;


    ---- Define remote meta
    execute format('create schema %I', 'current_revision');
    perform create_remote_meta('current_revision'::regnamespace, 'omni_schema'::name, conn,
                               materialize => true);


    --- 3. Provision the schema of the revision
    ----   We are doing this to compare them
    perform progress_report(format('Creating captured revision database %s', target));
    ---- TODO: in the future, we might want to be able to use a different database cluster
    ----       to avoid doing this on the production database
    declare
        rec         record;
        target_conn text := 'dbname=' || target || host;
    begin
        perform dblink(self_conn, format('create database %I', target));
        perform dblink(self_conn,
                       format('update pg_database set datistemplate = true where datname = %L', target));
        perform dblink_connect(target::text, target_conn);
        for rec in select * from omni_sql.raw_statements(schema::cstring, true)
            loop
                perform dblink_exec(target::text, rec.source || ';do $X$begin end;$X$;');
            end loop;

        perform dblink_disconnect(target::text);
    end;


--- 4. Diff-ish


    if result is null then
        perform progress_report(format('Migrations applied successfully'));
        perform dblink(conn, 'commit');
        perform dblink_disconnect(conn);
    else
        perform progress_report(format('Schema is not matching expectations, rolled back'));
        raise notice 'Schema is not matching expectation, rolling back';
        perform dblink(conn, 'rollback');
        perform dblink_disconnect(conn);
    end if;

    --- 5. Compare the data with the target revision, revision's data should be present

    --- 6. Cleanup
    perform dblink(self_conn,
                   format('update pg_database set datistemplate = false where datname = %L', target));
    perform dblink(self_conn, format('drop database %I', target));

    if rollback then
        raise exception 'migrate_to_schema_revision_done';
    end if;

    return result;

exception
    when others then
        if sqlerrm = 'migrate_to_schema_revision_done' then
            return result;
        else
            ---- re-raise otherwise
            raise;
        end if;

end;
$$;
