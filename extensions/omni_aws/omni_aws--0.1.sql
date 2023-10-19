
create or replace function hash_canonical_request(
	method text,
	uri text,
	query text,
	headers text[],
	sheaders text[],
	payload text)
    returnS text
    LANGUAGE 'sql'
    COST 100
    VOLATILE PARALLEL UNSAFE

return (((((((((((method || chr(10)) || uri) || chr(10)) || query) || chr(10)) || array_to_string(headers, chr(10))) || chr(10)) || chr(10)) || array_to_string(sheaders, ';'::text)) || chr(10)) || payload);

create or replace function public.hash_string_to_sign(
	algorithm text,
	ts8601 timestamp with time zone,
	region text,
	service text,
	canonical_request_hash text,
	secret_access_key text)
    returnS text
    LANGUAGE 'sql'
    
return 
encode(hmac((((((((((((
	algorithm || chr(10)) || to_char((ts8601 AT TIME ZONE 'UTC'::text), 'YYYYMMDD"T"HH24MISS"Z"'::text)) || chr(10)) || 
	to_char((ts8601 AT TIME ZONE 'UTC'::text), 'YYYYMMDD'::text)) || '/') || region) || '/') || service) || '/aws4_request') || chr(10)) || 
	canonical_request_hash)::bytea
	, hmac('aws4_request', 
	   hmac(service::bytea, 
			hmac(region::bytea, 
				 hmac(to_char((ts8601 AT TIME ZONE 'UTC'::text), 'YYYYMMDD'::text)::bytea, 
					  ('AWS4' || secret_access_key)::bytea, 'SHA256'::text)::bytea, 
				 'SHA256'::text)::bytea, 
			'SHA256'::text)::bytea, 
	   'SHA256'::text)::bytea, 
	'SHA256'::text), 
'hex');

create or replace function public.list_objects_v2(
	region text,
	service text,
	bucket text,
	secret_access_key text,
	access_key_id text,
	uri text,
	query text,
	payload text,
	ts8601 timestamp with time zone)

returns table(debug text, response text) AS $$
	select 
	'',
	convert_from(body, 'utf-8')
from omni_httpc.http_execute(
        omni_httpc.http_request('https://'||bucket||'.'||service||'.'||region||'.amazonaws.com' || uri || query, 
            headers => array[
                omni_http.http_header('X-Amz-Content-Sha256', encode(digest(payload, 'sha256'), 'hex')),
                omni_http.http_header('X-Amz-Date', to_char((ts8601 AT TIME ZONE 'UTC'::text), 'YYYYMMDD"T"HH24MISS"Z"'::text)),
                omni_http.http_header('Authorization', 'AWS4-HMAC-SHA256 Credential='||access_key_id||'/'||to_char((ts8601 AT TIME ZONE 'UTC'::text), 'YYYYMMDD'::text)||'/'|| region ||'/'|| service ||'/aws4_request, SignedHeaders=host;x-amz-content-sha256;x-amz-date, Signature=' || 
                    hash_string_to_sign(
                        'AWS4-HMAC-SHA256', 
                        ts8601, 
                        region, 
                        service, 
                        hash_canonical_request(
                            'GET',
                            '/',
                            '',
                            array['host:'|| bucket ||'.'|| service ||'.'|| region ||'.amazonaws.com',
                                'x-amz-content-sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855',
                                'x-amz-date:'||to_char((ts8601 AT TIME ZONE 'UTC'::text), 'YYYYMMDD"T"HH24MISS"Z"'::text)],
                            '{"host",
                                "x-amz-content-sha256",
                                "x-amz-date"}', 
                            'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'
                        ), 
                        secret_access_key
                    )
				)
                ] 
            )
        );
$$ LANGUAGE SQL;