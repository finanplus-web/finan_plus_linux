/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Backup criptografado com senha (o formato do backup automático do app Android 1.7, core/BackupCrypto.kt;
 * BACKUP-AUTOMATICO.md). Formato aberto, lido aqui com o OpenSSL:
 *
 *   {"format":"finanplus-backup-cifrado","v":1,"kdf":"PBKDF2-HMAC-SHA256","iter":310000,
 *    "salt":"<16 bytes, base64>","cipher":"AES-256-GCM","iv":"<12 bytes, base64>","data":"<texto cifrado + tag, base64>"}
 *
 * Dentro de "data" está o backup JSON normal. Uma cópia feita no Android abre no Linux e vice-versa.
 */
#pragma once
#include <glib.h>

G_BEGIN_DECLS

#define BACKUP_CRYPTO_FORMAT "finanplus-backup-cifrado"
#define BACKUP_CRYPTO_ITER 310000
#define BACKUP_CRYPTO_MIN_PASSWORD 8

/* o texto é um backup criptografado do Finan+ (só olha o envelope; não precisa da senha) */
gboolean backup_crypto_is_encrypted(const char *text, gssize len);
/* envelope JSON (g_free); [iter] 0 = BACKUP_CRYPTO_ITER */
char *backup_crypto_encrypt(const char *plain, const char *password, int iter, GError **error);
/* o backup JSON de dentro (g_free); erro "Senha incorreta ou arquivo danificado", "Arquivo danificado"… */
char *backup_crypto_decrypt(const char *text, gssize len, const char *password, GError **error);

G_END_DECLS
