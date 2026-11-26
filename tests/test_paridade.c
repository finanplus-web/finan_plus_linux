/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Paridade com o app Android 1.7 a 1.13: os casos de TransferTest, GoalReserveTest, SharedTest, TotalsTest,
 * DebtsTest, DayTotalTest, CategoryMigrationTest, AccumulatedBalanceTest, AccumulatedReportTest e TxFormTest,
 * portados para C. Rode com "meson test".
 */
#include "core/model.h"
#include "core/finance.h"
#include "core/ops.h"
#include "core/backup.h"
#include "core/period.h"
#include "core/report.h"
#include "core/shared.h"
#include "core/transfers.h"
#include "core/totals.h"
#include "core/debts.h"
#include "core/txform.h"
#include <string.h>

static Day D(const char *s) {
    Day d;
    g_assert_true(day_parse_iso(s, &d));
    return d;
}

static gboolean contains(const char *hay, const char *needle) { return hay && strstr(hay, needle) != NULL; }

/* estado só com as contas pedidas (id, nome, saldo inicial), sem lançamentos */
static AppState *with_accounts(int n, ...) {
    AppState *s = app_state_new();
    g_ptr_array_set_size(s->accounts, 0);
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        const char *id = va_arg(ap, const char *);
        const char *name = va_arg(ap, const char *);
        Cents ini = va_arg(ap, Cents);
        g_ptr_array_add(s->accounts, account_new(id, name, ini));
    }
    va_end(ap);
    return s;
}

static Tx *add_tx(AppState *s, const char *id, Kind k, Cents v, const char *date, const char *desc, const char *cat,
                  gboolean paid, const char *acc, const char *card) {
    Tx *t = tx_new(id, k, v, D(date), desc, cat, paid, acc, card ? card : "");
    g_ptr_array_add(s->txs, t);
    return t;
}

static int count_transfers(const AppState *s) {
    int n = 0;
    for (guint i = 0; i < s->txs->len; i++) if (tx_is_transfer((Tx *)s->txs->pdata[i])) n++;
    return n;
}

static AppState *roundtrip(const AppState *s, Dropped *dd) {
    g_autofree char *json = backup_to_json(s, NULL);
    g_autoptr(GError) e = NULL;
    AppState *b = backup_parse(json, -1, dd, &e);
    g_assert_no_error(e);
    return b;
}

/* ================================================================ transferências (TransferTest) */

static const char *TODAY_T = "2026-11-10";

/* R$ 250 da Conta principal para a Poupança, mais uma despesa comum de R$ 45 no mesmo dia */
static AppState *with_transfer(void) {
    AppState *s = with_accounts(2, "main", "Conta principal", (Cents)100000, "poup", "Poupança", (Cents)0);
    OpErr err;
    g_assert_true(ops_save_transfer(s, NULL, "main", "poup", "250", D(TODAY_T), "", D(TODAY_T), &err));
    add_tx(s, "c1", KIND_EXPENSE, 4500, TODAY_T, "Feira", "Alimentação", TRUE, "main", NULL);
    return s;
}

static void test_transfer_not_flow(void) {
    g_autoptr(AppState) s = with_transfer();
    g_assert_cmpint(count_transfers(s), ==, 2);
    Tx *leg = s->txs->pdata[0];
    TransferLegs l;
    g_assert_true(transfer_legs(s, leg->transfer_id, &l));
    g_assert_cmpstr(l.out->account_id, ==, "main");
    g_assert_cmpstr(l.into->account_id, ==, "poup");
    g_assert_cmpint(l.out->value, ==, 25000);
    for (int i = 0; i < 2; i++) {
        Tx *t = s->txs->pdata[i];
        g_assert_true(t->paid && !tx_is_flow(t));
        g_assert_cmpstr(t->category, ==, TRANSFER_CAT);
        g_assert_cmpstr(t->desc, ==, "Transferência");
        g_assert_false(ops_can_toggle_paid(t));
    }
    /* cada conta muda; o total não */
    g_assert_cmpint(account_balance(s, app_account(s, "main")), ==, 100000 - 25000 - 4500);
    g_assert_cmpint(account_balance(s, app_account(s, "poup")), ==, 25000);
    g_assert_cmpint(current_balance(s), ==, 100000 - 4500);
    Flow f = flow_of(s->txs);
    g_assert_cmpint(f.income, ==, 0);
    g_assert_cmpint(f.expense, ==, 4500);
    g_autoptr(GPtrArray) cats = category_totals(s, DAY_NONE, DAY_NONE);
    g_assert_cmpuint(cats->len, ==, 1);
    g_assert_cmpstr(((CatTotal *)cats->pdata[0])->name, ==, "Alimentação");
    g_assert_cmpint(budget_usage(s, day_ym(D(TODAY_T)), TRANSFER_CAT), ==, 0);
    DayTotal dt = period_day_total(s->txs);
    g_assert_cmpint(dt.net, ==, -4500);
    g_assert_cmpint(dt.goals, ==, 0);
    g_assert_true(dt.has_flow);
}

static void test_transfer_totals_calendar(void) {
    g_autoptr(AppState) s = with_transfer();
    MonthTotals m = totals_month(s, day_ym(D(TODAY_T)), D(TODAY_T));
    g_assert_cmpint(m.realized_in, ==, 0);
    g_assert_cmpint(m.realized_out, ==, 4500);
    g_assert_cmpint(m.opening, ==, current_balance(s));
    g_autoptr(CalMonth) cal = cal_build(s, day_ym(D(TODAY_T)), D(TODAY_T));
    const CalDay *day = cal_get(cal, D(TODAY_T));
    g_assert_cmpint(day->income, ==, 0);
    g_assert_cmpint(day->expense, ==, 4500);
    /* só a transferência no dia: o dia aparece na lista, mas sem total */
    g_autoptr(AppState) only = with_accounts(2, "main", "Conta principal", (Cents)100000, "poup", "Poupança", (Cents)0);
    g_assert_true(ops_save_transfer(only, NULL, "main", "poup", "10", D(TODAY_T), "", D(TODAY_T), NULL));
    g_autoptr(CalMonth) c2 = cal_build(only, day_ym(D(TODAY_T)), D(TODAY_T));
    g_assert_false(cal_get(c2, D(TODAY_T))->has_flow);
    g_assert_false(period_day_total(only->txs).has_flow);
}

static void test_transfer_validation(void) {
    g_autoptr(AppState) b = with_accounts(2, "main", "Conta principal", (Cents)100000, "poup", "Poupança", (Cents)0);
    Day t = D(TODAY_T);
    g_assert_false(ops_save_transfer(b, NULL, "main", "main", "10", t, "", t, NULL));
    g_assert_false(ops_save_transfer(b, NULL, "main", "poup", "0", t, "", t, NULL));
    g_assert_false(ops_save_transfer(b, NULL, "main", "xyz", "10", t, "", t, NULL));
    g_assert_false(ops_save_transfer(b, NULL, "main", "poup", "10", t + 1, "", t, NULL));
    g_assert_false(ops_save_transfer(b, NULL, "main", "poup", "10", DAY_NONE, "", t, NULL));
    g_autoptr(AppState) one = with_accounts(1, "main", "Conta principal", (Cents)0);
    g_assert_false(ops_save_transfer(one, NULL, "main", "main", "10", t, "", t, NULL));
    g_assert_cmpuint(b->txs->len, ==, 0);
    /* data passada pode */
    g_assert_true(ops_save_transfer(b, NULL, "main", "poup", "10", t - 3, "Guardar", t, NULL));
}

