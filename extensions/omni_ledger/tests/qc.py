import hypothesis.strategies as st
from hypothesis import assume, settings
from hypothesis.stateful import Bundle, RuleBasedStateMachine, initialize, consumes, rule, run_state_machine_as_test, \
    precondition, invariant

import psycopg2
import psycopg2.pool as pool

cpool = pool.ThreadedConnectionPool(1, 20, 'dbname=omni_ledger user=yrashk host=localhost')


class AccountModel:
    def __init__(self, debits_allowed_to_exceed_credits: False, credits_allowed_to_exceed_debits: False, closed: False):
        if not debits_allowed_to_exceed_credits and not credits_allowed_to_exceed_debits:
            raise "invalid account"
        self.debits_allowed_to_exceed_credits = debits_allowed_to_exceed_credits
        self.credits_allowed_to_exceed_debits = credits_allowed_to_exceed_debits
        self.closed = closed
        self.debits = []
        self.credits = []

    @st.composite
    @staticmethod
    def any(draw):
        (dac, cad, closed) = draw(
            st.tuples(st.booleans(), st.booleans(), st.booleans()).filter(lambda t: (t[0] or t[1]) and not t[2]))
        return AccountModel(debits_allowed_to_exceed_credits=dac,
                            credits_allowed_to_exceed_debits=cad, closed=closed)

    def __repr__(self):
        return f"(d>c={self.debits_allowed_to_exceed_credits}, c>d={self.credits_allowed_to_exceed_debits}, closed={self.closed}"

    def debit(self, amount):
        self.debits.append(amount)

    def credit(self, amount):
        self.credits.append(amount)

    def balance(self):
        return sum(self.credits) - sum(self.debits)


class LedgerModel:
    def __init__(self):
        self.accounts = {}
        self.id = None

    @staticmethod
    @st.composite
    def any(draw):
        draw(st.booleans())
        return LedgerModel()


class LedgerWorldModel:
    def __init__(self):
        self.ledgers = {}


