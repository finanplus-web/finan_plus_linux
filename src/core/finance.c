/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "finance.h"
#include <string.h>

/* ------------------------------------------------------------------ cartões e faturas */

Ym invoice_ym(const Card *card, Day date) {
    Ym ym = day_ym(date);
    int close = MIN(card->close, ym_len(ym));
    return day_dom(date) > close ? ym + 1 : ym;
}

Day invoice_due(const Card *card, Ym ym) {
    return ym_day_clamped(card->due > card->close ? ym : ym + 1, card->due);
}

Day invoice_close(const Card *card, Ym ym) { return ym_day_clamped(ym, card->close); }

typedef struct { Ym ym; Cents total; } YmTotal;

static gint cmp_ym_total(gconstpointer a, gconstpointer b) {
    const YmTotal *x = a, *y = b;
    return (x->ym > y->ym) - (x->ym < y->ym);
}

void card_status(const AppState *s, const Card *card, Day today, CardStatus *out) {
    GArray *by = g_array_new(FALSE, FALSE, sizeof(YmTotal));
    Cents spent = 0, paid = 0;
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (t->kind == KIND_EXPENSE && strcmp(t->card_id, card->id) == 0) {
            spent += t->value;
            Ym k = invoice_ym(card, t->date);
            guint j;
            for (j = 0; j < by->len; j++) if (g_array_index(by, YmTotal, j).ym == k) break;
            if (j == by->len) { YmTotal n = {k, 0}; g_array_append_val(by, n); }
            g_array_index(by, YmTotal, j).total += t->value;
        }
        if (t->paid && strcmp(t->card_payment, card->id) == 0) paid += t->value;
    }
    g_array_sort(by, cmp_ym_total);
    out->invoices = g_array_new(FALSE, FALSE, sizeof(Invoice));
    Cents left = paid;
    for (guint j = 0; j < by->len; j++) {
        YmTotal *yt = &g_array_index(by, YmTotal, j);
        Cents pay = MIN(left, yt->total);
        left -= pay;
        Invoice inv = {yt->ym, yt->total, pay, invoice_close(card, yt->ym), invoice_due(card, yt->ym), FALSE};
        inv.closed = today > inv.close;
        g_array_append_val(out->invoices, inv);
    }
    g_array_unref(by);
    Cents used = MAX(0, spent - paid);
    out->used = used;
    out->available = MAX(0, card->limit - used);
    out->credit = MAX(0, paid - spent);
    out->current = -1;
    for (guint j = 0; j < out->invoices->len && out->current < 0; j++) {
        Invoice *v = &g_array_index(out->invoices, Invoice, j);
        if (invoice_open(v) > 0 && v->closed) out->current = (int)j;
    }
    for (guint j = 0; j < out->invoices->len && out->current < 0; j++) {
        Invoice *v = &g_array_index(out->invoices, Invoice, j);
        if (invoice_open(v) > 0 && !v->closed) out->current = (int)j;
    }
}

void card_status_clear(CardStatus *st) {
    if (st->invoices) g_array_unref(st->invoices);
    st->invoices = NULL;
}

const Invoice *card_status_current(const CardStatus *st) {
    return st->current < 0 ? NULL : &g_array_index(st->invoices, Invoice, st->current);
}

/* ------------------------------------------------------------------ saldos */

Cents account_balance(const AppState *s, const Account *a) {
    Cents n = a->initial;
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (!t->paid || tx_is_card(t) || strcmp(t->account_id, a->id) != 0) continue;
        n += t->kind == KIND_INCOME ? t->value : -t->value;
    }
    return n;
}

Cents current_balance(const AppState *s) {
    Cents n = 0;
    for (guint i = 0; i < s->accounts->len; i++) n += account_balance(s, s->accounts->pdata[i]);
    return n;
}

Cents future_balance(const AppState *s, Day until, Day today) {
    Cents c = current_balance(s);
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (tx_is_card(t) || t->paid || t->date > until) continue;
        c += t->kind == KIND_INCOME ? t->value : -t->value;
    }
    for (guint i = 0; i < s->cards->len; i++) {
        CardStatus st;
        card_status(s, s->cards->pdata[i], today, &st);
        for (guint j = 0; j < st.invoices->len; j++) {
            Invoice *v = &g_array_index(st.invoices, Invoice, j);
            if (invoice_open(v) > 0 && v->due <= until) c -= invoice_open(v);
        }
        card_status_clear(&st);
    }
    return c;
}

/* ------------------------------------------------------------------ recorrências */

