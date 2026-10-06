/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "prefs.h"
#include <glib/gstdio.h>
#include <sodium.h>
#include <string.h>

#define GROUP "aparelho"
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

int pin_throttle_wait_seconds(const PinThrottle *t) {
    gint64 ms = (t->wait_until - g_get_monotonic_time()) / 1000;
    return ms > 0 ? (int)((ms + 999) / 1000) : 0;
}

void pin_throttle_fail(PinThrottle *t) {
    t->fails++;
    if (t->fails >= 5) t->wait_until = g_get_monotonic_time() + (gint64)30 * G_USEC_PER_SEC * (t->fails - 4);
}

void pin_throttle_reset(PinThrottle *t) { t->fails = 0; t->wait_until = 0; }
