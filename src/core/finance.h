/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Regras financeiras: faturas de cartão, saldos, recorrências, parcelas, metas, resumos,
 * lembretes e CSV. Mesmas convenções do app Android e do Finan+ web.
 */
#pragma once
#include "model.h"

G_BEGIN_DECLS

typedef struct {
    Ym ym;
    Cents total;
    Cents paid;
    Day close;
    Day due;
    gboolean closed;
} Invoice;
#define invoice_open(i) ((i)->total - (i)->paid)

typedef struct {
    Cents used;
    Cents available;
    Cents credit;
    GArray *invoices; /* Invoice, em ordem de mês */
    /* índice da fatura atual em invoices, ou -1 */
    int current;
} CardStatus;

typedef struct {
    Cents remaining;
    gboolean done;
    /* quanto guardar por mês para chegar no prazo (se has_needed) */
    gboolean has_needed;
    Cents needed;
    /* mês previsto de conclusão pelo plano mensal (YM_NONE se não houver) */
    Ym eta;
    gboolean late;
    gboolean past_due;
} GoalPlan;

typedef struct { Cents income; Cents expense; } Flow;
#define flow_balance(f) ((f).income - (f).expense)

typedef enum { REMINDER_BILL_OVERDUE, REMINDER_BILL_DUE, REMINDER_INCOME_DUE, REMINDER_INVOICE_DUE } ReminderType;
typedef struct {
    ReminderType type;
    char *title;
    Cents amount;
    Day date;
    char *ref_id;
} Reminder;

typedef struct { char *name; Cents value; } CatTotal;

/* ---- cartões e faturas ---- */
Ym invoice_ym(const Card *card, Day date);
Day invoice_due(const Card *card, Ym ym);
Day invoice_close(const Card *card, Ym ym);
void card_status(const AppState *s, const Card *card, Day today, CardStatus *out);
void card_status_clear(CardStatus *st);
const Invoice *card_status_current(const CardStatus *st);

/* ---- saldos ---- */
Cents account_balance(const AppState *s, const Account *a);
Cents current_balance(const AppState *s);
Cents future_balance(const AppState *s, Day until, Day today);

/* ---- recorrências previstas (RECORRENCIAS.md) ----
 * Próximas ocorrências de cada recorrência ativa com data entre from e to, só em meses depois do mês de
 * [today] e do último mês já gerado, nunca antes do início. Não são gravadas: o lançamento real continua
 * sendo criado quando o mês chega. id "prev:<recorrência>:<AAAA-MM>". GPtrArray de Tx* (donos), por data. */
#define PROJECTED_PREFIX "prev:"
GPtrArray *projection_between(const AppState *s, Day from, Day to, Day today);
#define tx_is_projected(t) (g_str_has_prefix((t)->id, PROJECTED_PREFIX))

/* ---- recorrências: gera até o mês de [today]; devolve quantos lançamentos foram criados ---- */
int generate_recurring(AppState *s, Day today);

/* ---- parcelas: divide [total] em [n]; a diferença de centavos fica na primeira ---- */
void split_installments(Cents total, int n, Cents *out);

/* ---- metas ---- */
GoalPlan goal_plan(const Goal *g, Day today);

/* ---- resumos ---- */
/* só lançamentos realizados que não são pagamento de fatura */
Flow flow_of(GPtrArray *txs);
Flow month_flow(const AppState *s, Ym ym);
/* despesas do mês da categoria, incluindo pendentes (para limites) */
Cents budget_usage(const AppState *s, Ym ym, const char *category);
/* despesas realizadas por categoria no período (from/to podem ser DAY_NONE), decrescente.
 * Devolve CatTotal* com free func. */
GPtrArray *category_totals(const AppState *s, Day from, Day to);
void cat_total_free(CatTotal *c);

/* ---- lembretes (notificações) ---- */
/* Contas pendentes atrasadas ou que vencem em até [days] dias, receitas a receber e faturas a vencer.
 * Devolve Reminder* com free func, ordenado por data. */
GPtrArray *reminders(const AppState *s, Day today, int days);
/* Próximo compromisso (conta pendente ou fatura em aberto); NULL se não houver. reminder_free. */
Reminder *next_due(const AppState *s, Day today);
void reminder_free(Reminder *r);

/* ---- CSV (separador ";", BOM UTF-8, proteção contra fórmulas) ---- */
char *csv_cell(const char *v);
char *csv_build(const AppState *s);

/* ordena lançamentos por data (estável) */
void txs_sort_by_date(GPtrArray *a);

G_END_DECLS
