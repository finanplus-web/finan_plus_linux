/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Transferência entre contas (tradução de core/Transfers.kt do app Android 1.13; TRANSFERENCIAS.md).
 * Uma transferência são dois lançamentos com o mesmo transfer_id: uma saída (despesa) na conta de origem e
 * uma entrada (receita) na conta de destino, com o mesmo valor e a mesma data, sempre realizados.
 * Cada conta muda de saldo, mas o saldo total não: fica fora de receitas, despesas, limites, relatórios e
 * assistente (tx_is_flow = FALSE).
 */
#pragma once
#include "model.h"

G_BEGIN_DECLS

/* os dois lados: out sai da conta de origem, into entra na conta de destino (ponteiros para dentro do estado) */
typedef struct { Tx *out; Tx *into; } TransferLegs;

/* os dois lados da transferência [transfer_id]; FALSE se não existe ou está incompleta */
gboolean transfer_legs(const AppState *s, const char *transfer_id, TransferLegs *out);
/* Arquivo de backup: um lado sozinho ou um par que não combina deixa de ser transferência (vira receita ou
 * despesa comum, para o valor continuar visível e editável em vez de mexer no saldo escondido). */
void transfers_sanitize(GPtrArray *txs);
/* "Conta principal → Poupança" (nomes atuais; conta que não existe mais vira "Conta"), g_free */
char *transfer_route(const AppState *s, const TransferLegs *l);
/* descrição gravada nos dois lados: o que a pessoa escreveu ou, sem nada, "Transferência"; g_free */
char *transfer_desc(const char *text);

G_END_DECLS
