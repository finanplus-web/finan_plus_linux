/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Arquivos: exportar CSV, salvar e restaurar backup JSON (compatível com o Finan+ web e o app
 * Android), apagar tudo. O usuário escolhe onde salvar (seletor de arquivos do sistema).
 */
#include "pages.h"
#include "theme.h"
#include "widgets.h"
#include "core/backup.h"
#include "core/backup_crypto.h"
#include "core/autobackup.h"
#include "core/txform.h"
#include "core/assist.h"
#include <string.h>

typedef char *(*Producer)(void);

static char *produce_csv(void) { return csv_build(APP->state); }
static char *produce_backup(void) { return backup_to_json(APP->state, APP_VERSION); }

typedef struct { Producer produce; char *done; } SaveCtx;

static void saved(GObject *src, GAsyncResult *res, gpointer p) {
    SaveCtx *c = p;
    g_autoptr(GError) e = NULL;
    app_external_end();
    g_autoptr(GFile) f = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &e);
    if (f && !APP->locked) {
        g_autofree char *data = c->produce();
        if (g_file_replace_contents(f, data, strlen(data), NULL, FALSE, G_FILE_CREATE_PRIVATE, NULL, NULL, &e)) {
            g_autofree char *name = g_file_get_basename(f);
            app_toast("%s: %s", c->done, name);
        } else dlg_notice("Não foi possível salvar", e->message);
    }
    g_free(c->done);
    g_free(c);
}

static void save_as(const char *title, const char *name, const char *mime, const char *pattern, Producer produce, const char *done) {
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, title);
    gtk_file_dialog_set_initial_name(d, name);
    GtkFileFilter *flt = gtk_file_filter_new();
    gtk_file_filter_add_mime_type(flt, mime);
    gtk_file_filter_add_pattern(flt, pattern);
    gtk_file_filter_set_name(flt, pattern);
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, flt);
    gtk_file_dialog_set_filters(d, G_LIST_MODEL(filters));
    SaveCtx *c = g_new0(SaveCtx, 1);
    c->produce = produce;
    c->done = g_strdup(done);
    app_external_begin();
    gtk_file_dialog_save(d, APP->window, APP->files, saved, c);
    g_object_unref(filters);
    g_object_unref(flt);
    g_object_unref(d);
}

void data_export_csv(void) {
    char d[11];
    g_autofree char *name = g_strdup_printf("lancamentos-%s.csv", day_iso(APP->today, d));
    save_as("Exportar CSV", name, "text/csv", "*.csv", produce_csv, "CSV salvo");
}

void data_export_backup(void) {
    char d[11];
    g_autofree char *name = g_strdup_printf("backup-finan-plus-%s.json", day_iso(APP->today, d));
    save_as("Salvar backup JSON", name, "application/json", "*.json", produce_backup, "Backup salvo");
}

/* ---------------------------------------------------------------- restaurar */

/* o estado lido fica com o diálogo até a confirmação; se o usuário cancelar, é liberado */
typedef struct { AppState *s; } Holder;
static void holder_free(gpointer p) { Holder *h = p; app_state_free(h->s); g_free(h); }

static void apply_restore(gpointer p) {
    Holder *h = p;
    AppState *s = h->s;
    h->s = NULL; /* agora pertence ao app */
    app_state_free(APP->state);
    APP->state = s;
    /* backup antigo: cartão e limite especial saem das categorias e Empréstimo vira Dívidas (como no Android) */
    txform_migrate(APP->state);
    generate_recurring(APP->state, APP->today);
    theme_apply(APP->state->theme);
    app_commit();
    app_toast("Backup restaurado");
}

static void review(const char *data, gsize len);

static void restore_with_password(const char *pw, gpointer data) {
    g_autoptr(GError) e = NULL;
    g_autofree char *plain = backup_crypto_decrypt(data, -1, pw, &e);
    if (!plain) { dlg_notice("Não foi possível restaurar", e ? e->message : "Senha incorreta ou arquivo danificado"); return; }
    review(plain, strlen(plain));
    memset(plain, 0, strlen(plain));
}

