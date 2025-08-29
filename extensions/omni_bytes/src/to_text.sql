create function to_text(data bytea, encoding text default pg_catalog.getdatabaseencoding()) returns text
    strict
return convert_from(data, encoding);
