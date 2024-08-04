--## set type_sizes = {"int1": 1, "uint1": 1}
--## set type_alignments = {"int1": "char", "uint1": "char"}
--## for type_name in ["int1","uint1"]
--## set fn_in = type_name + "_in"
--## set fn_out = type_name + "_out"

create type "/*{{ type_name }}*/";

create function "/*{{ fn_in }}*/"(cstring) returns "/*{{ type_name }}*/"
    immutable strict
    language c as
'MODULE_PATHNAME';

create function "/*{{ fn_out }}*/"("/*{{ type_name }}*/") returns cstring
    immutable strict
    language c as
'MODULE_PATHNAME';


create type "/*{{ type_name }}*/"
(
    input = "/*{{ fn_in }}*/",
    output = "/*{{ fn_out }}*/",
    internallength = /*{{ at(type_sizes, type_name) }}*/,
    passedbyvalue,
    alignment = /*{{ at(type_alignments, type_name) }}*/
);
--## endfor
