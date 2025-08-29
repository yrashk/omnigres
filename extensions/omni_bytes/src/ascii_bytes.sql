create or replace function ascii_bytes(data bytea, width integer default 16)
    returns table
            (
                line  integer,
                ascii text
            )
    strict
    language plpgsql
as
$$
declare
    ascii_line   text;
    byte_val     integer;
    i            integer;
    j            integer;
    data_length  integer;
    line_counter integer := 0;
begin
    data_length := length(data);

    for i in 0..data_length - 1 by width
        loop
            line_counter := line_counter + 1;
            ascii_line := '';

            for j in 0..width - 1
                loop
                    if i + j < data_length then
                        byte_val := get_byte(data, i + j);
                        if byte_val >= 32 and byte_val <= 126 then
                            ascii_line := ascii_line || chr(byte_val);
                        else
                            ascii_line := ascii_line || '.';
                        end if;
                    end if;
                end loop;

            line := line_counter;
            ascii := ascii_line;
            return next;
        end loop;
end;
$$;
