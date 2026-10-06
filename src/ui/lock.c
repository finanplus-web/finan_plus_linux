/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tela de bloqueio por PIN. A verificação (Argon2id) roda em segundo plano para não travar a janela.
 * Após 5 erros seguidos há espera crescente entre tentativas (30 s, 60 s, 90 s…).
 */
#include "pages.h"
#include "widgets.h"
#include <string.h>

static struct {
    GtkWidget *entry, *msg, *dots, *button;
    gboolean busy;
} L;

static void update_dots(void) {
    w_clear(L.dots);
    int n = (int)strlen(gtk_editable_get_text(GTK_EDITABLE(L.entry)));
    for (int i = 0; i < MAX(4, n); i++) {
        GtkWidget *d = w_hbox(0);
        gtk_widget_add_css_class(d, "fin-pin-dot");
        if (i < n) gtk_widget_add_css_class(d, "on");
        w_add(L.dots, d);
    }
    g_autofree char *a11y = g_strdup_printf("%d dígitos digitados", n);
    gtk_accessible_update_property(GTK_ACCESSIBLE(L.dots), GTK_ACCESSIBLE_PROPERTY_LABEL, a11y, -1);
}

static void on_changed(GtkEditable *e, gpointer u) {
    (void)u;
    /* só números, até 8 */
    const char *t = gtk_editable_get_text(e);
    char clean[9] = {0};
    int j = 0;
    gboolean dirty = FALSE;
    for (int i = 0; t[i]; i++) {
        if (g_ascii_isdigit(t[i]) && j < 8) clean[j++] = t[i];
        else dirty = TRUE;
    }
    if (dirty) {
        g_signal_handlers_block_by_func(e, on_changed, NULL);
        gtk_editable_set_text(e, clean);
        gtk_editable_set_position(e, -1);
        g_signal_handlers_unblock_by_func(e, on_changed, NULL);
    }
    update_dots();
}

static void verify_thread(GTask *task, gpointer src, gpointer data, GCancellable *c) {
    (void)src; (void)c;
    char **pair = data;
    g_task_return_boolean(task, pin_verify(pair[0], pair[1]));
}

static void verified(GObject *src, GAsyncResult *res, gpointer u) {
    (void)src; (void)u;
    gboolean ok = g_task_propagate_boolean(G_TASK(res), NULL);
    L.busy = FALSE;
    gtk_widget_set_sensitive(L.button, TRUE);
    gtk_editable_set_text(GTK_EDITABLE(L.entry), "");
    if (ok) {
        gtk_label_set_text(GTK_LABEL(L.msg), "");
        app_unlocked();
        return;
    }
    pin_throttle_fail(&APP->throttle);
    gtk_label_set_text(GTK_LABEL(L.msg), pin_throttle_wait_seconds(&APP->throttle) > 0 ? "PIN incorreto. Aguarde para tentar de novo." : "PIN incorreto.");
    gtk_widget_grab_focus(L.entry);
}

static void submit(GtkWidget *w, gpointer u) {
    (void)w; (void)u;
    const char *pin = gtk_editable_get_text(GTK_EDITABLE(L.entry));
    if (L.busy || strlen(pin) < 4) return;
    int wait = pin_throttle_wait_seconds(&APP->throttle);
    if (wait > 0) {
        g_autofree char *m = g_strdup_printf("Muitas tentativas. Aguarde %d s.", wait);
        gtk_label_set_text(GTK_LABEL(L.msg), m);
        gtk_editable_set_text(GTK_EDITABLE(L.entry), "");
        return;
    }
    L.busy = TRUE;
    gtk_widget_set_sensitive(L.button, FALSE);
    gtk_label_set_text(GTK_LABEL(L.msg), "Verificando…");
    char **pair = g_new0(char *, 3);
    pair[0] = g_strdup(pin);
    pair[1] = g_strdup(APP->prefs->pin_hash);
    GTask *task = g_task_new(NULL, NULL, verified, NULL);
    g_task_set_task_data(task, pair, (GDestroyNotify)g_strfreev);
    g_task_run_in_thread(task, verify_thread);
    g_object_unref(task);
}

