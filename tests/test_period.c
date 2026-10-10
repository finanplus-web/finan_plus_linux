/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Testes do período (‹ mês ›), do calendário, da comparação dos Relatórios e do simulador "E se…?":
 * os mesmos casos do app Android (PeriodTest, CalendarTest, SimulatorTest) e do Finan+ web.
 */
#include "core/model.h"
#include "core/finance.h"
#include "core/period.h"
#include "core/simulator.h"
#include <string.h>

static Day D(const char *s) { Day d; g_assert_true(day_parse_iso(s, &d)); return d; }

static Tx *mk(const char *id, Kind k, Cents v, const char *date, gboolean paid, const char *card, const char *pay) {
    Tx *t = tx_new(id, k, v, D(date), id, "Outros", paid, "main", card ? card : "");
    tx_set_str(&t->card_payment, pay ? pay : "");
    return t;
}
static Tx *parc(const char *id, Cents v, const char *date, gboolean paid, const char *card, const char *group, int n, int total, const char *desc) {
    Tx *t = tx_new(id, KIND_EXPENSE, v, D(date), desc ? desc : id, "Outros", paid, "main", card ? card : "");
    tx_set_str(&t->group_id, group);
    t->parcel_n = n;
    t->parcel_total = total;
    return t;
}
static AppState *st(Tx *first, ...) {
    AppState *s = app_state_new();
    va_list ap;
    va_start(ap, first);
    for (Tx *t = first; t; t = va_arg(ap, Tx *)) g_ptr_array_add(s->txs, t);
    va_end(ap);
    return s;
}
static void add_card(AppState *s, const char *id, int close, int due) { g_ptr_array_add(s->cards, card_new(id, "Roxo", 500000, close, due)); }
#define OCT ym_make(2026, 10)
#define STR_EQ(expr, lit) do { g_autofree char *_v = (expr); g_assert_cmpstr(_v, ==, lit); } while (0)

/* ------------------------------------------------------------------ período */

static void test_labels(void) {
    STR_EQ(period_label(D("2026-10-01"), D("2026-10-31")), "Outubro de 2026");
    STR_EQ(period_label(D("2028-02-01"), D("2028-02-29")), "Fevereiro de 2028");
    STR_EQ(period_label(D("2026-10-01"), D("2026-10-15")), "01/10/2026 a 15/10/2026");
    STR_EQ(period_label(DAY_NONE, DAY_NONE), "Todo o período");
    STR_EQ(period_label(D("2026-10-01"), DAY_NONE), "Desde 01/10/2026");
    STR_EQ(period_label(DAY_NONE, D("2026-10-15")), "Até 15/10/2026");
    STR_EQ(period_label(D("2026-10-08"), D("2026-10-08")), "08/10/2026");
    g_assert_cmpint(period_full_month(D("2026-10-02"), D("2026-10-31")), ==, YM_NONE);
}

static void test_arrows(void) {
    Day today = D("2026-10-08"), a, b;
    period_shift(D("2026-10-01"), D("2026-10-31"), 1, today, &a, &b);
    g_assert_cmpint(a, ==, D("2026-11-01")); g_assert_cmpint(b, ==, D("2026-11-30"));
    period_shift(D("2026-10-01"), D("2026-10-31"), -1, today, &a, &b);
    g_assert_cmpint(a, ==, D("2026-09-01")); g_assert_cmpint(b, ==, D("2026-09-30"));
    period_shift(D("2026-09-10"), D("2026-10-09"), 1, today, &a, &b);
    g_assert_cmpint(a, ==, D("2026-10-01")); g_assert_cmpint(b, ==, D("2026-10-31"));
    period_shift(DAY_NONE, DAY_NONE, -1, today, &a, &b);
    g_assert_cmpint(a, ==, D("2026-09-01"));
    period_shift(D("2026-12-01"), D("2026-12-31"), 1, today, &a, &b);
    g_assert_cmpint(a, ==, D("2027-01-01")); g_assert_cmpint(b, ==, D("2027-01-31"));
}