static void opened(GObject *src, GAsyncResult *res, gpointer u) {
    (void)u;
    g_autoptr(GError) e = NULL;
    app_external_end();
    g_autoptr(GFile) f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, &e);
    if (!f || APP->locked) return;
    g_autoptr(GFileInfo) info = g_file_query_info(f, G_FILE_ATTRIBUTE_STANDARD_SIZE, 0, NULL, NULL);
    if (info && g_file_info_get_size(info) > BACKUP_MAX_BYTES) {
        dlg_notice("Não foi possível restaurar", "Arquivo grande demais para um backup do Finan+. Nada foi alterado.");
        return;
    }
    char *data = NULL;
    gsize len = 0;
    if (!g_file_load_contents(f, NULL, &data, &len, NULL, &e)) {
        dlg_notice("Não foi possível restaurar", e->message);
        return;
    }
    /* cópia do backup automático (do Linux ou do Android): pede a senha antes */
    if (backup_crypto_is_encrypted(data, (gssize)len)) {
        dlg_input("Backup criptografado", "Digite a senha do backup automático usada para criar esta cópia.", "Senha", "", TRUE,
                  restore_with_password, data, g_free);
        return;
    }
    review(data, len);
    g_free(data);
}

static void review(const char *data, gsize len) {
    g_autoptr(GError) e = NULL;
    Dropped dr = {0};
    AppState *s = backup_parse(data, (gssize)len, &dr, &e);
    if (!s) {
        dlg_notice("Não foi possível restaurar", "Arquivo de backup inválido ou danificado. Nada foi alterado.");
        return;
    }
    int bad = dropped_total(dr);
    g_autofree char *extra = bad == 1 ? g_strdup("\n1 item inválido será ignorado.")
                           : bad > 1 ? g_strdup_printf("\n%d itens inválidos serão ignorados.", bad) : g_strdup("");
    g_autofree char *nt = br_plural((int)s->txs->len, "lançamento", "lançamentos"), *na = br_plural((int)s->accounts->len, "conta", "contas"),
                    *nc = br_plural((int)s->cards->len, "cartão", "cartões"), *ng = br_plural((int)s->goals->len, "meta", "metas");
    g_autofree char *nsh = s->shared->len ? g_strdup_printf(", %u despesa%s compartilhada%s", s->shared->len, s->shared->len == 1 ? "" : "s", s->shared->len == 1 ? "" : "s") : g_strdup("");
    g_autofree char *msg = g_strdup_printf("Backup com %s, %s, %s e %s%s.%s\n"
                                           "Substituir os dados atuais? O PIN deste computador é mantido.",
                                           nt, na, nc, ng, nsh, extra);
    Holder *h = g_new0(Holder, 1);
    h->s = s;
    dlg_confirm("Revisar restauração", msg, "Substituir", TRUE, apply_restore, h, holder_free);
}

void data_restore_backup(void) {
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, "Restaurar backup");
    GtkFileFilter *flt = gtk_file_filter_new();
    gtk_file_filter_add_pattern(flt, "*.json");
    gtk_file_filter_add_mime_type(flt, "application/json");
    gtk_file_filter_add_mime_type(flt, "text/plain");
    gtk_file_filter_set_name(flt, "Backup do Finan+ (*.json)");
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, flt);
    gtk_file_dialog_set_filters(d, G_LIST_MODEL(filters));
    app_external_begin();
    gtk_file_dialog_open(d, APP->window, APP->files, opened, NULL);
    g_object_unref(filters);
    g_object_unref(flt);
    g_object_unref(d);
}

/* ---------------------------------------------------------------- apagar tudo */

static void do_wipe(gpointer u) {
    (void)u;
    g_autoptr(GError) e = NULL;
    if (!store_wipe(APP->store, &e)) { dlg_notice("Não foi possível apagar", e->message); return; }
    app_state_free(APP->state);
    APP->state = app_state_new();
    g_free(APP->prefs->pin_hash);
    APP->prefs->pin_hash = g_strdup("");
    g_ptr_array_set_size(APP->prefs->dismissed_tips, 0);
    APP->prefs->pin_fails = 0; /* "Apagar tudo" também zera o limite de tentativas */
    /* e desliga o backup automático (a senha já saiu com store_wipe; as cópias da pasta ficam) */
    g_free(APP->prefs->auto_backup_dir); APP->prefs->auto_backup_dir = g_strdup("");
    g_free(APP->prefs->auto_backup_error); APP->prefs->auto_backup_error = g_strdup("");
    APP->prefs->auto_backup_last = 0;
    prefs_save(APP->prefs, NULL);
    theme_apply(APP->state->theme);
    app_commit();
    app_show_page(PAGE_HOME);
    app_toast("Todos os dados foram apagados");
}

void data_wipe(void) {
    dlg_confirm("Apagar todos os dados", "Apagar TODOS os dados deste computador, inclusive o PIN? Faça um backup antes.", "Apagar tudo", TRUE,
                do_wipe, NULL, NULL);
}

/* ---------------------------------------------------------------- backup automático (BACKUP-AUTOMATICO.md) */

