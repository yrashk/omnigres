create or replace function bytes_offsets(data bytea, width integer default 16, hex boolean default true,
                                         length int default 8, padding text default '0')
    returns table
            (
                line integer,
                data_offset text
            )
    strict
    language plpgsql
as
$$
declare
    i            integer;
    data_length  integer;
    line_counter integer := 0;
begin
    data_length := length(data);

    for i in 0..data_length - 1 by width
        loop
            line_counter := line_counter + 1;
            line := line_counter;
            data_offset := lpad(case when hex then to_hex(i) else i::text end, length, padding);
            return next;
        end loop;
end;
$$;
