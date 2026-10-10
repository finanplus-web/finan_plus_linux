/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Testes do período, do calendário, do simulador "E se…?" e das frases do Início: os mesmos casos
 * do app Android 1.4.0 (PeriodTest, CalendarTest, SimulatorTest e AssistTest.destaquesDoInicioPorPrioridade),
 * portados para C com o framework de testes da GLib.
 */
#include "core/model.h"
#include "core/period.h"
#include "core/simulator.h"
#include "core/assist.h"
#include "core/finance.h"
#include "core/ops.h"
#include <string.h>

static Day D(const char *s) {
    Day d;
    g_assert_true(day_parse_iso(s, &d));
    return d;
}

/* tx(id, tipo, valor, data, pago, cartão, pagamento de fatura) com descrição = id */
static Tx *tx(const char *id, Kind k, Cents v, const char *date, gboolean paid, const char *card, const char *pay) {
    Tx *t = tx_new(id, k, v, D(date), id, "Outros", paid, "main", card ? card : "");
    tx_set_str(&t->card_payment, pay ? pay : "");
    return t;
}
#define IN(id, v, date) tx(id, KIND_INCOME, v, date, TRUE, NULL, NULL)
#define EXP(id, v, date) tx(id, KIND_EXPENSE, v, date, TRUE, NULL, NULL)

