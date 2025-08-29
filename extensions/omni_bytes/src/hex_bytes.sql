create or replace function hex_bytes(
    data bytea,
    width integer default 16,
    group_size integer default 1,
    byte_separator text default ' ',
    group_separator text default '  '
)
    returns table
            (
                line integer,
                hex  text
            )
    language plpgsql
    strict
as
$$
declare
    hex_line     text;
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
            hex_line := '';

            for j in 0..width - 1
                loop
                    if i + j < data_length then
                        byte_val := get_byte(data, i + j);
                        hex_line := hex_line || lpad(to_hex(byte_val), 2, '0');

                        -- Add spacing
                        if j < width - 1 and i + j < data_length - 1 then
                            if (j + 1) % group_size = 0 and group_size > 1 then
                                hex_line := hex_line || group_separator;
                            else
                                hex_line := hex_line || byte_separator;
                            end if;
                        end if;
                    end if;
                end loop;

            line := line_counter;
            hex := hex_line;
            return next;
        end loop;
end;
$$;
