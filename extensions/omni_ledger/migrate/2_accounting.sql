create table accounting
(
    account_id account_id not null references accounts
);

create table assets
(
) inherits (accounting);

create table liabilities
(
) inherits (accounting);

create table equity
(
) inherits (accounting);

create table owners_equity
(
) inherits (equity);

create table revenue
(
) inherits (equity);

create table gains
(
) inherits (equity);

create table expenses
(
) inherits (equity);


/*{% include "../src/debit_normal.sql" %}*/

create or replace view accounting_balances as
select accounting.account_id,
       accounting.tableoid::regclass as type,
       credited,
       debited,
       balance * multiplier          as balance
from accounting
         inner join account_balances on account_balances.account_id = accounting.account_id
         join lateral (values (case
                                   when omni_ledger.
                                            debit_normal(accounting.tableoid) then -1
                                   else 1 end)) as t(multiplier) on true;

/*{% include "../src/_is_inherited_type.sql" %}*/
/*{% include "../src/ensure_affected_accounts_accounting_balances.sql" %}*/

create trigger balancing_accounting
    after insert
    on transfers
    for each statement
execute function ensure_affected_accounts_accounting_balances();
