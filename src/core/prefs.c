/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "prefs.h"
#include <glib/gstdio.h>
#include <sodium.h>
#include <string.h>

#define GROUP "aparelho"

const int DEVICE_AUTOLOCK_OPTIONS[6] = {AUTOLOCK_IMMEDIATE, 1, 5, 15, 30, AUTOLOCK_ON_OPEN};
const char *const DEVICE_AUTOLOCK_LABELS[6] = {"Imediatamente", "1 minuto sem usar", "5 minutos sem usar",
                                               "15 minutos sem usar", "30 minutos sem usar", "Só ao abrir o app"};

static gboolean valid_autolock(int v) {
    for (int i = 0; i < 6; i++) if (DEVICE_AUTOLOCK_OPTIONS[i] == v) return TRUE;
    return FALSE;
}
#define MAX_DISMISSED 200

static gboolean get_bool(GKeyFile *k, const char *key, gboolean def) {
    g_autoptr(GError) e = NULL;
    gboolean v = g_key_file_get_boolean(k, GROUP, key, &e);
    return e ? def : v;
}

static int get_int(GKeyFile *k, const char *key, int def) {
    g_autoptr(GError) e = NULL;
    int v = g_key_file_get_integer(k, GROUP, key, &e);
    return e ? def : v;
}

Prefs *prefs_load(const char *dir) {
    Prefs *p = g_new0(Prefs, 1);
    const char *env = g_getenv("FINAN_PLUS_CONFIG_DIR");
    g_autofree char *d = g_strdup(dir ? dir : env && *env ? env : NULL);
    if (!d) d = g_build_filename(g_get_user_config_dir(), "finan-plus", NULL);
    g_mkdir_with_parents(d, 0700);
    p->path = g_build_filename(d, "aparelho.ini", NULL);
    g_autoptr(GKeyFile) k = g_key_file_new();
    g_key_file_load_from_file(k, p->path, G_KEY_FILE_NONE, NULL);
    p->pin_hash = g_key_file_get_string(k, GROUP, "pin", NULL);
    if (!p->pin_hash) p->pin_hash = g_strdup("");
    p->notifications = get_bool(k, "avisos", TRUE);
    p->notify_on_login = get_bool(k, "avisos_ao_entrar", FALSE);
    p->assist_category = get_bool(k, "assistente_categoria", TRUE);
    p->assist_tips = get_bool(k, "assistente_dicas", TRUE);
    p->assist_ask = get_bool(k, "assistente_perguntas", TRUE);
    p->window_width = get_int(k, "janela_largura", 1280);
    p->window_height = get_int(k, "janela_altura", 820);
    p->window_maximized = get_bool(k, "janela_maximizada", FALSE);
    p->last_notified = g_key_file_get_string(k, GROUP, "ultimo_aviso", NULL);
    if (!p->last_notified) p->last_notified = g_strdup("");
    p->auto_lock_set = g_key_file_has_key(k, GROUP, "bloqueio_automatico", NULL);
    p->auto_lock = get_int(k, "bloqueio_automatico", AUTOLOCK_IMMEDIATE);
    if (!valid_autolock(p->auto_lock)) p->auto_lock = AUTOLOCK_IMMEDIATE;
    p->pin_fails = MAX(0, get_int(k, "pin_erros", 0));
    p->pin_fail_wall = g_key_file_get_int64(k, GROUP, "pin_erro_relogio", NULL);
    p->pin_fail_mono = g_key_file_get_int64(k, GROUP, "pin_erro_monotonico", NULL);
    p->pin_fail_boot = g_key_file_get_string(k, GROUP, "pin_erro_boot", NULL);
    if (!p->pin_fail_boot) p->pin_fail_boot = g_strdup("");
    p->dismissed_tips = g_ptr_array_new_with_free_func(g_free);
    gsize n = 0;
    g_auto(GStrv) tips = g_key_file_get_string_list(k, GROUP, "dicas_dispensadas", &n, NULL);
    for (gsize i = 0; tips && i < n; i++) g_ptr_array_add(p->dismissed_tips, g_strdup(tips[i]));
    return p;
}

