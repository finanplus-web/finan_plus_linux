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
#include <string.h>

typedef char *(*Producer)(void);

static char *produce_csv(void) { return csv_build(APP->state); }
static char *produce_backup(void) { return backup_to_json(APP->state, APP_VERSION); }

typedef struct { Producer produce; char *done; } SaveCtx;

static void saved(GObject *src, GAsyncResult *res, gpointer p) {
    SaveCtx *c = p;
    g_autoptr(GError) e = NULL;
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
    generate_recurring(APP->state, APP->today);
    theme_apply(APP->state->theme);
    app_commit();
    app_toast("Backup restaurado");
}

static void opened(GObject *src, GAsyncResult *res, gpointer u) {
    (void)u;
    g_autoptr(GError) e = NULL;
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
    Dropped dr = {0};
    AppState *s = backup_parse(data, (gssize)len, &dr, &e);
    g_free(data);
    if (!s) {
        dlg_notice("Não foi possível restaurar", "Arquivo de backup inválido ou danificado. Nada foi alterado.");
        return;
    }
    int bad = dropped_total(dr);
    g_autofree char *extra = bad > 0 ? g_strdup_printf("\n%d item(ns) inválido(s) será(ão) ignorado(s).", bad) : g_strdup("");
    g_autofree char *msg = g_strdup_printf("Backup com %u lançamentos, %u contas, %u cartões e %u metas.%s\n"
                                           "Substituir os dados atuais? O PIN deste computador é mantido.",
                                           s->txs->len, s->accounts->len, s->cards->len, s->goals->len, extra);
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
