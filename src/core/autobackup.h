/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Backup automático (como no app Android 1.7, data/AutoBackup.kt; BACKUP-AUTOMATICO.md): uma cópia por dia,
 * criptografada com senha (backup_crypto.h), numa pasta escolhida (do computador, de um pendrive, ou sincronizada
 * pelo Syncthing, Nextcloud, Google Drive…).
 *
 * - Arquivo "finan-plus-auto-AAAA-MM-DD.json" (um por dia; no mesmo dia, o novo substitui o anterior).
 * - Guarda as últimas [keep] cópias e apaga só arquivos com esse nome; o resto da pasta não é tocado.
 * - Sem lançamentos (app vazio), grava a cópia do dia mas não apaga as anteriores.
 * - Pasta, senha e opções pertencem a ESTE computador: não vão para o backup.
 */
#pragma once
#include "model.h"

G_BEGIN_DECLS

#define AUTOBACKUP_PREFIX "finan-plus-auto-"
#define AUTOBACKUP_DEFAULT_KEEP 14
extern const int AUTOBACKUP_KEEP_OPTIONS[4];

/* "finan-plus-auto-2026-11-26.json" é um nome de cópia automática? */
gboolean autobackup_is_name(const char *name);
/* grava a cópia de [today] em [folder] e apaga as mais antigas além de [keep]; devolve o nome gravado (g_free) */
char *autobackup_write(const AppState *s, const char *folder, const char *password, int keep, Day today,
                       const char *app_version, GError **error);

G_END_DECLS