static void test_transfer_edit_both(void) {
    g_autoptr(AppState) s = with_transfer();
    g_autofree char *tid = g_strdup(((Tx *)s->txs->pdata[0])->transfer_id);
    guint n = s->txs->len;
    Day t = D(TODAY_T);
    g_assert_true(ops_save_transfer(s, tid, "poup", "main", "100", t - 1, "Volta", t, NULL));
    TransferLegs l;
    g_assert_true(transfer_legs(s, tid, &l));
    g_assert_cmpstr(l.out->account_id, ==, "poup");
    g_assert_cmpstr(l.into->account_id, ==, "main");
    g_assert_cmpint(l.out->value, ==, 10000);
    g_assert_cmpint(l.out->date, ==, t - 1);
    g_assert_cmpstr(l.out->desc, ==, "Volta");
    g_assert_cmpstr(l.into->desc, ==, "Volta");
    g_assert_cmpuint(s->txs->len, ==, n);
    g_assert_cmpint(account_balance(s, app_account(s, "poup")), ==, -10000);
    /* pelo formulário comum de lançamento não dá */
    TxDraft d = {KIND_EXPENSE, "x", "5", "Outros", t, TRUE, "main", "", 1, REPS_TOTAL, FALSE};
    OpErr err;
    g_assert_false(ops_save_tx(s, l.out->id, &d, t, &err));
    g_assert_cmpstr(err.title, ==, "Transferência");
}

static void test_transfer_delete_both(void) {
    g_autoptr(AppState) s = with_transfer();
    const char *leg = NULL;
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (tx_is_transfer(t) && t->kind == KIND_INCOME) leg = t->id;
    }
    g_autofree char *id = g_strdup(leg);
    ops_delete_tx(s, id, FALSE);
    g_assert_cmpuint(s->txs->len, ==, 1);
    g_assert_cmpstr(((Tx *)s->txs->pdata[0])->id, ==, "c1");
    g_assert_cmpint(account_balance(s, app_account(s, "main")), ==, 100000 - 4500);
}

static void test_transfer_account_in_use(void) {
    g_autoptr(AppState) s = with_transfer();
    g_assert_false(ops_delete_account(s, "poup", TRUE, NULL));
}

static void test_transfer_backup(void) {
    g_autoptr(AppState) s = with_transfer();
    g_autoptr(AppState) back = roundtrip(s, NULL);
    const char *tid = ((Tx *)s->txs->pdata[0])->transfer_id;
    TransferLegs l;
    g_assert_true(transfer_legs(back, tid, &l));
    g_assert_cmpint(l.out->value, ==, 25000);
    g_assert_cmpint(flow_of(back->txs).expense, ==, flow_of(s->txs).expense);
    g_assert_cmpint(current_balance(back), ==, current_balance(s));
    g_autofree char *a = backup_to_json(s, NULL), *b = backup_to_json(back, NULL);
    g_assert_cmpstr(a, ==, b);
}

static void test_transfer_backup_one_leg(void) {
    g_autoptr(AppState) s = with_transfer();
    for (guint i = s->txs->len; i-- > 0;) {
        Tx *t = s->txs->pdata[i];
        if (tx_is_transfer(t) && t->kind == KIND_INCOME) g_ptr_array_remove_index(s->txs, i);
    }
    g_autoptr(AppState) back = roundtrip(s, NULL);
    g_assert_cmpint(count_transfers(back), ==, 0);
    /* o lado que sobrou continua visível como despesa comum */
    g_assert_cmpint(flow_of(back->txs).expense, ==, 25000 + 4500);
    /* lados que não combinam (valores diferentes) também */
    g_autoptr(AppState) w = with_transfer();
    for (guint i = 0; i < w->txs->len; i++) {
        Tx *t = w->txs->pdata[i];
        if (tx_is_transfer(t) && t->kind == KIND_INCOME) t->value = 1;
    }
    g_autofree char *tid = g_strdup(((Tx *)w->txs->pdata[0])->transfer_id);
    transfers_sanitize(w->txs);
    g_assert_false(transfer_legs(w, tid, NULL));
}

static void test_transfer_csv(void) {
    g_autoptr(AppState) s = with_transfer();
    g_autofree char *csv = csv_build(s);
    g_assert_true(contains(csv, "transferência enviada"));
    g_assert_true(contains(csv, "transferência recebida"));
}

/* ================================================================ reserva para meta (GoalReserveTest) */

static const char *TODAY_G = "2026-10-17";

static AppState *with_goal(char **id) {
    AppState *s = with_accounts(2, "main", "Conta principal", (Cents)100000, "poup", "Poupança", (Cents)0);
    g_assert_true(ops_save_goal(s, NULL, "Viagem", "6000", "", DAY_NONE, "", FALSE, "", D(TODAY_G), NULL));
    *id = g_strdup(((Goal *)s->goals->pdata[0])->id);
    return s;
}

static void test_goal_reserve_out(void) {
    g_autofree char *id = NULL;
    g_autoptr(AppState) s = with_goal(&id);
    g_assert_true(ops_save_goal(s, id, "Viagem", "6000", "300", DAY_NONE, "", TRUE, "main", D(TODAY_G), NULL));
    g_assert_cmpint(((Goal *)s->goals->pdata[0])->saved, ==, 30000);
    g_assert_cmpuint(s->txs->len, ==, 1);
    Tx *t = s->txs->pdata[0];
    g_assert_true(tx_is_goal(t) && !tx_is_flow(t) && t->paid);
    g_assert_cmpint(t->kind, ==, KIND_EXPENSE);
    g_assert_cmpstr(t->category, ==, GOAL_CAT);
    g_assert_cmpstr(t->desc, ==, "Meta › Viagem");
    g_assert_cmpint(account_balance(s, s->accounts->pdata[0]), ==, 70000);
    g_assert_cmpint(flow_of(s->txs).expense, ==, 0);
    g_autoptr(GPtrArray) cats = category_totals(s, DAY_NONE, DAY_NONE);
    g_assert_cmpuint(cats->len, ==, 0);
    g_assert_cmpint(budget_usage(s, day_ym(D(TODAY_G)), GOAL_CAT), ==, 0);
    g_assert_false(ops_can_toggle_paid(t));
    GoalMoves gm = goal_moves(s->txs);
    g_assert_cmpint(gm.saved, ==, 30000);
    g_assert_cmpint(gm.withdrawn, ==, 0);
}

static void test_goal_reserve_withdraw(void) {
    g_autofree char *id = NULL;
    g_autoptr(AppState) s = with_goal(&id);
    g_assert_true(ops_save_goal(s, id, "Viagem", "6000", "100", DAY_NONE, "", TRUE, "poup", D(TODAY_G), NULL));
    /* retirar mais do que tem: a meta para no zero e só volta o que havia */
    g_assert_true(ops_save_goal(s, id, "Viagem", "6000", "-250", DAY_NONE, "", TRUE, "poup", D(TODAY_G), NULL));
    g_assert_cmpint(((Goal *)s->goals->pdata[0])->saved, ==, 0);
    Tx *back = s->txs->pdata[s->txs->len - 1];
    g_assert_cmpint(back->kind, ==, KIND_INCOME);
    g_assert_cmpint(back->value, ==, 10000);
    g_assert_cmpstr(back->desc, ==, "Retirada da meta › Viagem");
    g_assert_cmpint(account_balance(s, app_account(s, "poup")), ==, 0);
    g_assert_cmpint(flow_of(s->txs).income, ==, 0);
}