static Tx *parcel(const char *id, Cents v, const char *date, gboolean paid, const char *card, const char *group, int n, int total,
                  const char *desc) {
    Tx *t = tx(id, KIND_EXPENSE, v, date, paid, card, NULL);
    tx_set_str(&t->group_id, group);
    tx_set_str(&t->desc, desc ? desc : id);
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

static void add_card(AppState *s, const char *id, const char *name, Cents limit, int close, int due) {
    g_ptr_array_add(s->cards, card_new(id, name, limit, close, due));
}

#define STR_EQ(expr, expected) do { g_autofree char *_v = (expr); g_assert_cmpstr(_v, ==, expected); } while (0)

/* ================================================================ PeriodTest */

static void test_period_labels(void) {
    Day today = D("2026-10-08");
    STR_EQ(period_label(D("2026-10-01"), D("2026-10-31")), "Outubro de 2026");
    STR_EQ(period_label(D("2028-02-01"), D("2028-02-29")), "Fevereiro de 2028");
    STR_EQ(period_label(D("2026-10-01"), D("2026-10-15")), "01/10/2026 a 15/10/2026");
    STR_EQ(period_label(DAY_NONE, DAY_NONE), "Todo o período");
    STR_EQ(period_label(D("2026-10-01"), DAY_NONE), "Desde 01/10/2026");
    STR_EQ(period_label(DAY_NONE, D("2026-10-15")), "Até 15/10/2026");
    STR_EQ(period_label(today, today), "08/10/2026");
    g_assert_false(period_full_month(D("2026-10-02"), D("2026-10-31"), NULL));
}

static void shift_is(Day f, Day t, int delta, Day today, const char *ef, const char *et) {
    Day a, b;
    period_shift(f, t, delta, today, &a, &b);
    g_assert_cmpint(a, ==, D(ef));
    g_assert_cmpint(b, ==, D(et));
}

static void test_period_arrows(void) {
    Day today = D("2026-10-08");
    shift_is(D("2026-10-01"), D("2026-10-31"), 1, today, "2026-11-01", "2026-11-30");
    shift_is(D("2026-10-01"), D("2026-10-31"), -1, today, "2026-09-01", "2026-09-30");
    /* período livre: vai para o mês vizinho de onde começa */
    shift_is(D("2026-09-10"), D("2026-10-09"), 1, today, "2026-10-01", "2026-10-31");
    /* sem datas: a partir do mês de hoje */
    shift_is(DAY_NONE, DAY_NONE, -1, today, "2026-09-01", "2026-09-30");
    /* virada de ano */
    shift_is(D("2026-12-01"), D("2026-12-31"), 1, today, "2027-01-01", "2027-01-31");
}

static void test_period_month_pending(void) {
    Day today = D("2026-10-08");
    g_autoptr(AppState) s = st(
        tx("sal", KIND_INCOME, 121362, "2026-10-15", FALSE, NULL, NULL),
        tx("alug", KIND_EXPENSE, 70000, "2026-10-15", FALSE, NULL, NULL),
        EXP("pago", 5000, "2026-10-02"),
        tx("nov", KIND_EXPENSE, 9999, "2026-11-02", FALSE, NULL, NULL),
        tx("compra", KIND_EXPENSE, 20000, "2026-09-25", TRUE, "c1", NULL), /* fatura vence 15/10 */
        NULL);
    add_card(s, "c1", "Roxo", 500000, 5, 15);
    Pending p = period_month_pending(s, ym_make(2026, 10), today);
    g_assert_cmpint(p.to_receive, ==, 121362);
    g_assert_cmpint(p.to_pay, ==, 70000 + 20000);
    g_autoptr(GPtrArray) oct = g_ptr_array_new();
    for (guint i = 0; i < s->txs->len; i++)
        if (day_ym(((Tx *)s->txs->pdata[i])->date) == ym_make(2026, 10)) g_ptr_array_add(oct, s->txs->pdata[i]);
    Pending q = period_pending(oct);
    g_assert_cmpint(q.to_receive, ==, 121362);
    g_assert_cmpint(q.to_pay, ==, 70000);
}

static void test_period_day_net_matches_calendar(void) {
    Day today = D("2026-10-08");
    g_autoptr(AppState) s = st(
        tx("a", KIND_INCOME, 121362, "2026-10-15", FALSE, NULL, NULL),
        tx("b", KIND_EXPENSE, 113240, "2026-10-15", FALSE, NULL, NULL),
        tx("c", KIND_EXPENSE, 9990, "2026-10-15", TRUE, "c1", NULL),
        tx("d", KIND_EXPENSE, 1000, "2026-10-15", TRUE, NULL, "c1"),
        NULL);
    add_card(s, "c1", "Roxo", 500000, 20, 28);
    g_assert_cmpint(period_cash_net(s->txs), ==, 121362 - 113240 - 1000);
    g_autoptr(CalMonth) m = cal_build(s, ym_make(2026, 10), today);
    g_assert_cmpint(cal_day_net(cal_get(m, D("2026-10-15"))), ==, period_cash_net(s->txs));
}

/* ================================================================ CalendarTest */

static void test_cal_grid(void) {
    Day cells[42];
    int n = cal_cells(ym_make(2026, 10), cells); /* 01/10/2026 é quinta */
    g_assert_cmpint(n, ==, 35);
    for (int i = 0; i < 4; i++) g_assert_cmpint(cells[i], ==, DAY_NONE);
    g_assert_cmpint(cells[4], ==, D("2026-10-01"));
    g_assert_cmpint(cells[34], ==, D("2026-10-31"));
    /* fevereiro de 2026 começa no domingo e tem 28 dias: exatamente 4 semanas */
    n = cal_cells(ym_make(2026, 2), cells);
    g_assert_cmpint(n, ==, 28);
    g_assert_cmpint(cells[0], ==, D("2026-02-01"));
    /* agosto de 2026 começa no sábado: 6 semanas */
    g_assert_cmpint(cal_cells(ym_make(2026, 8), cells), ==, 42);
}

static void test_cal_day_totals(void) {
    g_autoptr(AppState) s = st(
        IN("sal", 520000, "2026-10-05"),
        EXP("merc", 18240, "2026-10-05"),
        tx("compra", KIND_EXPENSE, 9990, "2026-10-05", TRUE, "c1", NULL),
        EXP("outro", 1000, "2026-11-05"),
        NULL);
    add_card(s, "c1", "Roxo", 500000, 20, 28);
    g_autoptr(CalMonth) m = cal_build(s, ym_make(2026, 10), D("2026-10-08"));
    const CalDay *day = cal_get(m, D("2026-10-05"));
    g_assert_nonnull(day);
    g_assert_cmpint(day->income, ==, 520000);
    g_assert_cmpint(day->expense, ==, 18240); /* a compra no cartão não sai da conta neste dia */
    g_assert_cmpint(cal_day_net(day), ==, 520000 - 18240);
    g_assert_cmpint(day->marks, ==, MARK_INCOME | MARK_EXPENSE | MARK_CARD);
    g_assert_cmpint(day->txs->len, ==, 3);
    g_assert_cmpstr(((Tx *)day->txs->pdata[0])->id, ==, "sal"); /* receitas primeiro */
    g_assert_null(cal_get(m, D("2026-11-05")));                 /* outro mês fica de fora */
}

static void test_cal_invoice_on_due_date(void) {
    g_autoptr(AppState) s = st(
        tx("c-a", KIND_EXPENSE, 40000, "2026-09-20", TRUE, "c1", NULL), /* fecha 05/10, vence 15/10 */
        tx("c-b", KIND_EXPENSE, 20000, "2026-09-25", TRUE, "c1", NULL),
        tx("pg", KIND_EXPENSE, 10000, "2026-10-01", TRUE, NULL, "c1"),  /* pagamento parcial */
        NULL);
    add_card(s, "c1", "Roxo", 500000, 5, 15);
    g_autoptr(CalMonth) m = cal_build(s, ym_make(2026, 10), D("2026-10-08"));
    const CalDay *due = cal_get(m, D("2026-10-15"));
    g_assert_cmpint(due->invoices->len, ==, 1);
    g_assert_cmpint(g_array_index(due->invoices, InvoiceDue, 0).amount, ==, 50000); /* 600 − 100 pagos */
    g_assert_cmpint(due->expense, ==, 50000);
    g_assert_false(due->overdue);
    g_assert_cmpint(due->marks, ==, MARK_CARD);
    /* o pagamento da fatura é dinheiro saindo da conta no dia em que foi feito */
    g_assert_cmpint(cal_get(m, D("2026-10-01"))->expense, ==, 10000);
    /* fatura vencida e em aberto fica em atraso */
    g_autoptr(CalMonth) later = cal_build(s, ym_make(2026, 10), D("2026-10-20"));
    g_assert_true(cal_get(later, D("2026-10-15"))->overdue);
}

static void test_cal_overdue(void) {
    g_autoptr(AppState) s = st(
        tx("net", KIND_EXPENSE, 11990, "2026-10-06", FALSE, NULL, NULL),
        tx("alug", KIND_EXPENSE, 150000, "2026-10-10", FALSE, NULL, NULL),
        EXP("farm", 4690, "2026-10-07"),
        NULL);
    g_autoptr(CalMonth) m = cal_build(s, ym_make(2026, 10), D("2026-10-08"));
    g_assert_true(cal_get(m, D("2026-10-06"))->overdue);
    g_assert_false(cal_get(m, D("2026-10-10"))->overdue);
    g_assert_false(cal_get(m, D("2026-10-07"))->overdue);
}

static void test_cal_month_totals(void) {
    g_autoptr(AppState) s = st(
        IN("a", 520000, "2026-10-05"),
        tx("b", KIND_INCOME, 90000, "2026-10-30", FALSE, NULL, NULL),
        tx("c", KIND_EXPENSE, 150000, "2026-10-10", FALSE, NULL, NULL),
        tx("d", KIND_EXPENSE, 34171, "2026-10-30", FALSE, NULL, NULL),
        NULL);
    g_autoptr(CalMonth) m = cal_build(s, ym_make(2026, 10), D("2026-10-08"));
    Cents inc, exp;
    cal_totals(m, &inc, &exp);
    g_assert_cmpint(inc, ==, 610000);
    g_assert_cmpint(exp, ==, 184171);
    g_assert_cmpint(cal_day_net(cal_get(m, D("2026-10-30"))), ==, 55829);
}

static void test_cal_compact(void) {
    struct { Cents c; const char *s; } cases[] = {
        {0, "0"}, {18240, "182"}, {11990, "120"}, {99949, "999"}, {99999, "1 mil"}, {100000, "1 mil"},
        {150000, "1,5 mil"}, {520000, "5,2 mil"}, {159999, "1,6 mil"}, {1500000, "15 mil"}, {1549900, "15 mil"},
        {99849999, "999 mil"}, {99949999, "1 mi"}, {99999999, "1 mi"}, {100000000, "1 mi"}, {120000000, "1,2 mi"},
        {2000000000000, "20 bi"}, {999999999999999, "10000 bi"},
    };
    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) STR_EQ(cal_compact(cases[i].c), cases[i].s);
    STR_EQ(cal_signed(520000), "+5,2 mil");
    STR_EQ(cal_signed(-11990), "−120");
    STR_EQ(cal_signed(0), "0");
}