gboolean prefs_save(Prefs *p, GError **error) {
    g_autoptr(GKeyFile) k = g_key_file_new();
    g_key_file_set_comment(k, NULL, NULL,
                           " Finan+ — configurações deste computador (não vão para o backup).\n"
                           " O PIN é guardado só como hash Argon2id.", NULL);
    g_key_file_set_string(k, GROUP, "pin", p->pin_hash);
    g_key_file_set_boolean(k, GROUP, "avisos", p->notifications);
    g_key_file_set_boolean(k, GROUP, "avisos_ao_entrar", p->notify_on_login);
    g_key_file_set_boolean(k, GROUP, "assistente_categoria", p->assist_category);
    g_key_file_set_boolean(k, GROUP, "assistente_dicas", p->assist_tips);
    g_key_file_set_boolean(k, GROUP, "assistente_perguntas", p->assist_ask);
    g_key_file_set_integer(k, GROUP, "janela_largura", p->window_width);
    g_key_file_set_integer(k, GROUP, "janela_altura", p->window_height);
    g_key_file_set_boolean(k, GROUP, "janela_maximizada", p->window_maximized);
    g_key_file_set_string(k, GROUP, "ultimo_aviso", p->last_notified);
    if (p->auto_lock_set) g_key_file_set_integer(k, GROUP, "bloqueio_automatico", p->auto_lock);
    g_key_file_set_integer(k, GROUP, "pin_erros", p->pin_fails);
    g_key_file_set_int64(k, GROUP, "pin_erro_relogio", p->pin_fail_wall);
    g_key_file_set_int64(k, GROUP, "pin_erro_monotonico", p->pin_fail_mono);
    g_key_file_set_string(k, GROUP, "pin_erro_boot", p->pin_fail_boot);
    /* guarda só as últimas 200 dicas dispensadas (os ids incluem o mês: as antigas deixam de importar) */
    guint start = p->dismissed_tips->len > MAX_DISMISSED ? p->dismissed_tips->len - MAX_DISMISSED : 0;
    g_key_file_set_string_list(k, GROUP, "dicas_dispensadas", (const char *const *)p->dismissed_tips->pdata + start,
                               p->dismissed_tips->len - start);
    gsize len = 0;
    g_autofree char *data = g_key_file_to_data(k, &len, NULL);
    if (!g_file_set_contents_full(p->path, data, (gssize)len, G_FILE_SET_CONTENTS_CONSISTENT, 0600, error)) return FALSE;
    return TRUE;
}

void prefs_free(Prefs *p) {
    if (!p) return;
    g_free(p->pin_hash);
    g_free(p->last_notified);
    g_free(p->pin_fail_boot);
    g_ptr_array_unref(p->dismissed_tips);
    g_free(p->path);
    g_free(p);
}

gboolean prefs_tip_dismissed(const Prefs *p, const char *id) {
    for (guint i = 0; i < p->dismissed_tips->len; i++) if (strcmp(p->dismissed_tips->pdata[i], id) == 0) return TRUE;
    return FALSE;
}

void prefs_dismiss_tip(Prefs *p, const char *id) {
    if (!prefs_tip_dismissed(p, id)) g_ptr_array_add(p->dismissed_tips, g_strdup(id));
}

void prefs_restore_tip(Prefs *p, const char *id) {
    for (guint i = 0; i < p->dismissed_tips->len; i++)
        if (strcmp(p->dismissed_tips->pdata[i], id) == 0) { g_ptr_array_remove_index(p->dismissed_tips, i); return; }
}

gboolean pin_valid_format(const char *pin) {
    size_t n = pin ? strlen(pin) : 0;
    if (n < 4 || n > 8) return FALSE;
    for (size_t i = 0; i < n; i++) if (!g_ascii_isdigit(pin[i])) return FALSE;
    return TRUE;
}

char *pin_hash(const char *pin) {
    if (sodium_init() < 0) return NULL;
    char out[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(out, pin, strlen(pin), crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
        return NULL;
    return g_strdup(out);
}

gboolean pin_verify(const char *pin, const char *hash) {
    if (!hash || !*hash || sodium_init() < 0) return FALSE;
    return crypto_pwhash_str_verify(hash, pin, strlen(pin)) == 0;
}

int pin_delay_seconds(int fails) {
    if (fails < PIN_FREE_TRIES) return 0;
    int k = MIN(fails - PIN_FREE_TRIES, 10);
    return MIN(30 << k, PIN_MAX_WAIT_S);
}

char *linux_boot_id(void) {
    g_autofree char *s = NULL;
    if (!g_file_get_contents("/proc/sys/kernel/random/boot_id", &s, NULL, NULL)) return g_strdup("");
    return g_strdup(g_strstrip(s));
}

int pin_wait_seconds_at(const Prefs *p, gint64 now_wall, gint64 now_mono, const char *boot) {
    gint64 delay = (gint64)pin_delay_seconds(p->pin_fails) * G_USEC_PER_SEC;
    if (delay == 0) return 0;
    gboolean same_boot = boot && *boot && p->pin_fail_boot && !strcmp(boot, p->pin_fail_boot);
    gint64 rem = same_boot ? p->pin_fail_mono + delay - now_mono : p->pin_fail_wall + delay - now_wall;
    rem = CLAMP(rem, 0, delay); /* atrasar o relógio não aumenta a espera além do previsto */
    return (int)((rem + G_USEC_PER_SEC - 1) / G_USEC_PER_SEC);
}

int pin_wait_seconds(const Prefs *p) {
    g_autofree char *boot = linux_boot_id();
    return pin_wait_seconds_at(p, g_get_real_time(), g_get_monotonic_time(), boot);
}

void pin_attempt_at(Prefs *p, gint64 now_wall, gint64 now_mono, const char *boot) {
    p->pin_fails++;
    p->pin_fail_wall = now_wall;
    p->pin_fail_mono = now_mono;
    g_free(p->pin_fail_boot);
    p->pin_fail_boot = g_strdup(boot ? boot : "");
}

void pin_attempt(Prefs *p) {
    g_autofree char *boot = linux_boot_id();
    pin_attempt_at(p, g_get_real_time(), g_get_monotonic_time(), boot);
    prefs_save(p, NULL);
}

void pin_reset_fails(Prefs *p) {
    if (p->pin_fails == 0) return;
    p->pin_fails = 0;
    p->pin_fail_wall = p->pin_fail_mono = 0;
    g_free(p->pin_fail_boot);
    p->pin_fail_boot = g_strdup("");
    prefs_save(p, NULL);
}