static void test_goal_reserve_off(void) {
    g_autofree char *id = NULL;
    g_autoptr(AppState) s = with_goal(&id);
    g_assert_true(ops_save_goal(s, id, "Viagem", "6000", "300", DAY_NONE, "", FALSE, "main", D(TODAY_G), NULL));
    g_assert_cmpint(((Goal *)s->goals->pdata[0])->saved, ==, 30000);
    g_assert_cmpuint(s->txs->len, ==, 0);
    g_assert_cmpint(account_balance(s, s->accounts->pdata[0]), ==, 100000);
}

static void test_goal_reserve_delete_tx(void) {
    g_autofree char *id = NULL;
    g_autoptr(AppState) s = with_goal(&id);
    g_assert_true(ops_save_goal(s, id, "Viagem", "6000", "300", DAY_NONE, "", TRUE, "main", D(TODAY_G), NULL));
    g_autofree char *tid = g_strdup(((Tx *)s->txs->pdata[0])->id);
    ops_delete_tx(s, tid, FALSE);
    g_assert_cmpint(((Goal *)s->goals->pdata[0])->saved, ==, 0);
    g_assert_cmpint(account_balance(s, s->accounts->pdata[0]), ==, 100000);
}

static void test_goal_reserve_edit_refused(void) {
    g_autofree char *id = NULL;
    g_autoptr(AppState) s = with_goal(&id);
    g_assert_true(ops_save_goal(s, id, "Viagem", "6000", "300", DAY_NONE, "", TRUE, "main", D(TODAY_G), NULL));
    TxDraft d = {KIND_EXPENSE, "x", "1", "Outros", D(TODAY_G), TRUE, "main", "", 1, REPS_TOTAL, FALSE};
    OpErr err;
    g_assert_false(ops_save_tx(s, ((Tx *)s->txs->pdata[0])->id, &d, D(TODAY_G), &err));
    g_assert_cmpstr(err.title, ==, "Lançamento de meta");
}

static void test_goal_delete_returns(void) {
    g_autofree char *id = NULL;
    g_autoptr(AppState) s = with_goal(&id);
    g_assert_true(ops_save_goal(s, id, "Viagem", "6000", "300", DAY_NONE, "", TRUE, "main", D(TODAY_G), NULL));
    /* 50 guardados sem tirar da conta não voltam (nunca saíram) */
    g_assert_true(ops_save_goal(s, id, "Viagem", "6000", "50", DAY_NONE, "", FALSE, "main", D(TODAY_G), NULL));
    g_assert_cmpint(ops_goal_from_accounts(s, id), ==, 30000);
    ops_delete_goal(s, id, D(TODAY_G));
    g_assert_cmpuint(s->goals->len, ==, 0);
    g_assert_cmpstr(((Tx *)s->txs->pdata[s->txs->len - 1])->desc, ==, "Meta excluída › Viagem");
    g_assert_cmpint(account_balance(s, s->accounts->pdata[0]), ==, 100000);
    g_assert_cmpint(flow_of(s->txs).income, ==, 0);
}

static void test_goal_backup(void) {
    g_autofree char *id = NULL;
    g_autoptr(AppState) s = with_goal(&id);
    g_assert_true(ops_save_goal(s, id, "Viagem", "6000", "300", DAY_NONE, "", TRUE, "main", D(TODAY_G), NULL));
    g_autoptr(AppState) again = roundtrip(s, NULL);
    g_assert_cmpstr(((Tx *)again->txs->pdata[0])->goal_id, ==, id);
    g_assert_false(tx_is_flow((Tx *)again->txs->pdata[0]));
    g_autofree char *a = backup_to_json(s, NULL), *b = backup_to_json(again, NULL);
    g_assert_cmpstr(a, ==, b);
}

/* ================================================================ despesas compartilhadas (SharedTest) */

static const char *DAY_S = "2026-11-02";

static AppState *mercado_igual(void) {
    AppState *s = app_state_new();
    SharedPersonDraft p[3] = {{"", "João", ""}, {"", "Maria", ""}, {"", "Pedro", ""}};
    SharedDraft d = {"Mercado", "450", D(DAY_S), SPLIT_EQUAL, p, 3, 0, ""};
    OpErr err;
    g_assert_true(shared_save(s, NULL, &d, &err));
    return s;
}

static SharedExpense *only(AppState *s) { g_assert_cmpuint(s->shared->len, ==, 1); return s->shared->pdata[0]; }
static SharedPerson *person(SharedExpense *e, int i) { return e->people->pdata[i]; }
#define ST(sum, i) (&g_array_index((sum).people, PersonStatus, (i)))

static void test_shared_equal(void) {
    g_autoptr(AppState) s = mercado_igual();
    SharedExpense *e = only(s);
    for (int i = 0; i < 3; i++) g_assert_cmpint(person(e, i)->share, ==, 15000);
    g_assert_cmpstr(person(e, 0)->id, ==, e->payer_id);
    SharedSummary st;
    shared_summary(e, &st);
    g_assert_cmpint(person_state(ST(st, 0)), ==, PERSON_NONE);
    g_assert_cmpint(ST(st, 0)->owed, ==, 0);
    g_assert_cmpint(ST(st, 1)->owed, ==, 15000);
    g_assert_cmpint(ST(st, 2)->owed, ==, 15000);
    g_assert_cmpint(st.to_receive, ==, 30000);
    g_assert_cmpint(st.received, ==, 0);
    g_assert_cmpint(summary_pending(&st), ==, 30000);
    g_assert_cmpint(summary_state(&st), ==, SHARED_OPEN);
    shared_summary_clear(&st);
    Cents sh[3];
    shared_equal_shares(10000, 3, sh);
    g_assert_cmpint(sh[0], ==, 3334);
    g_assert_cmpint(sh[1], ==, 3333);
    g_assert_cmpint(sh[2], ==, 3333);
}

static void test_shared_custom(void) {
    g_autoptr(AppState) s = app_state_new();
    SharedPersonDraft rows[3] = {{"", "João", "150"}, {"", "Maria", "120"}, {"", "Pedro", "180"}};
    SharedDraft d = {"Mercado", "450,00", D(DAY_S), SPLIT_CUSTOM, rows, 3, 0, ""};
    g_assert_true(shared_save(s, NULL, &d, NULL));
    SharedSummary st;
    shared_summary(only(s), &st);
    g_assert_cmpint(ST(st, 0)->owed, ==, 0);
    g_assert_cmpint(ST(st, 1)->owed, ==, 12000);
    g_assert_cmpint(ST(st, 2)->owed, ==, 18000);
    g_assert_cmpint(summary_pending(&st), ==, 30000);
    shared_summary_clear(&st);
    rows[2].share = "100";
    g_autoptr(AppState) e = app_state_new();
    SharedDraft w = {"Mercado", "450", D(DAY_S), SPLIT_CUSTOM, rows, 3, 0, ""};
    OpErr err;
    g_assert_false(shared_save(e, NULL, &w, &err));
    g_assert_true(contains(err.msg, "Faltam R$ 80,00"));
}

