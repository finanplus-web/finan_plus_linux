/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Período, pendências, saldo do dia, comparação dos Relatórios e calendário (ver period.h).
 */
#include "period.h"
#include "finance.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================ período */

gboolean period_full_month(Day from, Day to, Ym *out) {
    if (from == DAY_NONE || to == DAY_NONE || day_dom(from) != 1) return FALSE;
    Ym ym = day_ym(from);
    if (to != ym_last(ym)) return FALSE;
    if (out) *out = ym;
    return TRUE;
}

char *period_label(Day from, Day to) {
    Ym ym;
    if (period_full_month(from, to, &ym)) return cal_month_title(ym);
    char a[16], b[16];
    if (from == DAY_NONE && to == DAY_NONE) return g_strdup("Todo o período");
    if (from == DAY_NONE) return g_strdup_printf("Até %s", day_br(to, b));
    if (to == DAY_NONE) return g_strdup_printf("Desde %s", day_br(from, a));
    if (from == to) return g_strdup(day_br(from, a));
    return g_strdup_printf("%s a %s", day_br(from, a), day_br(to, b));
}

void period_shift(Day from, Day to, int delta, Day today, Day *out_from, Day *out_to) {
    Ym base;
    if (!period_full_month(from, to, &base))
        base = day_ym(from != DAY_NONE ? from : to != DAY_NONE ? to : today);
    Ym ym = base + delta;
    *out_from = ym_first(ym);
    *out_to = ym_last(ym);
}

Pending period_month_pending(const AppState *s, Ym ym, Day today) {
    Pending p = {0, 0};
    for (guint i = 0; i < s->txs->len; i++) {
        const Tx *t = s->txs->pdata[i];
        if (t->paid || tx_is_card(t) || day_ym(t->date) != ym) continue;
        if (t->kind == KIND_INCOME) p.to_receive += t->value;
        else p.to_pay += t->value;
    }
    g_autoptr(GArray) inv = cal_invoices_due(s, ym, today);
    for (guint i = 0; i < inv->len; i++) p.to_pay += g_array_index(inv, InvoiceDue, i).amount;
    return p;
}

Pending period_pending(GPtrArray *txs) {
    Pending p = {0, 0};
    for (guint i = 0; i < txs->len; i++) {
        const Tx *t = txs->pdata[i];
        if (t->paid || !tx_is_flow(t) || tx_is_card(t)) continue;
        if (t->kind == KIND_INCOME) p.to_receive += t->value;
        else p.to_pay += t->value;
    }
    return p;
}

Cents period_cash_net(GPtrArray *txs) {
    Cents n = 0;
    for (guint i = 0; i < txs->len; i++) {
        const Tx *t = txs->pdata[i];
        if (tx_is_card(t)) continue;
        n += t->kind == KIND_INCOME ? t->value : -t->value;
    }
    return n;
}

/* ================================================================ comparação */