static void test_month_pending(void) {
    g_autoptr(AppState) s = st(mk("sal", KIND_INCOME, 121362, "2026-10-15", FALSE, NULL, NULL),
                               mk("alug", KIND_EXPENSE, 70000, "2026-10-15", FALSE, NULL, NULL),
                               mk("pago", KIND_EXPENSE, 5000, "2026-10-02", TRUE, NULL, NULL),
                               mk("nov", KIND_EXPENSE, 9999, "2026-11-02", FALSE, NULL, NULL),
                               mk("compra", KIND_EXPENSE, 20000, "2026-09-25", TRUE, "c1", NULL), NULL);
    add_card(s, "c1", 5, 15);
    Pending p = period_month_pending(s, OCT, D("2026-10-08"));
    g_assert_cmpint(p.to_receive, ==, 121362);
    g_assert_cmpint(p.to_pay, ==, 70000 + 20000);
    g_autoptr(GPtrArray) oct = g_ptr_array_new();
    for (guint i = 0; i < s->txs->len; i++) { Tx *t = s->txs->pdata[i]; if (day_ym(t->date) == OCT) g_ptr_array_add(oct, t); }
    p = period_pending(oct);
    g_assert_cmpint(p.to_receive, ==, 121362);
    g_assert_cmpint(p.to_pay, ==, 70000);
}

static void test_day_net(void) {
    g_autoptr(AppState) s = st(mk("a", KIND_INCOME, 121362, "2026-10-15", FALSE, NULL, NULL),
                               mk("b", KIND_EXPENSE, 113240, "2026-10-15", FALSE, NULL, NULL),
                               mk("c", KIND_EXPENSE, 9990, "2026-10-15", TRUE, "c1", NULL),
                               mk("d", KIND_EXPENSE, 1000, "2026-10-15", TRUE, NULL, "c1"), NULL);
    add_card(s, "c1", 20, 28);
    g_assert_cmpint(period_cash_net(s->txs), ==, 121362 - 113240 - 1000);
    CalMonth *m = cal_month_build(s, OCT, D("2026-10-08"));
    g_assert_cmpint(cal_day_net(cal_month_day(m, D("2026-10-15"))), ==, period_cash_net(s->txs));
    cal_month_free(m);
}

/* ------------------------------------------------------------------ calendário */

static void test_grid(void) {
    Day c[42];
    int n = cal_cells(OCT, c);
    g_assert_cmpint(n, ==, 35);
    for (int i = 0; i < 4; i++) g_assert_cmpint(c[i], ==, DAY_NONE);
    g_assert_cmpint(c[4], ==, D("2026-10-01"));
    g_assert_cmpint(c[34], ==, D("2026-10-31"));
    n = cal_cells(ym_make(2026, 2), c);
    g_assert_cmpint(n, ==, 28);
    g_assert_cmpint(c[0], ==, D("2026-02-01"));
    g_assert_cmpint(cal_cells(ym_make(2026, 8), c), ==, 42);
}

static void test_day_totals(void) {
    g_autoptr(AppState) s = st(mk("sal", KIND_INCOME, 520000, "2026-10-05", TRUE, NULL, NULL),
                               mk("merc", KIND_EXPENSE, 18240, "2026-10-05", TRUE, NULL, NULL),
                               mk("compra", KIND_EXPENSE, 9990, "2026-10-05", TRUE, "c1", NULL),
                               mk("outro", KIND_EXPENSE, 1000, "2026-11-05", TRUE, NULL, NULL), NULL);
    add_card(s, "c1", 20, 28);
    CalMonth *m = cal_month_build(s, OCT, D("2026-10-08"));
    const CalDay *d = cal_month_day(m, D("2026-10-05"));
    g_assert_cmpint(d->income, ==, 520000);
    g_assert_cmpint(d->expense, ==, 18240);
    g_assert_cmpint(d->marks, ==, MARK_INCOME | MARK_EXPENSE | MARK_CARD);
    g_assert_cmpuint(d->txs->len, ==, 3);
    g_assert_cmpstr(((Tx *)d->txs->pdata[0])->id, ==, "sal");
    g_assert_null(cal_month_day(m, D("2026-11-05")));
    cal_month_free(m);
}

static void test_invoice_due(void) {
    g_autoptr(AppState) s = st(mk("c-a", KIND_EXPENSE, 40000, "2026-09-20", TRUE, "c1", NULL),
                               mk("c-b", KIND_EXPENSE, 20000, "2026-09-25", TRUE, "c1", NULL),
                               mk("pg", KIND_EXPENSE, 10000, "2026-10-01", TRUE, NULL, "c1"), NULL);
    add_card(s, "c1", 5, 15);
    CalMonth *m = cal_month_build(s, OCT, D("2026-10-08"));
    const CalDay *due = cal_month_day(m, D("2026-10-15"));
    g_assert_cmpuint(due->invoices->len, ==, 1);
    g_assert_cmpint(g_array_index(due->invoices, InvoiceDue, 0).amount, ==, 50000);
    g_assert_cmpint(due->expense, ==, 50000);
    g_assert_false(due->overdue);
    g_assert_cmpint(due->marks, ==, MARK_CARD);
    g_assert_cmpint(cal_month_day(m, D("2026-10-01"))->expense, ==, 10000);
    cal_month_free(m);
    m = cal_month_build(s, OCT, D("2026-10-20"));
    g_assert_true(cal_month_day(m, D("2026-10-15"))->overdue);
    cal_month_free(m);
}