static void test_shared_payments(void) {
    g_autoptr(AppState) s = mercado_igual();
    SharedExpense *e = only(s);
    g_autofree char *eid = g_strdup(e->id), *maria = g_strdup(person(e, 1)->id), *pedro = g_strdup(person(e, 2)->id),
                    *payer = g_strdup(e->payer_id);
    Day day = D(DAY_S);
    g_assert_true(shared_add_payment(s, eid, maria, "50", day, NULL));
    SharedSummary st;
    shared_summary(only(s), &st);
    g_assert_cmpint(person_state(ST(st, 1)), ==, PERSON_PARTIAL);
    g_assert_cmpint(person_remaining(ST(st, 1)), ==, 10000);
    g_assert_cmpint(summary_state(&st), ==, SHARED_PARTIAL);
    shared_summary_clear(&st);
    OpErr err;
    g_assert_false(shared_add_payment(s, eid, maria, "100,01", day, &err));
    g_assert_true(contains(err.msg, "R$ 100,00"));
    g_assert_false(shared_add_payment(s, eid, payer, "10", day, NULL));
    g_assert_true(shared_settle(s, eid, maria, day, NULL));
    g_assert_true(shared_settle(s, eid, pedro, day, NULL));
    shared_summary(only(s), &st);
    g_assert_cmpint(st.received, ==, 30000);
    g_assert_cmpint(summary_pending(&st), ==, 0);
    g_assert_cmpint(summary_state(&st), ==, SHARED_SETTLED);
    shared_summary_clear(&st);
    g_assert_false(shared_settle(s, eid, pedro, day, NULL));
    g_autofree char *first = g_strdup(((SharedPayment *)only(s)->payments->pdata[0])->id);
    shared_delete_payment(s, eid, first);
    shared_summary(only(s), &st);
    g_assert_cmpint(summary_pending(&st), ==, 5000);
    shared_summary_clear(&st);
}

static void test_shared_edit_guards(void) {
    g_autoptr(AppState) s = mercado_igual();
    SharedExpense *e = only(s);
    g_autofree char *eid = g_strdup(e->id);
    g_assert_true(shared_add_payment(s, eid, person(e, 1)->id, "150", D(DAY_S), NULL));
    e = only(s);
    g_autofree char *i0 = g_strdup(person(e, 0)->id), *i1 = g_strdup(person(e, 1)->id), *i2 = g_strdup(person(e, 2)->id);
    SharedPersonDraft rows[4] = {{i0, "João", ""}, {i1, "Maria", ""}, {i2, "Pedro", ""}, {"", "Ana", ""}};
    OpErr err;
    /* tirar a Maria (que já pagou): recusado */
    SharedPersonDraft no_maria[2] = {rows[0], rows[2]};
    SharedDraft d1 = {"Mercado", "450", D(DAY_S), SPLIT_EQUAL, no_maria, 2, 0, ""};
    g_assert_false(shared_save(s, eid, &d1, &err));
    g_assert_true(contains(err.msg, "Maria"));
    /* diminuir o total abaixo do que ela já devolveu: recusado */
    SharedDraft d2 = {"Mercado", "300", D(DAY_S), SPLIT_EQUAL, rows, 3, 0, ""};
    g_assert_false(shared_save(s, eid, &d2, NULL));
    /* incluir mais uma pessoa: as partes caem para 112,50 e a Maria passaria do devido */
    SharedDraft d3 = {"Mercado", "450", D(DAY_S), SPLIT_EQUAL, rows, 4, 0, ""};
    g_assert_false(shared_save(s, eid, &d3, NULL));
    /* mudar o nome mantém os ids e os pagamentos */
    rows[2].name = "Pedro H.";
    SharedDraft d4 = {"Mercado do mês", "450", D(DAY_S), SPLIT_EQUAL, rows, 3, 0, ""};
    g_assert_true(shared_save(s, eid, &d4, &err));
    SharedExpense *e2 = only(s);
    g_assert_cmpstr(person(e2, 0)->id, ==, i0);
    g_assert_cmpstr(person(e2, 1)->id, ==, i1);
    g_assert_cmpstr(person(e2, 2)->id, ==, i2);
    g_assert_cmpuint(e2->payments->len, ==, 1);
    g_assert_cmpstr(e2->name, ==, "Mercado do mês");
}

static void test_shared_validation(void) {
    g_autoptr(AppState) s = app_state_new();
    Day day = D(DAY_S);
    SharedPersonDraft ab[2] = {{"", "A", ""}, {"", "B", ""}};
    SharedPersonDraft a1[1] = {{"", "A", ""}};
    SharedPersonDraft dup[2] = {{"", "Ana", ""}, {"", "ana", ""}};
    SharedPersonDraft empty[2] = {{"", "A", ""}, {"", " ", ""}};
    SharedDraft cases[] = {
        {"", "10", day, SPLIT_EQUAL, ab, 2, 0, ""},
        {"X", "0", day, SPLIT_EQUAL, ab, 2, 0, ""},
        {"X", "10", DAY_NONE, SPLIT_EQUAL, ab, 2, 0, ""},
        {"X", "10", day, SPLIT_EQUAL, a1, 1, 0, ""},
        {"X", "10", day, SPLIT_EQUAL, dup, 2, 0, ""},
        {"X", "10", day, SPLIT_EQUAL, empty, 2, 0, ""},
        {"X", "10", day, SPLIT_EQUAL, ab, 2, 5, ""},
    };
    for (guint i = 0; i < G_N_ELEMENTS(cases); i++) g_assert_false(shared_save(s, NULL, &cases[i], NULL));
    g_assert_cmpuint(s->shared->len, ==, 0);
}

static GPtrArray *names_of(GPtrArray *l) {
    GPtrArray *n = g_ptr_array_new();
    for (guint i = 0; i < l->len; i++) g_ptr_array_add(n, ((SharedExpense *)l->pdata[i])->name);
    return n;
}
static void assert_names(GPtrArray *list, const SharedFilter *f, const char *a, const char *b) {
    g_autoptr(GPtrArray) r = shared_filter(list, f);
    g_autoptr(GPtrArray) n = names_of(r);
    g_assert_cmpuint(n->len, ==, b ? 2 : 1);
    g_assert_cmpstr(n->pdata[0], ==, a);
    if (b) g_assert_cmpstr(n->pdata[1], ==, b);
}

static void test_shared_history(void) {
    g_autoptr(AppState) s = mercado_igual();
    SharedPersonDraft p[3] = {{"", "Eu", ""}, {"", "Ana", ""}, {"", "Pedro", ""}};
    SharedDraft d = {"Pizza", "90", day_plus_months_clamped(D(DAY_S), -1), SPLIT_EQUAL, p, 3, 0, "aniversário"};
    g_assert_true(shared_save(s, NULL, &d, NULL));
    SharedExpense *pizza = s->shared->pdata[1];
    g_autofree char *pid = g_strdup(pizza->id), *ana = g_strdup(person(pizza, 1)->id), *ped = g_strdup(person(pizza, 2)->id);
    g_assert_true(shared_settle(s, pid, ana, D(DAY_S), NULL));
    g_assert_true(shared_settle(s, pid, ped, D(DAY_S), NULL));
    SharedFilter all = {NULL, DAY_NONE, DAY_NONE, NULL, -1};
    assert_names(s->shared, &all, "Mercado", "Pizza");
    SharedFilter q = {"aniversario", DAY_NONE, DAY_NONE, NULL, -1};
    assert_names(s->shared, &q, "Pizza", NULL);
    SharedFilter who = {NULL, DAY_NONE, DAY_NONE, "ana", -1};
    assert_names(s->shared, &who, "Pizza", NULL);
    SharedFilter who2 = {NULL, DAY_NONE, DAY_NONE, "Pedro", -1};
    assert_names(s->shared, &who2, "Mercado", "Pizza");
    SharedFilter from = {NULL, ym_first(day_ym(D(DAY_S))), DAY_NONE, NULL, -1};
    assert_names(s->shared, &from, "Mercado", NULL);
    SharedFilter st = {NULL, DAY_NONE, DAY_NONE, NULL, SHARED_SETTLED};
    assert_names(s->shared, &st, "Pizza", NULL);
    g_auto(GStrv) names = shared_people_names(s->shared);
    const char *want[] = {"Ana", "Eu", "João", "Maria", "Pedro"};
    g_assert_cmpuint(g_strv_length(names), ==, 5);
    for (int i = 0; i < 5; i++) g_assert_cmpstr(names[i], ==, want[i]);
    Cents pend, recv;
    shared_totals(s->shared, &pend, &recv);
    g_assert_cmpint(pend, ==, 30000);
    g_assert_cmpint(recv, ==, 6000);
}

