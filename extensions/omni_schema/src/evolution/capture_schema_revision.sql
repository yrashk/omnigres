create function progress_report(message text) returns text 
language sql
as $$
        select omni_cloudevents.publish(
            omni_cloudevents.cloudevent(
                id => gen_random_uuid(),
                source => format('psql://%s/%s', (select system_identifier from pg_control_system()), current_database()),
                type => 'org.omnigres.omni_schema.progress_report.v1',
                data => message
            )
        );
$$;

create function capture_schema_revision(fs anyelement, source_path text, revisions_path text,
                                        rollback bool default true,
                                        parents revision_id[] default null) returns revision_id
    language plpgsql
as
$$
declare
    revision          revision_id       := uuidv7();
    host              text              := ' host=' ||
                                           current_setting('unix_socket_directories') || ' port=' ||
                                           current_setting('port') ||
                                           ' user=' || current_user;
    self_conn         text              := 'dbname=' || current_database() || host;
    revision_database text              := revision;
    revision_conninfo text              := 'dbname=' || revision_database || host;
    has_parents       bool              := coalesce(cardinality(parents), 0) > 0;
    revision_parents  revision_id[]     := coalesce(parents, '{}'); -- TODO: do we actually need to allow overriding?
    revision_db omni_sqlite.sqlite := $db$
      create table revision (id text);
      create table schema (content text);
      create table data (content text);
      create table file (path text, content blob);
      create table parent (id text);
    $db$::omni_sqlite.sqlite;
begin
    --- 0. Prepare parents

    if parents is null then
        -- Find revisions that are not parents to any other revisions
        select
            coalesce(array_agg(r.revision), '{}')
        into revision_parents
        from
            schema_revisions(fs, revisions_path, leafs_only => true) r;
        has_parents := coalesce(cardinality(revision_parents), 0) > 0;
    end if;

    declare
        parent text;
    begin
        foreach parent in array revision_parents
            loop
                revision_db :=
                        omni_sqlite.sqlite_exec(revision_db, 'insert into parent (id) values ($1)', row (parent::text));
            end loop;
    end;

    --- 1. Assemble current revision

    ---- Prepare the database in which we'll be assembling it. Make it a template database
    ---- so that services know it is not an operational database
    perform progress_report(format('Creating revision database %s', revision_database));
    perform dblink(self_conn, format('create database %I', revision_database));
    perform dblink(self_conn,
                   format('update pg_database set datistemplate = true where datname = %L', revision_database));


    perform progress_report('Assembling schema for revision');
    perform from assemble_schema(revision_conninfo, fs, source_path) where execution_error is not null;
    if found then
        raise exception 'New revision cannot be assembled due to errors' using hint = 'Run assemble_schema to see errors';
    end if;


    --- 3. Write metadata
    perform progress_report(format('Writing metadata'));

    revision_db := omni_sqlite.sqlite_exec(revision_db, 'insert into revision (id) values ($1)', row (revision::text));

    --- 4. Capture current source

    perform progress_report(format('Writing sources'));
    declare
        path    text;
        content bytea;
    begin
        for path, content in select name,
                                    omni_vfs.read(fs, source_path || '/' || name)
    from
        omni_vfs.list_recursively(fs, source_path)
                             where kind = 'file'
            loop
                revision_db := omni_sqlite.sqlite_exec(revision_db, 'insert into file (path, content) values ($1,$2)',
                                                       row (path, content));
            end loop;
    end;

    --- 5. Capture the schema
    perform progress_report('Capturing the revision''s schema');
    revision_db := omni_sqlite.sqlite_exec(revision_db, 'insert into schema (content) values ($1)',
                                           row (dump(revision_conninfo, schema => true, data => false)));

    perform progress_report('Capturing the revision''s data');
    revision_db := omni_sqlite.sqlite_exec(revision_db, 'insert into data (content) values ($1)',
                                           row (dump(revision_conninfo, schema => false, data => true)));

    --- 6. Prepare a migration boilerplate?
    --- 7. Transformation functions?
    --- X. Done

    ---- Clean up
    perform progress_report('Cleaning up');
    perform dblink(self_conn,
                   format('update pg_database set datistemplate = false where datname = %L', revision_database));
    perform dblink(self_conn, format('drop database %I', revision_database));

    if rollback then
        raise exception 'capture_schema_revision_done';
    end if;

    return revision;

exception
    when others then
        if sqlerrm = 'capture_schema_revision_done' then
            ---- Record the revision
            perform omni_vfs.write(fs, revisions_path || '/' || revision || '/revision.db',
                                   omni_sqlite.sqlite_serialize(revision_db),
                                   create_file => true);
            return revision;
        else
            ---- re-raise otherwise
            raise;
        end if;
end;
$$;