static void test_cal_titles(void) {
    Day today = D("2026-10-08");
    STR_EQ(cal_month_title(ym_make(2026, 10)), "Outubro de 2026");
    STR_EQ(cal_month_title(ym_make(2027, 3)), "Março de 2027");
    STR_EQ(cal_day_title(D("2026-10-15"), today), "Quinta, 15 de outubro");
    STR_EQ(cal_day_title(D("2027-01-02"), today), "Sábado, 2 de janeiro de 2027");
    STR_EQ(cal_day_title(D("2026-10-05"), today), "Segunda, 5 de outubro");
    g_autoptr(AppState) s = st(tx("net", KIND_EXPENSE, 11990, "2026-10-06", FALSE, NULL, NULL), NULL);
    g_autoptr(CalMonth) m = cal_build(s, ym_make(2026, 10), today);
    const CalDay *day = cal_get(m, D("2026-10-06"));
    STR_EQ(cal_describe(D("2026-10-06"), day, today, FALSE),
           "6 de outubro, terça-feira, 1 lançamento, saldo do dia menos R$ 119,90, em atraso");
    STR_EQ(cal_describe(D("2026-10-06"), day, today, TRUE), "6 de outubro, terça-feira, 1 lançamento, em atraso");
    STR_EQ(cal_describe(today, NULL, today, FALSE), "8 de outubro, quinta-feira, hoje, sem lançamentos");
}

