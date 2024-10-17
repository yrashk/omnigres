create function ensure_affected_accounts_accounting_balances() returns trigger
    language plpgsql
as
$$
declare
    rec          record;
    _assets      numeric;
    _liabilities numeric;
    _equity      numeric;
    _ledger_id   omni_ledger.ledger_id;
begin
    _ledger_id := null;
    for rec in
        select sum(balance) balance, ledger_id, type
        from omni_ledger.accounting_balances ab
                 inner join omni_ledger.statement_affected_accounts() sac
                            on sac.account_id = ab.account_id
        group by sac.ledger_id, type
        loop
            if _ledger_id is null or rec.ledger_id != _ledger_id then
                _assets := 0;
                _liabilities := 0;
                _equity := 0;
                if _ledger_id is not null then
                    if _assets != _liabilities + _equity then
                        raise exception 'violation: accounting did not balance for ledger %: A (%) != L (%) + E (%)', _ledger_id, _assets, _liabilities, _equity;
                    end if;
                end if;
                _ledger_id := rec.ledger_id;
            end if;
            case
                when rec.type = 'omni_ledger.assets'::regclass then _assets := _assets + rec.balance;
                when rec.type = 'omni_ledger.liabilities'::regclass then _liabilities := _liabilities + rec.balance;
                when omni_ledger._is_inherited_type(rec.type, 'omni_ledger.equity'::regclass)
                    then _equity := _equity + rec.balance;
                end case;

        end loop;
    -- for the last ledger:
    if _assets != _liabilities + _equity then
        raise exception 'violation: accounting did not balance for ledger %: A (%) != L (%) + E (%)', _ledger_id, _assets, _liabilities, _equity;
    end if;
    return null;
end;
$$;