/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "report.h"
#include "finance.h"
#include <math.h>
#include <string.h>

gboolean report_savings_rate(const Report *r, double *out) {
    if (r->income <= 0) return FALSE;
    *out = report_balance(r) * 100.0 / r->income;
    return TRUE;
}

Cents report_daily_average(const Report *r) { return r->days > 0 ? r->expense / r->days : 0; }

gboolean report_change(Cents cur, Cents prev, double *out) {
    if (prev <= 0) return FALSE;
    *out = (cur - prev) * 100.0 / prev;
    return TRUE;
}

double goal_row_percent(const GoalRow *g) {
    if (g->target <= 0) return 0;
    double p = g->saved * 100.0 / g->target;
    return p > 100 ? 100 : p;
}

static void category_row_free(CategoryRow *r) { g_free(r->name); g_free(r); }

static gint cmp_cat_row(gconstpointer a, gconstpointer b) {
    const CategoryRow *x = *(CategoryRow *const *)a, *y = *(CategoryRow *const *)b;
    if (x->value != y->value) return (y->value > x->value) - (y->value < x->value);
    return strcmp(x->name, y->name);
}

static GPtrArray *by_category(GPtrArray *l, double span, const AppState *limits_from) {
    GPtrArray *out = g_ptr_array_new_with_free_func((GDestroyNotify)category_row_free);
    GHashTable *idx = g_hash_table_new(g_str_hash, g_str_equal);
    Cents total = 0;
    for (guint i = 0; i < l->len; i++) {
        Tx *t = l->pdata[i];
        total += t->value;
        CategoryRow *r = g_hash_table_lookup(idx, t->category);
        if (!r) {
            r = g_new0(CategoryRow, 1);
            r->name = g_strdup(t->category);
            g_ptr_array_add(out, r);
            g_hash_table_insert(idx, r->name, r);
        }
        r->value += t->value;
        r->count++;
    }
    g_hash_table_unref(idx);
    for (guint i = 0; i < out->len; i++) {
        CategoryRow *r = out->pdata[i];
        r->percent = total > 0 ? r->value * 100.0 / total : 0;
        r->monthly_average = (Cents)floor(r->value / MAX(span, 1.0) + 0.5);
        if (limits_from) r->has_limit = app_limit(limits_from, r->name, &r->monthly_limit);
    }
    g_ptr_array_sort(out, cmp_cat_row);
    return out;
}

static gint cmp_top(gconstpointer a, gconstpointer b) {
    const Tx *x = *(Tx *const *)a, *y = *(Tx *const *)b;
    if (x->value != y->value) return (y->value > x->value) - (y->value < x->value);
    return (x->date > y->date) - (x->date < y->date);
}

static gint cmp_list(gconstpointer a, gconstpointer b) {
    const Tx *x = *(Tx *const *)a, *y = *(Tx *const *)b;
    if (x->date != y->date) return (x->date > y->date) - (x->date < y->date);
    int kx = x->kind != KIND_INCOME, ky = y->kind != KIND_INCOME;
    if (kx != ky) return kx - ky;
    return strcmp(x->desc, y->desc);
}

Report *report_build(const AppState *s, Day from, Day to, Day today) {
    (void)today;
    if (to < from) return NULL;
    Report *r = g_new0(Report, 1);
    r->from = from;
    r->to = to;
    r->days = to - from + 1;
    r->prev_to = from - 1;
    r->prev_from = r->prev_to - (r->days - 1);

    g_autoptr(GPtrArray) inc = g_ptr_array_new(), exp = g_ptr_array_new();
    r->txs = g_ptr_array_new();
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (t->date >= from && t->date <= to) {
            g_ptr_array_add(r->txs, t);
            if (tx_is_flow(t)) {
                if (t->paid) {
                    if (t->kind == KIND_INCOME) { r->income += t->value; g_ptr_array_add(inc, t); }
                    else { r->expense += t->value; g_ptr_array_add(exp, t); }
                } else {
                    if (t->kind == KIND_INCOME) r->pending_income += t->value; else r->pending_expense += t->value;
                }
            }
        }
        if (tx_is_flow(t) && t->paid && t->date >= r->prev_from && t->date <= r->prev_to) {
            if (t->kind == KIND_INCOME) r->prev_income += t->value; else r->prev_expense += t->value;
        }
    }

    double span = 0;
    for (Ym m = day_ym(from); m <= day_ym(to); m++) {
        Day a = MAX(from, ym_first(m)), b = MIN(to, ym_last(m));
        span += (double)(b - a + 1) / ym_len(m);
    }
    r->month_span = span;
    r->expense_by_category = by_category(exp, span, s);
    r->income_by_category = by_category(inc, span, NULL);

    /* meses que tocam o período (valores só dos dias dentro do período) */
    r->months = g_array_new(FALSE, FALSE, sizeof(MonthRow));
    Ym m0 = day_ym(from);
    for (Ym m = m0; m <= day_ym(to); m++) {
        MonthRow mr = {m, 0, 0};
        g_array_append_val(r->months, mr);
    }
    for (guint i = 0; i < inc->len; i++) g_array_index(r->months, MonthRow, day_ym(((Tx *)inc->pdata[i])->date) - m0).income += ((Tx *)inc->pdata[i])->value;
    for (guint i = 0; i < exp->len; i++) g_array_index(r->months, MonthRow, day_ym(((Tx *)exp->pdata[i])->date) - m0).expense += ((Tx *)exp->pdata[i])->value;

    r->top_expenses = g_ptr_array_new();
    for (guint i = 0; i < exp->len; i++) g_ptr_array_add(r->top_expenses, exp->pdata[i]);
    g_ptr_array_sort(r->top_expenses, cmp_top);
    if (r->top_expenses->len > REPORT_TOP) g_ptr_array_set_size(r->top_expenses, REPORT_TOP);
    g_ptr_array_sort(r->txs, cmp_list);

    r->accounts = g_array_new(FALSE, FALSE, sizeof(AccountRow));
    for (guint i = 0; i < s->accounts->len; i++) {
        Account *a = s->accounts->pdata[i];
        AccountRow ar = {g_strdup(a->name), account_balance(s, a)};
        g_array_append_val(r->accounts, ar);
    }
    r->goals = g_array_new(FALSE, FALSE, sizeof(GoalRow));
    for (guint i = 0; i < s->goals->len; i++) {
        Goal *g = s->goals->pdata[i];
        GoalRow gr = {g_strdup(g->name), g->saved, g->target, g->deadline};
        g_array_append_val(r->goals, gr);
    }
    return r;
}

