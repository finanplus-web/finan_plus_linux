/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "transfers.h"
#include <string.h>

gboolean transfer_legs(const AppState *s, const char *transfer_id, TransferLegs *out) {
    if (!transfer_id || !*transfer_id) return FALSE;
    Tx *o = NULL, *i = NULL;
    int n = 0;
    for (guint k = 0; k < s->txs->len; k++) {
        Tx *t = s->txs->pdata[k];
        if (strcmp(t->transfer_id, transfer_id) != 0) continue;
        n++;
        if (t->kind == KIND_EXPENSE && !o) o = t;
        if (t->kind == KIND_INCOME && !i) i = t;
    }
    if (n != 2 || !o || !i) return FALSE;
    if (out) { out->out = o; out->into = i; }
    return TRUE;
}

/* os dois lados combinam: um sai e outro entra, mesmo valor e data, contas diferentes, fora do cartão */
static gboolean valid(const Tx *a, const Tx *b) {
    return a->kind != b->kind && a->value == b->value && a->date == b->date && strcmp(a->account_id, b->account_id) != 0 &&
           a->paid && b->paid && !tx_is_card(a) && !tx_is_card(b) && !tx_is_card_payment(a) && !tx_is_card_payment(b) &&
           !tx_is_goal(a) && !tx_is_goal(b);
}

void transfers_sanitize(GPtrArray *txs) {
    g_autoptr(GHashTable) groups = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify)g_ptr_array_unref);
    for (guint i = 0; i < txs->len; i++) {
        Tx *t = txs->pdata[i];
        if (!tx_is_transfer(t)) continue;
        GPtrArray *g = g_hash_table_lookup(groups, t->transfer_id);
        if (!g) { g = g_ptr_array_new(); g_hash_table_insert(groups, t->transfer_id, g); }
        g_ptr_array_add(g, t);
    }
    if (!g_hash_table_size(groups)) return;
    GPtrArray *bad = g_ptr_array_new();
    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, groups);
    while (g_hash_table_iter_next(&it, &k, &v)) {
        GPtrArray *g = v;
        if (g->len == 2 && valid(g->pdata[0], g->pdata[1])) continue;
        for (guint i = 0; i < g->len; i++) g_ptr_array_add(bad, g->pdata[i]);
    }
    /* depois de varrer: as chaves da tabela apontam para os próprios transfer_id */
    for (guint i = 0; i < bad->len; i++) tx_set_str(&((Tx *)bad->pdata[i])->transfer_id, "");
    g_ptr_array_free(bad, TRUE);
}

char *transfer_route(const AppState *s, const TransferLegs *l) {
    Account *a = app_account(s, l->out->account_id), *b = app_account(s, l->into->account_id);
    return g_strdup_printf("%s → %s", a ? a->name : "Conta", b ? b->name : "Conta");
}

char *transfer_desc(const char *text) {
    char *d = str_clean(text, 200);
    if (*d) return d;
    g_free(d);
    return g_strdup(TRANSFER_CAT);
}
