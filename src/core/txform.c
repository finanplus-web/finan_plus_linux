/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "txform.h"
#include "assist.h"
#include "debts.h"
#include "ops.h"
#include <string.h>

gboolean txform_is_payment_like(const char *c) {
    static const char *const L[] = {"cartao de credito", "cartao", "limite especial", "cheque especial"};
    g_autofree char *f = text_fold(c);
    for (guint i = 0; i < G_N_ELEMENTS(L); i++) if (strcmp(f, L[i]) == 0) return TRUE;
    return FALSE;
}

const char *txform_display_category(const char *c) { return txform_is_payment_like(c) ? "Outros" : c; }

GPtrArray *txform_choices(const AppState *s, Kind k) {
    GPtrArray *out = g_ptr_array_new();
    for (guint i = 0; i < s->cats[k]->len; i++) {
        const char *c = s->cats[k]->pdata[i];
        if (txform_is_payment_like(c)) continue;
        gboolean dup = FALSE;
        for (guint j = 0; j < out->len && !dup; j++) dup = strcmp(out->pdata[j], c) == 0;
        if (!dup) g_ptr_array_add(out, (gpointer)c);
    }
    return out;
}

static Limit *find_limit(AppState *s, const char *c) {
    for (guint i = 0; i < s->limits->len; i++) if (strcmp(((Limit *)s->limits->pdata[i])->category, c) == 0) return s->limits->pdata[i];
    return NULL;
}

gboolean txform_migrate(AppState *s) {
    gboolean changed = FALSE;
    GPtrArray *exp = s->cats[KIND_EXPENSE];
    guint keep = 0;
    for (guint i = 0; i < exp->len; i++) if (!txform_is_payment_like(exp->pdata[i])) keep++;
    if (keep != exp->len && keep > 0) {
        for (guint i = exp->len; i-- > 0;) if (txform_is_payment_like(exp->pdata[i])) g_ptr_array_remove_index(exp, i);
        changed = TRUE;
    }
    /* "Empréstimo" e parecidos → "Dívidas" */
    g_autoptr(GPtrArray) loans = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < exp->len; i++) {
        const char *c = exp->pdata[i];
        if (debts_is_category(c) && strcmp(c, DEBTS_CATEGORY) != 0) g_ptr_array_add(loans, g_strdup(c));
    }
    for (guint i = 0; i < loans->len; i++) {
        const char *old = loans->pdata[i];
        if (!app_has_category(s, KIND_EXPENSE, DEBTS_CATEGORY)) {
            if (!ops_rename_category(s, KIND_EXPENSE, old, DEBTS_CATEGORY, NULL)) continue;
        } else {
            for (guint k = exp->len; k-- > 0;) if (strcmp(exp->pdata[k], old) == 0) g_ptr_array_remove_index(exp, k);
            for (guint k = 0; k < s->txs->len; k++) {
                Tx *t = s->txs->pdata[k];
                if (t->kind == KIND_EXPENSE && strcmp(t->category, old) == 0) tx_set_str(&t->category, DEBTS_CATEGORY);
            }
            for (guint k = 0; k < s->recurring->len; k++) {
                Recurring *r = s->recurring->pdata[k];
                if (r->kind == KIND_EXPENSE && strcmp(r->category, old) == 0) tx_set_str(&r->category, DEBTS_CATEGORY);
            }
            Limit *l = find_limit(s, old);
            if (l) {
                if (!find_limit(s, DEBTS_CATEGORY)) {
                    /* como o LinkedHashMap.put do Kotlin: a chave nova vai para o fim */
                    Cents v = l->value;
                    g_ptr_array_remove(s->limits, l);
                    g_ptr_array_add(s->limits, limit_new(DEBTS_CATEGORY, v));
                } else g_ptr_array_remove(s->limits, l);
            }
        }
        changed = TRUE;
    }
    return changed;
}

char *txform_category_after_kind_change(const char *current, const AppState *s, Kind k) {
    g_autofree char *trim = g_strstrip(g_strdup(current ? current : ""));
    if (!*trim) return g_strdup("");
    g_autofree char *f = text_fold(current);
    for (guint i = 0; i < s->cats[k]->len; i++) {
        g_autofree char *g = text_fold(s->cats[k]->pdata[i]);
        if (strcmp(g, f) == 0) return g_strdup(s->cats[k]->pdata[i]);
    }
    return g_strdup("");
}

typedef struct { const Tx *t; guint idx; } Ix;
static gint cmp_recent(gconstpointer a, gconstpointer b) {
    const Ix *x = a, *y = b;
    if (x->t->date != y->t->date) return x->t->date > y->t->date ? -1 : 1;
    return x->idx > y->idx ? -1 : x->idx < y->idx;
}

