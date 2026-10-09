/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Testes do núcleo: os mesmos 60 casos do app Android (CoreTest, AssistTest e ReportTest),
 * portados para C com o framework de testes da GLib. Rode com "meson test".
 */
#include "core/model.h"
#include "core/finance.h"
#include "core/ops.h"
#include "core/backup.h"
#include "core/assist.h"
#include "core/report.h"
#include <string.h>

#ifndef DICT_PATH
#define DICT_PATH "data/dicionario.txt"
#endif

/* ------------------------------------------------------------------ utilidades */

static Day D(const char *s) {
    Day d;
    g_assert_true(day_parse_iso(s, &d));
    return d;
}

static int seq = 0;

static Tx *mk(const char *id, Kind k, Cents v, const char *date, gboolean paid, const char *card, const char *pay,
              const char *acc, const char *cat) {
    Tx *t = tx_new(id, k, v, D(date), id, cat ? cat : "Outros", paid, acc ? acc : "main", card ? card : "");
    tx_set_str(&t->card_payment, pay ? pay : "");
    return t;
}

static Tx *ex_full(const char *desc, const char *cat, Cents v, const char *date, gboolean paid, const char *rec, const char *group) {
    g_autofree char *id = g_strdup_printf("t%d", seq++);
    Tx *t = tx_new(id, KIND_EXPENSE, v, D(date), desc, cat, paid, "main", "");
    tx_set_str(&t->recurring_id, rec ? rec : "");
    tx_set_str(&t->group_id, group ? group : "");
    return t;
}
#define EX(desc, cat, v, date) ex_full(desc, cat, v, date, TRUE, NULL, NULL)

static Tx *inc_full(const char *desc, const char *cat, Cents v, const char *date, gboolean paid) {
    g_autofree char *id = g_strdup_printf("t%d", seq++);
    return tx_new(id, KIND_INCOME, v, D(date), desc, cat, paid, "main", "");
}
#define INC(desc, cat, v, date) inc_full(desc, cat, v, date, TRUE)

/* estado padrão com os lançamentos dados (termina com NULL) */
static AppState *st(Tx *first, ...) {
    AppState *s = app_state_new();
    va_list ap;
    va_start(ap, first);
    for (Tx *t = first; t; t = va_arg(ap, Tx *)) g_ptr_array_add(s->txs, t);
    va_end(ap);
    return s;
}

static gboolean has_line(GPtrArray *lines, const char *needle) {
    for (guint i = 0; i < lines->len; i++) if (strstr(lines->pdata[i], needle)) return TRUE;
    return FALSE;
}

static gboolean contains(const char *hay, const char *needle) {
    if (!strstr(hay, needle)) { g_printerr("\n  texto: %s\n  esperado conter: %s\n", hay, needle); return FALSE; }
    return TRUE;
}

static Dictionary *dict(void) {
    static Dictionary *d = NULL;
    if (!d) {
        g_autofree char *text = NULL;
        g_autoptr(GError) e = NULL;
        g_assert_true(g_file_get_contents(DICT_PATH, &text, NULL, &e));
        d = dictionary_parse(text);
    }
    return d;
}

static GPtrArray *cats_of(const char *const *names, int n, ...) {
    GPtrArray *a = g_ptr_array_new_with_free_func(g_free);
    for (int i = 0; i < n; i++) g_ptr_array_add(a, g_strdup(names[i]));
    va_list ap;
    va_start(ap, n);
    for (const char *x = va_arg(ap, const char *); x; x = va_arg(ap, const char *)) g_ptr_array_add(a, g_strdup(x));
    va_end(ap);
    return a;
}

static GPtrArray *tips_of(GPtrArray *tips, InsightType type) {
    GPtrArray *o = g_ptr_array_new();
    for (guint i = 0; i < tips->len; i++) if (((Insight *)tips->pdata[i])->type == type) g_ptr_array_add(o, tips->pdata[i]);
    return o;
}

/* ================================================================== JSON (backup) */

static void test_json_round_trip(void) {
    g_autoptr(AppState) s = backup_parse("{\"txs\":[{\"id\":\"a\",\"kind\":\"expense\",\"value\":2.5,\"date\":\"2026-10-01\","
                                         "\"desc\":\"x\\\"yé\\nz\",\"category\":\"Lazer\",\"paid\":true}]}", -1, NULL, NULL);
    g_assert_nonnull(s);
    g_assert_cmpstr(((Tx *)s->txs->pdata[0])->desc, ==, "x\"yé\nz");
    g_autofree char *j = backup_to_json(s, NULL);
    g_assert_true(contains(j, "\"desc\":\"x\\\"yé\\nz\""));
    g_autoptr(AppState) again = backup_parse(j, -1, NULL, NULL);
    g_autofree char *j2 = backup_to_json(again, NULL);
    g_assert_cmpstr(j, ==, j2);
}

static void test_json_rejects_garbage(void) {
    const char *bad[] = {"{", "{\"txs\":[{\"id\":1,\"kind\":\"expense\"", "[1,]", "tru", "{}x", "\"\\q\""};
    for (guint i = 0; i < G_N_ELEMENTS(bad); i++) {
        g_autoptr(GError) e = NULL;
        g_assert_null(backup_parse(bad[i], -1, NULL, &e));
        g_assert_nonnull(e);
    }
    /* aninhamento excessivo é recusado antes de ler */
    GString *deep = g_string_new("{\"txs\":[], \"x\":");
    for (int i = 0; i < 200; i++) g_string_append_c(deep, '[');
    for (int i = 0; i < 200; i++) g_string_append_c(deep, ']');
    g_string_append_c(deep, '}');
    g_autoptr(GError) e = NULL;
    g_assert_null(backup_parse(deep->str, -1, NULL, &e));
    g_string_free(deep, TRUE);
}

/* ================================================================== dinheiro */

static void test_money_parse(void) {
    struct { const char *in; Cents v; } ok[] = {
        {"1500,50", 150050}, {"1.500,50", 150050}, {"1500.50", 150050}, {"R$ 2.000", 200000},
        {"1,500.25", 150025}, {"-50", -5000}, {"0,1", 10}, {"10", 1000}, {"1,999", 200},
    };
    for (guint i = 0; i < G_N_ELEMENTS(ok); i++) {
        Cents v = 0;
        g_assert_true(money_parse(ok[i].in, &v));
        g_assert_cmpint(v, ==, ok[i].v);
    }
    const char *bad[] = {"", "abc", "1,2,3", "1.2.3,4.5", "x", "-"};
    for (guint i = 0; i < G_N_ELEMENTS(bad); i++) { Cents v; g_assert_false(money_parse(bad[i], &v)); }
}

static void test_money_format(void) {
    char b[32];
    g_assert_cmpstr(money_format(123456789, b), ==, "R$ 1.234.567,89");
    g_assert_cmpstr(money_format(0, b), ==, "R$ 0,00");
    g_assert_cmpstr(money_format(-50, b), ==, "-R$ 0,50");
    g_assert_cmpstr(money_input(150050, b), ==, "1500,50");
}

/* ================================================================== backup */

static void test_backup_fresh_states(void) {
    g_autoptr(AppState) a = app_state_new();
    g_autoptr(AppState) b = app_state_new();
    g_assert_cmpuint(b->txs->len, ==, 0);
    g_autofree char *ja = backup_to_json(a, NULL), *jb = backup_to_json(b, NULL);
    g_assert_cmpstr(ja, ==, jb);
}

static void test_backup_rejects_without_txs(void) {
    const char *raw[] = {"null", "{}", "{\"txs\":\"x\"}", "[]"};
    for (guint i = 0; i < G_N_ELEMENTS(raw); i++) {
        g_autoptr(GError) e = NULL;
        g_assert_null(backup_parse(raw[i], -1, NULL, &e));
    }
}

static void test_backup_drops_invalid(void) {
    Dropped dr;
    g_autoptr(AppState) s = backup_parse(
        "{\"txs\":["
        "{\"id\":1,\"kind\":\"expense\",\"value\":10,\"date\":\"2026-10-01\",\"desc\":\"ok\",\"category\":\"Lazer\"},"
        "{\"id\":2,\"kind\":\"expense\",\"value\":10,\"desc\":\"sem data\"},"
        "{\"id\":3,\"kind\":\"expense\",\"value\":10,\"date\":\"2026-02-31\"},"
        "{\"id\":4,\"kind\":\"x\",\"value\":10,\"date\":\"2026-10-01\"},"
        "{\"id\":5,\"kind\":\"income\",\"value\":-3,\"date\":\"2026-10-01\"},"
        "{\"id\":6,\"kind\":\"income\",\"value\":\"12.5\",\"date\":\"2026-10-02\"},\"lixo\",null],"
        "\"cats\":{\"expense\":[\"A\",\"a\",\"\",7]},\"accounts\":\"nope\",\"goals\":[{\"name\":\"g\",\"target\":0}],"
        "\"limits\":{\"A\":\"abc\",\"B\":50}}", -1, &dr, NULL);
    g_assert_nonnull(s);
    g_assert_cmpuint(s->txs->len, ==, 2);
    g_assert_cmpint(dr.txs, ==, 6);
    g_assert_cmpint(((Tx *)s->txs->pdata[1])->value, ==, 1250);
    g_assert_cmpuint(s->cats[KIND_EXPENSE]->len, ==, 2);
    g_assert_cmpstr(s->cats[KIND_EXPENSE]->pdata[0], ==, "A");
    g_assert_cmpstr(s->cats[KIND_EXPENSE]->pdata[1], ==, "7");
    g_assert_cmpuint(s->cats[KIND_INCOME]->len, ==, G_N_ELEMENTS(DEFAULT_INCOME));
    g_assert_cmpstr(((Account *)s->accounts->pdata[0])->id, ==, "main");
    g_assert_cmpuint(s->goals->len, ==, 0);
    g_assert_cmpuint(s->limits->len, ==, 1);
    Cents v;
    g_assert_true(app_limit(s, "B", &v));
    g_assert_cmpint(v, ==, 5000);
}

