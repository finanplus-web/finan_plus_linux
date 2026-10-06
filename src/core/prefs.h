/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Configurações que pertencem a ESTE computador e nunca vão para o backup:
 * PIN, avisos de vencimento, opções do assistente, dicas dispensadas e tamanho da janela.
 * Ficam em ~/.config/finan-plus/aparelho.ini (permissão 0600).
 *
 * O PIN é guardado só como hash Argon2id (libsodium, crypto_pwhash_str), com sal aleatório.
 */
#pragma once
#include <glib.h>

G_BEGIN_DECLS

typedef struct {
    char *pin_hash;           /* "" = sem PIN */
    gboolean notifications;   /* avisos de vencimento */
    gboolean notify_on_login; /* também ao entrar na sessão (arquivo de início automático) */
    gboolean assist_category;
    gboolean assist_tips;
    gboolean assist_ask;
    GPtrArray *dismissed_tips; /* char* */
    int window_width;
    int window_height;
    gboolean window_maximized;
    char *last_notified;       /* "AAAA-MM-DD" do último aviso, para não repetir no mesmo dia */
    char *path;
} Prefs;

Prefs *prefs_load(const char *dir);
gboolean prefs_save(Prefs *p, GError **error);
void prefs_free(Prefs *p);
#define prefs_has_pin(p) ((p)->pin_hash && (p)->pin_hash[0])
gboolean prefs_tip_dismissed(const Prefs *p, const char *id);
void prefs_dismiss_tip(Prefs *p, const char *id);
void prefs_restore_tip(Prefs *p, const char *id);

/* PIN: 4 a 8 números */
gboolean pin_valid_format(const char *pin);
/* hash Argon2id (lento de propósito: ~0,1–0,5 s). g_free; NULL se faltar memória. */
char *pin_hash(const char *pin);
gboolean pin_verify(const char *pin, const char *hash);

/* Espera crescente após 5 erros seguidos (30 s, 60 s, 90 s…), como no app Android. */
typedef struct { int fails; gint64 wait_until; } PinThrottle;
int pin_throttle_wait_seconds(const PinThrottle *t);
void pin_throttle_fail(PinThrottle *t);
void pin_throttle_reset(PinThrottle *t);

G_END_DECLS
