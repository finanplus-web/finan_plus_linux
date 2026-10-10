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
    /* Bloqueio automático (só neste computador; restaurar um backup não muda a segurança):
     * AUTOLOCK_IMMEDIATE, minutos sem usar (1, 5, 15, 30) ou AUTOLOCK_ON_OPEN. */
    int auto_lock;
    /* FALSE até a migração da 1.2.0 (o valor antigo vinha dos dados, que vão para o backup) */
    gboolean auto_lock_set;
    /* tentativas de PIN erradas seguidas, gravadas para que fechar o app não zere a espera */
    int pin_fails;
    gint64 pin_fail_wall;  /* g_get_real_time() da última tentativa */
    gint64 pin_fail_mono;  /* g_get_monotonic_time() da última tentativa */
    char *pin_fail_boot;   /* boot_id do Linux na última tentativa ("" = desconhecido) */
    char *path;
} Prefs;

#define AUTOLOCK_ON_OPEN (-1)
#define AUTOLOCK_IMMEDIATE 0
/* opções na ordem do seletor de Ajustes */
extern const int DEVICE_AUTOLOCK_OPTIONS[6];
extern const char *const DEVICE_AUTOLOCK_LABELS[6];

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

/* Limite de tentativas, como no app Android 1.1.1: depois de 5 PINs errados seguidos, cada nova
 * tentativa espera 30 s, 1 min, 2 min… (dobrando, até 1 hora). A contagem fica gravada em aparelho.ini:
 * fechar o app ou reiniciar o computador não zera a espera. A tentativa é contada ANTES da conferência
 * (fechar o app no meio da verificação não escapa da contagem); acertar zera. */
#define PIN_FREE_TRIES 5
#define PIN_MAX_WAIT_S 3600
/* espera depois de [fails] erros seguidos (segundos) */
int pin_delay_seconds(int fails);
/* segundos que ainda faltam para poder tentar. Mesmo boot: relógio monotônico (mudar a hora do
 * computador não adianta); outro boot: relógio de parede. Nunca mais que a espera prevista. */
int pin_wait_seconds(const Prefs *p);
int pin_wait_seconds_at(const Prefs *p, gint64 now_wall, gint64 now_mono, const char *boot);
/* conta uma tentativa (antes de conferir) e grava */
void pin_attempt(Prefs *p);
void pin_attempt_at(Prefs *p, gint64 now_wall, gint64 now_mono, const char *boot);
/* PIN certo, PIN removido ou "Apagar tudo": zera a contagem e grava */
void pin_reset_fails(Prefs *p);
/* boot_id do Linux (g_free; "" se não houver) */
char *linux_boot_id(void);

G_END_DECLS