static gboolean id_safe(const char *s) {
    if (!*s) return FALSE;
    for (; *s; s++) if (!g_ascii_isalnum(*s) && *s != '_' && *s != '.' && *s != '-') return FALSE;
    return TRUE;
}

static void test_backup_neutralizes_html(void) {
    const char *evil = "\\\"><img src=x onerror=alert(1)>";
    g_autofree char *json = g_strdup_printf(
        "{\"txs\":[{\"id\":\"%s\",\"kind\":\"expense\",\"value\":1,\"date\":\"2026-10-01\",\"cardId\":\"c1\"}],"
        "\"cards\":[{\"id\":\"c1\",\"name\":\"Nu\",\"limit\":100,\"close\":\"%s\",\"due\":\"%s\"}],"
        "\"accounts\":[{\"id\":\"%s\",\"name\":\"A\",\"initial\":0}],\"theme\":\"%s\",\"autoLock\":\"%s\"}",
        evil, evil, evil, evil, evil, evil);
    g_autoptr(AppState) s = backup_parse(json, -1, NULL, NULL);
    g_assert_nonnull(s);
    Card *c = s->cards->pdata[0];
    g_assert_cmpint(c->close, ==, 5);
    g_assert_cmpint(c->due, ==, 12);
    g_assert_true(id_safe(((Tx *)s->txs->pdata[0])->id));
    g_assert_true(id_safe(((Account *)s->accounts->pdata[0])->id));
    g_assert_cmpint(s->theme, ==, THEME_AUTO);
    g_assert_cmpint(s->auto_lock, ==, 0);
    g_assert_cmpstr(((Tx *)s->txs->pdata[0])->card_id, ==, "c1");
}

static void test_backup_duplicate_ids(void) {
    g_autoptr(AppState) s = backup_parse(
        "{\"txs\":[{\"id\":1,\"kind\":\"expense\",\"value\":1,\"date\":\"2026-10-01\",\"accountId\":\"x\",\"cardId\":\"x\"},"
        "{\"id\":1,\"kind\":\"income\",\"value\":1,\"date\":\"2026-10-01\",\"cardId\":\"c\"}],\"cards\":[{\"id\":\"c\",\"name\":\"C\"}]}",
        -1, NULL, NULL);
    Tx *a = s->txs->pdata[0], *b = s->txs->pdata[1];
    g_assert_cmpstr(a->id, !=, b->id);
    g_assert_cmpstr(a->account_id, ==, "main");
    g_assert_cmpstr(a->card_id, ==, "");
    g_assert_cmpstr(b->card_id, ==, ""); /* receita não vai para cartão */
}

static void test_backup_pwa_ids_and_export(void) {
    g_autoptr(AppState) s = backup_parse(
        "{\"txs\":[{\"id\":1696000000000.1234,\"kind\":\"expense\",\"value\":0.1,\"date\":\"2026-10-01\",\"parcel\":{\"n\":1,\"total\":3},"
        "\"groupId\":\"g1\"}],\"theme\":\"dark\",\"pin\":\"p1234\"}", -1, NULL, NULL);
    Tx *t = s->txs->pdata[0];
    g_assert_true(g_str_has_prefix(t->id, "1696000000000.123"));
    g_assert_cmpint(t->value, ==, 10);
    g_assert_cmpint(s->theme, ==, THEME_OLED);
    g_autofree char *json = backup_to_json(s, "1.1.0");
    g_assert_true(contains(json, "\"value\":0.10"));
    g_assert_null(strstr(json, "pin"));
    g_autoptr(AppState) again = backup_parse(json, -1, NULL, NULL);
    g_autofree char *a = backup_to_json(s, NULL), *b = backup_to_json(again, NULL);
    g_assert_cmpstr(a, ==, b);
}

/* ================================================================== finanças */

static AppState *with_card(Cents limit) {
    AppState *s = app_state_new();
    g_ptr_array_add(s->cards, card_new("c", "C", limit, 5, 12));
    return s;
}

static void test_dates_clamp(void) {
    g_assert_cmpint(day_plus_months_clamped(D("2026-01-31"), 1), ==, D("2026-02-28"));
    g_assert_cmpint(day_plus_months_clamped(D("2026-12-15"), 1), ==, D("2027-01-15"));
    g_assert_cmpint(ym_day_clamped(ym_make(2028, 2), 31), ==, D("2028-02-29"));
    /* calendário: ida e volta e dia da semana */
    g_assert_cmpint(day_weekday(D("2026-10-03")), ==, 6); /* sábado */
    char b[11];
    g_assert_cmpstr(day_iso(D("1999-12-31"), b), ==, "1999-12-31");
    g_autofree char *full = br_full_date(D("2026-10-03"));
    g_assert_cmpstr(full, ==, "03 de Outubro de 2026");
}

static void test_balance_card_and_payment(void) {
    g_autoptr(AppState) s = with_card(50000);
    ((Account *)s->accounts->pdata[0])->initial = 100000;
    g_assert_cmpint(current_balance(s), ==, 100000);
    g_ptr_array_add(s->txs, mk("t1", KIND_EXPENSE, 20000, "2026-10-02", TRUE, "c", NULL, NULL, NULL));
    g_assert_cmpint(current_balance(s), ==, 100000);
    g_assert_cmpint(account_balance(s, s->accounts->pdata[0]), ==, current_balance(s));
    g_ptr_array_add(s->txs, mk("t2", KIND_EXPENSE, 20000, "2026-10-12", TRUE, NULL, "c", NULL, NULL));
    g_assert_cmpint(current_balance(s), ==, 80000);
    CardStatus cs;
    card_status(s, s->cards->pdata[0], D("2026-10-20"), &cs);
    g_assert_cmpint(cs.used, ==, 0);
    card_status_clear(&cs);
}

static void test_invoice_close_due(void) {
    Card card = {"c", "C", 100000, 5, 12};
    g_assert_cmpint(invoice_ym(&card, D("2026-10-05")), ==, ym_make(2026, 10));
    g_assert_cmpint(invoice_ym(&card, D("2026-10-06")), ==, ym_make(2026, 11));
    g_assert_cmpint(invoice_due(&card, ym_make(2026, 10)), ==, D("2026-10-12"));
    Card d2 = {"d", "D", 0, 25, 5};
    g_assert_cmpint(invoice_due(&d2, ym_make(2026, 10)), ==, D("2026-11-05"));
    g_autoptr(AppState) s = with_card(100000);
    const char *dates[] = {"2026-10-02", "2026-11-02", "2026-12-02"};
    for (int i = 0; i < 3; i++) {
        g_autofree char *id = g_strdup_printf("p%d", i);
        g_ptr_array_add(s->txs, mk(id, KIND_EXPENSE, 10000, dates[i], TRUE, "c", NULL, NULL, NULL));
    }
    CardStatus cs;
    card_status(s, s->cards->pdata[0], D("2026-10-03"), &cs);
    g_assert_cmpint(cs.used, ==, 30000);
    g_assert_cmpint(cs.available, ==, 70000);
    const Invoice *cur = card_status_current(&cs);
    g_assert_nonnull(cur);
    g_assert_cmpint(cur->ym, ==, ym_make(2026, 10));
    g_assert_cmpint(invoice_open(cur), ==, 10000);
    card_status_clear(&cs);
}

static void test_future_balance(void) {
    g_autoptr(AppState) s = with_card(100000);
    ((Account *)s->accounts->pdata[0])->initial = 100000;
    g_ptr_array_add(s->txs, mk("a", KIND_EXPENSE, 30000, "2026-10-02", TRUE, "c", NULL, NULL, NULL));
    g_ptr_array_add(s->txs, mk("b", KIND_INCOME, 5000, "2026-10-20", FALSE, NULL, NULL, NULL, NULL));
    g_ptr_array_add(s->txs, mk("n", KIND_EXPENSE, 99900, "2026-11-20", FALSE, NULL, NULL, NULL, NULL));
    g_assert_cmpint(future_balance(s, D("2026-10-31"), D("2026-10-03")), ==, 75000);
}

static void test_recurring_future_start(void) {
    g_autoptr(AppState) s = app_state_new();
    g_ptr_array_add(s->recurring, recurring_new("r", KIND_EXPENSE, "Aluguel", 1000, "Moradia", "main", "", 10, TRUE,
                                                D("2026-12-10"), ym_make(2026, 12)));
    g_assert_cmpint(generate_recurring(s, D("2026-10-03")), ==, 0);
    ((Recurring *)s->recurring->pdata[0])->last = YM_NONE;
    g_assert_cmpint(generate_recurring(s, D("2026-10-03")), ==, 0);
}

