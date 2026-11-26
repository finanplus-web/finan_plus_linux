/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "autobackup.h"
#include "backup.h"
#include "backup_crypto.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

const int AUTOBACKUP_KEEP_OPTIONS[4] = {7, 14, 30, 60};

gboolean autobackup_is_name(const char *n) {
    /* ^finan-plus-auto-\d{4}-\d{2}-\d{2}\.json$ */
    size_t p = strlen(AUTOBACKUP_PREFIX);
    if (!n || strlen(n) != p + 15 || strncmp(n, AUTOBACKUP_PREFIX, p) != 0 || strcmp(n + p + 10, ".json") != 0) return FALSE;
    const char *d = n + p;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) { if (d[i] != '-') return FALSE; }
        else if (!g_ascii_isdigit(d[i])) return FALSE;
    }
    return TRUE;
}

static gint cmp_desc(gconstpointer a, gconstpointer b) { return -strcmp(*(char *const *)a, *(char *const *)b); }

char *autobackup_write(const AppState *s, const char *folder, const char *password, int keep, Day today,
                       const char *app_version, GError **error) {
    if (!folder || !*folder || !g_file_test(folder, G_FILE_TEST_IS_DIR)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "A pasta do backup não foi encontrada. Escolha a pasta de novo.");
        return NULL;
    }
    if (!password || !*password) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_ACCES, "A senha do backup não pôde ser lida neste computador. Defina a senha de novo.");
        return NULL;
    }
    g_autofree char *json = backup_to_json_ext(s, app_version, TRUE);
    g_autofree char *enc = backup_crypto_encrypt(json, password, 0, error);
    if (!enc) return NULL;
    char d[11];
    char *name = g_strdup_printf("%s%s.json", AUTOBACKUP_PREFIX, day_iso(today, d));
    g_autofree char *path = g_build_filename(folder, name, NULL);
    g_autoptr(GFile) f = g_file_new_for_path(path);
    g_autoptr(GError) e = NULL;
    /* gravação atômica (arquivo temporário + rename), sem backup ~ */
    if (!g_file_replace_contents(f, enc, strlen(enc), NULL, FALSE, G_FILE_CREATE_PRIVATE, NULL, NULL, &e)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_IO, "Não foi possível gravar a cópia (%s). Confira a pasta e o espaço livre.", e->message);
        g_free(name);
        return NULL;
    }
    /* apaga as cópias mais antigas (só os arquivos do backup automático); com o app vazio, não apaga nada */
    if (s->txs->len > 0) {
        gboolean ok = FALSE;
        for (guint i = 0; i < G_N_ELEMENTS(AUTOBACKUP_KEEP_OPTIONS); i++) ok |= keep == AUTOBACKUP_KEEP_OPTIONS[i];
        if (!ok) keep = AUTOBACKUP_DEFAULT_KEEP;
        g_autoptr(GDir) dir = g_dir_open(folder, 0, NULL);
        g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func(g_free);
        const char *n;
        while (dir && (n = g_dir_read_name(dir))) if (autobackup_is_name(n)) g_ptr_array_add(names, g_strdup(n));
        g_ptr_array_sort(names, cmp_desc);
        for (guint i = (guint)keep; i < names->len; i++) {
            g_autofree char *old = g_build_filename(folder, names->pdata[i], NULL);
            g_unlink(old);
        }
    }
    return name;
}
