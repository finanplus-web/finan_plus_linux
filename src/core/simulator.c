/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "simulator.h"
#include "finance.h"
#include <math.h>
#include <string.h>

static Cents round_div(Cents a, int n) { return (Cents)floor((double)a / n + 0.5); }

SimBase sim_base(const AppState *s, Day today) {
    SimBase b = {0, 0, 0};
    Cents inc = 0, exp = 0;
    for (int i = 1; i <= SIM_BASE_MONTHS; i++) {
        Flow f = month_flow(s, day_ym(today) - i);
        if (f.income == 0 && f.expense == 0) continue;
        inc += f.income;
        exp += f.expense;
        b.months++;
    }
    if (b.months) { b.income = round_div(inc, b.months); b.expense = round_div(exp, b.months); }
    return b;
}

SimSave sim_save(SimBase b, Cents per_month, int months) {
    SimSave r = {per_month, months, per_month * months, sim_left(b) - per_month, per_month > sim_left(b)};
    return r;
}

gboolean sim_buy(Cents price, Cents have, Cents per_month, Day today, SimBuy *out) {
    Cents missing = MAX(0, price - have);
    if (missing == 0) { out->missing = 0; out->months = 0; out->done_ym = day_ym(today); return TRUE; }
    if (per_month <= 0) return FALSE;
    out->missing = missing;
    out->months = (int)((missing + per_month - 1) / per_month);
    out->done_ym = day_ym(today) + out->months;
    return TRUE;
}

SimIncome sim_income(SimBase b, double percent, Cents goals_monthly) {
    SimIncome r;
    r.new_income = (Cents)floor((double)b.income * (1 + percent / 100.0) + 0.5);
    r.diff = r.new_income - b.income;
    r.new_left = sim_left(b) + r.diff;
    r.year_diff = r.diff * 12;
    r.goals_monthly = goals_monthly;
    return r;
}

Cents sim_goals_monthly(const AppState *s) {
    Cents n = 0;
    for (guint i = 0; i < s->goals->len; i++) {
        Goal *g = s->goals->pdata[i];
        if (g->saved < g->target) n += g->monthly;
    }
    return n;
}

static void debt_free(gpointer p) {
    SimDebt *d = p;
    g_free(d->group_id);
    g_free(d->name);
    g_free(d);
}

static gint cmp_left(gconstpointer a, gconstpointer b) {
    const SimDebt *x = *(SimDebt *const *)a, *y = *(SimDebt *const *)b;
    return (y->left > x->left) - (y->left < x->left);
}

GPtrArray *sim_debts(const AppState *s, Day today) {
    GPtrArray *out = g_ptr_array_new_with_free_func(debt_free);
    /* grupos na ordem em que aparecem */
    g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
    g_autoptr(GRegex) suffix = g_regex_new("\\s*\\(?\\d+/\\d+\\)?\\s*$", 0, 0, NULL);
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t0 = s->txs->pdata[i];
        if (!t0->group_id[0] || t0->parcel_total <= 1 || t0->kind != KIND_EXPENSE) continue;
        if (g_hash_table_contains(seen, t0->group_id)) continue;
        g_hash_table_add(seen, t0->group_id);
        Tx *first = NULL;
        int rest = 0;
        Cents left = 0, parcel = 0;
        for (guint j = i; j < s->txs->len; j++) {
            Tx *t = s->txs->pdata[j];
            if (strcmp(t->group_id, t0->group_id) || t->parcel_total <= 1 || t->kind != KIND_EXPENSE) continue;
            if (!first || t->parcel_n < first->parcel_n) first = t;
            gboolean open = tx_is_card(t) ? t->date > today : !t->paid;
            if (!open) continue;
            rest++;
            left += t->value;
            parcel = MAX(parcel, t->value);
        }
        if (!rest) continue;
        SimDebt *d = g_new0(SimDebt, 1);
        d->group_id = g_strdup(t0->group_id);
        g_autofree char *nm = g_regex_replace_literal(suffix, first->desc, -1, 0, "", 0, NULL);
        g_strstrip(nm);
        d->name = g_strdup(nm[0] ? nm : first->desc);
        d->parcel = parcel;
        d->remaining = rest;
        d->total = first->parcel_total;
        d->left = left;
        d->card = tx_is_card(first);
        g_ptr_array_add(out, d);
    }
    g_ptr_array_sort(out, cmp_left);
    return out;
}

SimPayoff sim_payoff(const SimDebt *d, Cents pay_now, Cents balance) {
    SimPayoff p = {pay_now, MAX(0, d->left - pay_now), d->parcel, d->remaining, balance - pay_now};
    return p;
}