static void test_recurring_catch_up(void) {
    g_autoptr(AppState) s = app_state_new();
    g_ptr_array_add(s->recurring, recurring_new("r", KIND_INCOME, "Salário", 1000, "Salário", "main", "", 31, TRUE,
                                                D("2026-06-30"), ym_make(2026, 7)));
    g_assert_cmpint(generate_recurring(s, D("2026-10-03")), ==, 3);
    const char *want[] = {"2026-08-31", "2026-09-30", "2026-10-31"};
    char b[11];
    for (int i = 0; i < 3; i++) g_assert_cmpstr(day_iso(((Tx *)s->txs->pdata[i])->date, b), ==, want[i]);
    g_assert_cmpint(generate_recurring(s, D("2026-10-20")), ==, 0);
}

static void test_recurring_never_before_start(void) {
    g_autoptr(AppState) s = app_state_new();
    g_ptr_array_add(s->recurring, recurring_new("r", KIND_EXPENSE, "x", 1000, "Lazer", "main", "", 5, TRUE, D("2026-10-15"), YM_NONE));
    generate_recurring(s, D("2026-11-20"));
    g_assert_cmpuint(s->txs->len, ==, 1);
    char b[11];
    g_assert_cmpstr(day_iso(((Tx *)s->txs->pdata[0])->date, b), ==, "2026-11-05");
}

static void test_installments_split(void) {
    Cents v[3];
    split_installments(10000, 3, v);
    g_assert_cmpint(v[0], ==, 3334);
    g_assert_cmpint(v[1], ==, 3333);
    g_assert_cmpint(v[2], ==, 3333);
}

static void test_goal_plan(void) {
    Goal g = {"g", "G", 120000, 20000, D("2027-03-31"), 10000};
    GoalPlan p = goal_plan(&g, D("2026-10-03"));
    g_assert_cmpint(p.remaining, ==, 100000);
    g_assert_true(p.has_needed);
    g_assert_cmpint(p.needed, ==, 16667);
    g_assert_cmpint(p.eta, ==, ym_make(2027, 7));
    g_assert_true(p.late);
    Goal done = {"g", "G", 1000, 1000, DAY_NONE, 0};
    g_assert_true(goal_plan(&done, D("2026-10-03")).done);
}

static void test_reminders(void) {
    g_autoptr(AppState) s = with_card(100000);
    g_ptr_array_add(s->txs, mk("late", KIND_EXPENSE, 100, "2026-10-01", FALSE, NULL, NULL, NULL, NULL));
    g_ptr_array_add(s->txs, mk("soon", KIND_EXPENSE, 200, "2026-10-04", FALSE, NULL, NULL, NULL, NULL));
    g_ptr_array_add(s->txs, mk("far", KIND_EXPENSE, 300, "2026-10-20", FALSE, NULL, NULL, NULL, NULL));
    g_ptr_array_add(s->txs, mk("buy", KIND_EXPENSE, 400, "2026-10-01", TRUE, "c", NULL, NULL, NULL));
    g_autoptr(GPtrArray) r = reminders(s, D("2026-10-10"), 2);
    g_assert_cmpuint(r->len, ==, 3);
    g_assert_cmpint(((Reminder *)r->pdata[0])->type, ==, REMINDER_BILL_OVERDUE);
    g_assert_cmpint(((Reminder *)r->pdata[1])->type, ==, REMINDER_BILL_OVERDUE);
    g_assert_cmpint(((Reminder *)r->pdata[2])->type, ==, REMINDER_INVOICE_DUE);
    g_assert_cmpstr(((Reminder *)r->pdata[2])->title, ==, "Fatura C");
    Reminder *n = next_due(s, D("2026-10-02"));
    g_assert_cmpstr(n->ref_id, ==, "late");
    reminder_free(n);
}

static void test_csv(void) {
    g_autoptr(AppState) s = app_state_new();
    g_ptr_array_add(s->cards, card_new("c", "Nu", 1, 1, 2));
    g_ptr_array_add(s->txs, tx_new("a", KIND_EXPENSE, 1050, D("2026-10-01"), "=HYPERLINK(\"x\")", "Ca;sa \"1\"", TRUE, "main", "c"));
    g_autofree char *csv = csv_build(s);
    g_assert_true(g_str_has_prefix(csv, "\xEF\xBB\xBF" "data;tipo;categoria;descricao;valor;situacao;conta;cartao"));
    g_assert_true(contains(csv, "\"'=HYPERLINK(\"\"x\"\")\""));
    g_assert_true(contains(csv, "\"Ca;sa \"\"1\"\"\""));
    g_assert_true(contains(csv, "\"10,50\""));
    g_assert_true(g_str_has_suffix(csv, "\"Nu\""));
}

/* ================================================================== operações */

static TxDraft draft(const char *value, int reps, RepsMode mode, const char *card, gboolean rec, const char *date, const char *desc) {
    TxDraft d = {KIND_EXPENSE, desc, value, "Alimentação", D(date), TRUE, "main", card, reps, mode, rec};
    return d;
}
#define DRAFT() draft("100", 1, REPS_TOTAL, "", FALSE, "2026-10-03", "Mercado")

static void test_ops_validation(void) {
    g_autoptr(AppState) s = app_state_new();
    OpErr e;
    TxDraft d = DRAFT(); d.desc = "   ";
    g_assert_false(ops_save_tx(s, NULL, &d, &e)); g_assert_cmpstr(e.msg, ==, "Informe uma descrição.");
    d = DRAFT(); d.value = "0";
    g_assert_false(ops_save_tx(s, NULL, &d, &e)); g_assert_true(contains(e.msg, "maior que zero"));
    d = DRAFT(); d.card_id = "nope";
    g_assert_false(ops_save_tx(s, NULL, &d, &e)); g_assert_true(contains(e.msg, "Cadastre um cartão"));
    d = DRAFT(); d.reps = 3; d.recurring = TRUE;
    g_assert_false(ops_save_tx(s, NULL, &d, &e)); g_assert_true(contains(e.msg, "parcelas ou repetição"));
    g_assert_false(ops_save_account(s, NULL, "", "", &e)); g_assert_cmpstr(e.msg, ==, "Informe o nome da conta.");
    g_assert_cmpuint(s->txs->len, ==, 0); /* nada foi alterado */
}

static void test_ops_installments(void) {
    g_autoptr(AppState) s = app_state_new();
    TxDraft d = draft("1000", 3, REPS_TOTAL, "", FALSE, "2026-10-03", "Mercado");
    g_assert_true(ops_save_tx(s, NULL, &d, NULL));
    Tx *t0 = s->txs->pdata[0], *t1 = s->txs->pdata[1], *t2 = s->txs->pdata[2];
    g_assert_cmpint(t0->value, ==, 33334); g_assert_cmpint(t1->value, ==, 33333); g_assert_cmpint(t2->value, ==, 33333);
    g_assert_true(t0->paid); g_assert_false(t1->paid); g_assert_false(t2->paid);
    g_assert_cmpstr(t0->group_id, ==, t1->group_id); g_assert_cmpstr(t1->group_id, ==, t2->group_id);
    g_assert_cmpstr(t1->desc, ==, "Mercado (2/3)");
    g_assert_cmpint(ops_later_parcels(s, t0->id), ==, 2);
    g_autoptr(AppState) a = app_state_copy(s);
    ops_delete_tx(a, t0->id, TRUE);
    g_assert_cmpuint(a->txs->len, ==, 0);
    ops_delete_tx(s, t0->id, FALSE);
    g_assert_cmpuint(s->txs->len, ==, 2);
}

static void test_ops_recurring_from_tx(void) {
    g_autoptr(AppState) s = app_state_new();
    TxDraft d = draft("100", 1, REPS_TOTAL, "", TRUE, "2026-12-10", "Mercado");
    g_assert_true(ops_save_tx(s, NULL, &d, NULL));
    g_assert_cmpuint(s->txs->len, ==, 1);
    g_assert_cmpint(generate_recurring(s, D("2026-10-03")), ==, 0);
    g_assert_cmpint(generate_recurring(s, D("2026-12-20")), ==, 0);
    g_assert_cmpint(generate_recurring(s, D("2027-01-20")), ==, 1);
}

static void test_ops_card_and_payment(void) {
    g_autoptr(AppState) s = app_state_new();
    OpErr e;
    g_assert_true(ops_save_account(s, "main", "Conta", "1.000,00", NULL));
    g_assert_true(ops_save_card(s, NULL, "Nu", "500", "5", "12", NULL));
    const char *c = ((Card *)s->cards->pdata[0])->id;
    TxDraft d = draft("300", 3, REPS_TOTAL, c, FALSE, "2026-10-03", "Mercado");
    g_assert_true(ops_save_tx(s, NULL, &d, NULL));
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        g_assert_true(t->paid);
        g_assert_cmpstr(t->card_id, ==, c);
    }
    g_assert_cmpint(current_balance(s), ==, 100000);
    CardStatus cs;
    card_status(s, s->cards->pdata[0], D("2026-10-03"), &cs);
    g_assert_cmpint(cs.available, ==, 20000);
    card_status_clear(&cs);
    g_assert_true(ops_pay_invoice(s, c, "100", "main", D("2026-10-12"), NULL));
    g_assert_cmpint(current_balance(s), ==, 90000);
    g_assert_false(ops_delete_card(s, c, FALSE, &e));
    g_assert_true(contains(e.msg, "compras ou pagamentos"));
}

