/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Armazenamento local criptografado.
 *
 * - Os dados ficam em ~/.local/share/finan-plus/dados.fin (ou $XDG_DATA_HOME), cifrados com
 *   XChaCha20-Poly1305 (libsodium) e uma chave aleatória de 256 bits.
 * - A chave fica no chaveiro do sistema (Secret Service: GNOME Keyring, KWallet…), via libsecret.
 *   Se não houver chaveiro disponível, ela é guardada em um arquivo legível só pelo seu usuário
 *   (permissão 0600) e o app avisa isso em Ajustes.
 * - A gravação é atômica (arquivo temporário + fsync + rename): uma queda de energia não deixa
 *   o arquivo pela metade. A versão anterior fica em dados.fin.anterior.
 *
 * Configurações deste computador (PIN, avisos, assistente) ficam à parte em prefs.c e nunca vão
 * para o backup.
 */
#pragma once
#include "model.h"
#include "backup.h"

G_BEGIN_DECLS

typedef enum { KEY_SOURCE_KEYRING, KEY_SOURCE_FILE } KeySource;

typedef struct Store Store;

#define STORE_ERROR (store_error_quark())
GQuark store_error_quark(void);
typedef enum {
    STORE_ERROR_NO_CRYPTO,
    STORE_ERROR_IO,
    /* o arquivo existe, mas não pode ser aberto com a chave atual (chave perdida ou arquivo danificado) */
    STORE_ERROR_UNREADABLE,
} StoreErrorCode;

/* [dir] NULL = pasta padrão. Obtém (ou cria) a chave. */
Store *store_open(const char *dir, GError **error);
void store_free(Store *st);

KeySource store_key_source(const Store *st);
const char *store_dir(const Store *st);
const char *store_data_path(const Store *st);

/* NULL sem erro = ainda não há dados (primeiro uso). NULL com erro = problema para mostrar ao usuário. */
AppState *store_load(Store *st, Dropped *dropped, GError **error);
gboolean store_save(Store *st, const AppState *s, GError **error);

/* Move o arquivo ilegível para "dados.fin.ilegivel-AAAAMMDD-HHMMSS" (nada é apagado). Devolve o novo caminho. */
char *store_quarantine(Store *st, GError **error);

/* Apaga os dados e a chave (Ajustes › Dados › Apagar tudo). */
gboolean store_wipe(Store *st, GError **error);

/* Cifra/decifra um bloco com a chave do Store (usado também pelos testes). */
GBytes *store_seal(Store *st, const char *plain, gsize len);
char *store_open_sealed(Store *st, const guint8 *data, gsize len, gsize *out_len);

G_END_DECLS