static void test_shared_backup(void) {
    g_autoptr(AppState) s = mercado_igual();
    SharedExpense *e = only(s);
    g_assert_true(shared_add_payment(s, e->id, person(e, 2)->id, "20,50", D(DAY_S), NULL));
    tx_set_str(&only(s)->receipt, "rcpt123");
    tx_set_str(&only(s)->receipt_type, "image/jpeg");
    Dropped dd = {0};
    g_autoptr(AppState) back = roundtrip(s, &dd);
    g_assert_cmpint(dropped_total(dd), ==, 0);
    g_autofree char *a = backup_to_json(s, NULL), *b = backup_to_json(back, NULL);
    g_assert_cmpstr(a, ==, b);
    g_assert_cmpstr(only(back)->receipt, ==, "rcpt123");
    g_assert_cmpuint(only(back)->payments->len, ==, 1);
    /* sem despesas, o campo nem aparece (o arquivo continua igual ao do Finan+ web) */
    g_autoptr(AppState) empty = app_state_new();
    g_autofree char *j = backup_to_json(empty, NULL);
    g_assert_false(contains(j, "\"shared\""));
}

static void test_shared_backup_invalid(void) {
    const char *json = "{\"txs\":[],\"shared\":[{\"id\":\"a\",\"name\":\"X\",\"total\":10,\"date\":\"2026-11-02\",\"payer\":\"p1\","
                       "\"people\":[{\"id\":\"p1\",\"name\":\"A\",\"share\":5},{\"id\":\"p2\",\"name\":\"B\",\"share\":4}]}]}";
    Dropped dd = {0};
    g_autoptr(AppState) n = backup_parse(json, -1, &dd, NULL);
    g_assert_nonnull(n);
    g_assert_cmpuint(n->shared->len, ==, 0);
    g_assert_cmpint(dd.shared, ==, 1);
    const char *json2 = "{\"txs\":[],\"shared\":[{\"id\":\"a\",\"name\":\"X\",\"total\":10,\"date\":\"2026-11-02\",\"payer\":\"p1\","
                        "\"people\":[{\"id\":\"p1\",\"name\":\"A\",\"share\":5},{\"id\":\"p2\",\"name\":\"B\",\"share\":5}],"
                        "\"payments\":[{\"id\":\"x\",\"person\":\"p2\",\"value\":6,\"date\":\"2026-11-02\"},"
                        "{\"id\":\"y\",\"person\":\"p1\",\"value\":1,\"date\":\"2026-11-02\"}]}]}";
    g_autoptr(AppState) n2 = backup_parse(json2, -1, NULL, NULL);
    g_assert_cmpuint(only(n2)->payments->len, ==, 0);
}

static void test_shared_names(void) {
    g_autoptr(AppState) s = mercado_igual();
    tx_set_str(&only(s)->name, "Mercado do Mês!");
    g_autofree char *pdf = shared_pdf_name(only(s)), *png = shared_image_name(only(s));
    g_assert_cmpstr(pdf, ==, "comprovante-mercado-do-mes-2026-11-02.pdf");
    g_assert_cmpstr(png, ==, "finanplus-mercado-do-mes-2026-11-02.png");
}

/* ================================================================ glossário único (TotalsTest) */

static const char *TODAY_X = "2026-11-02";

/* saldo R$ 166,88 depois de guardar R$ 100,00 no Notebook, a receber R$ 3.413,62,
 * a pagar R$ 121,40 (Mercado Pago) + fatura de R$ 488,51 */
static AppState *totals_state(Cents monthly) {
    AppState *s = with_accounts(1, "main", "Conta principal", (Cents)26688);
    g_ptr_array_add(s->cards, card_new("sofisa", "Sofisa", 50000, 5, 15));
    g_ptr_array_add(s->goals, goal_new("g1", "Notebook", 800000, 10000, D("2028-12-31"), monthly));
    Tx *m = add_tx(s, "meta", KIND_EXPENSE, 10000, TODAY_X, "Meta › Notebook", GOAL_CAT, TRUE, "main", NULL);
    tx_set_str(&m->goal_id, "g1");
    add_tx(s, "compra", KIND_EXPENSE, 48851, "2026-10-20", "Compras", "Outros", TRUE, "main", "sofisa");
    add_tx(s, "mp", KIND_EXPENSE, 12140, "2026-11-13", "Mercado Pago", "Dívidas", FALSE, "main", NULL);
    add_tx(s, "adi", KIND_INCOME, 121362, "2026-11-13", "Adiantamento", "Salário", FALSE, "main", NULL);
    add_tx(s, "sal", KIND_INCOME, 80000, "2026-11-30", "Salário", "Salário", FALSE, "main", NULL);
    add_tx(s, "13", KIND_INCOME, 140000, "2026-11-30", "13º salário", "13º salário", FALSE, "main", NULL);
    return s;
}

static void test_totals_no_double_goal(void) {
    g_autoptr(AppState) s = totals_state(0);
    Day today = D(TODAY_X);
    MonthTotals m = totals_month(s, ym_make(2026, 11), today);
    g_assert_cmpint(m.opening, ==, 16688);
    g_assert_cmpint(m.to_receive, ==, 341362);
    g_assert_cmpint(m.to_pay, ==, 60991);
    g_assert_cmpint(m.goals_planned, ==, 0);
    g_assert_cmpint(m.goals_saved, ==, 10000);
    g_assert_cmpint(m.closing, ==, 297059);
    g_assert_cmpint(totals_forecast_at(s, D("2026-11-30"), today), ==, m.closing);
}

static void test_totals_monthly_planned(void) {
    g_autoptr(AppState) s = totals_state(30385);
    Day today = D(TODAY_X);
    MonthTotals m = totals_month(s, ym_make(2026, 11), today);
    g_assert_cmpint(m.goals_planned, ==, 20385);
    g_assert_cmpint(m.closing, ==, 297059 - 20385);
    g_assert_cmpint(totals_forecast_at(s, D("2026-11-30"), today), ==, m.closing);
    g_assert_cmpint(totals_goals_planned(s, ym_make(2026, 12), today), ==, 30385);
    g_assert_cmpint(totals_goals_planned(s, ym_make(2026, 10), today), ==, 0);
}

static void test_totals_planned_capped(void) {
    g_autoptr(AppState) s = totals_state(50000);
    ((Goal *)s->goals->pdata[0])->target = 40000;
    Day today = D(TODAY_X);
    g_assert_cmpint(totals_goals_planned(s, ym_make(2026, 11), today), ==, 30000);
    g_assert_cmpint(totals_goals_planned(s, ym_make(2026, 12), today), ==, 0);
}

