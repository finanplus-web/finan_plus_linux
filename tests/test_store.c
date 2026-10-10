/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Testes do armazenamento criptografado e das configurações do computador (exclusivos da versão Linux).
 */
#include "core/model.h"
#include "core/store.h"
#include "core/prefs.h"
#include <glib/gstdio.h>
#include <string.h>

static char *tmpdir(void) {
    g_autoptr(GError) e = NULL;
    char *d = g_dir_make_tmp("finan-test-XXXXXX", &e);
    g_assert_no_error(e);
    return d;
}

static void rm_rf(const char *dir) {
    GDir *d = g_dir_open(dir, 0, NULL);
    const char *n;
    while (d && (n = g_dir_read_name(d))) {
        g_autofree char *p = g_build_filename(dir, n, NULL);
        g_unlink(p);
    }
    if (d) g_dir_close(d);
    g_rmdir(dir);
}

static void test_seal_round_trip_and_tamper(void) {
    g_autofree char *dir = tmpdir();
    g_autoptr(GError) e = NULL;
    Store *st = store_open(dir, &e);
    g_assert_no_error(e);
    const char *msg = "{\"txs\":[]} segredo";
    g_autoptr(GBytes) b = store_seal(st, msg, strlen(msg));
    gsize n;
    const guint8 *data = g_bytes_get_data(b, &n);
    g_assert_null(memmem(data, n, "segredo", 7)); /* o texto não aparece no arquivo */
    gsize out_len;
    g_autofree char *back = store_open_sealed(st, data, n, &out_len);
    g_assert_cmpstr(back, ==, msg);
    /* qualquer byte alterado invalida (autenticação Poly1305) */
    guint8 *copy = g_memdup2(data, n);
    copy[n - 1] ^= 1;
    g_assert_null(store_open_sealed(st, copy, n, NULL));
    copy[n - 1] ^= 1;
    copy[3] ^= 1; /* cabeçalho também é autenticado */
    g_assert_null(store_open_sealed(st, copy, n, NULL));
    g_free(copy);
    store_free(st);
    rm_rf(dir);
}

static void test_save_load(void) {
    g_autofree char *dir = tmpdir();
    g_autoptr(GError) e = NULL;
    Store *st = store_open(dir, &e);
    g_assert_no_error(e);
    g_assert_null(store_load(st, NULL, &e)); /* primeiro uso: sem dados e sem erro */
    g_assert_no_error(e);
    g_autoptr(AppState) s = app_state_new();
    Day d;
    day_parse_iso("2026-10-03", &d);
    g_ptr_array_add(s->txs, tx_new("a", KIND_EXPENSE, 1234, d, "Padaria São João", "Alimentação", TRUE, "main", ""));
    s->theme = THEME_NORD;
    g_assert_true(store_save(st, s, &e));
    g_assert_true(store_save(st, s, &e)); /* segunda gravação cria a cópia anterior */
    g_autofree char *prev = g_strdup_printf("%s.anterior", store_data_path(st));
    g_assert_true(g_file_test(prev, G_FILE_TEST_EXISTS));
    store_free(st);

    st = store_open(dir, &e);
    g_assert_no_error(e);
    g_autoptr(AppState) again = store_load(st, NULL, &e);
    g_assert_no_error(e);
    g_assert_nonnull(again);
    g_assert_cmpuint(again->txs->len, ==, 1);
    g_assert_cmpstr(((Tx *)again->txs->pdata[0])->desc, ==, "Padaria São João");
    g_assert_cmpint(again->theme, ==, THEME_NORD);
    /* permissão do arquivo: só o usuário */
    GStatBuf sb;
    g_assert_cmpint(g_stat(store_data_path(st), &sb), ==, 0);
    g_assert_cmpint(sb.st_mode & 077, ==, 0);
    g_assert_true(store_wipe(st, &e));
    g_assert_false(g_file_test(store_data_path(st), G_FILE_TEST_EXISTS));
    store_free(st);
    rm_rf(dir);
}

