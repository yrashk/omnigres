:- multifile prolog:error_message//1.

 prolog:error_message(postgres_error(Message)) -->
     [ 'Postgres error: ~w'-[Message] ].