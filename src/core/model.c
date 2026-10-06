/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "model.h"
#include <string.h>

const int AUTOLOCK_OPTIONS[5] = {0, 1, 5, 15, 30};
const char *const DEFAULT_EXPENSE[7] = {"Alimentação", "Transporte", "Moradia", "Saúde", "Lazer", "Educação", "Outros"};
const char *const DEFAULT_INCOME[4] = {"Salário", "Extra", "Investimentos", "Outros"};

const char *kind_json(Kind k) { return k == KIND_INCOME ? "income" : "expense"; }

gboolean kind_parse(const char *s, Kind *out) {
    if (!s) return FALSE;
    if (strcmp(s, "income") == 0) { *out = KIND_INCOME; return TRUE; }
    if (strcmp(s, "expense") == 0) { *out = KIND_EXPENSE; return TRUE; }
    return FALSE;
}

static const char *const THEME_JSON[THEME_COUNT] = {"auto", "light", "materialBlue", "oledGray", "tokyo", "nord"};
static const char *const THEME_LABEL[THEME_COUNT] = {"Sistema", "Claro", "Material You", "OLED Cinza", "Tokyo Night", "Nord"};

const char *theme_json(ThemeId t) { return THEME_JSON[t]; }
const char *theme_label(ThemeId t) { return THEME_LABEL[t]; }

ThemeId theme_parse(const char *s) {
    if (!s) return THEME_AUTO;
    if (strcmp(s, "dark") == 0) return THEME_OLED; /* nome antigo do PWA */
    for (int i = 0; i < THEME_COUNT; i++) if (strcmp(s, THEME_JSON[i]) == 0) return (ThemeId)i;
    return THEME_AUTO;
}

static char *dup0(const char *s) { return g_strdup(s ? s : ""); }

void tx_set_str(char **field, const char *v) {
    char *n = dup0(v);
    g_free(*field);
    *field = n;
}

Tx *tx_new(const char *id, Kind kind, Cents value, Day date, const char *desc, const char *category,
           gboolean paid, const char *account_id, const char *card_id) {
    Tx *t = g_new0(Tx, 1);
    t->id = dup0(id);
    t->kind = kind;
    t->value = value;
    t->date = date;
    t->desc = dup0(desc);
    t->category = dup0(category);
    t->paid = paid;
    t->account_id = dup0(account_id);
    t->card_id = dup0(card_id);
    t->card_payment = dup0("");
    t->recurring_id = dup0("");
    t->group_id = dup0("");
    return t;
}

Tx *tx_copy(const Tx *s) {
    Tx *t = tx_new(s->id, s->kind, s->value, s->date, s->desc, s->category, s->paid, s->account_id, s->card_id);
    tx_set_str(&t->card_payment, s->card_payment);
    tx_set_str(&t->recurring_id, s->recurring_id);
    tx_set_str(&t->group_id, s->group_id);
    t->parcel_n = s->parcel_n;
    t->parcel_total = s->parcel_total;
    return t;
}

void tx_free(Tx *t) {
    if (!t) return;
    g_free(t->id); g_free(t->desc); g_free(t->category); g_free(t->account_id);
    g_free(t->card_id); g_free(t->card_payment); g_free(t->recurring_id); g_free(t->group_id);
    g_free(t);
}

Account *account_new(const char *id, const char *name, Cents initial) {
    Account *a = g_new0(Account, 1);
    a->id = dup0(id); a->name = dup0(name); a->initial = initial;
    return a;
}
void account_free(Account *a) { if (!a) return; g_free(a->id); g_free(a->name); g_free(a); }

Card *card_new(const char *id, const char *name, Cents limit, int close, int due) {
    Card *c = g_new0(Card, 1);
    c->id = dup0(id); c->name = dup0(name); c->limit = limit; c->close = close; c->due = due;
    return c;
}
void card_free(Card *c) { if (!c) return; g_free(c->id); g_free(c->name); g_free(c); }

