/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "period.h"
#include <math.h>
#include <string.h>

static const char *const WEEKDAYS[7] = {"segunda-feira", "terça-feira", "quarta-feira", "quinta-feira", "sexta-feira", "sábado", "domingo"};

/* primeira letra maiúscula (UTF-8 seguro) */
static char *cap(const char *s) {
    gunichar c = g_utf8_get_char(s);
    char b[8] = {0};
    g_unichar_to_utf8(g_unichar_toupper(c), b);
    return g_strconcat(b, g_utf8_next_char(s), NULL);
}

/* ---------------------------------------------------------------- período */

Ym period_full_month(Day from, Day to) {
    if (from == DAY_NONE || to == DAY_NONE || day_dom(from) != 1) return YM_NONE;
    Ym ym = day_ym(from);
    return to == ym_last(ym) ? ym : YM_NONE;
}

char *cal_month_title(Ym ym) {
    g_autofree char *m = cap(br_month(ym));
    return g_strdup_printf("%s de %d", m, ym_year(ym));
}

char *period_label(Day from, Day to) {
    Ym ym = period_full_month(from, to);
    if (ym != YM_NONE) return cal_month_title(ym);
    char a[11], b[11];
    if (from == DAY_NONE && to == DAY_NONE) return g_strdup("Todo o período");
    if (from == DAY_NONE) return g_strdup_printf("Até %s", day_br(to, b));
    if (to == DAY_NONE) return g_strdup_printf("Desde %s", day_br(from, a));
    if (from == to) return g_strdup(day_br(from, a));
    return g_strdup_printf("%s a %s", day_br(from, a), day_br(to, b));
}

void period_shift(Day from, Day to, int delta, Day today, Day *out_from, Day *out_to) {
    Ym base = period_full_month(from, to);
    if (base == YM_NONE) base = day_ym(from != DAY_NONE ? from : to != DAY_NONE ? to : today);
    Ym ym = base + delta;
    *out_from = ym_first(ym);
    *out_to = ym_last(ym);
}

Pending period_month_pending(const AppState *s, Ym ym, Day today) {
    Pending p = {0, 0};
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (t->paid || tx_is_card(t) || day_ym(t->date) != ym) continue;
        if (t->kind == KIND_INCOME) p.to_receive += t->value; else p.to_pay += t->value;
    }
    GArray *inv = cal_invoices_due(s, ym, today);
    for (guint i = 0; i < inv->len; i++) p.to_pay += g_array_index(inv, InvoiceDue, i).amount;
    cal_invoices_free(inv);
    return p;
}

Pending period_pending(GPtrArray *txs) {
    Pending p = {0, 0};
    for (guint i = 0; i < txs->len; i++) {
        Tx *t = txs->pdata[i];
        if (t->paid || !tx_is_flow(t) || tx_is_card(t)) continue;
        if (t->kind == KIND_INCOME) p.to_receive += t->value; else p.to_pay += t->value;
    }
    return p;
}

Cents period_cash_net(GPtrArray *txs) {
    Cents n = 0;
    for (guint i = 0; i < txs->len; i++) {
        Tx *t = txs->pdata[i];
        if (tx_is_card(t)) continue;
        n += t->kind == KIND_INCOME ? t->value : -t->value;
    }
    return n;
}

/* ---------------------------------------------------------------- comparação */

gboolean period_compare(Day from, Day to, Day today, Compare *out) {
    Ym ym = period_full_month(from, to);
    if (ym != YM_NONE) {
        Ym prev = ym - 1;
        out->from = ym_first(prev);
        if (ym == day_ym(today)) {
            out->to = ym_day_clamped(prev, day_dom(today));
            g_snprintf(out->label, sizeof out->label, "vs. %s (mesmos dias)", BR_MONTHS_SHORT[ym_month(prev) - 1]);
        } else {
            out->to = ym_last(prev);
            g_snprintf(out->label, sizeof out->label, "vs. %s", br_month(prev));
        }
        return TRUE;
    }
    if (from == DAY_NONE || to == DAY_NONE || to < from) return FALSE;
    int days = to - from + 1;
    out->from = from - days;
    out->to = from - 1;
    g_strlcpy(out->label, "vs. período anterior", sizeof out->label);
    return TRUE;
}

