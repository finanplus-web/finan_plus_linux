/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Período da aba Lançamentos (‹ mês ›), pendências, saldo do dia, comparação dos Relatórios e
 * as regras do calendário de lançamentos. Só cálculo, sem interface.
 * Tradução de Period.kt e MonthCalendar.kt do app Android 1.4.0 (e de js/calendar.js e
 * js/simulator.js do Finan+ web 1.3.0), com os mesmos testes. Detalhes em CALENDARIO.md.
 *
 * Datas "abertas" usam DAY_NONE (sem início ou sem fim).
 */
#pragma once
#include "model.h"

G_BEGIN_DECLS

/* ================================================================ período */

/* O que falta entrar e sair (o que ainda está pendente). */
typedef struct { Cents to_receive; Cents to_pay; } Pending;

/* O período é exatamente um mês inteiro (do dia 1 ao último)? Se for, devolve o mês em [out]. */
gboolean period_full_month(Day from, Day to, Ym *out);

/* "Outubro de 2026", "Todo o período", "01/10/2026 a 15/10/2026", "Desde 01/10/2026",
 * "Até 15/10/2026" ou "08/10/2026" (g_free) */
char *period_label(Day from, Day to);

/* Setas ‹ ›: anda um mês inteiro. Período que não é um mês inteiro vai para o mês vizinho de onde
 * ele começa (ou de hoje, se não tem início nem fim). */
void period_shift(Day from, Day to, int delta, Day today, Day *out_from, Day *out_to);

/* Pendências do mês: receitas a receber e contas a pagar fora do cartão, mais as faturas em aberto
 * que vencem no mês (compras no cartão são pagas pela fatura, não contam de novo). */
Pending period_month_pending(const AppState *s, Ym ym, Day today);

/* Pendências de uma lista já filtrada (Tx*), fora do cartão e sem pagamentos de fatura. */
Pending period_pending(GPtrArray *txs);

/* Saldo de um dia: entra menos sai das contas, realizado ou pendente. Compras no cartão ficam de
 * fora (contam pela fatura); pagamento de fatura conta como saída. Mesma regra do calendário. */
Cents period_cash_net(GPtrArray *txs);

/* ================================================================ comparação dos Relatórios */

typedef struct {
    Day from;
    Day to;
    /* "vs. set (mesmos dias)", "vs. agosto", "vs. período anterior" */
    char label[48];
} Compare;

/* - mês atual inteiro: o mês anterior até o mesmo dia;
 * - outro mês inteiro: o mês anterior inteiro;
 * - período livre com início e fim: o período do mesmo tamanho logo antes;
 * - sem início ou sem fim: FALSE (sem comparação). */
gboolean period_compare(Day from, Day to, Day today, Compare *out);

/* "+12% vs. …", "−9% vs. …", "0% vs. …"; sem valor na referência: "Sem base para comparar" (g_free) */
char *period_compare_text(Cents cur, Cents prev, const Compare *c);

/* ================================================================ calendário */

typedef enum { MARK_INCOME = 1 << 0, MARK_EXPENSE = 1 << 1, MARK_CARD = 1 << 2 } DayMark;

/* Fatura em aberto de um cartão, mostrada no dia do vencimento. Os textos apontam para o estado. */
typedef struct {
    const char *card_id;
    const char *card_name;
    Cents amount;
    Day due;
    gboolean overdue;
} InvoiceDue;

/* Um dia do calendário.
 * income/expense = dinheiro que entra/sai das contas nesse dia (realizado ou pendente): receitas e
 * despesas fora do cartão, pagamentos de fatura e faturas em aberto no vencimento. Compras no
 * cartão aparecem em txs (e marcam MARK_CARD), mas não entram na soma. */
typedef struct {
    Day date;
    GPtrArray *txs;     /* Tx* do estado: receitas primeiro, depois fora do cartão, depois cartão */
    GArray *invoices;   /* InvoiceDue */
    Cents income;
    Cents expense;
    int marks;          /* DayMark */
    /* conta pendente com data passada ou fatura vencida em aberto */
    gboolean overdue;
} CalDay;
#define cal_day_net(d) ((d)->income - (d)->expense)
#define cal_day_count(d) ((int)(d)->txs->len + (int)(d)->invoices->len)

/* Mês montado: só os dias que têm algo (os outros ficam NULL). Os Tx* apontam para o estado:
 * use antes de alterar o estado. */
typedef struct {
    Ym ym;
    CalDay *days[31];
    GPtrArray *projected; /* recorrências previstas do mês (donas dos Tx* que aparecem nos dias) */
} CalMonth;

/* Faturas em aberto (de todos os cartões) que vencem no mês. GArray de InvoiceDue. */
GArray *cal_invoices_due(const AppState *s, Ym ym, Day today);
CalMonth *cal_build(const AppState *s, Ym ym, Day today);
/* dia do mês montado, ou NULL se não tem nada (ou é de outro mês) */
const CalDay *cal_get(const CalMonth *m, Day d);
void cal_month_free(CalMonth *m);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(CalMonth, cal_month_free)
/* totais do mês: a soma de todos os dias */
void cal_totals(const CalMonth *m, Cents *income, Cents *expense);

/* Grade do mês, semana começando no domingo: DAY_NONE antes do dia 1 e no fim, sempre em semanas
 * completas de 7. [out] precisa de 42 posições; devolve quantas foram usadas (28, 35 ou 42). */
int cal_cells(Ym ym, Day out[42]);
extern const char *const CAL_WEEK_HEADER[7];

/* Valor curto para caber no quadradinho do dia, sem "R$": 182 · 1,5 mil · 15 mil · 1,2 mi.
 * R$ 119,90 vira 120; R$ 999,99 vira 1 mil. (g_free) */
char *cal_compact(Cents c);
/* com sinal: "+5,2 mil", "−120"; zero fica "0" (g_free) */
char *cal_signed(Cents c);

/* "Outubro de 2026" (g_free) */
char *cal_month_title(Ym ym);
/* "Quinta, 15 de outubro" (ano só quando não é o de [today]) (g_free) */
char *cal_day_title(Day d, Day today);
/* texto do leitor de tela de um dia; com [hide] não fala valores (g_free) */
char *cal_describe(Day d, const CalDay *day, Day today, gboolean hide);

G_END_DECLS
