/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Regras da tela de lançamento que não dependem da interface (tradução de core/TxForm.kt do app Android 1.10/1.11):
 * categorias × formas de pagamento, migração das categorias, recentes, sugestões de descrição e o valor digitado.
 */
#pragma once
#include "model.h"

G_BEGIN_DECLS

#define LEGACY_CARD_CATEGORY "Cartão de Crédito"

/* "Cartão de Crédito", "Cartão", "Limite Especial" e "Cheque especial" eram categorias; hoje cartão é forma de
 * pagamento ("Pagar com") e limite especial é da conta (REDESIGN-COERENCIA.md) */
gboolean txform_is_payment_like(const char *c);
/* nome mostrado: lançamento antigo salvo com uma dessas categorias aparece como "Outros" (o dado não muda) */
const char *txform_display_category(const char *c);
/* categorias escolhíveis: as do tipo, sem as formas de pagamento antigas (char* do estado, sem dono) */
GPtrArray *txform_choices(const AppState *s, Kind k);
/* Migração das categorias (uma vez por computador, ao abrir): as formas de pagamento antigas saem da lista de
 * despesas; "Empréstimo" (e parecidos) vira "Dívidas" — lançamentos, recorrências e limites acompanham.
 * Devolve TRUE se mudou algo. */
gboolean txform_migrate(AppState *s);
/* ao trocar Receita/Despesa: a categoria continua se existir no novo tipo (sem diferenciar maiúsculas e
 * acentos); senão "" (g_free) */
char *txform_category_after_kind_change(const char *current, const AppState *s, Kind k);
/* "Usadas recentemente": até [n] categorias do tipo, da mais recente para a menos (data; empate: o gravado
 * depois). Só as que existem; reservas, transferências e pagamentos de fatura não contam. g_strfreev */
char **txform_recent_categories(const AppState *s, Kind k, int n);
/* sugestões de descrição: as mais usadas do tipo (empate: a mais recente), sem a que é igual à categoria. g_strfreev */
char **txform_description_suggestions(const AppState *s, Kind k, int n);

/* valor digitado → texto formatado pt-BR enquanto digita: "1234,5" → "1.234,5"; "12." → "12,"; "" → "" (g_free) */
char *amount_input_format(const char *raw);

G_END_DECLS
