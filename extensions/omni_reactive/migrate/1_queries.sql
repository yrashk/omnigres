create table reactive_queries
(
    relations regclass[] not null default array []::regclass[],
    query     text       not null
);