class OmniLedgerStateMachine(RuleBasedStateMachine):
    ledgers = Bundle("ledgers")
    equity_accounts = Bundle("equity_accounts")
    asset_accounts = Bundle("asset_accounts")
    liability_accounts = Bundle("liability_accounts")
    uncategorized_accounts = Bundle("uncategorized_accounts")
    categories = Bundle("categories")
    in_tx = False

    def __init__(self):
        super().__init__()
        # self.model = LedgerModel()
        self.conn = cpool.getconn()
        self.conn.autocommit = False
        cur = self.conn.cursor()
        cur.execute("""insert into omni_ledger.account_categories (name, type, debit_normal) values
                                                                          ('Assets','asset', true),
                                                                          ('Owner''s equity', 'equity', false),
                                                                          ('Liability', 'liability', false)
                                                                          returning id, name, type""")

        self.categories_ = {(row[1]): (row[0], row[2])
                            for row in cur.fetchall()}

        self.conn.commit()
        self.accounts = {}

    @precondition(lambda self: len(self.categories_) > 0)
    @rule(data=st.data(), target=categories)
    def injest_category(self, data):
        key = data.draw(st.sampled_from(sorted(self.categories_.keys())))
        category = self.categories_[key]
        del self.categories_[key]
        return category

    @precondition(lambda self: not self.in_tx and len(self.categories_) == 0)
    @rule()
    def start_tx(self):
        cur = self.conn.cursor()
        cur.execute("set transaction isolation level serializable")
        cur.execute("select")
        self.in_tx = True

    @rule(ledger=LedgerModel.any(), target=ledgers)
    @precondition(lambda self: self.in_tx)
    def start_a_ledger(self, ledger):
        cur = self.conn.cursor()
        cur.execute("insert into omni_ledger.ledgers default values returning id")
        (id,) = cur.fetchone()
        ledger.id = id
        # print(f"Opened ledger {id}")
        return ledger

    # @rule(ledger=consumes(ledgers))
    # @precondition(lambda self: self.in_tx)
    # def print(self, ledger):
    #     print(ledger.id)

    @rule(acc=AccountModel.any(), ledger=ledgers, target=uncategorized_accounts)
    @precondition(lambda self: self.in_tx)
    def open_account(self, acc, ledger):
        cur = self.conn.cursor()
        cur.execute(
            "insert into omni_ledger.accounts (ledger_id, debits_allowed_to_exceed_credits, credits_allowed_to_exceed_debits, closed) values (%s,%s,%s,%s) returning id",
            (ledger.id, acc.debits_allowed_to_exceed_credits, acc.credits_allowed_to_exceed_debits, acc.closed))
        (id,) = cur.fetchone()
        acc.id = id
        # print(f"Opened account {id} in ledger {ledger.id}")
        ledger.accounts[id] = acc
        return acc

    @rule(acc=consumes(uncategorized_accounts), target=asset_accounts)
    @precondition(lambda self: self.in_tx)
    def categorize_asset_account(self, acc):
        # print(f"Assigning asset to {acc.id}")
        assume(acc.credits_allowed_to_exceed_debits)
        cur = self.conn.cursor()
        cur.execute(
            "insert into omni_ledger.account_categorizations (account_id, category_id) select %s, account_categories.id from omni_ledger.account_categories where account_categories.name = %s",
            (acc.id, 'Assets'))
        return acc

    @rule(acc=consumes(uncategorized_accounts), target=liability_accounts)
    @precondition(lambda self: self.in_tx)
    def categorize_liability_account(self, acc):
        # print(f"Assigning liability to {acc.id}")
        cur = self.conn.cursor()
        cur.execute(
            "insert into omni_ledger.account_categorizations (account_id, category_id) select %s, account_categories.id from omni_ledger.account_categories where account_categories.name = %s",
            (acc.id, 'Liability'))
        return acc

    @rule(acc=consumes(uncategorized_accounts), target=equity_accounts)
    @precondition(lambda self: self.in_tx)
    def categorize_equity_account(self, acc):
        assume(acc.debits_allowed_to_exceed_credits)
        # print(f"Assigning equity to {acc.id}")
        cur = self.conn.cursor()
        cur.execute(
            "insert into omni_ledger.account_categorizations (account_id, category_id) select %s, account_categories.id from omni_ledger.account_categories where account_categories.name = %s",
            (acc.id, 'Owner''s equity'))
        return acc

    @rule(asset=asset_accounts, equity=equity_accounts, amount=st.integers(min_value=1, max_value=9223372036854775807))
    @precondition(lambda self: self.in_tx)
    def fund_from_equity(self, equity, asset, amount):
        assume(asset.balance() == 0)
        assume(not equity.closed)
        assume(not asset.closed)
        print(f"Transfer: {equity.id} → {asset.id}")
        cur = self.conn.cursor()
        cur.execute(
            "insert into omni_ledger.transfers (debit_account_id, credit_account_id, amount) values (%s, %s, %s)",
            (equity.id, asset.id, amount))
        equity.debit(amount)
        asset.credit(amount)
        cur.execute("select balance from omni_ledger.account_balances where account_id = %s",
                    (equity.id,))
        assert cur.fetchone()[0] == equity.balance()
        cur.execute("select balance from omni_ledger.account_balances where account_id = %s",
                    (asset.id,))
        assert cur.fetchone()[0] == asset.balance()

    @rule(data=st.data(), asset=asset_accounts, liability=liability_accounts)
    @precondition(lambda self: self.in_tx)
    def move_asset_to_liability(self, asset, liability, data):
        assume(asset.balance() > 0)
        amount = data.draw(st.integers(min_value=1, max_value=asset.balance()))
        assume(not liability.closed)
        assume(not asset.closed)
        print(f"[MOVE] Transfer: {asset.id} → {liability.id}")
        cur = self.conn.cursor()
        cur.execute(
            "insert into omni_ledger.transfers (debit_account_id, credit_account_id, amount) values (%s, %s, %s)",
            (asset.id, liability.id, amount))
        asset.debit(amount)
        liability.credit(amount)
        cur.execute("select balance from omni_ledger.account_balances where account_id = %s",
                    (asset.id,))
        assert cur.fetchone()[0] == asset.balance()
        cur.execute("select balance from omni_ledger.account_balances where account_id = %s",
                    (liability.id,))
        assert cur.fetchone()[0] == liability.balance()

    def teardown(self):
        cpool.putconn(self.conn)


settings.register_profile('wide', max_examples=2000)
settings.load_profile('wide')
run_state_machine_as_test(OmniLedgerStateMachine)
