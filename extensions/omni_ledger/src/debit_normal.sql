create function debit_normal(regclass) returns boolean
    immutable
    language sql as
$$
select $1 in ('omni_ledger.expenses'::regclass, 'omni_ledger.assets'::regclass)
$$;