char *period_compare_text(Cents cur, Cents prev, const Compare *c) {
    if (prev <= 0) return g_strdup("Sem base para comparar");
    long long pct = (long long)floor((double)(cur - prev) * 100.0 / (double)prev + 0.5);
    return g_strdup_printf("%s%lld%% %s", pct > 0 ? "+" : pct < 0 ? "−" : "", pct < 0 ? -pct : pct, c->label);
}

/* ---------------------------------------------------------------- calendário */

GArray *cal_invoices_due(const AppState *s, Ym ym, Day today) {
    GArray *out = g_array_new(FALSE, FALSE, sizeof(InvoiceDue));
    for (guint i = 0; i < s->cards->len; i++) {
        Card *c = s->cards->pdata[i];
        CardStatus st;
        card_status(s, c, today, &st);
        for (guint j = 0; j < st.invoices->len; j++) {
            Invoice *v = &g_array_index(st.invoices, Invoice, j);
            if (invoice_open(v) <= 0 || day_ym(v->due) != ym) continue;
            InvoiceDue d = {g_strdup(c->id), g_strdup(c->name), invoice_open(v), v->due, v->due < today};
            g_array_append_val(out, d);
        }
        card_status_clear(&st);
    }
    return out;
}

void cal_invoices_free(GArray *a) {
    if (!a) return;
    for (guint i = 0; i < a->len; i++) {
        InvoiceDue *d = &g_array_index(a, InvoiceDue, i);
        g_free(d->card_id);
        g_free(d->card_name);
    }
    g_array_unref(a);
}

/* receitas primeiro, depois conta, depois cartão; empate pelo id */
static gint cmp_day_tx(gconstpointer a, gconstpointer b) {
    const Tx *x = *(Tx *const *)a, *y = *(Tx *const *)b;
    int kx = x->kind != KIND_INCOME, ky = y->kind != KIND_INCOME;
    if (kx != ky) return kx - ky;
    int cx = tx_is_card(x), cy = tx_is_card(y);
    if (cx != cy) return cx - cy;
    return strcmp(x->id, y->id);
}

CalMonth *cal_month_build(const AppState *s, Ym ym, Day today) {
    CalMonth *m = g_new0(CalMonth, 1);
    m->ym = ym;
    m->len = ym_len(ym);
    for (int i = 0; i < m->len; i++) {
        m->days[i].date = ym_first(ym) + i;
        m->days[i].txs = g_ptr_array_new();
        m->days[i].invoices = g_array_new(FALSE, FALSE, sizeof(InvoiceDue));
    }
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (day_ym(t->date) != ym) continue;
        g_ptr_array_add(m->days[day_dom(t->date) - 1].txs, t);
    }
    GArray *inv = cal_invoices_due(s, ym, today);
    for (guint i = 0; i < inv->len; i++) {
        InvoiceDue *d = &g_array_index(inv, InvoiceDue, i);
        g_array_append_val(m->days[day_dom(d->due) - 1].invoices, *d); /* passa a posse das strings */
    }
    g_array_unref(inv);
    for (int i = 0; i < m->len; i++) {
        CalDay *c = &m->days[i];
        g_ptr_array_sort(c->txs, cmp_day_tx);
        for (guint j = 0; j < c->txs->len; j++) {
            Tx *t = c->txs->pdata[j];
            if (tx_is_card(t)) c->marks |= MARK_CARD;
            else if (t->kind == KIND_INCOME) { c->income += t->value; c->marks |= MARK_INCOME; }
            else { c->expense += t->value; c->marks |= tx_is_flow(t) ? MARK_EXPENSE : MARK_CARD; }
            if (!t->paid && !tx_is_card(t) && t->date < today) c->overdue = TRUE;
        }
        for (guint j = 0; j < c->invoices->len; j++) {
            InvoiceDue *d = &g_array_index(c->invoices, InvoiceDue, j);
            c->expense += d->amount;
            c->marks |= MARK_CARD;
            if (d->overdue) c->overdue = TRUE;
        }
    }
    return m;
}

void cal_month_free(CalMonth *m) {
    if (!m) return;
    for (int i = 0; i < m->len; i++) {
        g_ptr_array_unref(m->days[i].txs);
        cal_invoices_free(m->days[i].invoices);
    }
    g_free(m);
}