static void test_ops_goal(void) {
    g_autoptr(AppState) s = app_state_new();
    g_assert_true(ops_save_goal(s, NULL, "Carro", "1500,50", "", DAY_NONE, "100", NULL));
    Goal *g = s->goals->pdata[0];
    g_assert_cmpint(g->target, ==, 150050);
    g_autofree char *id = g_strdup(g->id);
    g_assert_true(ops_save_goal(s, id, "Carro novo", "1500,50", "200,25", DAY_NONE, "100", NULL));
    g_assert_cmpint(g->saved, ==, 20025);
    g_assert_cmpstr(g->name, ==, "Carro novo");
    g_assert_true(ops_save_goal(s, id, "Carro novo", "1500,50", "-999999", DAY_NONE, "", NULL));
    g_assert_cmpint(g->saved, ==, 0);
}

static void test_ops_rename_category(void) {
    g_autoptr(AppState) s = app_state_new();
    OpErr e;
    TxDraft d = draft("100", 1, REPS_TOTAL, "", TRUE, "2026-10-03", "Mercado");
    g_assert_true(ops_save_tx(s, NULL, &d, NULL));
    g_assert_true(ops_save_limit(s, NULL, "Alimentação", "300", NULL));
    g_assert_true(ops_rename_category(s, KIND_EXPENSE, "Alimentação", "Comida", NULL));
    g_assert_cmpstr(((Tx *)s->txs->pdata[0])->category, ==, "Comida");
    g_assert_cmpstr(((Recurring *)s->recurring->pdata[0])->category, ==, "Comida");
    g_assert_cmpuint(s->limits->len, ==, 1);
    Cents v;
    g_assert_true(app_limit(s, "Comida", &v));
    g_assert_cmpint(v, ==, 30000);
    g_assert_false(ops_add_category(s, KIND_EXPENSE, "comida", &e));
    g_assert_true(contains(e.msg, "já existe"));
    g_assert_false(ops_check_delete_category(s, KIND_EXPENSE, "Comida", &e));
    g_assert_true(contains(e.msg, "recorrência"));
}

static void test_ops_account_in_use(void) {
    g_autoptr(AppState) s = app_state_new();
    OpErr e;
    g_assert_true(ops_save_account(s, NULL, "Banco 2", "", NULL));
    g_autofree char *b2 = g_strdup(((Account *)s->accounts->pdata[1])->id);
    g_assert_true(ops_save_recurring(s, NULL, KIND_EXPENSE, "Curso", "250", "10", "Educação", b2, "", TRUE, D("2026-12-01"),
                                     D("2026-10-03"), NULL));
    g_assert_false(ops_delete_account(s, b2, FALSE, &e));
    g_assert_true(contains(e.msg, "recorrências"));
    g_autoptr(AppState) fresh = app_state_new();
    g_assert_false(ops_delete_account(fresh, "main", FALSE, &e));
    g_assert_true(contains(e.msg, "pelo menos uma"));
}

/* ================================================================== assistente: texto e dicionário */

static void test_text_normalize(void) {
    g_autofree char *f = text_fold("PAG*Uber  Trip (2/3)");
    g_assert_cmpstr(f, ==, "pag uber trip 2 3");
    g_auto(GStrv) t = text_tokens("PAG*Uber  Trip (2/3)");
    g_assert_cmpuint(g_strv_length(t), ==, 2);
    g_assert_cmpstr(t[0], ==, "uber"); g_assert_cmpstr(t[1], ==, "trip");
    g_auto(GStrv) t2 = text_tokens("Pão de Açúcar");
    g_assert_cmpuint(g_strv_length(t2), ==, 2);
    g_assert_cmpstr(t2[0], ==, "pao"); g_assert_cmpstr(t2[1], ==, "acucar");
    g_assert_true(text_same("Saúde", "SAUDE"));
}

static const char *match_cat(const char *desc, Kind k, GPtrArray *cats) {
    static char buf[64];
    DictMatch *m = dictionary_match(dict(), desc, k, cats);
    if (!m) return NULL;
    g_strlcpy(buf, m->category, sizeof buf);
    dict_match_free(m);
    return buf;
}

static void test_dict_real_file(void) {
    g_assert_cmpuint(dict()->sections->len, >=, 20);
    g_autoptr(GPtrArray) cats = cats_of(DEFAULT_EXPENSE, 7, NULL);
    g_autoptr(GPtrArray) inc = cats_of(DEFAULT_INCOME, 4, NULL);
    g_assert_cmpstr(match_cat("iFood pizza", KIND_EXPENSE, cats), ==, "Alimentação");
    g_assert_cmpstr(match_cat("Supermercados Pão de Açúcar", KIND_EXPENSE, cats), ==, "Alimentação");
    g_assert_cmpstr(match_cat("PAG*UBER TRIP", KIND_EXPENSE, cats), ==, "Transporte");
    g_assert_cmpstr(match_cat("Conta Sabesp", KIND_EXPENSE, cats), ==, "Moradia");
    g_assert_cmpstr(match_cat("Drogasil", KIND_EXPENSE, cats), ==, "Saúde");
    g_assert_cmpstr(match_cat("Netflix.com", KIND_EXPENSE, cats), ==, "Lazer");
    g_assert_cmpstr(match_cat("Salário setembro", KIND_INCOME, inc), ==, "Salário");
    g_assert_cmpstr(match_cat("Presente da Ana", KIND_EXPENSE, cats), ==, "Outros");
}

static void test_dict_specific_category(void) {
    g_autoptr(GPtrArray) cats = cats_of(DEFAULT_EXPENSE, 7, "Mercado", "Assinaturas", NULL);
    g_assert_cmpstr(match_cat("Carrefour", KIND_EXPENSE, cats), ==, "Mercado");
    g_assert_cmpstr(match_cat("Spotify", KIND_EXPENSE, cats), ==, "Assinaturas");
}

static void test_dict_longer_term_wins(void) {
    g_autoptr(GPtrArray) cats = cats_of(DEFAULT_EXPENSE, 7, NULL);
    g_assert_cmpstr(match_cat("Uber Eats", KIND_EXPENSE, cats), ==, "Alimentação");
    g_assert_cmpstr(match_cat("Mercado Livre", KIND_EXPENSE, cats), ==, "Outros");
}

static void test_dict_tie(void) {
    Dictionary *dd = dictionary_parse("[despesa: A]\nfoo\n[despesa: B]\nbar");
    const char *ab[] = {"A", "B"};
    g_autoptr(GPtrArray) cats = cats_of(ab, 2, NULL);
    g_autoptr(GPtrArray) only_b = cats_of(ab + 1, 1, NULL);
    DictMatch *m = dictionary_match(dd, "foo bar", KIND_EXPENSE, cats);
    g_assert_null(m);
    m = dictionary_match(dd, "foo", KIND_EXPENSE, cats);
    g_assert_cmpstr(m->category, ==, "A");
    dict_match_free(m);
    g_assert_null(dictionary_match(dd, "foo", KIND_EXPENSE, only_b)); /* categoria inexistente nunca é sugerida */
    dictionary_free(dd);
}

/* ================================================================== assistente: categorias */

static AppState *history(void) {
    return st(EX("Feira do Seu Zé", "Alimentação", 4500, "2026-07-02"), EX("Feira do Seu Zé", "Alimentação", 3900, "2026-07-09"),
              EX("Uber casa", "Transporte", 2300, "2026-07-03"), EX("Uber trabalho", "Transporte", 1800, "2026-07-04"),
              EX("Uber aeroporto", "Transporte", 6100, "2026-07-10"), EX("Academia Fit", "Saúde", 9900, "2026-07-05"),
              EX("Cinema shopping", "Lazer", 5000, "2026-07-06"), EX("Livro faculdade", "Educação", 7000, "2026-07-07"), NULL);
}

static void test_cat_same_description(void) {
    g_autoptr(AppState) s = history();
    Categorizer *c = categorizer_build(s, KIND_EXPENSE, dict());
    Suggestion *g = categorizer_suggest(c, "feira do seu zé");
    g_assert_nonnull(g);
    g_assert_cmpstr(g->category, ==, "Alimentação");
    g_assert_cmpint(g->source, ==, SOURCE_SAME_DESCRIPTION);
    g_assert_true(contains(g->why, "2 vezes"));
    suggestion_free(g);
    categorizer_free(c);
}

static void test_cat_learns(void) {
    g_autoptr(AppState) s = history();
    Categorizer *c = categorizer_build(s, KIND_EXPENSE, NULL);
    g_assert_null(categorizer_suggest(c, "Uber shopping")); /* ambíguo: não sugere */
    Suggestion *g = categorizer_suggest(c, "Uber noite");
    g_assert_nonnull(g);
    g_assert_cmpstr(g->category, ==, "Transporte");
    g_assert_cmpint(g->source, ==, SOURCE_LEARNED);
    g_assert_true(contains(g->why, "“uber”") && contains(g->why, "3 lançamento"));
    suggestion_free(g);
    categorizer_free(c);
    c = categorizer_build(s, KIND_EXPENSE, dict());
    g = categorizer_suggest(c, "Seu Zé banca"); /* aprendizado pessoal vence o dicionário */
    g_assert_nonnull(g);
    g_assert_cmpstr(g->category, ==, "Alimentação");
    suggestion_free(g);
    categorizer_free(c);
}