gboolean period_compare(Day from, Day to, Day today, Compare *out) {
    Ym ym;
    if (period_full_month(from, to, &ym)) {
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
    /* arredonda como Math.round do Kotlin/JS: floor(x + 0.5) */
    long pct = (long)floor((double)(cur - prev) * 100.0 / (double)prev + 0.5);
    return g_strdup_printf("%s%ld%% %s", pct > 0 ? "+" : pct < 0 ? "−" : "", labs(pct), c->label);
}

/* ================================================================ calendário */

const char *const CAL_WEEK_HEADER[7] = {"DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SÁB"};
static const char *const WEEKDAYS[7] = {
    "segunda-feira", "terça-feira", "quarta-feira", "quinta-feira", "sexta-feira", "sábado", "domingo"};

GArray *cal_invoices_due(const AppState *s, Ym ym, Day today) {
    GArray *out = g_array_new(FALSE, FALSE, sizeof(InvoiceDue));
    for (guint i = 0; i < s->cards->len; i++) {
        const Card *c = s->cards->pdata[i];
        CardStatus st;
        card_status(s, c, today, &st);
        for (guint j = 0; j < st.invoices->len; j++) {
            const Invoice *v = &g_array_index(st.invoices, Invoice, j);
            if (invoice_open(v) <= 0 || day_ym(v->due) != ym) continue;
            InvoiceDue d = {c->id, c->name, invoice_open(v), v->due, v->due < today};
            g_array_append_val(out, d);
        }
        card_status_clear(&st);
    }
    return out;
}

/* receitas primeiro, depois o que é da conta, depois o cartão; empate pelo id */
static int cmp_day_tx(gconstpointer a, gconstpointer b) {
    const Tx *x = *(const Tx *const *)a, *y = *(const Tx *const *)b;
    int kx = x->kind != KIND_INCOME, ky = y->kind != KIND_INCOME;
    if (kx != ky) return kx - ky;
    int cx = tx_is_card(x), cy = tx_is_card(y);
    if (cx != cy) return cx - cy;
    return strcmp(x->id, y->id);
}

static CalDay *day_slot(CalMonth *m, Day d) {
    int i = day_dom(d) - 1;
    if (!m->days[i]) {
        CalDay *c = g_new0(CalDay, 1);
        c->date = d;
        c->txs = g_ptr_array_new();
        c->invoices = g_array_new(FALSE, FALSE, sizeof(InvoiceDue));
        m->days[i] = c;
    }
    return m->days[i];
}

CalMonth *cal_build(const AppState *s, Ym ym, Day today) {
    CalMonth *m = g_new0(CalMonth, 1);
    m->ym = ym;
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (day_ym(t->date) == ym) g_ptr_array_add(day_slot(m, t->date)->txs, t);
    }
    g_autoptr(GArray) inv = cal_invoices_due(s, ym, today);
    for (guint i = 0; i < inv->len; i++) {
        InvoiceDue *v = &g_array_index(inv, InvoiceDue, i);
        g_array_append_val(day_slot(m, v->due)->invoices, *v);
    }
    for (int k = 0; k < 31; k++) {
        CalDay *c = m->days[k];
        if (!c) continue;
        g_ptr_array_sort(c->txs, cmp_day_tx);
        for (guint i = 0; i < c->txs->len; i++) {
            const Tx *t = c->txs->pdata[i];
            if (tx_is_card(t)) c->marks |= MARK_CARD; /* compra no cartão: só na fatura */
            else if (t->kind == KIND_INCOME) { c->income += t->value; c->marks |= MARK_INCOME; }
            else { c->expense += t->value; c->marks |= tx_is_flow(t) ? MARK_EXPENSE : MARK_CARD; }
            if (!t->paid && !tx_is_card(t) && t->date < today) c->overdue = TRUE;
        }
        for (guint i = 0; i < c->invoices->len; i++) {
            const InvoiceDue *v = &g_array_index(c->invoices, InvoiceDue, i);
            c->expense += v->amount;
            c->marks |= MARK_CARD;
            if (v->overdue) c->overdue = TRUE;
        }
    }
    return m;
}

const CalDay *cal_get(const CalMonth *m, Day d) {
    if (d == DAY_NONE || day_ym(d) != m->ym) return NULL;
    return m->days[day_dom(d) - 1];
}

void cal_month_free(CalMonth *m) {
    if (!m) return;
    for (int k = 0; k < 31; k++) {
        if (!m->days[k]) continue;
        g_ptr_array_unref(m->days[k]->txs);
        g_array_unref(m->days[k]->invoices);
        g_free(m->days[k]);
    }
    g_free(m);
}

void cal_totals(const CalMonth *m, Cents *income, Cents *expense) {
    Cents a = 0, b = 0;
    for (int k = 0; k < 31; k++)
        if (m->days[k]) { a += m->days[k]->income; b += m->days[k]->expense; }
    *income = a;
    *expense = b;
}

int cal_cells(Ym ym, Day out[42]) {
    Day first = ym_first(ym);
    int lead = day_weekday(first) % 7; /* domingo (7) → 0 */
    int n = 0;
    for (int i = 0; i < lead; i++) out[n++] = DAY_NONE;
    for (int d = 0; d < ym_len(ym); d++) out[n++] = first + d;
    while (n % 7) out[n++] = DAY_NONE;
    return n;
}