/* ================================================================ SimulatorTest */

static void test_sim_base(void) {
    Day today = D("2026-10-09");
    g_autoptr(AppState) s = st(
        IN("a", 500000, "2026-09-05"), EXP("b", 300000, "2026-09-10"),
        IN("c", 400000, "2026-08-05"), EXP("d", 200000, "2026-08-10"),
        EXP("e", 99999, "2026-10-02"),                                 /* mês atual: fica de fora */
        tx("f", KIND_EXPENSE, 50000, "2026-09-20", FALSE, NULL, NULL), /* pendente: fica de fora */
        NULL);
    SimBase b = sim_base(s, today); /* julho sem dados: média de 2 meses */
    g_assert_cmpint(b.income, ==, 450000);
    g_assert_cmpint(b.expense, ==, 250000);
    g_assert_cmpint(b.months, ==, 2);
    g_assert_cmpint(sim_base_left(b), ==, 200000);
    g_autoptr(AppState) empty = app_state_new();
    SimBase z = sim_base(empty, today);
    g_assert_cmpint(z.income + z.expense + z.months, ==, 0);
}

static void test_sim_save_and_buy(void) {
    Day today = D("2026-10-09");
    SimBase base = {211362, 113240, 3}; /* sobra R$ 981,22 */
    SimSave sv = sim_save(base, 20000, 12);
    g_assert_cmpint(sv.total, ==, 240000);
    g_assert_cmpint(sv.new_left, ==, 78122);
    g_assert_false(sv.over_left);
    g_assert_true(sim_save(base, 100000, 12).over_left);
    /* computador de R$ 4.500 guardando R$ 300: 15 meses, a partir de novembro → janeiro de 2028 */
    SimBuy b;
    g_assert_true(sim_buy(450000, 0, 30000, today, &b));
    g_assert_cmpint(b.months, ==, 15);
    g_assert_cmpint(b.done, ==, ym_make(2028, 1));
    STR_EQ(br_month_year(b.done), "janeiro de 2028");
    g_assert_true(sim_buy(450000, 0, 50000, today, &b));
    g_assert_cmpint(b.months, ==, 9);
    g_assert_true(sim_buy(450000, 0, 20000, today, &b));
    g_assert_cmpint(b.months, ==, 23); /* arredonda para cima */
    g_assert_true(sim_buy(450000, 500000, 0, today, &b));
    g_assert_cmpint(b.months, ==, 0); /* já tem o dinheiro */
    g_assert_false(sim_buy(450000, 0, 0, today, &b));
}

static void test_sim_income(void) {
    SimBase base = {500000, 400000, 3};
    SimIncome r = sim_income(base, -15.0, 60000);
    g_assert_cmpint(r.new_income, ==, 425000);
    g_assert_cmpint(r.diff, ==, -75000);
    g_assert_cmpint(r.new_left, ==, 25000);
    g_assert_cmpint(r.year_diff, ==, -900000);
    g_assert_cmpint(r.goals_monthly, ==, 60000);
    g_autoptr(AppState) s = app_state_new();
    g_ptr_array_add(s->goals, goal_new("g1", "Viagem", 800000, 310000, DAY_NONE, 60000));
    g_ptr_array_add(s->goals, goal_new("g2", "Pronta", 1000, 1000, DAY_NONE, 5000));
    g_assert_cmpint(sim_goals_monthly(s), ==, 60000); /* meta concluída não pede mais nada */
}

