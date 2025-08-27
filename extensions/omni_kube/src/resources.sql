create function resources(group_version text, resource text, label_selector text default null,
                          field_selector text default null)
    returns setof jsonb
    language plpgsql
as
$resources$
declare
    response jsonb = api(url_with(resources_url(group_version, resource), label_selector => label_selector,
                                  field_selector => field_selector));
begin
    return query select jsonb_array_elements(response -> 'items');
end;
$resources$;