static void test_cat_dictionary_fallback(void) {
    g_autoptr(AppState) s = app_state_new();
    Categorizer *c = categorizer_build(s, KIND_EXPENSE, dict());
    Suggestion *g = categorizer_suggest(c, "Drogaria São Paulo");
    g_assert_nonnull(g);
    g_assert_cmpint(g->source, ==, SOURCE_DICTIONARY);
    suggestion_free(g);
    g_assert_null(categorizer_suggest(c, "xyz abc"));
    g_assert_null(categorizer_suggest(c, ""));
    categorizer_free(c);
}

static void test_cat_same_desc_different_cats(void) {
    g_autoptr(AppState) s = st(EX("Loja", "Lazer", 100, "2026-07-01"), EX("Loja", "Outros", 100, "2026-07-02"), NULL);
    Categorizer *c = categorizer_build(s, KIND_EXPENSE, NULL);
    g_assert_null(categorizer_suggest(c, "Loja"));
    categorizer_free(c);
}

static void test_cat_parcel_suffix_and_deleted_cat(void) {
    g_autofree char *clean = text_clean_parcel("Compra TV (3/10)");
    g_assert_cmpstr(clean, ==, "Compra TV");
    g_autoptr(AppState) s = st(EX("Coisa x", "Antiga", 100, "2026-07-01"), NULL);
    Categorizer *c = categorizer_build(s, KIND_EXPENSE, NULL);
    g_assert_null(categorizer_suggest(c, "Coisa x")); /* "Antiga" não está nas categorias */
    categorizer_free(c);
}

static void test_cat_learned_words(void) {
    g_autoptr(AppState) s = history();
    Categorizer *c = categorizer_build(s, KIND_EXPENSE, NULL);
    g_autoptr(GPtrArray) w = categorizer_learned_words(c, 6);
    gboolean found = FALSE;
    for (guint i = 0; i < w->len; i++) {
        LearnedCategory *l = w->pdata[i];
        if (strcmp(l->category, "Transporte") != 0) continue;
        found = TRUE;
        g_assert_cmpuint(l->words->len, ==, 1);
        g_assert_cmpstr(g_array_index(l->words, LearnedWord, 0).word, ==, "uber");
        g_assert_cmpint(g_array_index(l->words, LearnedWord, 0).count, ==, 3);
    }
    g_assert_true(found);
    categorizer_free(c);
}

/* ================================================================== assistente: resumo e dicas */

#define TODAY D("2026-10-15")

static void test_ins_report(void) {
    g_autoptr(AppState) s = st(
        EX("Mercado", "Alimentação", 30000, "2026-10-05"), EX("Uber", "Transporte", 10000, "2026-10-10"),
        EX("Mercado", "Alimentação", 20000, "2026-09-05"), EX("Aluguel", "Moradia", 100000, "2026-09-20"),
        INC("Salário", "Salário", 300000, "2026-10-05"), ex_full("Luz", "Moradia", 15000, "2026-10-25", FALSE, NULL, NULL), NULL);
    MonthReport *r = insights_report(s, TODAY, money_fmt);
    const char *l0 = r->lines->pdata[0];
    g_assert_true(contains(l0, "R$ 400,00") && contains(l0, "100% a mais") && contains(l0, "R$ 200,00"));
    g_assert_true(has_line(r->lines, "sobram R$ 2.600,00"));
    g_assert_true(has_line(r->lines, "maior categoria é Alimentação") && has_line(r->lines, "75%"));
    g_assert_true(has_line(r->lines, "faltam R$ 150,00 em 1 conta"));
    month_report_free(r);
    g_autoptr(AppState) s2 = st(inc_full("Salário", "Salário", 90000, "2026-10-30", FALSE),
                                inc_full("Adiantamento", "Extra", 121362, "2026-10-15", FALSE), NULL);
    r = insights_report(s2, TODAY, money_fmt);
    gboolean exact = FALSE;
    for (guint i = 0; i < r->lines->len; i++)
        if (strcmp(r->lines->pdata[i], "A receber neste mês: R$ 2.113,62 em 2 lançamentos.") == 0) exact = TRUE;
    g_assert_true(exact);
    month_report_free(r);
}

static void test_ins_duplicate(void) {
    g_autoptr(AppState) s = st(EX("Padaria", "Alimentação", 1250, "2026-10-10"), EX("padaria", "Alimentação", 1250, "2026-10-10"),
                               EX("Padaria", "Alimentação", 1250, "2026-10-11"), NULL);
    g_autoptr(GPtrArray) tips = insights_tips(s, TODAY, money_fmt);
    g_autoptr(GPtrArray) l = tips_of(tips, INSIGHT_DUPLICATE);
    g_assert_cmpuint(l->len, ==, 1);
    g_assert_true(contains(((Insight *)l->pdata[0])->text, "2 vezes em 10/10"));
}

static void test_ins_parcels_not_duplicates(void) {
    g_autoptr(AppState) s = st(ex_full("TV (1/2)", "Outros", 50000, "2026-10-10", TRUE, NULL, "g"),
                               ex_full("TV (2/2)", "Outros", 50000, "2026-10-10", TRUE, NULL, "g"), NULL);
    g_autoptr(GPtrArray) tips = insights_tips(s, TODAY, money_fmt);
    g_autoptr(GPtrArray) l = tips_of(tips, INSIGHT_DUPLICATE);
    g_assert_cmpuint(l->len, ==, 0);
}

static void test_ins_subscriptions_and_price_up(void) {
    g_autoptr(AppState) s = st(
        EX("Netflix", "Lazer", 4490, "2026-07-08"), EX("Netflix", "Lazer", 4490, "2026-08-08"),
        EX("Netflix", "Lazer", 4490, "2026-09-08"), EX("Netflix", "Lazer", 5590, "2026-10-08"),
        EX("Spotify", "Lazer", 2190, "2026-08-02"), EX("Spotify", "Lazer", 2190, "2026-09-02"), EX("Spotify", "Lazer", 2190, "2026-10-02"),
        EX("Uber", "Transporte", 2000, "2026-09-01"), EX("Uber", "Transporte", 2500, "2026-09-03"), EX("Uber", "Transporte", 2200, "2026-10-01"),
        EX("Cinema", "Lazer", 3000, "2026-08-10"), EX("Cinema", "Lazer", 3000, "2026-10-10"), NULL);
    g_autoptr(GPtrArray) subs = insights_recurring_expenses(s, TODAY);
    g_assert_cmpuint(subs->len, ==, 2);
    gboolean n = FALSE, sp = FALSE;
    for (guint i = 0; i < subs->len; i++) {
        const char *name = ((MonthlyExpense *)subs->pdata[i])->name;
        if (!strcmp(name, "Netflix")) n = TRUE;
        if (!strcmp(name, "Spotify")) sp = TRUE;
    }
    g_assert_true(n && sp);
    g_autoptr(GPtrArray) tips = insights_tips(s, TODAY, money_fmt);
    g_autoptr(GPtrArray) sum = tips_of(tips, INSIGHT_SUBSCRIPTIONS);
    g_assert_cmpuint(sum->len, ==, 1);
    const char *t = ((Insight *)sum->pdata[0])->text;
    g_assert_true(contains(t, "R$ 77,80 por mês") && contains(t, "R$ 933,60 por ano"));
    g_autoptr(GPtrArray) up = tips_of(tips, INSIGHT_PRICE_UP);
    g_assert_cmpuint(up->len, ==, 1);
    t = ((Insight *)up->pdata[0])->text;
    g_assert_true(contains(t, "R$ 44,90 para R$ 55,90") && contains(t, "+24%"));
}

static void test_ins_category_spike(void) {
    g_autoptr(AppState) s = st(
        EX("Bar", "Lazer", 10000, "2026-07-10"), EX("Bar", "Lazer", 10000, "2026-08-10"), EX("Bar", "Lazer", 10000, "2026-09-10"),
        EX("Show", "Lazer", 25000, "2026-10-03"),
        EX("Mercado", "Alimentação", 50000, "2026-09-10"), EX("Mercado", "Alimentação", 52000, "2026-10-10"), NULL);
    g_autoptr(GPtrArray) tips = insights_tips(s, TODAY, money_fmt);
    g_autoptr(GPtrArray) l = tips_of(tips, INSIGHT_CATEGORY_SPIKE);
    g_assert_cmpuint(l->len, ==, 1);
    Insight *i = l->pdata[0];
    g_assert_cmpstr(i->query, ==, "Lazer");
    g_assert_true(contains(i->text, "R$ 250,00") && contains(i->text, "150% acima") && contains(i->text, "R$ 100,00"));
}