void report_free(Report *r) {
    if (!r) return;
    g_ptr_array_unref(r->expense_by_category);
    g_ptr_array_unref(r->income_by_category);
    g_array_unref(r->months);
    g_ptr_array_unref(r->top_expenses);
    g_ptr_array_unref(r->txs);
    for (guint i = 0; i < r->accounts->len; i++) g_free(g_array_index(r->accounts, AccountRow, i).name);
    g_array_unref(r->accounts);
    for (guint i = 0; i < r->goals->len; i++) g_free(g_array_index(r->goals, GoalRow, i).name);
    g_array_unref(r->goals);
    g_free(r);
}

static char *one(double v) {
    double x = floor(v * 10 + 0.5) / 10.0;
    if (x == floor(x)) return g_strdup_printf("%" G_GINT64_FORMAT, (gint64)x);
    char *s = g_strdup_printf("%.1f", x);
    for (char *p = s; *p; p++) if (*p == '.') *p = ',';
    return s;
}

char *report_compact(Cents c) {
    gboolean neg = c < 0;
    double r = (c < 0 ? -(double)c : (double)c) / 100.0;
    char *s;
    if (r < 1000) s = g_strdup_printf("R$ %" G_GINT64_FORMAT, (gint64)floor(r + 0.5));
    else if (r < 1000000) { g_autofree char *o = one(r / 1000); s = g_strdup_printf("R$ %s mil", o); }
    else { g_autofree char *o = one(r / 1000000); s = g_strdup_printf("R$ %s mi", o); }
    if (!neg) return s;
    char *n = g_strdup_printf("-%s", s);
    g_free(s);
    return n;
}

Cents report_nice_step(Cents max, int ticks) {
    if (max <= 0) return 10000;
    double raw = (double)max / ticks;
    double mag = pow(10.0, floor(log10(raw)));
    double n = raw / mag;
    double f = n <= 1 ? 1.0 : n <= 2 ? 2.0 : n <= 2.5 ? 2.5 : n <= 5 ? 5.0 : 10.0;
    /* nunca zero: com valores de 1 ou 2 centavos o passo arredondava para 0 e o gráfico dividia por
     * zero (o app fechava ao abrir Relatórios sem lançamentos no período) */
    return MAX((Cents)floor(f * mag + 0.5), 100);
}

char *report_file_name(Day from, Day to) {
    char a[11], b[11];
    return g_strdup_printf("relatorio-finan-plus-%s-a-%s.pdf", day_iso(from, a), day_iso(to, b));
}

void report_preset(const char *key, Day today, Day first, Day last, Day *from, Day *to) {
    Ym ym = day_ym(today);
    if (strcmp(key, "mes") == 0) { *from = ym_first(ym); *to = ym_last(ym); }
    else if (strcmp(key, "anterior") == 0) { *from = ym_first(ym - 1); *to = ym_last(ym - 1); }
    else if (strcmp(key, "ano") == 0) { *from = day_from_ymd(day_year(today), 1, 1); *to = day_from_ymd(day_year(today), 12, 31); }
    else if (strcmp(key, "12m") == 0) { *from = ym_first(ym - 11); *to = ym_last(ym); }
    else {
        *from = MIN(first == DAY_NONE ? ym_first(ym) : first, ym_first(ym));
        *to = MAX(last == DAY_NONE ? today : last, today);
    }
}