typedef struct { char *dir; char *pw1; } AutoSetup;
static void auto_setup_free(gpointer p) {
    AutoSetup *a = p;
    g_free(a->dir);
    if (a->pw1) { memset(a->pw1, 0, strlen(a->pw1)); g_free(a->pw1); }
    g_free(a);
}

static void auto_enable(AutoSetup *a, const char *pw) {
    g_autoptr(GError) e = NULL;
    if (pw && !store_set_auto_backup_password(APP->store, pw, &e)) { dlg_notice("Não foi possível guardar a senha", e ? e->message : ""); return; }
    if (a->dir) { g_free(APP->prefs->auto_backup_dir); APP->prefs->auto_backup_dir = g_strdup(a->dir); }
    g_free(APP->prefs->auto_backup_error); APP->prefs->auto_backup_error = g_strdup("");
    prefs_save(APP->prefs, NULL);
    app_refresh_page(PAGE_SETTINGS);
    /* a primeira cópia é feita na hora */
    app_auto_backup(TRUE);
}

static void pw_second(const char *pw2, gpointer p) {
    AutoSetup *a = p;
    if (strcmp(a->pw1, pw2) != 0) { dlg_notice("Senha não definida", "As senhas não conferem."); return; }
    auto_enable(a, pw2);
}

static void pw_first(const char *pw1, gpointer p) {
    AutoSetup *a = p;
    if (g_utf8_strlen(pw1, -1) < BACKUP_CRYPTO_MIN_PASSWORD) {
        dlg_notice("Senha curta", "Use pelo menos 8 caracteres. Guarde a senha fora do computador: sem ela, as cópias não abrem.");
        return;
    }
    AutoSetup *n = g_new0(AutoSetup, 1);
    n->dir = g_strdup(a->dir);
    n->pw1 = g_strdup(pw1);
    dlg_input("Confirme a senha", "Digite a mesma senha de novo.", "Senha", "", TRUE, pw_second, n, auto_setup_free);
}

static void ask_password(const char *dir) {
    AutoSetup *a = g_new0(AutoSetup, 1);
    a->dir = g_strdup(dir);
    dlg_input("Senha do backup automático",
              "As cópias são criptografadas (AES-256-GCM) com esta senha, no mesmo formato do app Android. Mínimo de 8 caracteres. "
              "Guarde a senha fora do computador: sem ela, as cópias não abrem.",
              "Senha", "", TRUE, pw_first, a, auto_setup_free);
}

static void folder_picked(GObject *src, GAsyncResult *res, gpointer change_only) {
    g_autoptr(GError) e = NULL;
    app_external_end();
    g_autoptr(GFile) f = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(src), res, &e);
    if (!f || APP->locked) return;
    g_autofree char *path = g_file_get_path(f);
    if (!path) { dlg_notice("Pasta não suportada", "Escolha uma pasta deste computador (ou sincronizada nele)."); return; }
    if (GPOINTER_TO_INT(change_only)) {
        AutoSetup a = {path, NULL};
        auto_enable(&a, NULL);
        return;
    }
    ask_password(path);
}

static void pick_folder(gboolean change_only) {
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, "Pasta do backup automático");
    gtk_file_dialog_set_accept_label(d, "Usar esta pasta");
    app_external_begin();
    gtk_file_dialog_select_folder(d, APP->window, APP->files, folder_picked, GINT_TO_POINTER(change_only));
    g_object_unref(d);
}

void data_auto_backup_enable(void) { pick_folder(FALSE); }
void data_auto_backup_change_folder(void) { pick_folder(TRUE); }
void data_auto_backup_change_password(void) { ask_password(NULL); }

static void do_disable(gpointer u) {
    (void)u;
    store_set_auto_backup_password(APP->store, NULL, NULL);
    g_free(APP->prefs->auto_backup_dir); APP->prefs->auto_backup_dir = g_strdup("");
    g_free(APP->prefs->auto_backup_error); APP->prefs->auto_backup_error = g_strdup("");
    g_free(APP->prefs->auto_backup_file); APP->prefs->auto_backup_file = g_strdup("");
    APP->prefs->auto_backup_last = 0;
    prefs_save(APP->prefs, NULL);
    app_refresh_page(PAGE_SETTINGS);
    app_toast("Backup automático desligado");
}

void data_auto_backup_disable(void) {
    dlg_confirm("Desligar o backup automático?", "A pasta e a senha são esquecidas. As cópias já gravadas continuam na pasta.",
                "Desligar", TRUE, do_disable, NULL, NULL);
}