static void test_sim_debts(void) {
    Day today = D("2026-10-09");
    g_autoptr(AppState) s = st(
        parcel("p1", 12140, "2026-09-15", TRUE, NULL, "g", 1, 6, "Mercado Pago 1/6"),
        parcel("p2", 12140, "2026-10-15", FALSE, NULL, "g", 2, 6, "Mercado Pago 2/6"),
        parcel("p3", 12140, "2026-11-15", FALSE, NULL, "g", 3, 6, "Mercado Pago 3/6"),
        parcel("c1", 60000, "2026-09-02", TRUE, "nu", "h", 1, 3, "Notebook (1/3)"),
        parcel("c2", 60000, "2026-10-02", TRUE, "nu", "h", 2, 3, "Notebook (2/3)"),
        parcel("c3", 60000, "2026-11-02", TRUE, "nu", "h", 3, 3, "Notebook (3/3)"),
        parcel("q1", 1000, "2026-09-01", TRUE, NULL, "z", 1, 2, NULL),
        parcel("q2", 1000, "2026-09-30", TRUE, NULL, "z", 2, 2, NULL),
        NULL);
    g_autoptr(GPtrArray) l = sim_debts(s, today);
    g_assert_cmpint(l->len, ==, 2); /* quitada ("z") fica de fora; maior saldo primeiro */
    const SimDebt *a = l->pdata[0], *b = l->pdata[1];
    g_assert_cmpstr(a->group_id, ==, "h");
    g_assert_cmpstr(a->name, ==, "Notebook");
    g_assert_cmpint(a->parcel, ==, 60000);
    g_assert_cmpint(a->remaining, ==, 1); /* no cartão: só as parcelas que ainda não chegaram */
    g_assert_cmpint(a->total, ==, 3);
    g_assert_cmpint(a->left, ==, 60000);
    g_assert_true(a->card);
    g_assert_cmpstr(b->name, ==, "Mercado Pago");
    g_assert_cmpint(b->parcel, ==, 12140);
    g_assert_cmpint(b->remaining, ==, 2);
    g_assert_cmpint(b->total, ==, 6);
    g_assert_cmpint(b->left, ==, 24280);
    g_assert_false(b->card);
    SimPayoff p = sim_payoff(b, 22000, 100000);
    g_assert_cmpint(p.pay_now, ==, 22000);
    g_assert_cmpint(p.saved, ==, 2280);
    g_assert_cmpint(p.freed_per_month, ==, 12140);
    g_assert_cmpint(p.months, ==, 2);
    g_assert_cmpint(p.balance_after, ==, 78000);
}

static void test_report_comparison(void) {
    Day today = D("2026-10-09");
    Compare c;
    g_assert_true(period_compare(D("2026-10-01"), D("2026-10-31"), today, &c));
    g_assert_cmpint(c.from, ==, D("2026-09-01"));
    g_assert_cmpint(c.to, ==, D("2026-09-09"));
    g_assert_cmpstr(c.label, ==, "vs. set (mesmos dias)");
    Compare c2;
    g_assert_true(period_compare(D("2026-09-01"), D("2026-09-30"), today, &c2));
    g_assert_cmpint(c2.from, ==, D("2026-08-01"));
    g_assert_cmpint(c2.to, ==, D("2026-08-31"));
    g_assert_cmpstr(c2.label, ==, "vs. agosto");
    /* 31 de março: fevereiro tem menos dias */
    g_assert_true(period_compare(D("2026-03-01"), D("2026-03-31"), D("2026-03-31"), &c2));
    g_assert_cmpint(c2.to, ==, D("2026-02-28"));
    g_assert_true(period_compare(D("2026-10-01"), D("2026-10-10"), today, &c2));
    g_assert_cmpint(c2.from, ==, D("2026-09-21"));
    g_assert_cmpint(c2.to, ==, D("2026-09-30"));
    g_assert_cmpstr(c2.label, ==, "vs. período anterior");
    g_assert_false(period_compare(DAY_NONE, DAY_NONE, today, &c2));
    g_assert_false(period_compare(D("2026-10-01"), DAY_NONE, today, &c2));
    STR_EQ(period_compare_text(520000, 585000, &c), "−11% vs. set (mesmos dias)");
    STR_EQ(period_compare_text(120, 100, &c), "+20% vs. set (mesmos dias)");
    STR_EQ(period_compare_text(100, 100, &c), "0% vs. set (mesmos dias)");
    STR_EQ(period_compare_text(100, 0, &c), "Sem base para comparar");
}