static void test_wrong_key_is_reported_not_overwritten(void) {
    g_autofree char *dir = tmpdir();
    g_autoptr(GError) e = NULL;
    Store *st = store_open(dir, &e);
    g_autoptr(AppState) s = app_state_new();
    g_assert_true(store_save(st, s, &e));
    gboolean file_key = store_key_source(st) == KEY_SOURCE_FILE;
    store_free(st);
    g_assert_true(file_key);
    /* troca a chave: os dados não podem ser abertos, e o app avisa em vez de apagar */
    g_autofree char *kp = g_build_filename(dir, "chave", NULL);
    g_assert_true(g_file_set_contents(kp, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", -1, NULL));
    st = store_open(dir, &e);
    g_assert_no_error(e);
    g_assert_null(store_load(st, NULL, &e));
    g_assert_error(e, STORE_ERROR, STORE_ERROR_UNREADABLE);
    g_clear_error(&e);
    /* com a chave errada, gravar é recusado: o arquivo original fica intacto */
    gsize before_len = 0;
    g_autofree char *before = NULL;
    g_file_get_contents(store_data_path(st), &before, &before_len, NULL);
    g_assert_false(store_save(st, s, &e));
    g_clear_error(&e);
    g_autofree char *after = NULL;
    gsize after_len = 0;
    g_file_get_contents(store_data_path(st), &after, &after_len, NULL);
    g_assert_cmpmem(before, before_len, after, after_len);
    g_autofree char *moved = store_quarantine(st, &e);
    g_assert_no_error(e);
    g_assert_true(g_file_test(moved, G_FILE_TEST_EXISTS));
    store_free(st);
    rm_rf(dir);
}

static void test_prefs_and_pin(void) {
    g_autofree char *dir = tmpdir();
    Prefs *p = prefs_load(dir);
    g_assert_false(prefs_has_pin(p));
    g_assert_true(p->assist_tips);
    g_assert_true(pin_valid_format("1234"));
    g_assert_false(pin_valid_format("12a4"));
    g_assert_false(pin_valid_format("123"));
    g_assert_false(pin_valid_format("123456789"));
    g_autofree char *h = pin_hash("4321");
    g_assert_true(g_str_has_prefix(h, "$argon2id$"));
    g_assert_true(pin_verify("4321", h));
    g_assert_false(pin_verify("4322", h));
    g_free(p->pin_hash);
    p->pin_hash = g_strdup(h);
    p->assist_tips = FALSE;
    prefs_dismiss_tip(p, "dup:x");
    g_assert_true(prefs_save(p, NULL));
    prefs_free(p);
    p = prefs_load(dir);
    g_assert_true(prefs_has_pin(p));
    g_assert_false(p->assist_tips);
    g_assert_true(prefs_tip_dismissed(p, "dup:x"));
    prefs_restore_tip(p, "dup:x");
    g_assert_false(prefs_tip_dismissed(p, "dup:x"));
    prefs_free(p);
    rm_rf(dir);
}

/* limite de tentativas do PIN (Android 1.1.1): grava no aparelho, dobra até 1 hora, sobrevive a fechar o app */
static void test_pin_limit_persists(void) {
    g_autofree char *dir = tmpdir();
    g_assert_cmpint(pin_delay_seconds(4), ==, 0);
    g_assert_cmpint(pin_delay_seconds(5), ==, 30);
    g_assert_cmpint(pin_delay_seconds(6), ==, 60);
    g_assert_cmpint(pin_delay_seconds(7), ==, 120);
    g_assert_cmpint(pin_delay_seconds(12), ==, 3600);
    g_assert_cmpint(pin_delay_seconds(40), ==, 3600);
    Prefs *p = prefs_load(dir);
    gint64 wall = 1760000000LL * G_USEC_PER_SEC, mono = 500LL * G_USEC_PER_SEC;
    for (int i = 0; i < 4; i++) pin_attempt_at(p, wall, mono, "boot-a");
    g_assert_cmpint(pin_wait_seconds_at(p, wall, mono, "boot-a"), ==, 0);
    pin_attempt_at(p, wall, mono, "boot-a");
    g_assert_cmpint(pin_wait_seconds_at(p, wall, mono, "boot-a"), ==, 30);
    g_assert_true(prefs_save(p, NULL));
    prefs_free(p);
    /* "fechar o app": a contagem volta do arquivo */
    p = prefs_load(dir);
    g_assert_cmpint(p->pin_fails, ==, 5);
    g_assert_cmpint(pin_wait_seconds_at(p, wall, mono + 10 * G_USEC_PER_SEC, "boot-a"), ==, 20);
    /* mesmo boot: adiantar o relógio de parede não adianta */
    g_assert_cmpint(pin_wait_seconds_at(p, wall + 3600LL * G_USEC_PER_SEC, mono + 10 * G_USEC_PER_SEC, "boot-a"), ==, 20);
    /* outro boot: vale o relógio de parede */
    g_assert_cmpint(pin_wait_seconds_at(p, wall + 25 * G_USEC_PER_SEC, 1, "boot-b"), ==, 5);
    /* atrasar o relógio não aumenta a espera além dos 30 s */
    g_assert_cmpint(pin_wait_seconds_at(p, wall - 3600LL * G_USEC_PER_SEC, 1, "boot-b"), ==, 30);
    pin_reset_fails(p);
    g_assert_cmpint(pin_wait_seconds_at(p, wall, mono, "boot-a"), ==, 0);
    prefs_free(p);
    p = prefs_load(dir);
    g_assert_cmpint(p->pin_fails, ==, 0);
    /* bloqueio automático: sem valor gravado até a migração; opções válidas */
    g_assert_false(p->auto_lock_set);
    p->auto_lock = AUTOLOCK_ON_OPEN;
    p->auto_lock_set = TRUE;
    g_assert_true(prefs_save(p, NULL));
    prefs_free(p);
    p = prefs_load(dir);
    g_assert_true(p->auto_lock_set);
    g_assert_cmpint(p->auto_lock, ==, AUTOLOCK_ON_OPEN);
    prefs_free(p);
    rm_rf(dir);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_setenv("FINAN_PLUS_NO_KEYRING", "1", TRUE); /* nunca toca no chaveiro real de quem roda os testes */
    g_test_add_func("/store/seal-round-trip-and-tamper", test_seal_round_trip_and_tamper);
    g_test_add_func("/store/save-load", test_save_load);
    g_test_add_func("/store/wrong-key-reported", test_wrong_key_is_reported_not_overwritten);
    g_test_add_func("/prefs/prefs-and-pin", test_prefs_and_pin);
    g_test_add_func("/prefs/pin-limit-persists", test_pin_limit_persists);
    return g_test_run();
}