int generate_recurring(AppState *s, Day today) {
    Ym cur = day_ym(today);
    int created = 0;
    for (guint i = 0; i < s->recurring->len; i++) {
        Recurring *r = s->recurring->pdata[i];
        if (!r->active) continue;
        Ym start_ym = r->start != DAY_NONE ? day_ym(r->start) : (r->last != YM_NONE ? r->last + 1 : cur);
        Ym m = (r->last != YM_NONE && r->last + 1 > start_ym) ? r->last + 1 : start_ym;
        Ym last = r->last;
        int guard = 0;
        while (m <= cur && guard < 24) {
            Day date = ym_day_clamped(m, r->day);
            last = m;
            if (r->start == DAY_NONE || date >= r->start) {
                g_autofree char *id = ids_new();
                Tx *t = tx_new(id, r->kind, r->value, date, r->desc, r->category, r->card_id[0] != '\0', r->account_id, r->card_id);
                tx_set_str(&t->recurring_id, r->id);
                g_ptr_array_add(s->txs, t);
                created++;
            }
            m++;
            guard++;
        }
        r->last = last;
    }
    return created;
}

/* ------------------------------------------------------------------ parcelas */

void split_installments(Cents total, int n, Cents *out) {
    Cents base = total / n, rest = total - base * n;
    for (int i = 0; i < n; i++) out[i] = base + (i == 0 ? rest : 0);
}

/* ------------------------------------------------------------------ metas */

GoalPlan goal_plan(const Goal *g, Day today) {
    GoalPlan p = {0};
    p.eta = YM_NONE;
    p.remaining = MAX(0, g->target - g->saved);
    if (p.remaining == 0) { p.done = TRUE; return p; }
    if (g->deadline != DAY_NONE) {
        if (g->deadline < today) p.past_due = TRUE;
        else {
            gint64 months = (gint64)(day_ym(g->deadline) - day_ym(today)) + 1;
            p.has_needed = TRUE;
            p.needed = (p.remaining + months - 1) / months;
        }
    }
    if (g->monthly > 0) {
        gint64 months = (p.remaining + g->monthly - 1) / g->monthly;
        if (months > 1200) months = 1200;
        p.eta = day_ym(today) + (Ym)(months - 1);
        if (g->deadline != DAY_NONE && p.eta > day_ym(g->deadline)) p.late = TRUE;
    }
    return p;
}

/* ------------------------------------------------------------------ resumos */

Flow flow_of(GPtrArray *txs) {
    Flow f = {0, 0};
    for (guint i = 0; i < txs->len; i++) {
        Tx *t = txs->pdata[i];
        if (!tx_is_flow(t) || !t->paid) continue;
        if (t->kind == KIND_INCOME) f.income += t->value; else f.expense += t->value;
    }
    return f;
}

Flow month_flow(const AppState *s, Ym ym) {
    Flow f = {0, 0};
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (!tx_is_flow(t) || !t->paid || day_ym(t->date) != ym) continue;
        if (t->kind == KIND_INCOME) f.income += t->value; else f.expense += t->value;
    }
    return f;
}

Cents budget_usage(const AppState *s, Ym ym, const char *category) {
    Cents n = 0;
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (t->kind == KIND_EXPENSE && tx_is_flow(t) && day_ym(t->date) == ym && strcmp(t->category, category) == 0) n += t->value;
    }
    return n;
}

void cat_total_free(CatTotal *c) { if (!c) return; g_free(c->name); g_free(c); }

static gint cmp_cat_desc(gconstpointer a, gconstpointer b) {
    const CatTotal *x = *(CatTotal *const *)a, *y = *(CatTotal *const *)b;
    return (y->value > x->value) - (y->value < x->value);
}

GPtrArray *category_totals(const AppState *s, Day from, Day to) {
    GPtrArray *out = g_ptr_array_new_with_free_func((GDestroyNotify)cat_total_free);
    GHashTable *idx = g_hash_table_new(g_str_hash, g_str_equal);
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (t->kind != KIND_EXPENSE || !t->paid || !tx_is_flow(t)) continue;
        if ((from != DAY_NONE && t->date < from) || (to != DAY_NONE && t->date > to)) continue;
        CatTotal *c = g_hash_table_lookup(idx, t->category);
        if (!c) {
            c = g_new0(CatTotal, 1);
            c->name = g_strdup(t->category);
            g_ptr_array_add(out, c);
            g_hash_table_insert(idx, c->name, c);
        }
        c->value += t->value;
    }
    g_hash_table_unref(idx);
    g_ptr_array_sort(out, cmp_cat_desc);
    return out;
}

/* ------------------------------------------------------------------ lembretes */

void reminder_free(Reminder *r) { if (!r) return; g_free(r->title); g_free(r->ref_id); g_free(r); }

static Reminder *reminder_new(ReminderType type, const char *title, Cents amount, Day date, const char *ref) {
    Reminder *r = g_new0(Reminder, 1);
    r->type = type; r->title = g_strdup(title); r->amount = amount; r->date = date; r->ref_id = g_strdup(ref);
    return r;
}

