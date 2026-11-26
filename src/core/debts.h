/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Dívidas (tradução de core/Debts.kt do app Android 1.11; Meu dinheiro › Dívidas): empréstimos e financiamentos
 * são lançamentos da categoria "Dívidas" (o antigo "Empréstimo"), normalmente em parcelas. Nada novo é gravado:
 * as dívidas são montadas a partir dos lançamentos que já existem, agrupando as parcelas do mesmo parcelamento
 * (ou a mesma descrição).
 */
#pragma once
#include "model.h"

G_BEGIN_DECLS

#define DEBTS_CATEGORY "Dívidas"

typedef struct {
    char *name;
    GPtrArray *parcels;   /* Tx* do estado, por data (sem dono) */
    /* próxima parcela a pagar (a mais antiga pendente), ou NULL */
    const Tx *next;
    int pending_count;
    Cents pending_total;
    int paid_count;
    /* total de parcelas quando o lançamento diz (ex.: 3/10), senão o número de lançamentos */
    int total_count;
} Debt;
#define debt_done(d) ((d)->pending_count == 0)

gboolean debts_is_category(const char *c);
/* "Notebook (3/10)" → "Notebook" (g_free) */
char *debts_base_name(const char *desc);
/* abertas primeiro, pela próxima parcela; depois por nome. Debt* com free func; use antes de alterar o estado. */
GPtrArray *debts_list(const AppState *s);
void debt_free(Debt *d);
/* "1 dívida · parcela de R$ 121,40 em 13/11"; sem dívidas abertas: NULL (g_free) */
char *debts_summary(const AppState *s, gboolean hide);

G_END_DECLS
