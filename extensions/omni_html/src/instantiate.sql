create function instantiate(schema regnamespace default 'omni_html') returns void
    language plpgsql
as
$instantiate$
declare
    old_search_path text := current_setting('search_path');
begin
    -- Set the search path to target schema and public
    perform
        set_config('search_path', schema::text || ',public', true);

    --- HTML document
    create type html_document;

    create function html_document_in(cstring)
        returns html_document
    as
    'MODULE_PATHNAME',
    'html_document_in'
        language c
        immutable
        strict;

    create function html_document_out(html_document)
        returns cstring
    as
    'MODULE_PATHNAME',
    'html_document_out'
        language c
        immutable
        strict;

    create type html_document
    (
        input = html_document_in,
        output = html_document_out,
        alignment = int4,
        storage = 'extended',
        internallength = -1
    );

    --- HTML element
    create type html_element;

    create function html_element_in(cstring)
        returns html_element
    as
    'MODULE_PATHNAME',
    'html_element_in'
        language c
        immutable
        strict;

    create function html_element_out(html_element)
        returns cstring
    as
    'MODULE_PATHNAME',
    'html_element_out'
        language c
        immutable
        strict;

    create type html_element
    (
        input = html_element_in,
        output = html_element_out,
        alignment = int4,
        storage = 'extended',
        internallength = -1
    );

    create function html_document(html_element)
        returns html_document
    as
    'MODULE_PATHNAME',
    'html_document_get'
        language c
        immutable
        strict;

    create function html_element_replace(html_element, html_element)
        returns html_element
    as
    'MODULE_PATHNAME'
        language c
        immutable
        strict;



    create function html_elements_by_tag_name(html_element, tag_name text)
        returns setof html_element
    as
    'MODULE_PATHNAME'
        language c
        immutable
        strict;

    create function html_elements_by_tag_name(html_document, tag_name text)
        returns setof html_element
    as
    'MODULE_PATHNAME',
    'html_elements_by_tag_name_doc'
        language c immutable
                   strict;

    create function html_transform(html_document, finder regproc, transformer regproc, arg anyelement default null)
        returns html_document as
    'MODULE_PATHNAME' immutable language c;


    -- Restore the path
    perform set_config('search_path', old_search_path, true);
end
$instantiate$;