/* ================================================================ AssistTest.destaquesDoInicioPorPrioridade */

static Tx *ex(const char *desc, const char *cat, Cents v, const char *date, gboolean paid) {
    Tx *t = tx(desc, KIND_EXPENSE, v, date, paid, NULL, NULL);
    tx_set_str(&t->category, cat);
    return t;
}

static void test_home_highlights(void) {
    Day today = D("2026-10-15");
    /* sem despesas realizadas: "Ainda não há despesas" fica de fora; contas a pagar vêm antes do que falta receber */
    g_autoptr(AppState) s = st(
        ex("Aluguel", "Moradia", 70000, "2026-10-20", FALSE), ex("Luz", "Moradia", 31250, "2026-10-20", FALSE),
        tx("Salário", KIND_INCOME, 121362, "2026-10-20", FALSE, NULL, NULL), NULL);
    MonthReport *r = insights_report(s, today, money_fmt);
    g_assert_cmpint(r->highlights->len, ==, 2);
    g_assert_cmpstr(r->highlights->pdata[0], ==, "Ainda faltam R$ 1.012,50 em 2 contas a pagar até o fim do mês.");
    g_assert_cmpstr(r->highlights->pdata[1], ==, "A receber neste mês: R$ 1.213,62 em 1 lançamento.");
    month_report_free(r);
    /* conta em atraso vem primeiro */
    g_autoptr(AppState) s2 = st(ex("Internet", "Moradia", 11990, "2026-10-06", FALSE), ex("Mercado", "Alimentação", 30000, "2026-10-05", TRUE), NULL);
    r = insights_report(s2, today, money_fmt);
    g_assert_cmpint(r->highlights->len, ==, 2);
    g_assert_nonnull(strstr(r->highlights->pdata[0], "em atraso"));
    g_assert_nonnull(strstr(r->highlights->pdata[1], "você gastou R$ 300,00"));
    month_report_free(r);
}

/* ================================================================ recorrências previstas (ProjectionTest) */

static AppState *with_adiant(gboolean active) {
    AppState *s = app_state_new();
    g_ptr_array_add(s->recurring, recurring_new("adiant", KIND_INCOME, "adiant", 121362, "Salário", "main", "", 15, active, D("2026-10-15"), ym_make(2026, 10)));
    return s;
}

static void test_proj_next_months(void) {
    Day today = D("2026-10-10");
    g_autoptr(AppState) s = with_adiant(TRUE);
    g_autoptr(GPtrArray) l = projection_between(s, D("2026-10-01"), D("2026-12-31"), today);
    g_assert_cmpuint(l->len, ==, 2);
    Tx *a = l->pdata[0], *b = l->pdata[1];
    g_assert_cmpint(a->date, ==, D("2026-11-15"));
    g_assert_cmpint(b->date, ==, D("2026-12-15"));
    g_assert_cmpstr(a->id, ==, "prev:adiant:2026-11");
    g_assert_true(tx_is_projected(a) && !a->paid && a->kind == KIND_INCOME && a->value == 121362);
    g_assert_cmpstr(a->recurring_id, ==, "adiant");
    g_assert_false(ops_can_toggle_paid(a));
}

static void test_proj_rules(void) {
    Day today = D("2026-10-10");
    g_autoptr(AppState) off = with_adiant(FALSE);
    g_autoptr(GPtrArray) none = projection_between(off, today, D("2027-12-31"), today);
    g_assert_cmpuint(none->len, ==, 0);
    g_autoptr(AppState) s = app_state_new();
    g_ptr_array_add(s->recurring, recurring_new("r", KIND_EXPENSE, "r", 5000, "Outros", "main", "", 31, TRUE, D("2027-02-10"), YM_NONE));
    g_autoptr(GPtrArray) l = projection_between(s, today, D("2027-04-30"), today);
    g_assert_cmpuint(l->len, ==, 3);
    g_assert_cmpint(((Tx *)l->pdata[0])->date, ==, D("2027-02-28"));
    g_assert_cmpint(((Tx *)l->pdata[1])->date, ==, D("2027-03-31"));
    g_assert_cmpint(((Tx *)l->pdata[2])->date, ==, D("2027-04-30"));
    g_autoptr(AppState) s2 = app_state_new();
    g_ptr_array_add(s2->recurring, recurring_new("s", KIND_INCOME, "s", 1000, "Outros", "main", "", 5, TRUE, DAY_NONE, ym_make(2026, 8)));
    g_autoptr(GPtrArray) l2 = projection_between(s2, today, D("2026-11-30"), today);
    g_assert_cmpuint(l2->len, ==, 1);
    g_assert_cmpint(((Tx *)l2->pdata[0])->date, ==, D("2026-11-05"));
}

