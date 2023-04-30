create type statement;

create function statement_in(cstring) returns statement
as
'MODULE_PATHNAME',
'statement_in'
    language c strict
               immutable;

create function statement_out(statement) returns cstring
as
'MODULE_PATHNAME',
'statement_out'
    language c strict
               immutable;

create type statement
(
    input = statement_in,
    output = statement_out,
    like = text
);

create function add_cte(statement, name text, cte statement,
                        recursive bool default false, prepend bool default false) returns statement
as
'MODULE_PATHNAME',
'add_cte'
    language c strict
               immutable;

create function is_parameterized(statement) returns bool
as
'MODULE_PATHNAME',
'is_parameterized'
    language c strict
               immutable;

create function is_valid(statement) returns bool
as
'MODULE_PATHNAME',
'is_valid'
    language c strict
               immutable;

create function extended_sql(statement text, pipe boolean default false) returns statement
as
'MODULE_PATHNAME'
    language c;