static void test_totals_realized(void) {
    g_autoptr(AppState) s = totals_state(0);
    Tx *pg = add_tx(s, "pg", KIND_EXPENSE, 20000, "2026-11-01", "Pagamento fatura Sofisa", CARD_PAYMENT_CAT, TRUE, "main", NULL);
    tx_set_str(&pg->card_payment, "sofisa");
    add_tx(s, "cine", KIND_EXPENSE, 5000, "2026-11-01", "Cinema", "Lazer", TRUE, "main", NULL);
    add_tx(s, "pix", KIND_INCOME, 3000, "2026-11-01", "Pix", "Extra", TRUE, "main", NULL);
    add_tx(s, "nov-cartao", KIND_EXPENSE, 7000, "2026-11-01", "Livro", "Educação", TRUE, "main", "sofisa");
    MonthTotals m = totals_month(s, ym_make(2026, 11), D(TODAY_X));
    g_assert_cmpint(m.realized_in, ==, 3000);
    g_assert_cmpint(m.realized_out, ==, 25000);
    g_assert_cmpint(m.goals_saved, ==, 10000);
    g_assert_cmpint(totals_result(&m), ==, 3000 - 25000 - 10000);
}

static void test_totals_late_in_current(void) {
    g_autoptr(AppState) s = totals_state(0);
    add_tx(s, "luz", KIND_EXPENSE, 9000, "2026-10-25", "Luz", "Moradia", FALSE, "main", NULL);
    Day today = D(TODAY_X);
    MonthTotals m = totals_month(s, ym_make(2026, 11), today);
    g_assert_cmpint(m.to_pay, ==, 60991 + 9000);
    g_assert_cmpint(totals_forecast_at(s, D("2026-11-30"), today), ==, m.closing);
    g_assert_cmpint(totals_month(s, ym_make(2026, 10), today).to_pay, ==, 9000);
}

static void test_totals_future_month(void) {
    g_autoptr(AppState) s = totals_state(30385);
    g_ptr_array_add(s->recurring, recurring_new("r1", KIND_EXPENSE, "Aluguel", 90000, "Moradia", "main", "", 5, TRUE, DAY_NONE, YM_NONE));
    Day today = D(TODAY_X);
    MonthTotals nov = totals_month(s, ym_make(2026, 11), today), dez = totals_month(s, ym_make(2026, 12), today);
    g_assert_cmpint(nov.closing, ==, dez.opening);
    g_assert_cmpint(dez.to_pay, ==, 90000);
    g_assert_cmpint(dez.goals_planned, ==, 30385);
    g_assert_cmpint(totals_forecast_at(s, D("2026-12-31"), today), ==, dez.closing);
}

static void test_totals_past_month(void) {
    g_autoptr(AppState) s = totals_state(0);
    MonthTotals out = totals_month(s, ym_make(2026, 10), D(TODAY_X));
    g_assert_cmpint(out.closing, ==, balance_on(s, D("2026-10-31")));
    g_assert_cmpint(out.opening, ==, balance_on(s, D("2026-09-30")));
}

/* ================================================================ dívidas (DebtsTest) */

static void add_parcel(AppState *s, int n, gboolean paid, const char *cat) {
    g_autofree char *id = g_strdup_printf("mp%d", n);
    g_autofree char *desc = g_strdup_printf("Mercado Pago (%d/4)", n);
    Tx *t = tx_new(id, KIND_EXPENSE, 12140, day_plus_months_clamped(D("2026-10-13"), n - 1), desc, cat, paid, "main", "");
    tx_set_str(&t->group_id, "mp");
    t->parcel_n = n;
    t->parcel_total = 4;
    g_ptr_array_add(s->txs, t);
}

static void test_debts_group(void) {
    g_autoptr(AppState) s = app_state_new();
    add_parcel(s, 1, TRUE, "Dívidas");
    for (int i = 2; i <= 4; i++) add_parcel(s, i, FALSE, "Dívidas");
    g_autoptr(GPtrArray) l = debts_list(s);
    g_assert_cmpuint(l->len, ==, 1);
    Debt *d = l->pdata[0];
    g_assert_cmpstr(d->name, ==, "Mercado Pago");
    g_assert_cmpstr(d->next->id, ==, "mp2");
    g_assert_cmpint(d->pending_count, ==, 3);
    g_assert_cmpint(d->pending_total, ==, 3 * 12140);
    g_assert_cmpint(d->total_count, ==, 4);
    g_autofree char *a = debts_summary(s, FALSE), *b = debts_summary(s, TRUE);
    g_assert_cmpstr(a, ==, "1 dívida · parcela de R$ 121,40 em 13/11");
    g_assert_cmpstr(b, ==, "1 dívida · parcela de R$ •••• em 13/11");
}

static void test_debts_legacy(void) {
    g_autoptr(AppState) s = app_state_new();
    add_parcel(s, 1, FALSE, "Empréstimo");
    add_tx(s, "x", KIND_EXPENSE, 500, "2026-11-01", "Mercado", "Alimentação", FALSE, "main", NULL);
    g_autoptr(GPtrArray) l = debts_list(s);
    g_assert_cmpuint(l->len, ==, 1);
    g_assert_true(debts_is_category("emprestimos"));
}

static void test_debts_done(void) {
    g_autoptr(AppState) s = app_state_new();
    for (int i = 1; i <= 4; i++) add_parcel(s, i, TRUE, "Dívidas");
    g_autoptr(GPtrArray) l = debts_list(s);
    g_assert_true(debt_done((Debt *)l->pdata[0]));
    g_assert_null(debts_summary(s, FALSE));
}

/* ================================================================ total do dia (DayTotalTest) */

static Tx *dtx(const char *id, Kind k, Cents v, gboolean paid, const char *card, const char *goal) {
    gboolean is_goal = goal && *goal;
    Tx *t = tx_new(id, k, v, D("2026-11-02"), id, is_goal ? GOAL_CAT : "Outros", paid || (card && *card) || is_goal, "main", card ? card : "");
    tx_set_str(&t->goal_id, goal ? goal : "");
    return t;
}

static void test_day_total(void) {
    g_autoptr(GPtrArray) a = g_ptr_array_new_with_free_func((GDestroyNotify)tx_free);
    g_ptr_array_add(a, dtx("a", KIND_INCOME, 50000, TRUE, NULL, NULL));
    g_ptr_array_add(a, dtx("b", KIND_EXPENSE, 12000, FALSE, NULL, NULL));
    DayTotal t = period_day_total(a);
    g_assert_cmpint(t.net, ==, 38000); g_assert_cmpint(t.goals, ==, 0); g_assert_true(t.has_flow);

    g_autoptr(GPtrArray) b = g_ptr_array_new_with_free_func((GDestroyNotify)tx_free);
    g_ptr_array_add(b, dtx("m", KIND_EXPENSE, 10000, TRUE, NULL, "g1"));
    t = period_day_total(b);
    g_assert_cmpint(t.net, ==, 0); g_assert_cmpint(t.goals, ==, 10000); g_assert_false(t.has_flow);

    g_autoptr(GPtrArray) c = g_ptr_array_new_with_free_func((GDestroyNotify)tx_free);
    g_ptr_array_add(c, dtx("a", KIND_INCOME, 50000, TRUE, NULL, NULL));
    g_ptr_array_add(c, dtx("m", KIND_EXPENSE, 10000, TRUE, NULL, "g1"));
    g_ptr_array_add(c, dtx("r", KIND_INCOME, 2500, TRUE, NULL, "g1"));
    t = period_day_total(c);
    g_assert_cmpint(t.net, ==, 50000); g_assert_cmpint(t.goals, ==, 7500); g_assert_true(t.has_flow);
}

