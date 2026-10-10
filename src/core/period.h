/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Período da aba Lançamentos (‹ mês ›), calendário do mês e comparação dos Relatórios.
 * Mesmas regras e testes do app Android (core/Period.kt, core/MonthCalendar.kt) e do Finan+ web.
 * Detalhes em CALENDARIO.md e SIMULADOR.md.
 */
#pragma once
#include "model.h"
#include "finance.h"

G_BEGIN_DECLS

/* ---------------------------------------------------------------- período */
/* o que falta entrar e sair (pendente) */
typedef struct { Cents to_receive; Cents to_pay; } Pending;

/* o período é exatamente um mês inteiro? Devolve o mês ou YM_NONE */
Ym period_full_month(Day from, Day to);
/* "Outubro de 2026", "Todo o período", "01/10/2026 a 15/10/2026", "Desde …", "Até …" (g_free) */
char *period_label(Day from, Day to);
/* setas ‹ ›: anda um mês inteiro; período livre vai para o mês vizinho de onde começa (ou de hoje) */
void period_shift(Day from, Day to, int delta, Day today, Day *out_from, Day *out_to);
/* pendências do mês: a receber e a pagar fora do cartão + faturas em aberto que vencem no mês */
Pending period_month_pending(const AppState *s, Ym ym, Day today);
/* pendências de uma lista já filtrada (Tx*), fora do cartão */
Pending period_pending(GPtrArray *txs);
/* saldo de um dia: entra menos sai das contas (realizado ou pendente); compras no cartão de fora */
Cents period_cash_net(GPtrArray *txs);

/* ---------------------------------------------------------------- comparação dos Relatórios */
typedef struct { Day from; Day to; char label[48]; } Compare;
/* mês atual inteiro → mês anterior até o mesmo dia; outro mês → anterior inteiro;
 * período livre → mesmo tamanho logo antes; sem início ou fim → FALSE */
gboolean period_compare(Day from, Day to, Day today, Compare *out);
/* "+12% vs. …", "−9% vs. …", "0% vs. …"; sem valor na referência: "Sem base para comparar" (g_free) */
char *period_compare_text(Cents cur, Cents prev, const Compare *c);

/* ---------------------------------------------------------------- calendário */
enum { MARK_INCOME = 1, MARK_EXPENSE = 2, MARK_CARD = 4 };

/* fatura em aberto mostrada no dia do vencimento */
typedef struct { char *card_id; char *card_name; Cents amount; Day due; gboolean overdue; } InvoiceDue;

typedef struct {
    Day date;
    GPtrArray *txs;      /* Tx* do estado (não são donos); receitas primeiro, depois conta, depois cartão */
    GArray *invoices;    /* InvoiceDue */
    Cents income;        /* dinheiro que entra nas contas no dia */
    Cents expense;       /* dinheiro que sai (inclui pagamento de fatura e fatura no vencimento) */
    int marks;           /* MARK_* */
    gboolean overdue;    /* conta pendente no passado ou fatura vencida em aberto */
} CalDay;
#define cal_day_net(d) ((d)->income - (d)->expense)
#define cal_day_count(d) ((d)->txs->len + (d)->invoices->len)

typedef struct { Ym ym; int len; CalDay days[31]; } CalMonth;

/* faturas em aberto que vencem no mês (GArray de InvoiceDue; liberar com cal_invoices_free) */
GArray *cal_invoices_due(const AppState *s, Ym ym, Day today);
void cal_invoices_free(GArray *a);
CalMonth *cal_month_build(const AppState *s, Ym ym, Day today);
void cal_month_free(CalMonth *m);
/* dia do mês com algo, ou NULL */
const CalDay *cal_month_day(const CalMonth *m, Day d);
void cal_month_totals(const CalMonth *m, Cents *income, Cents *expense);
/* grade do mês começando no domingo, semanas completas; DAY_NONE nas sobras. out ≥ 42; devolve o tamanho */
int cal_cells(Ym ym, Day *out);
/* valor curto sem "R$": 182 · 1,5 mil · 15 mil · 1,2 mi. buf ≥ 32 */
char *cal_compact(Cents c, char *buf);
/* com sinal: "+5,2 mil", "−120", "0". buf ≥ 32 */
char *cal_signed(Cents c, char *buf);
/* "Outubro de 2026" (g_free) */
char *cal_month_title(Ym ym);
/* "Quinta, 15 de outubro" (ano só se diferente do de hoje) (g_free) */
char *cal_day_title(Day d, Day today);
/* frase para leitores de tela (g_free) */
char *cal_describe(Day d, const CalDay *day, Day today, gboolean hide);

G_END_DECLS
