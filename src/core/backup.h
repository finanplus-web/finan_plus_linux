/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Leitura e escrita do formato JSON do Finan+ (o mesmo do backup do Finan+ web e do app
 * Android, versões 4 e 5). Toda entrada externa é validada: tipos, datas, valores, ids e
 * referências; itens inválidos são descartados e contados.
 */
#pragma once
#include "model.h"

G_BEGIN_DECLS

#define BACKUP_VERSION 5
/* tamanho máximo aceito para um arquivo de backup */
#define BACKUP_MAX_BYTES (30 * 1024 * 1024)

typedef struct { int txs, goals, accounts, cards, recurring; } Dropped;
#define dropped_total(d) ((d).txs + (d).goals + (d).accounts + (d).cards + (d).recurring)

#define BACKUP_ERROR (backup_error_quark())
GQuark backup_error_quark(void);

/* NULL + error se o texto não for JSON válido ou não tiver a lista de lançamentos */
AppState *backup_parse(const char *text, gssize len, Dropped *dropped, GError **error);

/* Serializa no formato do Finan+ web (valores em reais). Com [app_version] != NULL inclui
 * o bloco "_backup" usado nos arquivos exportados. */
char *backup_to_json(const AppState *s, const char *app_version);

G_END_DECLS