static void test_day_total_card_and_goal(void) {
    g_autoptr(AppState) s = app_state_new();
    g_ptr_array_add(s->cards, card_new("itau", "Itaú", 20000, 24, 1));
    g_ptr_array_add(s->txs, dtx("c", KIND_EXPENSE, 12600, TRUE, "itau", NULL));
    g_ptr_array_add(s->txs, dtx("m", KIND_EXPENSE, 10000, TRUE, NULL, "g1"));
    DayTotal t = period_day_total(s->txs);
    g_assert_cmpint(t.net, ==, 0); g_assert_cmpint(t.goals, ==, 10000); g_assert_false(t.has_flow);
    g_autoptr(CalMonth) cal = cal_build(s, ym_make(2026, 11), D("2026-11-02"));
    const CalDay *d = cal_get(cal, D("2026-11-02"));
    g_assert_cmpint(cal_day_flow_net(d), ==, 0);
    g_assert_cmpint(d->goals, ==, 10000);
    g_assert_false(d->has_flow);
    g_assert_cmpint(cal_day_net(d), ==, -10000);
    g_assert_true(d->marks & MARK_GOAL);
    g_autofree char *say = cal_describe(D("2026-11-02"), d, D("2026-11-02"), FALSE);
    g_assert_true(contains(say, "R$ 100,00 guardados em metas"));
    g_assert_false(contains(say, "saldo do dia"));
}

/* ================================================================ categorias (CategoryMigrationTest) */

static AppState *migration_state(void) {
    AppState *s = app_state_new();
    g_ptr_array_set_size(s->cats[KIND_EXPENSE], 0);
    const char *exp[] = {"Alimentação", "Cartão de Crédito", "Empréstimo", "Limite Especial", "Outros"};
    for (guint i = 0; i < G_N_ELEMENTS(exp); i++) g_ptr_array_add(s->cats[KIND_EXPENSE], g_strdup(exp[i]));
    g_ptr_array_set_size(s->cats[KIND_INCOME], 0);
    g_ptr_array_add(s->cats[KIND_INCOME], g_strdup("Salário"));
    g_ptr_array_add(s->cats[KIND_INCOME], g_strdup("Caju"));
    add_tx(s, "a", KIND_EXPENSE, 100, "2026-11-02", "Anuidade", "Cartão de Crédito", TRUE, "main", NULL);
    add_tx(s, "b", KIND_EXPENSE, 12140, "2026-11-02", "Mercado Pago (2/4)", "Empréstimo", FALSE, "main", NULL);
    add_tx(s, "c", KIND_EXPENSE, 800, "2026-11-02", "Juros", "Limite Especial", TRUE, "main", NULL);
    g_ptr_array_add(s->limits, limit_new("Empréstimo", 50000));
    return s;
}

static void test_migration(void) {
    g_autoptr(AppState) s = migration_state();
    g_assert_true(txform_migrate(s));
    g_assert_cmpuint(s->cats[KIND_EXPENSE]->len, ==, 3);
    g_assert_cmpstr(s->cats[KIND_EXPENSE]->pdata[0], ==, "Alimentação");
    g_assert_cmpstr(s->cats[KIND_EXPENSE]->pdata[1], ==, "Dívidas");
    g_assert_cmpstr(s->cats[KIND_EXPENSE]->pdata[2], ==, "Outros");
    g_assert_cmpstr(app_tx(s, "b")->category, ==, "Dívidas");
    Cents v = 0;
    g_assert_true(app_limit(s, "Dívidas", &v));
    g_assert_cmpint(v, ==, 50000);
    g_assert_cmpstr(app_tx(s, "a")->category, ==, "Cartão de Crédito");
    g_assert_cmpstr(txform_display_category("Limite Especial"), ==, "Outros");
    g_assert_cmpuint(s->cats[KIND_INCOME]->len, ==, 2);
    g_assert_false(txform_migrate(s));
}

static void test_migration_merge(void) {
    g_autoptr(AppState) s = migration_state();
    g_ptr_array_set_size(s->cats[KIND_EXPENSE], 0);
    g_ptr_array_add(s->cats[KIND_EXPENSE], g_strdup("Dívidas"));
    g_ptr_array_add(s->cats[KIND_EXPENSE], g_strdup("Empréstimo"));
    g_ptr_array_add(s->cats[KIND_EXPENSE], g_strdup("Outros"));
    g_assert_true(txform_migrate(s));
    g_assert_cmpuint(s->cats[KIND_EXPENSE]->len, ==, 2);
    for (guint i = 0; i < s->txs->len; i++) g_assert_cmpstr(((Tx *)s->txs->pdata[i])->category, !=, "Empréstimo");
}

static void test_migration_default(void) {
    g_autoptr(AppState) s = app_state_new();
    g_assert_true(app_has_category(s, KIND_EXPENSE, "Dívidas"));
    g_assert_false(txform_migrate(s));
}

/* ================================================================ saldo acumulado (AccumulatedBalanceTest) */

static AppState *acc_state(void) {
    AppState *s = with_accounts(2, "main", "Conta principal", (Cents)100000, "poup", "Poupança", (Cents)50000);
    add_tx(s, "sal-set", KIND_INCOME, 300000, "2026-09-05", "a", "Outros", TRUE, "main", NULL);
    add_tx(s, "alug-set", KIND_EXPENSE, 120000, "2026-09-10", "a", "Outros", TRUE, "main", NULL);
    add_tx(s, "sal-out", KIND_INCOME, 300000, "2026-10-05", "a", "Outros", TRUE, "main", NULL);
    add_tx(s, "alug-out", KIND_EXPENSE, 120000, "2026-10-10", "a", "Outros", TRUE, "poup", NULL);
    add_tx(s, "luz-nov", KIND_EXPENSE, 20000, "2026-11-20", "a", "Outros", FALSE, "main", NULL);
    add_tx(s, "freela-dez", KIND_INCOME, 80000, "2026-12-15", "a", "Outros", FALSE, "main", NULL);
    add_tx(s, "mercado", KIND_EXPENSE, 9999, "2026-10-12", "a", "Outros", TRUE, "main", "visa");
    add_tx(s, "orfao", KIND_INCOME, 777, "2026-10-01", "a", "Outros", TRUE, "apagada", NULL);
    return s;
}

static void test_accumulated_balance(void) {
    g_autoptr(AppState) s = acc_state();
    Day today = D("2026-11-02");
    g_assert_cmpint(balance_on(s, D("2026-08-31")), ==, 150000);
    g_assert_cmpint(balance_on(s, D("2026-09-30")), ==, 330000);
    g_assert_cmpint(balance_on(s, D("2026-10-31")), ==, 510000);
    g_assert_cmpint(balance_on(s, D("2026-11-30")), ==, 510000);
    g_assert_cmpint(forecast_from_start(s, D("2026-11-30"), today, TRUE), ==, 490000);
    g_assert_cmpint(forecast_from_start(s, D("2026-12-31"), today, TRUE), ==, 570000);
    Day untils[] = {today, D("2026-11-30"), D("2026-12-31")};
    for (int i = 0; i < 3; i++) g_assert_cmpint(future_balance(s, untils[i], today), ==, forecast_from_start(s, untils[i], today, TRUE));
    g_assert_cmpint(current_balance(s), ==, balance_on(s, today));
}