static void test_proj_calendar_pending_forecast(void) {
    Day today = D("2026-10-10");
    g_autoptr(AppState) s = with_adiant(TRUE);
    g_ptr_array_add(s->txs, tx("alug", KIND_EXPENSE, 12140, "2026-11-30", FALSE, NULL, NULL));
    Ym nov = ym_make(2026, 11);
    g_autoptr(CalMonth) m = cal_build(s, nov, today);
    const CalDay *d = cal_get(m, D("2026-11-15"));
    g_assert_nonnull(d);
    g_assert_cmpint(d->income, ==, 121362);
    g_assert_cmpint(d->marks, ==, MARK_INCOME);
    Pending p = period_month_pending(s, nov, today);
    g_assert_cmpint(p.to_receive, ==, 121362);
    g_assert_cmpint(p.to_pay, ==, 12140);
    g_assert_cmpint(future_balance(s, D("2026-11-30"), today) - current_balance(s), ==, 121362 - 12140);
    g_autoptr(AppState) e = app_state_new();
    g_autoptr(AppState) a = with_adiant(TRUE);
    g_assert_cmpint(future_balance(a, D("2026-10-31"), today), ==, future_balance(e, D("2026-10-31"), today));
}

static void test_proj_month_arrives(void) {
    Day nov1 = D("2026-11-01");
    g_autoptr(AppState) s = with_adiant(TRUE);
    g_assert_cmpint(generate_recurring(s, nov1), ==, 1);
    g_assert_cmpint(((Tx *)s->txs->pdata[0])->date, ==, D("2026-11-15"));
    g_autoptr(GPtrArray) l = projection_between(s, nov1, D("2026-12-31"), nov1);
    g_assert_cmpuint(l->len, ==, 1);
    g_assert_cmpint(((Tx *)l->pdata[0])->date, ==, D("2026-12-15"));
    g_autoptr(CalMonth) m = cal_build(s, ym_make(2026, 11), nov1);
    g_assert_cmpint(cal_get(m, D("2026-11-15"))->income, ==, 121362);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/period/labels", test_period_labels);
    g_test_add_func("/period/arrows-move-whole-months", test_period_arrows);
    g_test_add_func("/period/month-pending-includes-invoices", test_period_month_pending);
    g_test_add_func("/period/day-net-matches-calendar", test_period_day_net_matches_calendar);
    g_test_add_func("/calendar/grid", test_cal_grid);
    g_test_add_func("/calendar/day-totals", test_cal_day_totals);
    g_test_add_func("/calendar/invoice-on-due-date", test_cal_invoice_on_due_date);
    g_test_add_func("/calendar/overdue", test_cal_overdue);
    g_test_add_func("/calendar/month-totals", test_cal_month_totals);
    g_test_add_func("/calendar/compact", test_cal_compact);
    g_test_add_func("/calendar/titles", test_cal_titles);
    g_test_add_func("/simulator/base", test_sim_base);
    g_test_add_func("/simulator/save-and-buy", test_sim_save_and_buy);
    g_test_add_func("/simulator/income", test_sim_income);
    g_test_add_func("/simulator/debts-and-payoff", test_sim_debts);
    g_test_add_func("/simulator/report-comparison", test_report_comparison);
    g_test_add_func("/assist/home-highlights", test_home_highlights);
    g_test_add_func("/projection/next-months", test_proj_next_months);
    g_test_add_func("/projection/rules", test_proj_rules);
    g_test_add_func("/projection/calendar-pending-forecast", test_proj_calendar_pending_forecast);
    g_test_add_func("/projection/month-arrives", test_proj_month_arrives);
    return g_test_run();
}