static void test_ins_limit_pace(void) {
    /* dia 15 de 31: 3 despesas variáveis (600) + compromisso fixo (100, recorrência, não é extrapolado) */
    g_autoptr(AppState) s = st(EX("Feira", "Alimentação", 20000, "2026-10-03"), EX("Restaurante", "Alimentação", 20000, "2026-10-08"),
                               EX("Mercado", "Alimentação", 20000, "2026-10-12"),
                               ex_full("Assinatura comida", "Alimentação", 10000, "2026-10-01", TRUE, "r1", NULL), NULL);
    g_ptr_array_add(s->limits, limit_new("Alimentação", 100000));
    g_autoptr(GPtrArray) tips = insights_tips(s, TODAY, money_fmt);
    g_autoptr(GPtrArray) l = tips_of(tips, INSIGHT_LIMIT_PACE);
    g_assert_cmpuint(l->len, ==, 1);
    /* projeção = 100 + 600/15*31 = 1340 */
    const char *t = ((Insight *)l->pdata[0])->text;
    g_assert_true(contains(t, "R$ 1.340,00") && contains(t, "R$ 18,75 por dia") && contains(t, "16 dias"));
    g_autoptr(GPtrArray) early = insights_tips(s, D("2026-10-05"), money_fmt);
    g_autoptr(GPtrArray) l2 = tips_of(early, INSIGHT_LIMIT_PACE);
    g_assert_cmpuint(l2->len, ==, 0); /* antes do dia 7 não projeta */
    /* só 2 despesas variáveis na categoria: não há ritmo para projetar */
    g_autoptr(AppState) s2 = st(EX("Restaurante", "Alimentação", 30000, "2026-10-08"), EX("Mercado", "Alimentação", 30000, "2026-10-12"), NULL);
    g_ptr_array_add(s2->limits, limit_new("Alimentação", 100000));
    g_autoptr(GPtrArray) tips3 = insights_tips(s2, TODAY, money_fmt);
    g_autoptr(GPtrArray) l3 = tips_of(tips3, INSIGHT_LIMIT_PACE);
    g_assert_cmpuint(l3->len, ==, 0);
}

static void test_ins_over_income(void) {
    /* 5 despesas variáveis de R$ 160 até o dia 15: 800/15*31 = 1653,33 */
    g_autoptr(AppState) s = st(INC("Salário", "Salário", 100000, "2026-10-05"), EX("Gasto 1", "Outros", 16000, "2026-10-01"),
                               EX("Gasto 2", "Outros", 16000, "2026-10-03"), EX("Gasto 3", "Outros", 16000, "2026-10-06"),
                               EX("Gasto 4", "Outros", 16000, "2026-10-09"), EX("Gasto 5", "Outros", 16000, "2026-10-10"), NULL);
    g_autoptr(GPtrArray) tips = insights_tips(s, TODAY, money_fmt);
    g_autoptr(GPtrArray) l = tips_of(tips, INSIGHT_OVER_INCOME);
    g_assert_cmpuint(l->len, ==, 1);
    g_assert_true(contains(((Insight *)l->pdata[0])->text, "R$ 1.653,33"));
}

static void test_ins_pace_few_and_one_off(void) {
    /* o caso relatado: dia 8, R$ 500 de receita e uma despesa só de R$ 200 → antes projetava R$ 775 */
    g_autoptr(AppState) caso = st(INC("Salário", "Salário", 50000, "2026-10-05"), EX("Mercado", "Alimentação", 20000, "2026-10-06"), NULL);
    g_autoptr(GPtrArray) t1 = insights_tips(caso, D("2026-10-08"), money_fmt);
    g_autoptr(GPtrArray) l1 = tips_of(t1, INSIGHT_OVER_INCOME);
    g_assert_cmpuint(l1->len, ==, 0);
    /* uma despesa grande isolada (R$ 600 de R$ 800) conta uma vez: 600 + 200/15*31 = 1013,33 */
    g_autoptr(AppState) s = st(INC("Salário", "Salário", 100000, "2026-10-05"), EX("Notebook", "Outros", 60000, "2026-10-02"),
                               EX("Café 1", "Alimentação", 5000, "2026-10-03"), EX("Café 2", "Alimentação", 5000, "2026-10-06"),
                               EX("Café 3", "Alimentação", 5000, "2026-10-09"), EX("Café 4", "Alimentação", 5000, "2026-10-10"), NULL);
    g_autoptr(GPtrArray) t2 = insights_tips(s, TODAY, money_fmt);
    g_autoptr(GPtrArray) l2 = tips_of(t2, INSIGHT_OVER_INCOME);
    g_assert_cmpuint(l2->len, ==, 1);
    Insight *i = l2->pdata[0];
    g_assert_true(contains(i->text, "R$ 1.013,33"));
    g_assert_true(contains(i->why, "gasto pontual R$ 600,00 (conta uma vez)"));
    g_autoptr(GPtrArray) exp = g_ptr_array_new();
    for (guint k = 0; k < s->txs->len; k++) { Tx *t = s->txs->pdata[k]; if (t->kind == KIND_EXPENSE) g_ptr_array_add(exp, t); }
    Projection p = insights_project(exp, TODAY, INS_PACE_MIN_COUNT);
    g_assert_cmpint(p.one_off, ==, 60000);
    g_assert_cmpint(p.count, ==, 5);
    g_assert_true(p.enough);
}

static void test_ins_small_spends(void) {
    AppState *s = app_state_new();
    for (int i = 1; i <= 10; i++) {
        g_autofree char *date = g_strdup_printf("2026-10-%02d", i);
        g_ptr_array_add(s->txs, EX(i % 2 == 0 ? "Café" : "Bala", "Alimentação", 800, date));
    }
    g_ptr_array_add(s->txs, EX("Mercado", "Alimentação", 22000, "2026-10-02"));
    g_autoptr(GPtrArray) tips = insights_tips(s, TODAY, money_fmt);
    g_autoptr(GPtrArray) l = tips_of(tips, INSIGHT_SMALL_SPENDS);
    g_assert_cmpuint(l->len, ==, 1);
    const char *t = ((Insight *)l->pdata[0])->text;
    g_assert_true(contains(t, "10 compras de até R$ 20,00 somaram R$ 80,00") && contains(t, "27%"));
    /* com 9 pequenas compras não avisa */
    g_ptr_array_set_size(s->txs, 9);
    g_autoptr(GPtrArray) tips2 = insights_tips(s, TODAY, money_fmt);
    g_autoptr(GPtrArray) l2 = tips_of(tips2, INSIGHT_SMALL_SPENDS);
    g_assert_cmpuint(l2->len, ==, 0);
    app_state_free(s);
}

static void test_ins_hidden_values(void) {
    g_autoptr(AppState) s = st(EX("Mercado", "Alimentação", 30000, "2026-10-05"), NULL);
    MonthReport *r = insights_report(s, TODAY, money_fmt_hidden);
    for (guint i = 0; i < r->lines->len; i++) g_assert_null(strstr(r->lines->pdata[i], "300"));
    month_report_free(r);
}

/* ================================================================== assistente: perguntas */

static AppState *ask_state(void) {
    return st(EX("Mercado Extra", "Alimentação", 25000, "2026-08-05"), EX("Uber centro", "Transporte", 3000, "2026-08-07"),
              EX("Uber casa", "Transporte", 2500, "2026-10-14"), EX("Netflix", "Lazer", 5590, "2026-10-08"),
              EX("Aluguel", "Moradia", 150000, "2026-10-10"), ex_full("Luz", "Moradia", 18000, "2026-10-28", FALSE, NULL, NULL),
              INC("Salário", "Salário", 400000, "2026-10-05"), INC("Freela", "Extra", 50000, "2025-12-10"), NULL);
}

static void test_ask_total_category_month(void) {
    g_autoptr(AppState) s = ask_state();
    AskAnswer *a = ask_answer("Quanto gastei com alimentação em agosto?", s, TODAY, money_fmt);
    g_assert_cmpstr(a->parsed->category, ==, "Alimentação");
    g_assert_cmpint(a->parsed->from, ==, D("2026-08-01"));
    g_assert_true(contains(a->text, "R$ 250,00"));
    g_assert_true(g_str_has_prefix(a->understood, "Como entendi: total de despesas · agosto de 2026 · categoria Alimentação"));
    ask_answer_free(a);
}

static void test_ask_word_filter_relative(void) {
    g_autoptr(AppState) s = ask_state();
    AskAnswer *a = ask_answer("quantas vezes usei uber nos ultimos 90 dias", s, TODAY, money_fmt);
    g_assert_cmpint(a->parsed->intent, ==, INTENT_COUNT);
    g_assert_cmpuint(g_strv_length(a->parsed->words), ==, 1);
    g_assert_cmpstr(a->parsed->words[0], ==, "uber");
    g_assert_true(g_str_has_prefix(a->text, "2 lançamentos") && contains(a->text, "R$ 55,00"));
    ask_answer_free(a);
}

static void test_ask_max_balance_pending(void) {
    g_autoptr(AppState) s = ask_state();
    AskAnswer *a = ask_answer("maior gasto do mês", s, TODAY, money_fmt);
    g_assert_true(contains(a->text, "“Aluguel”: R$ 1.500,00"));
    ask_answer_free(a);
    a = ask_answer("quanto gastei este mês", s, TODAY, money_fmt);
    g_assert_true(contains(a->text, "R$ 1.580,90") && contains(a->text, "R$ 180,00 pendente"));
    ask_answer_free(a);
    a = ask_answer("saldo do mês", s, TODAY, money_fmt);
    g_assert_true(contains(a->text, "receitas R$ 4.000,00") && contains(a->text, "saldo R$ 2.419,10"));
    ask_answer_free(a);
}

static void test_ask_income_month_without_year(void) {
    g_autoptr(AppState) s = ask_state();
    AskAnswer *a = ask_answer("quanto recebi em dezembro", s, TODAY, money_fmt); /* dezembro ainda não chegou → 2025 */
    g_assert_cmpint(day_year(a->parsed->from), ==, 2025);
    g_assert_true(contains(a->text, "R$ 500,00"));
    g_assert_true(a->parsed->has_kind);
    g_assert_cmpint(a->parsed->kind, ==, KIND_INCOME);
    ask_answer_free(a);
}

