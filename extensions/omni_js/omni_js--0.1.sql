create function javascript_handler() returns language_handler
as
'MODULE_PATHNAME' language c;

create trusted language javascript handler javascript_handler;

alter language javascript owner to @extowner@;

comment on language javascript is 'JavaScript language (trusted)';