Goal *goal_new(const char *id, const char *name, Cents target, Cents saved, Day deadline, Cents monthly) {
    Goal *g = g_new0(Goal, 1);
    g->id = dup0(id); g->name = dup0(name); g->target = target; g->saved = saved; g->deadline = deadline; g->monthly = monthly;
    return g;
}
void goal_free(Goal *g) { if (!g) return; g_free(g->id); g_free(g->name); g_free(g); }

Recurring *recurring_new(const char *id, Kind kind, const char *desc, Cents value, const char *category,
                         const char *account_id, const char *card_id, int day, gboolean active, Day start, Ym last) {
    Recurring *r = g_new0(Recurring, 1);
    r->id = dup0(id); r->kind = kind; r->desc = dup0(desc); r->value = value; r->category = dup0(category);
    r->account_id = dup0(account_id); r->card_id = dup0(card_id); r->day = day; r->active = active;
    r->start = start; r->last = last;
    return r;
}
void recurring_free(Recurring *r) {
    if (!r) return;
    g_free(r->id); g_free(r->desc); g_free(r->category); g_free(r->account_id); g_free(r->card_id); g_free(r);
}

Limit *limit_new(const char *category, Cents value) {
    Limit *l = g_new0(Limit, 1);
    l->category = dup0(category); l->value = value;
    return l;
}
void limit_free(Limit *l) { if (!l) return; g_free(l->category); g_free(l); }

AppState *app_state_new_empty(void) {
    AppState *s = g_new0(AppState, 1);
    s->txs = g_ptr_array_new_with_free_func((GDestroyNotify)tx_free);
    s->goals = g_ptr_array_new_with_free_func((GDestroyNotify)goal_free);
    s->accounts = g_ptr_array_new_with_free_func((GDestroyNotify)account_free);
    s->cards = g_ptr_array_new_with_free_func((GDestroyNotify)card_free);
    s->recurring = g_ptr_array_new_with_free_func((GDestroyNotify)recurring_free);
    s->cats[KIND_INCOME] = g_ptr_array_new_with_free_func(g_free);
    s->cats[KIND_EXPENSE] = g_ptr_array_new_with_free_func(g_free);
    s->limits = g_ptr_array_new_with_free_func((GDestroyNotify)limit_free);
    s->theme = THEME_AUTO;
    return s;
}

AppState *app_state_new(void) {
    AppState *s = app_state_new_empty();
    g_ptr_array_add(s->accounts, account_new(MAIN_ACCOUNT, "Conta principal", 0));
    for (guint i = 0; i < G_N_ELEMENTS(DEFAULT_EXPENSE); i++) g_ptr_array_add(s->cats[KIND_EXPENSE], g_strdup(DEFAULT_EXPENSE[i]));
    for (guint i = 0; i < G_N_ELEMENTS(DEFAULT_INCOME); i++) g_ptr_array_add(s->cats[KIND_INCOME], g_strdup(DEFAULT_INCOME[i]));
    return s;
}

AppState *app_state_copy(const AppState *o) {
    AppState *s = app_state_new_empty();
    for (guint i = 0; i < o->txs->len; i++) g_ptr_array_add(s->txs, tx_copy(o->txs->pdata[i]));
    for (guint i = 0; i < o->goals->len; i++) {
        Goal *g = o->goals->pdata[i];
        g_ptr_array_add(s->goals, goal_new(g->id, g->name, g->target, g->saved, g->deadline, g->monthly));
    }
    for (guint i = 0; i < o->accounts->len; i++) {
        Account *a = o->accounts->pdata[i];
        g_ptr_array_add(s->accounts, account_new(a->id, a->name, a->initial));
    }
    for (guint i = 0; i < o->cards->len; i++) {
        Card *c = o->cards->pdata[i];
        g_ptr_array_add(s->cards, card_new(c->id, c->name, c->limit, c->close, c->due));
    }
    for (guint i = 0; i < o->recurring->len; i++) {
        Recurring *r = o->recurring->pdata[i];
        g_ptr_array_add(s->recurring, recurring_new(r->id, r->kind, r->desc, r->value, r->category, r->account_id,
                                                    r->card_id, r->day, r->active, r->start, r->last));
    }
    for (int k = 0; k < 2; k++)
        for (guint i = 0; i < o->cats[k]->len; i++) g_ptr_array_add(s->cats[k], g_strdup(o->cats[k]->pdata[i]));
    for (guint i = 0; i < o->limits->len; i++) {
        Limit *l = o->limits->pdata[i];
        g_ptr_array_add(s->limits, limit_new(l->category, l->value));
    }
    s->privacy = o->privacy;
    s->auto_lock = o->auto_lock;
    s->theme = o->theme;
    return s;
}

