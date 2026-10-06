/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Dados do relatório em PDF de um período. Só cálculo, sem interface.
 *
 * Convenções (as mesmas dos Relatórios do app):
 * - receitas/despesas = lançamentos realizados (pagos/recebidos) no período;
 * - pagamento de fatura não é despesa nova (as compras no cartão já contam na data da compra);
 * - pendentes (a receber / a pagar) aparecem à parte.
 */
#pragma once
#include "model.h"

G_BEGIN_DECLS

#define REPORT_TOP 10
#define REPORT_MAX_CHART_MONTHS 24

typedef struct {
    char *name;
    Cents value;
    double percent;
    int count;
    /* valor médio por mês no período */
    Cents monthly_average;
    /* limite mensal definido pelo usuário (só despesas) */
    gboolean has_limit;
    Cents monthly_limit;
} CategoryRow;
#define category_over_limit(r) ((r)->has_limit && (r)->monthly_average > (r)->monthly_limit)

typedef struct { Ym ym; Cents income, expense; } MonthRow;
typedef struct { char *name; Cents balance; } AccountRow;
typedef struct { char *name; Cents saved, target; Day deadline; } GoalRow;

typedef struct {
    Day from, to;
    int days;
    /* nº de meses do período: meses inteiros contam 1; meses parciais, a fração de dias */
    double month_span;
    Cents income, expense, pending_income, pending_expense;
    Day prev_from, prev_to;
    Cents prev_income, prev_expense;
    GPtrArray *expense_by_category; /* CategoryRow* */
    GPtrArray *income_by_category;  /* CategoryRow* */
    GArray *months;                 /* MonthRow */
    GPtrArray *top_expenses;        /* Tx* (do estado) */
    /* todos os lançamentos do período, por data (inclui pendentes e pagamentos de fatura) */
    GPtrArray *txs;                 /* Tx* (do estado) */
    GArray *accounts;               /* AccountRow */
    GArray *goals;                  /* GoalRow */
} Report;

#define report_balance(r) ((r)->income - (r)->expense)
/* % das receitas que sobrou; FALSE sem receitas */
gboolean report_savings_rate(const Report *r, double *out);
Cents report_daily_average(const Report *r);
/* variação percentual; FALSE se não houver base */
gboolean report_change(Cents cur, Cents prev, double *out);
double goal_row_percent(const GoalRow *g);

/* NULL se to < from. Os ponteiros Tx* apontam para dentro de [s]: use o relatório antes de alterar o estado. */
Report *report_build(const AppState *s, Day from, Day to, Day today);
void report_free(Report *r);

/* valor curto para eixos: "R$ 950", "R$ 1,2 mil", "R$ 3,4 mi" (g_free) */
char *report_compact(Cents c);
/* passo "bonito" para o eixo do gráfico */
Cents report_nice_step(Cents max, int ticks);
/* "relatorio-finan-plus-2026-10-01-a-2026-10-31.pdf" (g_free) */
char *report_file_name(Day from, Day to);
/* atalhos: "mes", "anterior", "ano", "12m", "tudo" */
void report_preset(const char *key, Day today, Day first, Day last, Day *from, Day *to);

G_END_DECLS