static void key(GtkButton *b, gpointer u) {
    (void)u;
    const char *k = gtk_button_get_label(b);
    g_autofree char *cur = g_strdup(gtk_editable_get_text(GTK_EDITABLE(L.entry)));
    if (!k) { /* apagar */
        if (*cur) cur[strlen(cur) - 1] = 0;
        gtk_editable_set_text(GTK_EDITABLE(L.entry), cur);
    } else if (strlen(cur) < 8) {
        g_autofree char *n = g_strconcat(cur, k, NULL);
        gtk_editable_set_text(GTK_EDITABLE(L.entry), n);
    }
    gtk_widget_grab_focus(L.entry);
    gtk_editable_set_position(GTK_EDITABLE(L.entry), -1);
}

void lock_page_reset(void) {
    if (!L.entry) return;
    gtk_editable_set_text(GTK_EDITABLE(L.entry), "");
    gtk_label_set_text(GTK_LABEL(L.msg), "");
    gtk_widget_grab_focus(L.entry);
}

GtkWidget *lock_page_new(void) {
    GtkWidget *root = w_vbox(0);
    gtk_widget_add_css_class(root, "fin-lock");
    /* barra transparente só com os botões da janela (fechar, minimizar, maximizar), também com o app bloqueado */
    GtkWidget *hb = adw_header_bar_new();
    adw_header_bar_set_show_title(ADW_HEADER_BAR(hb), FALSE);
    gtk_widget_add_css_class(hb, "flat");
    w_add(root, hb);
    GtkWidget *box = w_vbox(14);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(box, TRUE);
    GtkWidget *logo = gtk_image_new_from_icon_name(APP_ID);
    gtk_image_set_pixel_size(GTK_IMAGE(logo), 96);
    w_add(box, logo);
    GtkWidget *t = w_label("Finan+", "title-1");
    gtk_label_set_xalign(GTK_LABEL(t), 0.5);
    w_add(box, t);
    GtkWidget *s = w_label("Digite seu PIN para desbloquear", "fin-muted");
    gtk_label_set_xalign(GTK_LABEL(s), 0.5);
    w_add(box, s);
    L.dots = w_hbox(12);
    gtk_widget_set_halign(L.dots, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(L.dots, 6);
    w_add(box, L.dots);
    GtkWidget *row = w_hbox(8);
    gtk_widget_set_halign(row, GTK_ALIGN_CENTER);
    L.entry = gtk_password_entry_new();
    g_object_set(L.entry, "placeholder-text", "PIN", "activates-default", FALSE, NULL);
    gtk_editable_set_width_chars(GTK_EDITABLE(L.entry), 12);
    gtk_accessible_update_property(GTK_ACCESSIBLE(L.entry), GTK_ACCESSIBLE_PROPERTY_LABEL, "PIN", -1);
    g_signal_connect(L.entry, "changed", G_CALLBACK(on_changed), NULL);
    g_signal_connect(L.entry, "activate", G_CALLBACK(submit), NULL);
    w_add(row, L.entry);
    L.button = gtk_button_new_with_label("Desbloquear");
    gtk_widget_add_css_class(L.button, "suggested-action");
    g_signal_connect(L.button, "clicked", G_CALLBACK(submit), NULL);
    w_add(row, L.button);
    w_add(box, row);
    L.msg = w_label("", "fin-red");
    gtk_label_set_xalign(GTK_LABEL(L.msg), 0.5);
    gtk_accessible_update_property(GTK_ACCESSIBLE(L.msg), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, "Mensagem", -1);
    w_add(box, L.msg);
    /* teclado numérico (útil em notebooks com tela sensível ao toque) */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
    gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);
    const char *keys[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "", "0", NULL};
    for (int i = 0; i < 12; i++) {
        if (i == 9) continue;
        GtkWidget *b = keys[i] ? gtk_button_new_with_label(keys[i]) : gtk_button_new_from_icon_name("fin-backspace-symbolic");
        gtk_widget_add_css_class(b, "circular");
        gtk_widget_add_css_class(b, "title-3");
        gtk_widget_set_size_request(b, 64, 64);
        gtk_widget_set_focusable(b, FALSE);
        if (!keys[i]) gtk_widget_set_tooltip_text(b, "Apagar");
        g_signal_connect(b, "clicked", G_CALLBACK(key), NULL);
        gtk_grid_attach(GTK_GRID(grid), b, i % 3, i / 3, 1, 1);
    }
    gtk_widget_set_margin_top(grid, 6);
    w_add(box, grid);
    w_add(root, box);
    update_dots();
    return root;
}