static gint cmp_reminder(gconstpointer a, gconstpointer b) {
    const Reminder *x = *(Reminder *const *)a, *y = *(Reminder *const *)b;
    return (x->date > y->date) - (x->date < y->date);
}

GPtrArray *reminders(const AppState *s, Day today, int days) {
    Day limit = today + days;
    GPtrArray *out = g_ptr_array_new_with_free_func((GDestroyNotify)reminder_free);
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (t->paid || tx_is_card(t) || t->date > limit) continue;
        ReminderType type;
        if (t->kind == KIND_INCOME) {
            if (t->date > today) continue;
            type = REMINDER_INCOME_DUE;
        } else type = t->date < today ? REMINDER_BILL_OVERDUE : REMINDER_BILL_DUE;
        g_ptr_array_add(out, reminder_new(type, t->desc, t->value, t->date, t->id));
    }
    for (guint i = 0; i < s->cards->len; i++) {
        Card *c = s->cards->pdata[i];
        CardStatus st;
        card_status(s, c, today, &st);
        for (guint j = 0; j < st.invoices->len; j++) {
            Invoice *v = &g_array_index(st.invoices, Invoice, j);
            if (invoice_open(v) > 0 && v->due <= limit && v->closed) {
                g_autofree char *title = g_strdup_printf("Fatura %s", c->name);
                g_ptr_array_add(out, reminder_new(REMINDER_INVOICE_DUE, title, invoice_open(v), v->due, c->id));
            }
        }
        card_status_clear(&st);
    }
    g_ptr_array_sort(out, cmp_reminder);
    return out;
}

Reminder *next_due(const AppState *s, Day today) {
    Reminder *best = NULL;
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (t->paid || tx_is_card(t) || t->kind != KIND_EXPENSE) continue;
        if (!best || t->date < best->date) {
            reminder_free(best);
            best = reminder_new(t->date < today ? REMINDER_BILL_OVERDUE : REMINDER_BILL_DUE, t->desc, t->value, t->date, t->id);
        }
    }
    for (guint i = 0; i < s->cards->len; i++) {
        Card *c = s->cards->pdata[i];
        CardStatus st;
        card_status(s, c, today, &st);
        for (guint j = 0; j < st.invoices->len; j++) {
            Invoice *v = &g_array_index(st.invoices, Invoice, j);
            if (invoice_open(v) > 0 && (!best || v->due < best->date)) {
                reminder_free(best);
                g_autofree char *title = g_strdup_printf("Fatura %s", c->name);
                best = reminder_new(REMINDER_INVOICE_DUE, title, invoice_open(v), v->due, c->id);
            }
        }
        card_status_clear(&st);
    }
    return best;
}

/* ------------------------------------------------------------------ CSV */

char *csv_cell(const char *v) {
    GString *s = g_string_new("\"");
    if (v && v[0] && strchr("=+-@\t\r", v[0])) g_string_append_c(s, '\''); /* impede fórmula no Excel/Calc */
    for (const char *p = v ? v : ""; *p; p++) {
        if (*p == '"') g_string_append(s, "\"\"");
        else g_string_append_c(s, *p);
    }
    g_string_append_c(s, '"');
    return g_string_free(s, FALSE);
}

static gint cmp_tx_date(gconstpointer a, gconstpointer b) {
    const Tx *x = *(Tx *const *)a, *y = *(Tx *const *)b;
    return (x->date > y->date) - (x->date < y->date);
}

void txs_sort_by_date(GPtrArray *a) { g_ptr_array_sort(a, cmp_tx_date); }

char *csv_build(const AppState *s) {
    GString *out = g_string_new("\xEF\xBB\xBF" "data;tipo;categoria;descricao;valor;situacao;conta;cartao");
    g_autoptr(GPtrArray) sorted = g_ptr_array_new();
    for (guint i = 0; i < s->txs->len; i++) g_ptr_array_add(sorted, s->txs->pdata[i]);
    txs_sort_by_date(sorted);
    for (guint i = 0; i < sorted->len; i++) {
        Tx *t = sorted->pdata[i];
        char d[11], v[32];
        Account *a = tx_is_card(t) ? NULL : app_account(s, t->account_id);
        Card *c = app_card(s, tx_is_card(t) ? t->card_id : t->card_payment);
        const char *cols[8] = {
            day_iso(t->date, d), t->kind == KIND_INCOME ? "receita" : "despesa", t->category, t->desc,
            money_input(t->value, v), t->paid ? "realizado" : "pendente", a ? a->name : "", c ? c->name : "",
        };
        g_string_append(out, "\r\n");
        for (int k = 0; k < 8; k++) {
            g_autofree char *cell = csv_cell(cols[k]);
            if (k) g_string_append_c(out, ';');
            g_string_append(out, cell);
        }
    }
    if (sorted->len == 0) g_string_append(out, "\r\n");
    return g_string_free(out, FALSE);
}