static void test_ask_unknown_words(void) {
    g_autoptr(AppState) s = ask_state();
    AskAnswer *a = ask_answer("quantas vezes pedi uber nos ultimos 90 dias", s, TODAY, money_fmt);
    g_assert_cmpuint(g_strv_length(a->parsed->words), ==, 1);
    g_assert_cmpstr(a->parsed->words[0], ==, "uber");
    g_assert_cmpuint(g_strv_length(a->parsed->ignored), ==, 1);
    g_assert_cmpstr(a->parsed->ignored[0], ==, "pedi");
    g_assert_true(contains(a->understood, "Ignorei “pedi”"));
    ask_answer_free(a);
}

static void test_ask_daily_average(void) {
    g_autoptr(AppState) s = ask_state();
    AskAnswer *a = ask_answer("média de gastos este mês", s, TODAY, money_fmt);
    g_assert_true(contains(a->text, "R$ 105,39 por dia") && contains(a->text, "15 dia(s)"));
    ask_answer_free(a);
}

/* ================================================================== relatório */

static AppState *report_state(void) {
    int n = 0;
#define RID() g_strdup_printf("r%d", n++)
    AppState *s = app_state_new();
    struct { Kind k; const char *desc, *cat; Cents v; const char *date; gboolean paid; const char *pay; } rows[] = {
        {KIND_INCOME, "Salário", "Salário", 500000, "2026-10-05", TRUE, ""},
        {KIND_INCOME, "Freela", "Extra", 80000, "2026-10-20", FALSE, ""},
        {KIND_EXPENSE, "Aluguel", "Moradia", 150000, "2026-10-10", TRUE, ""},
        {KIND_EXPENSE, "Mercado", "Alimentação", 60000, "2026-10-06", TRUE, ""},
        {KIND_EXPENSE, "iFood", "Alimentação", 15000, "2026-10-12", TRUE, ""},
        {KIND_EXPENSE, "Luz", "Moradia", 20000, "2026-10-28", FALSE, ""},
        {KIND_EXPENSE, "Pagamento fatura", CARD_PAYMENT_CAT, 90000, "2026-10-15", TRUE, "c1"}, /* não conta como despesa */
        {KIND_EXPENSE, "Mercado", "Alimentação", 50000, "2026-09-06", TRUE, ""},
        {KIND_INCOME, "Salário", "Salário", 500000, "2026-09-05", TRUE, ""},
        {KIND_EXPENSE, "Fora", "Lazer", 9900, "2026-11-02", TRUE, ""},
    };
    for (guint i = 0; i < G_N_ELEMENTS(rows); i++) {
        g_autofree char *id = RID();
        Tx *t = tx_new(id, rows[i].k, rows[i].v, D(rows[i].date), rows[i].desc, rows[i].cat, rows[i].paid, "main", "");
        tx_set_str(&t->card_payment, rows[i].pay);
        g_ptr_array_add(s->txs, t);
    }
    g_ptr_array_add(s->limits, limit_new("Alimentação", 70000));
    g_ptr_array_add(s->goals, goal_new("g", "Viagem", 1000000, 250000, DAY_NONE, 0));
#undef RID
    return s;
}

static void test_report_totals(void) {
    g_autoptr(AppState) s = report_state();
    Report *r = report_build(s, D("2026-10-01"), D("2026-10-31"), D("2026-10-31"));
    g_assert_cmpint(r->days, ==, 31);
    g_assert_cmpint(r->income, ==, 500000);
    g_assert_cmpint(r->expense, ==, 225000);
    g_assert_cmpint(report_balance(r), ==, 275000);
    g_assert_cmpint(r->pending_income, ==, 80000);
    g_assert_cmpint(r->pending_expense, ==, 20000);
    double sr;
    g_assert_true(report_savings_rate(r, &sr));
    g_assert_cmpfloat_with_epsilon(sr, 55.0, 0.001);
    g_assert_cmpint(report_daily_average(r), ==, 225000 / 31);
    report_free(r);
}

static void test_report_previous_period(void) {
    g_autoptr(AppState) s = report_state();
    Report *r = report_build(s, D("2026-10-01"), D("2026-10-31"), D("2026-10-31"));
    g_assert_cmpint(r->prev_from, ==, D("2026-08-31"));
    g_assert_cmpint(r->prev_to, ==, D("2026-09-30"));
    g_assert_cmpint(r->prev_expense, ==, 50000);
    double c;
    g_assert_true(report_change(r->expense, r->prev_expense, &c));
    g_assert_cmpfloat_with_epsilon(c, 350.0, 0.001);
    g_assert_false(report_change(100, 0, &c));
    report_free(r);
}

static void test_report_categories(void) {
    g_autoptr(AppState) s = report_state();
    Report *r = report_build(s, D("2026-10-01"), D("2026-10-31"), D("2026-10-31"));
    GPtrArray *c = r->expense_by_category;
    g_assert_cmpuint(c->len, ==, 2);
    CategoryRow *a = c->pdata[0], *food = c->pdata[1];
    g_assert_cmpstr(a->name, ==, "Moradia");
    g_assert_cmpstr(food->name, ==, "Alimentação");
    g_assert_cmpint(a->value, ==, 150000);
    g_assert_cmpfloat_with_epsilon(a->percent, 66.67, 0.01);
    g_assert_cmpint(food->count, ==, 2);
    g_assert_cmpint(food->monthly_average, ==, 75000);
    g_assert_true(food->has_limit);
    g_assert_cmpint(food->monthly_limit, ==, 70000);
    g_assert_true(category_over_limit(food));
    g_assert_cmpuint(r->income_by_category->len, ==, 1);
    g_assert_cmpstr(((CategoryRow *)r->income_by_category->pdata[0])->name, ==, "Salário");
    report_free(r);
}

static void test_report_months_top_list(void) {
    g_autoptr(AppState) s = report_state();
    Report *r = report_build(s, D("2026-10-01"), D("2026-10-31"), D("2026-10-31"));
    g_assert_cmpuint(r->months->len, ==, 1);
    MonthRow *m = &g_array_index(r->months, MonthRow, 0);
    g_assert_cmpint(m->ym, ==, ym_make(2026, 10));
    g_assert_cmpint(m->income, ==, 500000);
    g_assert_cmpint(m->expense, ==, 225000);
    g_assert_cmpuint(r->top_expenses->len, ==, 3);
    g_assert_cmpstr(((Tx *)r->top_expenses->pdata[0])->desc, ==, "Aluguel");
    g_assert_cmpstr(((Tx *)r->top_expenses->pdata[1])->desc, ==, "Mercado");
    g_assert_cmpstr(((Tx *)r->top_expenses->pdata[2])->desc, ==, "iFood");
    g_assert_cmpuint(r->txs->len, ==, 7);
    g_assert_cmpstr(((Tx *)r->txs->pdata[0])->desc, ==, "Salário");
    g_assert_cmpfloat_with_epsilon(goal_row_percent(&g_array_index(r->goals, GoalRow, 0)), 25.0, 0.001);
    report_free(r);
}

static void test_report_multi_month_presets(void) {
    g_autoptr(AppState) s = report_state();
    Report *y = report_build(s, D("2026-09-01"), D("2026-11-30"), D("2026-10-31"));
    g_assert_cmpfloat_with_epsilon(y->month_span, 3.0, 1e-9);
    Report *h = report_build(s, D("2026-11-01"), D("2026-11-15"), D("2026-11-15"));
    g_assert_cmpfloat_with_epsilon(h->month_span, 0.5, 1e-9);
    g_assert_cmpuint(y->months->len, ==, 3);
    g_assert_cmpint(g_array_index(y->months, MonthRow, 0).expense, ==, 50000);
    g_assert_cmpint(g_array_index(y->months, MonthRow, 2).expense, ==, 9900);
    report_free(y);
    report_free(h);
    Day a, b, today = D("2026-10-03");
    report_preset("anterior", today, DAY_NONE, DAY_NONE, &a, &b);
    g_assert_cmpint(a, ==, D("2026-09-01")); g_assert_cmpint(b, ==, D("2026-09-30"));
    report_preset("12m", today, DAY_NONE, DAY_NONE, &a, &b);
    g_assert_cmpint(a, ==, D("2025-11-01")); g_assert_cmpint(b, ==, D("2026-10-31"));
    report_preset("tudo", today, D("2024-02-03"), D("2026-12-01"), &a, &b);
    g_assert_cmpint(a, ==, D("2024-02-03")); g_assert_cmpint(b, ==, D("2026-12-01"));
}

static void test_report_formats(void) {
    char *x;
    x = report_compact(95000); g_assert_cmpstr(x, ==, "R$ 950"); g_free(x);
    x = report_compact(123400); g_assert_cmpstr(x, ==, "R$ 1,2 mil"); g_free(x);
    x = report_compact(1500000); g_assert_cmpstr(x, ==, "R$ 15 mil"); g_free(x);
    x = report_compact(250000000); g_assert_cmpstr(x, ==, "R$ 2,5 mi"); g_free(x);
    g_assert_cmpint(report_nice_step(320000, 4), ==, 100000);
    g_assert_cmpint(report_nice_step(100000, 4), ==, 25000);
    for (Cents v = 0; v < 500; v++) g_assert_cmpint(report_nice_step(v, 4), >=, 100); /* nunca zero */
    x = report_file_name(D("2026-10-01"), D("2026-10-31"));
    g_assert_cmpstr(x, ==, "relatorio-finan-plus-2026-10-01-a-2026-10-31.pdf");
    g_free(x);
}

