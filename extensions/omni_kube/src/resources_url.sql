create function resources_url(group_version text, resource text, namespace text default null) returns text
    immutable
return group_url(group_version) || '/' || coalesce('namespaces/' || namespace || '/', '') ||
       resource;
