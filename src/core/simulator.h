/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Simulador "E se…?": contas para testar decisões sem mudar nenhum dado (nada aqui grava no estado).
 * Tradução de Simulator.kt do app Android 1.4.0 e de js/simulator.js do Finan+ web 1.3.0, com os
 * mesmos casos de teste. Detalhes e exemplos em SIMULADOR.md.
 */
#pragma once
#include "model.h"

G_BEGIN_DECLS

/* meses olhados para trás na base */
#define SIM_BASE_MONTHS 3

/* O que entra, sai e sobra num mês típico. months = quantos meses com dados entraram na média (0 = sem histórico). */
typedef struct { Cents income; Cents expense; int months; } SimBase;
#define sim_base_left(b) ((b).income - (b).expense)

/* média dos 3 meses completos antes do mês de [today], só valores realizados (como em Relatórios)
 * e só meses que tiveram algum valor. O mês atual fica de fora porque ainda não terminou. */
SimBase sim_base(const AppState *s, Day today);

/* ---- E se eu economizar… ---- */
typedef struct { Cents per_month; int months; Cents total; Cents new_left; gboolean over_left; } SimSave;
SimSave sim_save(SimBase base, Cents per_month, int months);

/* ---- Quanto tempo para comprar… ---- */
/* months meses guardando, a partir do mês que vem; done = mês em que o valor fica completo */
typedef struct { Cents missing; int months; Ym done; } SimBuy;
/* FALSE quando não dá para calcular (nada a guardar por mês e ainda falta dinheiro) */
gboolean sim_buy(Cents price, Cents have, Cents per_month, Day today, SimBuy *out);

/* ---- E se minha renda mudar… ---- */
typedef struct { Cents new_income; Cents diff; Cents new_left; Cents year_diff; Cents goals_monthly; } SimIncome;
/* renda muda [percent]% (ex.: −15); [goals_monthly] = contribuições mensais planejadas das metas em andamento */
SimIncome sim_income(SimBase base, double percent, Cents goals_monthly);
/* soma do que as metas não concluídas pedem por mês */
Cents sim_goals_monthly(const AppState *s);

/* ---- E se eu antecipar uma dívida… ---- */
typedef struct {
    char *group_id;
    char *name;
    Cents parcel;
    int remaining;
    int total;
    Cents left;
    gboolean card;
} SimDebt;
void sim_debt_free(SimDebt *d);
/* Parcelamentos com parcelas a pagar: na conta, as pendentes; no cartão, as que ainda não chegaram
 * (data depois de hoje). Do maior para o menor saldo. GPtrArray de SimDebt* (com free func). */
GPtrArray *sim_debts(const AppState *s, Day today);

typedef struct { Cents pay_now; Cents saved; Cents freed_per_month; int months; Cents balance_after; } SimPayoff;
/* Quitar hoje por [pay_now] (o valor que o credor oferece). O app não conhece os juros: a economia
 * é só a diferença entre o que falta e o valor oferecido. */
SimPayoff sim_payoff(const SimDebt *d, Cents pay_now, Cents balance);

G_END_DECLS
