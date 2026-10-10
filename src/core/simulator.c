/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Simulador "E se…?" (ver simulator.h e SIMULADOR.md).
 */
#include "simulator.h"
#include "finance.h"
#include <math.h>
#include <string.h>

SimBase sim_base(const AppState *s, Day today) {
    Cents inc = 0, exp = 0;
    int n = 0;
    Ym cur = day_ym(today);
    for (int k = 1; k <= SIM_BASE_MONTHS; k++) {
        Flow f = month_flow(s, cur - k);
        if (f.income <= 0 && f.expense <= 0) continue;
        inc += f.income;
        exp += f.expense;
        n++;
    }
    if (!n) return (SimBase){0, 0, 0};
    return (SimBase){(Cents)llround((double)inc / n), (Cents)llround((double)exp / n), n};
}

SimSave sim_save(SimBase base, Cents per_month, int months) {
    Cents left = sim_base_left(base);
    return (SimSave){per_month, months, per_month * months, left - per_month, per_month > left};
}

gboolean sim_buy(Cents price, Cents have, Cents per_month, Day today, SimBuy *out) {
    Cents missing = MAX(0, price - have);
    if (missing == 0) { *out = (SimBuy){0, 0, day_ym(today)}; return TRUE; }
    if (per_month <= 0) return FALSE;
    int months = (int)((missing + per_month - 1) / per_month);
    *out = (SimBuy){missing, months, day_ym(today) + months};
    return TRUE;
}

SimIncome sim_income(SimBase base, double percent, Cents goals_monthly) {
    /* Math.round: floor(x + 0.5) */
    Cents ni = (Cents)floor((double)base.income * (1 + percent / 100.0) + 0.5);
    Cents diff = ni - base.income;
    return (SimIncome){ni, diff, sim_base_left(base) + diff, diff * 12, goals_monthly};
}

Cents sim_goals_monthly(const AppState *s) {
    Cents n = 0;
    for (guint i = 0; i < s->goals->len; i++) {
        const Goal *g = s->goals->pdata[i];
        if (g->saved < g->target) n += g->monthly;
    }
    return n;
}

void sim_debt_free(SimDebt *d) {
    if (!d) return;
    g_free(d->group_id);
    g_free(d->name);
    g_free(d);
}

static int cmp_debt(gconstpointer a, gconstpointer b) {
    const SimDebt *x = *(const SimDebt *const *)a, *y = *(const SimDebt *const *)b;
    return x->left < y->left ? 1 : x->left > y->left ? -1 : 0;
}

/* "Mercado Pago 1/6" → "Mercado Pago"; "Notebook (2/3)" → "Notebook" */
static char *debt_name(const char *desc) {
    g_autoptr(GRegex) re = g_regex_new("\\s*\\(?\\d+/\\d+\\)?\\s*$", 0, 0, NULL);
    char *out = g_regex_replace_literal(re, desc, -1, 0, "", 0, NULL);
    g_strstrip(out);
    if (!*out) { g_free(out); return g_strdup(desc); }
    return out;
}

GPtrArray *sim_debts(const AppState *s, Day today) {
    GPtrArray *out = g_ptr_array_new_with_free_func((GDestroyNotify)sim_debt_free);
    /* grupos na ordem em que aparecem */
    g_autoptr(GHashTable) seen = g_hash_table_new(g_str_hash, g_str_equal);
    for (guint i = 0; i < s->txs->len; i++) {
        const Tx *t = s->txs->pdata[i];
        if (!t->group_id[0] || t->parcel_total <= 1 || t->kind != KIND_EXPENSE) continue;
        if (g_hash_table_contains(seen, t->group_id)) continue;
        g_hash_table_add(seen, t->group_id);
        const Tx *first = NULL;
        Cents left = 0, parcel = 0;
        int rest = 0;
        for (guint j = 0; j < s->txs->len; j++) {
            const Tx *u = s->txs->pdata[j];
            if (strcmp(u->group_id, t->group_id) != 0 || u->parcel_total <= 1 || u->kind != KIND_EXPENSE) continue;
            if (!first || u->parcel_n < first->parcel_n) first = u;
            gboolean due = tx_is_card(u) ? u->date > today : !u->paid;
            if (!due) continue;
            rest++;
            left += u->value;
            parcel = MAX(parcel, u->value);
        }
        if (!rest) continue;
        SimDebt *d = g_new0(SimDebt, 1);
        d->group_id = g_strdup(t->group_id);
        d->name = debt_name(first->desc);
        d->parcel = parcel;
        d->remaining = rest;
        d->total = first->parcel_total;
        d->left = left;
        d->card = tx_is_card(first);
        g_ptr_array_add(out, d);
    }
    g_ptr_array_sort(out, cmp_debt);
    return out;
}

SimPayoff sim_payoff(const SimDebt *d, Cents pay_now, Cents balance) {
    return (SimPayoff){pay_now, MAX(0, d->left - pay_now), d->parcel, d->remaining, balance - pay_now};
}