static char *compact_unit(int64_t reais, int64_t unit, const char *suffix) {
    int64_t tenths = (reais * 10 + unit / 2) / unit;
    if (tenths >= 100) return g_strdup_printf("%" G_GINT64_FORMAT " %s", (reais + unit / 2) / unit, suffix);
    if (tenths % 10 == 0) return g_strdup_printf("%" G_GINT64_FORMAT " %s", tenths / 10, suffix);
    return g_strdup_printf("%" G_GINT64_FORMAT ",%" G_GINT64_FORMAT " %s", tenths / 10, tenths % 10, suffix);
}

char *cal_compact(Cents c) {
    int64_t reais = ((c < 0 ? -c : c) + 50) / 100;
    g_autofree char *body = NULL;
    /* a unidade é escolhida pelo valor já arredondado (999.999 vira "1 mi", não "1000 mil") */
    if (reais < 1000) body = g_strdup_printf("%" G_GINT64_FORMAT, reais);
    else if ((reais + 500) / 1000 < 1000) body = compact_unit(reais, 1000, "mil");
    else if ((reais + 500000) / 1000000 < 1000) body = compact_unit(reais, 1000000, "mi");
    else body = compact_unit(reais, 1000000000, "bi");
    return g_strconcat(c < 0 ? "−" : "", body, NULL);
}

char *cal_signed(Cents c) {
    if (c > 0) {
        g_autofree char *v = cal_compact(c);
        return g_strconcat("+", v, NULL);
    }
    return c < 0 ? cal_compact(c) : g_strdup("0");
}

char *cal_month_title(Ym ym) {
    const char *m = BR_MONTHS[ym_month(ym) - 1];
    gunichar first = g_utf8_get_char(m);
    char up[8] = {0};
    g_unichar_to_utf8(g_unichar_toupper(first), up);
    return g_strdup_printf("%s%s de %d", up, g_utf8_next_char(m), ym_year(ym));
}

char *cal_day_title(Day d, Day today) {
    const char *wd = WEEKDAYS[day_weekday(d) - 1];
    const char *dash = strchr(wd, '-');
    g_autofree char *name = dash ? g_strndup(wd, (gsize)(dash - wd)) : g_strdup(wd);
    gunichar first = g_utf8_get_char(name);
    char up[8] = {0};
    g_unichar_to_utf8(g_unichar_toupper(first), up);
    const char *month = BR_MONTHS[day_month(d) - 1];
    if (day_year(d) != day_year(today))
        return g_strdup_printf("%s%s, %d de %s de %d", up, g_utf8_next_char(name), day_dom(d), month, day_year(d));
    return g_strdup_printf("%s%s, %d de %s", up, g_utf8_next_char(name), day_dom(d), month);
}

char *cal_describe(Day d, const CalDay *day, Day today, gboolean hide) {
    GString *s = g_string_new(NULL);
    g_string_printf(s, "%d de %s, %s", day_dom(d), BR_MONTHS[day_month(d) - 1], WEEKDAYS[day_weekday(d) - 1]);
    if (d == today) g_string_append(s, ", hoje");
    int n = day ? cal_day_count(day) : 0;
    if (n == 0) g_string_append(s, ", sem lançamentos");
    else {
        if (n == 1) g_string_append(s, ", 1 lançamento");
        else g_string_append_printf(s, ", %d lançamentos", n);
        if (!hide && (day->income != 0 || day->expense != 0)) {
            char buf[40];
            Cents net = cal_day_net(day);
            if (net > 0) g_string_append_printf(s, ", saldo do dia mais %s", money_format(net, buf));
            else if (net < 0) g_string_append_printf(s, ", saldo do dia menos %s", money_format(-net, buf));
            else g_string_append(s, ", saldo do dia zero");
        }
        if (day->overdue) g_string_append(s, ", em atraso");
    }
    return g_string_free(s, FALSE);
}