static void test_overdue(void) {
    g_autoptr(AppState) s = st(mk("net", KIND_EXPENSE, 11990, "2026-10-06", FALSE, NULL, NULL),
                               mk("alug", KIND_EXPENSE, 150000, "2026-10-10", FALSE, NULL, NULL),
                               mk("farm", KIND_EXPENSE, 4690, "2026-10-07", TRUE, NULL, NULL), NULL);
    CalMonth *m = cal_month_build(s, OCT, D("2026-10-08"));
    g_assert_true(cal_month_day(m, D("2026-10-06"))->overdue);
    g_assert_false(cal_month_day(m, D("2026-10-10"))->overdue);
    g_assert_false(cal_month_day(m, D("2026-10-07"))->overdue);
    cal_month_free(m);
}

static void test_month_totals(void) {
    g_autoptr(AppState) s = st(mk("a", KIND_INCOME, 520000, "2026-10-05", TRUE, NULL, NULL),
                               mk("b", KIND_INCOME, 90000, "2026-10-30", FALSE, NULL, NULL),
                               mk("c", KIND_EXPENSE, 150000, "2026-10-10", FALSE, NULL, NULL),
                               mk("d", KIND_EXPENSE, 34171, "2026-10-30", FALSE, NULL, NULL), NULL);
    CalMonth *m = cal_month_build(s, OCT, D("2026-10-08"));
    Cents i, e;
    cal_month_totals(m, &i, &e);
    g_assert_cmpint(i, ==, 610000);
    g_assert_cmpint(e, ==, 184171);
    g_assert_cmpint(cal_day_net(cal_month_day(m, D("2026-10-30"))), ==, 55829);
    cal_month_free(m);
}

static void test_compact(void) {
    struct { Cents c; const char *s; } cases[] = {
        {0, "0"}, {18240, "182"}, {11990, "120"}, {99949, "999"}, {99999, "1 mil"}, {100000, "1 mil"},
        {150000, "1,5 mil"}, {520000, "5,2 mil"}, {159999, "1,6 mil"}, {1500000, "15 mil"}, {1549900, "15 mil"},
        {99849999, "999 mil"}, {99949999, "1 mi"}, {99999999, "1 mi"}, {100000000, "1 mi"}, {120000000, "1,2 mi"},
        {2000000000000, "20 bi"}, {999999999999999, "10000 bi"},
    };
    char b[32];
    for (guint k = 0; k < G_N_ELEMENTS(cases); k++) g_assert_cmpstr(cal_compact(cases[k].c, b), ==, cases[k].s);
    g_assert_cmpstr(cal_signed(520000, b), ==, "+5,2 mil");
    g_assert_cmpstr(cal_signed(-11990, b), ==, "−120");
    g_assert_cmpstr(cal_signed(0, b), ==, "0");
}

static void test_titles(void) {
    Day today = D("2026-10-08");
    STR_EQ(cal_month_title(OCT), "Outubro de 2026");
    STR_EQ(cal_month_title(ym_make(2027, 3)), "Março de 2027");
    STR_EQ(cal_day_title(D("2026-10-15"), today), "Quinta, 15 de outubro");
    STR_EQ(cal_day_title(D("2027-01-02"), today), "Sábado, 2 de janeiro de 2027");
    STR_EQ(cal_day_title(D("2026-10-05"), today), "Segunda, 5 de outubro");
    g_autoptr(AppState) s = st(mk("net", KIND_EXPENSE, 11990, "2026-10-06", FALSE, NULL, NULL), NULL);
    CalMonth *m = cal_month_build(s, OCT, today);
    const CalDay *d = cal_month_day(m, D("2026-10-06"));
    STR_EQ(cal_describe(D("2026-10-06"), d, today, FALSE), "6 de outubro, terça-feira, 1 lançamento, saldo do dia menos R$ 119,90, em atraso");
    STR_EQ(cal_describe(D("2026-10-06"), d, today, TRUE), "6 de outubro, terça-feira, 1 lançamento, em atraso");
    STR_EQ(cal_describe(today, NULL, today, FALSE), "8 de outubro, quinta-feira, hoje, sem lançamentos");
    cal_month_free(m);
}

