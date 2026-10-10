/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Simulador "E se…?": contas para testar decisões sem mudar nenhum dado (nada aqui grava no estado).
 * Mesmas regras e testes do app Android (core/Simulator.kt) e do Finan+ web (js/simulator.js).
 * Detalhes em SIMULADOR.md.
 */
#pragma once
#include "model.h"

G_BEGIN_DECLS

#define SIM_BASE_MONTHS 3

/* mês típico: o que entra e sai; months = meses com dados na média (0 = sem histórico) */
typedef struct { Cents income; Cents expense; int months; } SimBase;
#define sim_left(b) ((b).income - (b).expense)

/* média dos 3 meses completos antes do mês de hoje, só realizados, só meses com algum valor */
SimBase sim_base(const AppState *s, Day today);

typedef struct { Cents per_month; int months; Cents total; Cents new_left; gboolean over_left; } SimSave;
SimSave sim_save(SimBase b, Cents per_month, int months);

/* meses até juntar, começando no mês que vem; done_ym = mês em que completa.
 * FALSE quando não dá para calcular (nada a guardar por mês e ainda falta dinheiro) */
typedef struct { Cents missing; int months; Ym done_ym; } SimBuy;
gboolean sim_buy(Cents price, Cents have, Cents per_month, Day today, SimBuy *out);

typedef struct { Cents new_income; Cents diff; Cents new_left; Cents year_diff; Cents goals_monthly; } SimIncome;
SimIncome sim_income(SimBase b, double percent, Cents goals_monthly);
/* soma das contribuições mensais das metas não concluídas */
Cents sim_goals_monthly(const AppState *s);

/* parcelamento com parcelas a pagar */
typedef struct { char *group_id; char *name; Cents parcel; int remaining; int total; Cents left; gboolean card; } SimDebt;
/* na conta, as parcelas pendentes; no cartão, as com data depois de hoje. Maior saldo primeiro.
 * GPtrArray de SimDebt* com função de liberar */
GPtrArray *sim_debts(const AppState *s, Day today);

typedef struct { Cents pay_now; Cents saved; Cents freed_per_month; int months; Cents balance_after; } SimPayoff;
/* o app não conhece os juros: a economia é a diferença entre o que falta e o valor oferecido */
SimPayoff sim_payoff(const SimDebt *d, Cents pay_now, Cents balance);

G_END_DECLS
