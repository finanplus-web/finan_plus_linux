/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "debts.h"
#include "assist.h"
#include <string.h>

gboolean debts_is_category(const char *c) {
    static const char *const NAMES[] = {"dividas", "divida", "emprestimo", "emprestimos", "financiamento", "financiamentos"};
    g_autofree char *f = text_fold(c);
    for (guint i = 0; i < G_N_ELEMENTS(NAMES); i++) if (strcmp(f, NAMES[i]) == 0) return TRUE;
    return FALSE;
}

char *debts_base_name(const char *desc) {
    /* tira o sufixo "\s*(n/m)\s*" do fim */
    g_autofree char *t = g_strdup(desc ? desc : "");
    g_strchomp(t);
    size_t n = strlen(t);
    if (n >= 5 && t[n - 1] == ')') {
        ssize_t i = (ssize_t)n - 2, digits2 = 0, digits1 = 0;
        while (i >= 0 && g_ascii_isdigit(t[i])) { i--; digits2++; }
        if (digits2 && i >= 0 && t[i] == '/') {
            i--;
            while (i >= 0 && g_ascii_isdigit(t[i])) { i--; digits1++; }
            if (digits1 && i >= 0 && t[i] == '(') t[i] = '\0';
        }
    }
    return g_strstrip(g_strdup(t));
}

void debt_free(Debt *d) {
    if (!d) return;
    g_free(d->name);
    g_ptr_array_unref(d->parcels);
    g_free(d);
}

static gint cmp_tx_date(gconstpointer a, gconstpointer b) {
    const Tx *x = *(Tx *const *)a, *y = *(Tx *const *)b;
    return x->date < y->date ? -1 : x->date > y->date;
}

static gint cmp_debt(gconstpointer a, gconstpointer b) {
    const Debt *x = *(Debt *const *)a, *y = *(Debt *const *)b;
    if (debt_done(x) != debt_done(y)) return debt_done(x) ? 1 : -1;
    Day dx = x->next ? x->next->date : G_MAXINT32, dy = y->next ? y->next->date : G_MAXINT32;
    if (dx != dy) return dx < dy ? -1 : 1;
    return strcmp(x->name, y->name);
}

GPtrArray *debts_list(const AppState *s) {
    /* chave do grupo → GPtrArray de Tx*, na ordem em que os grupos aparecem */
    g_autoptr(GHashTable) groups = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_autoptr(GPtrArray) order = g_ptr_array_new_with_free_func((GDestroyNotify)g_ptr_array_unref);
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (t->kind != KIND_EXPENSE || !tx_is_flow(t) || tx_is_card(t) || !debts_is_category(t->category)) continue;
        char *key;
        if (t->group_id[0]) key = g_strconcat("g:", t->group_id, NULL);
        else { g_autofree char *b = debts_base_name(t->desc); g_autofree char *f = text_fold(b); key = g_strconcat("d:", f, NULL); }
        GPtrArray *g = g_hash_table_lookup(groups, key);
        if (!g) { g = g_ptr_array_new(); g_ptr_array_add(order, g); g_hash_table_insert(groups, key, g); }
        else g_free(key);
        g_ptr_array_add(g, t);
    }
    GPtrArray *out = g_ptr_array_new_with_free_func((GDestroyNotify)debt_free);
    for (guint i = 0; i < order->len; i++) {
        GPtrArray *l = order->pdata[i];
        Debt *d = g_new0(Debt, 1);
        d->parcels = g_ptr_array_copy(l, NULL, NULL);
        /* ordem estável por data, como o sortedBy do Kotlin */
        g_ptr_array_sort(d->parcels, cmp_tx_date);
        const Tx *last = d->parcels->pdata[d->parcels->len - 1];
        g_autofree char *base = debts_base_name(last->desc);
        d->name = g_strdup(*base ? base : DEBTS_CATEGORY);
        int max_total = 0;
        for (guint k = 0; k < d->parcels->len; k++) {
            const Tx *t = d->parcels->pdata[k];
            if (!t->paid) { if (!d->next) d->next = t; d->pending_count++; d->pending_total += t->value; }
            else d->paid_count++;
            max_total = MAX(max_total, t->parcel_total);
        }
        d->total_count = MAX((int)d->parcels->len, max_total);
        g_ptr_array_add(out, d);
    }
    g_ptr_array_sort(out, cmp_debt);
    return out;
}

char *debts_summary(const AppState *s, gboolean hide) {
    g_autoptr(GPtrArray) all = debts_list(s);
    int open = 0;
    const Tx *next = NULL;
    for (guint i = 0; i < all->len; i++) {
        Debt *d = all->pdata[i];
        if (debt_done(d)) continue;
        open++;
        if (d->next && (!next || d->next->date < next->date)) next = d->next;
    }
    if (!open) return NULL;
    g_autofree char *n = br_plural(open, "dívida", "dívidas");
    if (!next) return g_steal_pointer(&n);
    char v[32], d[8];
    if (hide) g_strlcpy(v, "R$ ••••", sizeof v); else money_format(next->value, v);
    day_br_short(next->date, d);
    return g_strdup_printf(open == 1 ? "%s · parcela de %s em %s" : "%s · próxima parcela de %s em %s", n, v, d);
}