/* ------------------------------------------------------------------ simulador e comparação */

static void test_sim_base(void) {
    Day today = D("2026-10-09");
    g_autoptr(AppState) s = st(mk("a", KIND_INCOME, 500000, "2026-09-05", TRUE, NULL, NULL),
                               mk("b", KIND_EXPENSE, 300000, "2026-09-10", TRUE, NULL, NULL),
                               mk("c", KIND_INCOME, 400000, "2026-08-05", TRUE, NULL, NULL),
                               mk("d", KIND_EXPENSE, 200000, "2026-08-10", TRUE, NULL, NULL),
                               mk("e", KIND_EXPENSE, 99999, "2026-10-02", TRUE, NULL, NULL),
                               mk("f", KIND_EXPENSE, 50000, "2026-09-20", FALSE, NULL, NULL), NULL);
    SimBase b = sim_base(s, today);
    g_assert_cmpint(b.income, ==, 450000); g_assert_cmpint(b.expense, ==, 250000); g_assert_cmpint(b.months, ==, 2);
    g_assert_cmpint(sim_left(b), ==, 200000);
    g_autoptr(AppState) e = app_state_new();
    b = sim_base(e, today);
    g_assert_cmpint(b.income + b.expense + b.months, ==, 0);
}

static void test_sim_save_buy(void) {
    Day today = D("2026-10-09");
    SimBase base = {211362, 113240, 3};
    SimSave sv = sim_save(base, 20000, 12);
    g_assert_cmpint(sv.total, ==, 240000); g_assert_cmpint(sv.new_left, ==, 78122); g_assert_false(sv.over_left);
    g_assert_true(sim_save(base, 100000, 12).over_left);
    SimBuy b;
    g_assert_true(sim_buy(450000, 0, 30000, today, &b));
    g_assert_cmpint(b.months, ==, 15); g_assert_cmpint(b.done_ym, ==, ym_make(2028, 1));
    STR_EQ(br_month_year(b.done_ym), "janeiro de 2028");
    g_assert_true(sim_buy(450000, 0, 50000, today, &b)); g_assert_cmpint(b.months, ==, 9);
    g_assert_true(sim_buy(450000, 0, 20000, today, &b)); g_assert_cmpint(b.months, ==, 23);
    g_assert_true(sim_buy(450000, 500000, 0, today, &b)); g_assert_cmpint(b.months, ==, 0);
    g_assert_false(sim_buy(450000, 0, 0, today, &b));
}

static void test_sim_income(void) {
    SimBase base = {500000, 400000, 3};
    SimIncome r = sim_income(base, -15, 60000);
    g_assert_cmpint(r.new_income, ==, 425000); g_assert_cmpint(r.diff, ==, -75000);
    g_assert_cmpint(r.new_left, ==, 25000); g_assert_cmpint(r.year_diff, ==, -900000); g_assert_cmpint(r.goals_monthly, ==, 60000);
    g_autoptr(AppState) s = app_state_new();
    g_ptr_array_add(s->goals, goal_new("g1", "Viagem", 800000, 310000, DAY_NONE, 60000));
    g_ptr_array_add(s->goals, goal_new("g2", "Pronta", 1000, 1000, DAY_NONE, 5000));
    g_assert_cmpint(sim_goals_monthly(s), ==, 60000);
}