void app_state_free(AppState *s) {
    if (!s) return;
    g_ptr_array_unref(s->txs);
    g_ptr_array_unref(s->goals);
    g_ptr_array_unref(s->accounts);
    g_ptr_array_unref(s->cards);
    g_ptr_array_unref(s->recurring);
    g_ptr_array_unref(s->cats[0]);
    g_ptr_array_unref(s->cats[1]);
    g_ptr_array_unref(s->limits);
    g_free(s);
}

#define FIND_BY_ID(arr, T)                                            \
    if (!id) return NULL;                                             \
    for (guint i = 0; i < (arr)->len; i++) {                          \
        T *x = (arr)->pdata[i];                                       \
        if (strcmp(x->id, id) == 0) return x;                         \
    }                                                                 \
    return NULL;

Account *app_account(const AppState *s, const char *id) { FIND_BY_ID(s->accounts, Account) }
Card *app_card(const AppState *s, const char *id) { FIND_BY_ID(s->cards, Card) }
Tx *app_tx(const AppState *s, const char *id) { FIND_BY_ID(s->txs, Tx) }
Goal *app_goal(const AppState *s, const char *id) { FIND_BY_ID(s->goals, Goal) }
Recurring *app_recurring(const AppState *s, const char *id) { FIND_BY_ID(s->recurring, Recurring) }

gboolean app_limit(const AppState *s, const char *cat, Cents *out) {
    for (guint i = 0; i < s->limits->len; i++) {
        Limit *l = s->limits->pdata[i];
        if (strcmp(l->category, cat) == 0) { if (out) *out = l->value; return TRUE; }
    }
    return FALSE;
}

gboolean app_has_category(const AppState *s, Kind k, const char *name) {
    for (guint i = 0; i < s->cats[k]->len; i++) if (strcmp(s->cats[k]->pdata[i], name) == 0) return TRUE;
    return FALSE;
}

const char *app_first_category(const AppState *s, Kind k) {
    return s->cats[k]->len ? s->cats[k]->pdata[0] : "Outros";
}

char *ids_new(void) {
    static guint64 seq = 0;
    G_LOCK_DEFINE_STATIC(ids);
    G_LOCK(ids);
    guint64 n = seq++;
    G_UNLOCK(ids);
    guint64 parts[3] = {(guint64)(g_get_real_time() / 1000), n, (guint64)g_random_int_range(0, 1 << 20)};
    GString *out = g_string_new(NULL);
    for (int p = 0; p < 3; p++) {
        char tmp[16];
        int i = 0;
        guint64 v = parts[p];
        do { tmp[i++] = "0123456789abcdefghijklmnopqrstuvwxyz"[v % 36]; v /= 36; } while (v && i < 15);
        while (i) g_string_append_c(out, tmp[--i]);
    }
    return g_string_free(out, FALSE);
}

char *str_clean(const char *s, int max) {
    g_autofree char *t = g_strstrip(g_strdup(s ? s : ""));
    if (!g_utf8_validate(t, -1, NULL)) {
        g_autofree char *v = g_utf8_make_valid(t, -1);
        g_free(t);
        t = g_strdup(v);
    }
    if (g_utf8_strlen(t, -1) <= max) return g_steal_pointer(&t);
    return g_utf8_substring(t, 0, max);
}

gboolean str_same_ci(const char *a, const char *b) {
    g_autofree char *x = g_utf8_strdown(a, -1);
    g_autofree char *y = g_utf8_strdown(b, -1);
    return strcmp(x, y) == 0;
}