char **txform_recent_categories(const AppState *s, Kind k, int n) {
    g_autoptr(GArray) l = g_array_new(FALSE, FALSE, sizeof(Ix));
    for (guint i = 0; i < s->txs->len; i++) {
        const Tx *t = s->txs->pdata[i];
        if (t->kind == k && tx_is_flow(t) && *t->category) { Ix x = {t, i}; g_array_append_val(l, x); }
    }
    g_array_sort(l, cmp_recent);
    GPtrArray *out = g_ptr_array_new();
    for (guint i = 0; i < l->len && (int)out->len < n; i++) {
        g_autofree char *f = text_fold(g_array_index(l, Ix, i).t->category);
        const char *hit = NULL;
        for (guint c = 0; c < s->cats[k]->len && !hit; c++) {
            g_autofree char *g = text_fold(s->cats[k]->pdata[c]);
            if (strcmp(f, g) == 0) hit = s->cats[k]->pdata[c];
        }
        if (!hit) continue;
        gboolean dup = FALSE;
        for (guint j = 0; j < out->len && !dup; j++) dup = strcmp(out->pdata[j], hit) == 0;
        if (!dup) g_ptr_array_add(out, g_strdup(hit));
    }
    g_ptr_array_add(out, NULL);
    return (char **)g_ptr_array_free(out, FALSE);
}

typedef struct { char *text; int count; Day date; guint idx; } Sug;
static gint cmp_sug(gconstpointer a, gconstpointer b) {
    const Sug *x = a, *y = b;
    if (x->count != y->count) return x->count > y->count ? -1 : 1;
    if (x->date != y->date) return x->date > y->date ? -1 : 1;
    return x->idx > y->idx ? -1 : x->idx < y->idx;
}

char **txform_description_suggestions(const AppState *s, Kind k, int n) {
    g_autoptr(GHashTable) pos = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_autoptr(GArray) sug = g_array_new(FALSE, FALSE, sizeof(Sug));
    for (guint i = 0; i < s->txs->len; i++) {
        const Tx *t = s->txs->pdata[i];
        if (t->kind != k || !tx_is_flow(t)) continue;
        g_autofree char *d = g_strstrip(g_strdup(t->desc));
        g_autofree char *c = g_strstrip(g_strdup(t->category));
        if (!*d || strcmp(d, c) == 0) continue;
        char *key = text_fold(d);
        gpointer p;
        if (g_hash_table_lookup_extended(pos, key, NULL, &p)) {
            g_free(key);
            Sug *x = &g_array_index(sug, Sug, GPOINTER_TO_UINT(p));
            x->count++;
            /* a mais recente (data; empate: o gravado depois) dá o texto */
            if (t->date >= x->date) { g_free(x->text); x->text = g_strdup(d); x->date = t->date; x->idx = i; }
        } else {
            Sug x = {g_strdup(d), 1, t->date, i};
            g_hash_table_insert(pos, key, GUINT_TO_POINTER(sug->len));
            g_array_append_val(sug, x);
        }
    }
    g_array_sort(sug, cmp_sug);
    GPtrArray *out = g_ptr_array_new();
    for (guint i = 0; i < sug->len; i++) {
        Sug *x = &g_array_index(sug, Sug, i);
        if ((int)out->len < n) g_ptr_array_add(out, x->text); else g_free(x->text);
    }
    g_ptr_array_add(out, NULL);
    return (char **)g_ptr_array_free(out, FALSE);
}

char *amount_input_format(const char *raw) {
    GString *s = g_string_new(NULL);
    for (const char *p = raw ? raw : ""; *p; p++) if (g_ascii_isdigit(*p) || *p == ',' || *p == '.') g_string_append_c(s, *p);
    /* um ponto digitado no fim ("12.") vale como vírgula; os pontos do meio são o agrupamento de milhar */
    if (s->len && s->str[s->len - 1] == '.' && !strchr(s->str, ',')) s->str[s->len - 1] = ',';
    GString *t = g_string_new(NULL);
    for (gsize i = 0; i < s->len; i++) if (s->str[i] != '.') g_string_append_c(t, s->str[i]);
    g_string_free(s, TRUE);
    char *comma = strchr(t->str, ',');
    GString *ints = g_string_new(NULL), *dec = NULL;
    for (char *p = t->str; *p && p != comma; p++) if (g_ascii_isdigit(*p) && ints->len < 13) g_string_append_c(ints, *p);
    if (comma) {
        dec = g_string_new(NULL);
        for (char *p = comma + 1; *p; p++) if (g_ascii_isdigit(*p) && dec->len < 2) g_string_append_c(dec, *p);
    }
    g_string_free(t, TRUE);
    if (!ints->len && !dec) { g_string_free(ints, TRUE); return g_strdup(""); }
    const char *clean = ints->str;
    while (*clean == '0') clean++;
    if (!*clean) clean = "0";
    GString *out = g_string_new(NULL);
    size_t len = strlen(clean);
    for (size_t i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0) g_string_append_c(out, '.');
        g_string_append_c(out, clean[i]);
    }
    if (dec) { g_string_append_c(out, ','); g_string_append(out, dec->str); g_string_free(dec, TRUE); }
    g_string_free(ints, TRUE);
    return g_string_free(out, FALSE);
}