const CalDay *cal_month_day(const CalMonth *m, Day d) {
    if (day_ym(d) != m->ym) return NULL;
    const CalDay *c = &m->days[day_dom(d) - 1];
    return cal_day_count(c) > 0 ? c : NULL;
}

void cal_month_totals(const CalMonth *m, Cents *income, Cents *expense) {
    Cents a = 0, b = 0;
    for (int i = 0; i < m->len; i++) { a += m->days[i].income; b += m->days[i].expense; }
    *income = a;
    *expense = b;
}

int cal_cells(Ym ym, Day *out) {
    Day first = ym_first(ym);
    int lead = day_weekday(first) % 7; /* domingo = 0 */
    int n = 0;
    for (int i = 0; i < lead; i++) out[n++] = DAY_NONE;
    for (int d = 0; d < ym_len(ym); d++) out[n++] = first + d;
    while (n % 7) out[n++] = DAY_NONE;
    return n;
}

static char *short_unit(gint64 reais, gint64 unit, const char *suffix, char *buf) {
    gint64 tenths = (reais * 10 + unit / 2) / unit;
    if (tenths >= 100) g_snprintf(buf, 32, "%" G_GINT64_FORMAT " %s", (reais + unit / 2) / unit, suffix);
    else if (tenths % 10 == 0) g_snprintf(buf, 32, "%" G_GINT64_FORMAT " %s", tenths / 10, suffix);
    else g_snprintf(buf, 32, "%" G_GINT64_FORMAT ",%" G_GINT64_FORMAT " %s", tenths / 10, tenths % 10, suffix);
    return buf;
}

char *cal_compact(Cents c, char *buf) {
    gint64 reais = ((c < 0 ? -c : c) + 50) / 100;
    char body[32];
    if (reais < 1000) g_snprintf(body, sizeof body, "%" G_GINT64_FORMAT, reais);
    else if ((reais + 500) / 1000 < 1000) short_unit(reais, 1000, "mil", body);
    else if ((reais + 500000) / 1000000 < 1000) short_unit(reais, 1000000, "mi", body);
    else short_unit(reais, 1000000000, "bi", body);
    g_snprintf(buf, 32, "%s%s", c < 0 ? "−" : "", body);
    return buf;
}

char *cal_signed(Cents c, char *buf) {
    if (c == 0) { g_strlcpy(buf, "0", 32); return buf; }
    char b[32];
    cal_compact(c, b);
    g_snprintf(buf, 32, "%s%s", c > 0 ? "+" : "", b);
    return buf;
}

char *cal_day_title(Day d, Day today) {
    const char *wd = WEEKDAYS[day_weekday(d) - 1];
    const char *dash = strchr(wd, '-');
    g_autofree char *w = dash ? g_strndup(wd, dash - wd) : g_strdup(wd);
    g_autofree char *W = cap(w);
    if (day_year(d) != day_year(today))
        return g_strdup_printf("%s, %d de %s de %d", W, day_dom(d), br_month(day_ym(d)), day_year(d));
    return g_strdup_printf("%s, %d de %s", W, day_dom(d), br_month(day_ym(d)));
}

char *cal_describe(Day d, const CalDay *day, Day today, gboolean hide) {
    GString *s = g_string_new(NULL);
    g_string_append_printf(s, "%d de %s, %s", day_dom(d), br_month(day_ym(d)), WEEKDAYS[day_weekday(d) - 1]);
    if (d == today) g_string_append(s, ", hoje");
    if (!day || cal_day_count(day) == 0) g_string_append(s, ", sem lançamentos");
    else {
        guint n = cal_day_count(day);
        if (n == 1) g_string_append(s, ", 1 lançamento"); else g_string_append_printf(s, ", %u lançamentos", n);
        if (!hide && (day->income != 0 || day->expense != 0)) {
            Cents net = cal_day_net(day);
            char m[32];
            if (net > 0) g_string_append_printf(s, ", saldo do dia mais %s", money_format(net, m));
            else if (net < 0) g_string_append_printf(s, ", saldo do dia menos %s", money_format(-net, m));
            else g_string_append(s, ", saldo do dia zero");
        }
        if (day->overdue) g_string_append(s, ", em atraso");
    }
    return g_string_free(s, FALSE);
}