/* ------------------------------------------------------------------ correções da auditoria (06/10/2026), iguais ao Android 1.1.1 */

static void test_audit_resume_recurring(void) {
    g_autoptr(AppState) s = app_state_new();
    Recurring *r = recurring_new("r", KIND_EXPENSE, "x", 1000, "Lazer", "main", "", 5, FALSE, D("2026-01-05"), day_ym(D("2026-03-01")));
    g_ptr_array_add(s->recurring, r);
    OpErr err = {0};
    g_assert_true(ops_save_recurring(s, "r", KIND_EXPENSE, "x", "10,00", "5", "Lazer", "main", "", TRUE, DAY_NONE, D("2026-10-06"), &err));
    g_assert_cmpuint(s->txs->len, ==, 1); /* só o mês atual, não os 7 meses parados */
    g_assert_cmpint(((Tx *)s->txs->pdata[0])->date, ==, D("2026-10-05"));
    g_assert_cmpint(((Recurring *)s->recurring->pdata[0])->last, ==, day_ym(D("2026-10-01")));
    /* recorrência que já estava ativa continua recuperando os meses atrasados */
    g_autoptr(AppState) s2 = app_state_new();
    g_ptr_array_add(s2->recurring, recurring_new("r", KIND_EXPENSE, "x", 1000, "Lazer", "main", "", 5, TRUE, D("2026-01-05"), day_ym(D("2026-03-01"))));
    g_assert_true(ops_save_recurring(s2, "r", KIND_EXPENSE, "x", "10,00", "5", "Lazer", "main", "", TRUE, DAY_NONE, D("2026-10-06"), &err));
    g_assert_cmpuint(s2->txs->len, ==, 7);
}

static void test_audit_toggle_card_payment(void) {
    g_autoptr(AppState) s = st(mk("p", KIND_EXPENSE, 30000, "2026-10-12", TRUE, NULL, "c", NULL, CARD_PAYMENT_CAT),
                               mk("b", KIND_EXPENSE, 5000, "2026-10-01", TRUE, "c", NULL, NULL, NULL),
                               mk("l", KIND_EXPENSE, 2000, "2026-10-10", FALSE, NULL, NULL, NULL, NULL), NULL);
    g_assert_false(ops_can_toggle_paid(s->txs->pdata[0]));
    g_assert_false(ops_can_toggle_paid(s->txs->pdata[1]));
    g_assert_true(ops_can_toggle_paid(s->txs->pdata[2]));
    ops_toggle_paid(s, "p");
    ops_toggle_paid(s, "l");
    g_assert_true(((Tx *)s->txs->pdata[0])->paid);
    g_assert_true(((Tx *)s->txs->pdata[2])->paid);
}

static void test_audit_backup_value_limits(void) {
    g_autoptr(AppState) s = backup_parse(
        "{\"accounts\":[{\"id\":\"main\",\"name\":\"C\",\"initial\":-1e300}],\"txs\":["
        "{\"id\":\"a\",\"kind\":\"income\",\"value\":1e300,\"date\":\"2026-10-01\",\"desc\":\"x\",\"category\":\"Salário\",\"paid\":true},"
        "{\"id\":\"b\",\"kind\":\"expense\",\"value\":true,\"date\":\"2026-10-01\",\"desc\":\"y\",\"category\":\"Lazer\",\"paid\":true},"
        "{\"id\":\"c\",\"kind\":\"expense\",\"value\":9999999999999.99,\"date\":\"2026-10-01\",\"desc\":\"z\",\"category\":\"Lazer\",\"paid\":true}]}",
        -1, NULL, NULL);
    g_assert_nonnull(s);
    g_assert_cmpint(((Account *)s->accounts->pdata[0])->initial, ==, 0);
    g_assert_cmpuint(s->txs->len, ==, 1); /* valor inválido descarta o lançamento; o limite exato ainda vale */
    g_assert_cmpstr(((Tx *)s->txs->pdata[0])->id, ==, "c");
    g_assert_cmpint(((Tx *)s->txs->pdata[0])->value, ==, 999999999999999LL);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    /* JSON e dinheiro (4) */
    g_test_add_func("/json/round-trip", test_json_round_trip);
    g_test_add_func("/json/rejects-garbage", test_json_rejects_garbage);
    g_test_add_func("/money/parse", test_money_parse);
    g_test_add_func("/money/format", test_money_format);
    /* backup (6) */
    g_test_add_func("/backup/fresh-states", test_backup_fresh_states);
    g_test_add_func("/backup/rejects-without-txs", test_backup_rejects_without_txs);
    g_test_add_func("/backup/drops-invalid", test_backup_drops_invalid);
    g_test_add_func("/backup/neutralizes-html", test_backup_neutralizes_html);
    g_test_add_func("/backup/duplicate-ids", test_backup_duplicate_ids);
    g_test_add_func("/backup/pwa-ids-and-export", test_backup_pwa_ids_and_export);
    /* finanças (11) */
    g_test_add_func("/finance/dates-clamp", test_dates_clamp);
    g_test_add_func("/finance/balance-card-payment", test_balance_card_and_payment);
    g_test_add_func("/finance/invoice-close-due", test_invoice_close_due);
    g_test_add_func("/finance/future-balance", test_future_balance);
    g_test_add_func("/finance/recurring-future-start", test_recurring_future_start);
    g_test_add_func("/finance/recurring-catch-up", test_recurring_catch_up);
    g_test_add_func("/finance/recurring-never-before-start", test_recurring_never_before_start);
    g_test_add_func("/finance/installments-split", test_installments_split);
    g_test_add_func("/finance/goal-plan", test_goal_plan);
    g_test_add_func("/finance/reminders", test_reminders);
    g_test_add_func("/finance/csv", test_csv);
    /* operações (7) */
    g_test_add_func("/ops/validation", test_ops_validation);
    g_test_add_func("/ops/installments", test_ops_installments);
    g_test_add_func("/ops/recurring-from-tx", test_ops_recurring_from_tx);
    g_test_add_func("/ops/card-and-payment", test_ops_card_and_payment);
    g_test_add_func("/ops/goal", test_ops_goal);
    g_test_add_func("/ops/rename-category", test_ops_rename_category);
    g_test_add_func("/ops/account-in-use", test_ops_account_in_use);
    /* assistente (26) */
    g_test_add_func("/assist/text-normalize", test_text_normalize);
    g_test_add_func("/assist/dict-real-file", test_dict_real_file);
    g_test_add_func("/assist/dict-specific-category", test_dict_specific_category);
    g_test_add_func("/assist/dict-longer-term-wins", test_dict_longer_term_wins);
    g_test_add_func("/assist/dict-tie", test_dict_tie);
    g_test_add_func("/assist/cat-same-description", test_cat_same_description);
    g_test_add_func("/assist/cat-learns", test_cat_learns);
    g_test_add_func("/assist/cat-dictionary-fallback", test_cat_dictionary_fallback);
    g_test_add_func("/assist/cat-same-desc-different-cats", test_cat_same_desc_different_cats);
    g_test_add_func("/assist/cat-parcel-suffix", test_cat_parcel_suffix_and_deleted_cat);
    g_test_add_func("/assist/cat-learned-words", test_cat_learned_words);
    g_test_add_func("/assist/insights-report", test_ins_report);
    g_test_add_func("/assist/insights-duplicate", test_ins_duplicate);
    g_test_add_func("/assist/insights-parcels-not-duplicates", test_ins_parcels_not_duplicates);
    g_test_add_func("/assist/insights-subscriptions-price-up", test_ins_subscriptions_and_price_up);
    g_test_add_func("/assist/insights-category-spike", test_ins_category_spike);
    g_test_add_func("/assist/insights-limit-pace", test_ins_limit_pace);
    g_test_add_func("/assist/insights-over-income", test_ins_over_income);
    g_test_add_func("/assist/insights-pace-few-and-one-off", test_ins_pace_few_and_one_off);
    g_test_add_func("/assist/insights-small-spends", test_ins_small_spends);
    g_test_add_func("/assist/insights-hidden-values", test_ins_hidden_values);
    g_test_add_func("/assist/ask-total-category-month", test_ask_total_category_month);
    g_test_add_func("/assist/ask-word-filter-relative", test_ask_word_filter_relative);
    g_test_add_func("/assist/ask-max-balance-pending", test_ask_max_balance_pending);
    g_test_add_func("/assist/ask-income-month-without-year", test_ask_income_month_without_year);
    g_test_add_func("/assist/ask-unknown-words", test_ask_unknown_words);
    g_test_add_func("/assist/ask-daily-average", test_ask_daily_average);
    /* relatório (6) */
    g_test_add_func("/report/totals", test_report_totals);
    g_test_add_func("/report/previous-period", test_report_previous_period);
    g_test_add_func("/report/categories", test_report_categories);
    g_test_add_func("/report/months-top-list", test_report_months_top_list);
    g_test_add_func("/report/multi-month-presets", test_report_multi_month_presets);
    g_test_add_func("/report/formats", test_report_formats);
    /* correções da auditoria (3) */
    g_test_add_func("/audit/resume-recurring", test_audit_resume_recurring);
    g_test_add_func("/audit/toggle-card-payment", test_audit_toggle_card_payment);
    g_test_add_func("/audit/backup-value-limits", test_audit_backup_value_limits);
    return g_test_run();
}
