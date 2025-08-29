create function to_bytes(data text, encoding text default getdatabaseencoding()) returns bytea
    strict
return convert_to(data, encoding);