static void test_sim_debts(void) {
    Day today = D("2026-10-09");
    g_autoptr(AppState) s = st(parc("p1", 12140, "2026-09-15", TRUE, NULL, "g", 1, 6, "Mercado Pago 1/6"),
                               parc("p2", 12140, "2026-10-15", FALSE, NULL, "g", 2, 6, "Mercado Pago 2/6"),
                               parc("p3", 12140, "2026-11-15", FALSE, NULL, "g", 3, 6, "Mercado Pago 3/6"),
                               parc("c1", 60000, "2026-09-02", TRUE, "nu", "h", 1, 3, "Notebook (1/3)"),
                               parc("c2", 60000, "2026-10-02", TRUE, "nu", "h", 2, 3, "Notebook (2/3)"),
                               parc("c3", 60000, "2026-11-02", TRUE, "nu", "h", 3, 3, "Notebook (3/3)"),
                               parc("q1", 1000, "2026-09-01", TRUE, NULL, "z", 1, 2, NULL),
                               parc("q2", 1000, "2026-09-30", TRUE, NULL, "z", 2, 2, NULL), NULL);
    g_autoptr(GPtrArray) l = sim_debts(s, today);
    g_assert_cmpuint(l->len, ==, 2);
    SimDebt *a = l->pdata[0], *b = l->pdata[1];
    g_assert_cmpstr(a->name, ==, "Notebook"); g_assert_cmpstr(a->group_id, ==, "h");
    g_assert_cmpint(a->parcel, ==, 60000); g_assert_cmpint(a->remaining, ==, 1); g_assert_cmpint(a->total, ==, 3);
    g_assert_cmpint(a->left, ==, 60000); g_assert_true(a->card);
    g_assert_cmpstr(b->name, ==, "Mercado Pago");
    g_assert_cmpint(b->parcel, ==, 12140); g_assert_cmpint(b->remaining, ==, 2); g_assert_cmpint(b->total, ==, 6);
    g_assert_cmpint(b->left, ==, 24280); g_assert_false(b->card);
    SimPayoff p = sim_payoff(b, 22000, 100000);
    g_assert_cmpint(p.pay_now, ==, 22000); g_assert_cmpint(p.saved, ==, 2280); g_assert_cmpint(p.freed_per_month, ==, 12140);
    g_assert_cmpint(p.months, ==, 2); g_assert_cmpint(p.balance_after, ==, 78000);
}

static void test_compare(void) {
    Day today = D("2026-10-09");
    Compare c, x;
    g_assert_true(period_compare(D("2026-10-01"), D("2026-10-31"), today, &c));
    g_assert_cmpint(c.from, ==, D("2026-09-01")); g_assert_cmpint(c.to, ==, D("2026-09-09"));
    g_assert_cmpstr(c.label, ==, "vs. set (mesmos dias)");
    g_assert_true(period_compare(D("2026-09-01"), D("2026-09-30"), today, &x));
    g_assert_cmpint(x.from, ==, D("2026-08-01")); g_assert_cmpint(x.to, ==, D("2026-08-31")); g_assert_cmpstr(x.label, ==, "vs. agosto");
    g_assert_true(period_compare(D("2026-03-01"), D("2026-03-31"), D("2026-03-31"), &x));
    g_assert_cmpint(x.to, ==, D("2026-02-28"));
    g_assert_true(period_compare(D("2026-10-01"), D("2026-10-10"), today, &x));
    g_assert_cmpint(x.from, ==, D("2026-09-21")); g_assert_cmpint(x.to, ==, D("2026-09-30")); g_assert_cmpstr(x.label, ==, "vs. período anterior");
    g_assert_false(period_compare(DAY_NONE, DAY_NONE, today, &x));
    g_assert_false(period_compare(D("2026-10-01"), DAY_NONE, today, &x));
    STR_EQ(period_compare_text(520000, 585000, &c), "−11% vs. set (mesmos dias)");
    STR_EQ(period_compare_text(120, 100, &c), "+20% vs. set (mesmos dias)");
    STR_EQ(period_compare_text(100, 100, &c), "0% vs. set (mesmos dias)");
    STR_EQ(period_compare_text(100, 0, &c), "Sem base para comparar");
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/period/labels", test_labels);
    g_test_add_func("/period/arrows", test_arrows);
    g_test_add_func("/period/month-pending", test_month_pending);
    g_test_add_func("/period/day-net", test_day_net);
    g_test_add_func("/calendar/grid", test_grid);
    g_test_add_func("/calendar/day-totals", test_day_totals);
    g_test_add_func("/calendar/invoice-due", test_invoice_due);
    g_test_add_func("/calendar/overdue", test_overdue);
    g_test_add_func("/calendar/month-totals", test_month_totals);
    g_test_add_func("/calendar/compact", test_compact);
    g_test_add_func("/calendar/titles", test_titles);
    g_test_add_func("/simulator/base", test_sim_base);
    g_test_add_func("/simulator/save-buy", test_sim_save_buy);
    g_test_add_func("/simulator/income", test_sim_income);
    g_test_add_func("/simulator/debts", test_sim_debts);
    g_test_add_func("/reports/compare", test_compare);
    return g_test_run();
}
