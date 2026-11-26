/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Glossário único de valores (tradução de core/Totals.kt do app Android 1.11; REDESIGN-COERENCIA.md): Início,
 * Lançamentos (lista e calendário) e Resumo leem estes números, para que o mesmo valor tenha o mesmo nome e a
 * mesma conta em qualquer tela.
 *
 * - Realizado: o que já entrou ou saiu das contas (pago ou recebido). Compras no cartão não entram (o que sai é o
 *   pagamento da fatura). Reservas para meta ficam à parte ("guardado em metas"), nunca como despesa.
 * - A receber: receitas pendentes fora do cartão (no mês atual, também as atrasadas) e recorrências previstas.
 * - A pagar: despesas pendentes fora do cartão (no mês atual, também as atrasadas), faturas abertas que vencem
 *   no mês e recorrências previstas. Compras no cartão entram pela fatura.
 * - Reservado para metas: aportes mensais planejados que ainda não foram feitos no mês. O que já foi guardado
 *   já saiu do saldo e não é descontado de novo.
 * - Transferência entre contas: muda o saldo de cada conta, mas não entra no realizado nem nas pendências.
 * - Saldo atual: saldo das contas hoje (current_balance).
 * - Previsto no fim do mês: saldo atual + a receber − a pagar − reservado para metas.
 */
#pragma once
#include "model.h"

G_BEGIN_DECLS

typedef enum { WHEN_PAST, WHEN_CURRENT, WHEN_FUTURE } MonthWhen;

typedef struct {
    Ym ym;
    MonthWhen when;
    /* saldo no começo da conta: saldo atual (mês atual), previsto no fim do mês anterior (futuro),
     * saldo no fim do mês anterior (passado) */
    Cents opening;
    Cents realized_in;
    Cents realized_out;
    /* guardado em metas − retirado das metas no mês (já fora do saldo) */
    Cents goals_saved;
    Cents to_receive;
    Cents to_pay;
    Cents goals_planned;
    /* previsto no fim do mês (atual e futuro) ou saldo no fim do mês (passado) */
    Cents closing;
} MonthTotals;
/* resultado do mês só com o realizado (visão isolada): entrou − saiu − guardado */
#define totals_result(m) ((m)->realized_in - (m)->realized_out - (m)->goals_saved)
/* resultado do mês com o que falta (visão isolada) */
#define totals_result_forecast(m) (totals_result(m) + (m)->to_receive - (m)->to_pay - (m)->goals_planned)

/* aportes planejados (Goal.monthly) ainda não feitos no mês [ym]; 0 em meses passados; limitado ao que falta */
Cents totals_goals_planned(const AppState *s, Ym ym, Day today);
/* previsto ao fim do dia [d]: future_balance − aportes planejados dos meses já encerrados até [d]
 * (contam no último dia do mês) */
Cents totals_forecast_at(const AppState *s, Day d, Day today);
MonthTotals totals_month(const AppState *s, Ym ym, Day today);

G_END_DECLS
