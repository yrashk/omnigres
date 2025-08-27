create function watch_url(group_version text, resource text, resource_version text default null)
    returns text
    immutable
return
    resources_url(group_version, resource_version) || '?watch=1&resourceVersion=' || resource_version;