static void test_accumulated_report(void) {
    g_autoptr(AppState) s = with_accounts(1, "main", "Conta principal", (Cents)100000);
    add_tx(s, "a", KIND_INCOME, 300000, "2026-10-05", "Salário", "Salário", TRUE, "main", NULL);
    add_tx(s, "b", KIND_EXPENSE, 120000, "2026-10-10", "Aluguel", "Moradia", TRUE, "main", NULL);
    add_tx(s, "c", KIND_EXPENSE, 20000, "2026-11-01", "Luz", "Moradia", TRUE, "main", NULL);
    Day today = D("2026-11-02");
    Report *off = report_build(s, D("2026-11-01"), D("2026-11-30"), today);
    g_assert_false(off->accumulate);
    report_free(off);
    Report *r = report_build_ext(s, D("2026-11-01"), D("2026-11-30"), today, TRUE);
    g_assert_cmpint(r->carried, ==, 280000);
    g_assert_cmpint(r->accumulated, ==, 260000);
    g_assert_cmpint(report_balance(r), ==, -20000);
    report_free(r);
    Report *y = report_build_ext(s, D("2026-09-01"), D("2026-11-30"), today, TRUE);
    g_assert_cmpuint(y->months->len, ==, 3);
    g_assert_cmpint(g_array_index(y->months, MonthRow, 0).accumulated, ==, 100000);
    g_assert_cmpint(g_array_index(y->months, MonthRow, 1).accumulated, ==, 280000);
    g_assert_cmpint(g_array_index(y->months, MonthRow, 2).accumulated, ==, 260000);
    g_assert_cmpint(y->carried, ==, 100000);
    report_free(y);
}

/* ================================================================ tela de lançamento (TxFormTest) */

static void test_amount_input(void) {
    struct { const char *in, *out; } c[] = {
        {"", ""}, {"0", "0"}, {"12,5", "12,5"}, {"1234,56", "1.234,56"}, {"1234", "1.234"}, {"12.", "12,"},
        {"007", "7"}, {"0,5", "0,5"}, {",5", "0,5"}, {"12,345", "12,34"}, {"1.234.567", "1.234.567"},
    };
    for (guint i = 0; i < G_N_ELEMENTS(c); i++) {
        g_autofree char *o = amount_input_format(c[i].in);
        g_assert_cmpstr(o, ==, c[i].out);
    }
}

static void test_recent_and_suggestions(void) {
    g_autoptr(AppState) s = app_state_new();
    add_tx(s, "1", KIND_EXPENSE, 100, "2026-11-01", "Padaria", "Alimentação", TRUE, "main", NULL);
    add_tx(s, "2", KIND_EXPENSE, 100, "2026-11-03", "Uber", "Transporte", TRUE, "main", NULL);
    add_tx(s, "3", KIND_EXPENSE, 100, "2026-11-02", "Padaria", "Alimentação", TRUE, "main", NULL);
    add_tx(s, "4", KIND_EXPENSE, 100, "2026-11-03", "Lazer", "Lazer", TRUE, "main", NULL);
    Tx *g = add_tx(s, "5", KIND_EXPENSE, 100, "2026-11-04", "Meta › X", GOAL_CAT, TRUE, "main", NULL);
    tx_set_str(&g->goal_id, "g");
    g_auto(GStrv) rec = txform_recent_categories(s, KIND_EXPENSE, 4);
    g_assert_cmpuint(g_strv_length(rec), ==, 3);
    g_assert_cmpstr(rec[0], ==, "Lazer");     /* 03/11, gravado depois */
    g_assert_cmpstr(rec[1], ==, "Transporte");
    g_assert_cmpstr(rec[2], ==, "Alimentação");
    g_auto(GStrv) sug = txform_description_suggestions(s, KIND_EXPENSE, 8);
    g_assert_cmpuint(g_strv_length(sug), ==, 2); /* "Lazer" = categoria e a reserva não entram */
    g_assert_cmpstr(sug[0], ==, "Padaria");
    g_assert_cmpstr(sug[1], ==, "Uber");
    g_autofree char *k = txform_category_after_kind_change("saude", s, KIND_EXPENSE);
    g_assert_cmpstr(k, ==, "Saúde");
    g_autofree char *k2 = txform_category_after_kind_change("Saúde", s, KIND_INCOME);
    g_assert_cmpstr(k2, ==, "");
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/transfer/not-flow", test_transfer_not_flow);
    g_test_add_func("/transfer/totals-calendar", test_transfer_totals_calendar);
    g_test_add_func("/transfer/validation", test_transfer_validation);
    g_test_add_func("/transfer/edit-both", test_transfer_edit_both);
    g_test_add_func("/transfer/delete-both", test_transfer_delete_both);
    g_test_add_func("/transfer/account-in-use", test_transfer_account_in_use);
    g_test_add_func("/transfer/backup", test_transfer_backup);
    g_test_add_func("/transfer/backup-one-leg", test_transfer_backup_one_leg);
    g_test_add_func("/transfer/csv", test_transfer_csv);
    g_test_add_func("/goal/reserve-out", test_goal_reserve_out);
    g_test_add_func("/goal/reserve-withdraw", test_goal_reserve_withdraw);
    g_test_add_func("/goal/reserve-off", test_goal_reserve_off);
    g_test_add_func("/goal/reserve-delete-tx", test_goal_reserve_delete_tx);
    g_test_add_func("/goal/reserve-edit-refused", test_goal_reserve_edit_refused);
    g_test_add_func("/goal/delete-returns", test_goal_delete_returns);
    g_test_add_func("/goal/backup", test_goal_backup);
    g_test_add_func("/shared/equal", test_shared_equal);
    g_test_add_func("/shared/custom", test_shared_custom);
    g_test_add_func("/shared/payments", test_shared_payments);
    g_test_add_func("/shared/edit-guards", test_shared_edit_guards);
    g_test_add_func("/shared/validation", test_shared_validation);
    g_test_add_func("/shared/history", test_shared_history);
    g_test_add_func("/shared/backup", test_shared_backup);
    g_test_add_func("/shared/backup-invalid", test_shared_backup_invalid);
    g_test_add_func("/shared/names", test_shared_names);
    g_test_add_func("/totals/no-double-goal", test_totals_no_double_goal);
    g_test_add_func("/totals/monthly-planned", test_totals_monthly_planned);
    g_test_add_func("/totals/planned-capped", test_totals_planned_capped);
    g_test_add_func("/totals/realized", test_totals_realized);
    g_test_add_func("/totals/late-in-current", test_totals_late_in_current);
    g_test_add_func("/totals/future-month", test_totals_future_month);
    g_test_add_func("/totals/past-month", test_totals_past_month);
    g_test_add_func("/debts/group", test_debts_group);
    g_test_add_func("/debts/legacy", test_debts_legacy);
    g_test_add_func("/debts/done", test_debts_done);
    g_test_add_func("/day-total/basic", test_day_total);
    g_test_add_func("/day-total/card-and-goal", test_day_total_card_and_goal);
    g_test_add_func("/categories/migration", test_migration);
    g_test_add_func("/categories/migration-merge", test_migration_merge);
    g_test_add_func("/categories/default", test_migration_default);
    g_test_add_func("/accumulated/balance", test_accumulated_balance);
    g_test_add_func("/accumulated/report", test_accumulated_report);
    g_test_add_func("/txform/amount-input", test_amount_input);
    g_test_add_func("/txform/recent-and-suggestions", test_recent_and_suggestions);
    return g_test_run();
}
