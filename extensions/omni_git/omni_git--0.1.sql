CREATE TABLE repositories (
    id integer PRIMARY KEY GENERATED ALWAYS AS IDENTITY,
    metadata JSONB
);

CREATE TABLE config  (
    id integer PRIMARY KEY GENERATED ALWAYS AS IDENTITY,
    key text NOT NULL,
    value text,
    repository_id integer REFERENCES repositories(id)
);


CREATE TABLE objects (
    oid bytea PRIMARY KEY,
    type smallint,
    size integer,
    data bytea
);

CREATE TABLE refs (
    id integer PRIMARY KEY GENERATED ALWAYS AS IDENTITY,
    repository_id integer NOT NULL REFERENCES repositories(id),
    type smallint,
    reference bytea,
    peel bytea
);

CREATE FUNCTION clone(url text) RETURNS integer
    AS 'MODULE_PATHNAME', 'clone'
    LANGUAGE C;