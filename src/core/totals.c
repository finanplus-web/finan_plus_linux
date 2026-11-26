/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "totals.h"
#include "finance.h"
#include <string.h>

Cents totals_goals_planned(const AppState *s, Ym ym, Day today) {
    Ym cur = day_ym(today);
    if (ym < cur) return 0;
    Cents total = 0;
    for (guint i = 0; i < s->goals->len; i++) {
        const Goal *g = s->goals->pdata[i];
        if (g->monthly <= 0) continue;
        Cents left = MAX(0, g->target - g->saved);
        if (left == 0) continue;
        Cents done_now = 0;
        for (guint k = 0; k < s->txs->len; k++) {
            const Tx *t = s->txs->pdata[k];
            if (strcmp(t->goal_id, g->id) == 0 && t->paid && day_ym(t->date) == cur)
                done_now += t->kind == KIND_EXPENSE ? t->value : -t->value;
        }
        for (Ym m = cur; m <= ym && left > 0; m++) {
            Cents want = m == cur ? MAX(0, g->monthly - done_now) : g->monthly;
            Cents take = MIN(want, left);
            if (m == ym) total += take;
            left -= take;
        }
    }
    return total;
}

Cents totals_forecast_at(const AppState *s, Day d, Day today) {
    Cents c = future_balance(s, d, today);
    for (Ym m = day_ym(today); ym_last(m) <= d; m++) c -= totals_goals_planned(s, m, today);
    return c;
}

MonthTotals totals_month(const AppState *s, Ym ym, Day today) {
    MonthTotals r = {0};
    Ym cur = day_ym(today);
    r.ym = ym;
    r.when = ym < cur ? WHEN_PAST : ym == cur ? WHEN_CURRENT : WHEN_FUTURE;
    for (guint i = 0; i < s->txs->len; i++) {
        const Tx *t = s->txs->pdata[i];
        if (!t->paid || tx_is_card(t) || !app_account(s, t->account_id) || day_ym(t->date) != ym) continue;
        /* transferência entre contas: sai de uma e entra na outra; não é entrada nem saída do mês */
        if (tx_is_transfer(t)) continue;
        if (tx_is_goal(t)) r.goals_saved += t->kind == KIND_EXPENSE ? t->value : -t->value;
        else if (t->kind == KIND_INCOME) r.realized_in += t->value;
        else r.realized_out += t->value;
    }
    /* pendências: no mês atual, também as atrasadas (é o que o previsto desconta) */
    Day end = ym_last(ym);
    for (guint i = 0; i < s->txs->len; i++) {
        const Tx *t = s->txs->pdata[i];
        if (t->paid || tx_is_card(t)) continue;
        gboolean counts = r.when == WHEN_CURRENT ? t->date <= end : day_ym(t->date) == ym;
        if (!counts) continue;
        if (t->kind == KIND_INCOME) r.to_receive += t->value; else r.to_pay += t->value;
    }
    for (guint i = 0; i < s->cards->len; i++) {
        CardStatus st;
        card_status(s, s->cards->pdata[i], today, &st);
        for (guint j = 0; j < st.invoices->len; j++) {
            const Invoice *v = &g_array_index(st.invoices, Invoice, j);
            if (invoice_open(v) <= 0) continue;
            gboolean counts = r.when == WHEN_CURRENT ? v->due <= end : day_ym(v->due) == ym;
            if (counts) r.to_pay += invoice_open(v);
        }
        card_status_clear(&st);
    }
    if (r.when != WHEN_PAST) {
        g_autoptr(GPtrArray) prev = projection_between(s, ym_first(ym), end, today);
        for (guint i = 0; i < prev->len; i++) {
            const Tx *t = prev->pdata[i];
            if (tx_is_card(t)) continue;
            if (t->kind == KIND_INCOME) r.to_receive += t->value; else r.to_pay += t->value;
        }
    }
    r.goals_planned = totals_goals_planned(s, ym, today);
    r.opening = r.when == WHEN_CURRENT ? current_balance(s)
              : r.when == WHEN_FUTURE ? totals_forecast_at(s, ym_last(ym - 1), today)
              : balance_on(s, ym_last(ym - 1));
    r.closing = r.when == WHEN_PAST ? balance_on(s, end) : r.opening + r.to_receive - r.to_pay - r.goals_planned;
    return r;
}
