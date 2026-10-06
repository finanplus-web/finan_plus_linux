/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Avisos de vencimento pelas notificações da área de trabalho (GNotification).
 * - Com o Finan+ aberto: uma vez por dia, a partir das 9h (e de novo se surgirem itens novos).
 * - Opcional: ao entrar na sessão, mesmo com o app fechado (arquivo em ~/.config/autostart que
 *   roda "finan-plus --avisos": verifica, avisa e sai).
 * Com "Ocultar valores" ligado, o aviso mostra só os nomes.
 */
#include "pages.h"
#include "widgets.h"
#include <glib/gstdio.h>
#include <string.h>

#define AUTOSTART_NAME APP_ID "-avisos.desktop"

static char *line(const Reminder *r, Day today, gboolean values) {
    char d[11];
    g_autofree char *what = NULL;
    switch (r->type) {
    case REMINDER_BILL_OVERDUE: what = g_strdup_printf("Atrasada desde %s", day_br_short(r->date, d)); break;
    case REMINDER_BILL_DUE: what = r->date == today ? g_strdup("Vence hoje") : g_strdup_printf("Vence %s", day_br_short(r->date, d)); break;
    case REMINDER_INCOME_DUE: what = g_strdup("A receber"); break;
    case REMINDER_INVOICE_DUE: what = r->date < today ? g_strdup("Fatura vencida") : g_strdup_printf("Fatura vence %s", day_br_short(r->date, d)); break;
    }
    if (!values) return g_strdup_printf("%s · %s", r->title, what);
    g_autofree char *m = money_fmt(r->amount);
    return g_strdup_printf("%s · %s · %s", r->title, what, m);
}

/* chave do aviso: o mesmo conjunto de itens não é avisado duas vezes no mesmo dia */
static char *notice_key(GPtrArray *items, Day today) {
    char d[11];
    GString *k = g_string_new(day_iso(today, d));
    g_string_append_c(k, ':');
    for (guint i = 0; i < items->len; i++) {
        Reminder *r = items->pdata[i];
        g_string_append_printf(k, "%s%s%s", i ? "," : "", r->ref_id, day_iso(r->date, d));
    }
    return g_string_free(k, FALSE);
}

static GNotification *build(const AppState *s, GPtrArray *items, Day today) {
    gboolean values = !s->privacy;
    g_autofree char *first = line(items->pdata[0], today, values);
    g_autofree char *title = NULL;
    GString *body = g_string_new(NULL);
    if (items->len == 1) {
        const char *sep = strstr(first, " · ");
        title = sep ? g_strndup(first, (gsize)(sep - first)) : g_strdup(first);
        g_string_append(body, sep ? sep + 3 : "");
    } else {
        title = g_strdup_printf("%u lançamentos pedem atenção", items->len);
        for (guint i = 0; i < items->len && i < 6; i++) {
            g_autofree char *l = line(items->pdata[i], today, values);
            g_string_append_printf(body, "%s%s", i ? "\n" : "", l);
        }
        if (items->len > 6) g_string_append_printf(body, "\ne mais %u", items->len - 6);
    }
    GNotification *n = g_notification_new(title);
    g_notification_set_body(n, body->str);
    g_notification_set_category(n, "x-gnome.reminder");
    g_autoptr(GIcon) icon = g_themed_icon_new(APP_ID);
    g_notification_set_icon(n, icon);
    g_notification_set_default_action(n, "app.page-home");
    g_notification_add_button(n, "Abrir Lançamentos", "app.page-moves");
    g_string_free(body, TRUE);
    return n;
}

void notify_check(gboolean force) {
    if (!APP || !APP->state) return;
    if (!force) {
        if (!APP->prefs->notifications || APP->locked) return;
        g_autoptr(GDateTime) now = g_date_time_new_now_local();
        if (g_date_time_get_hour(now) < 9) return;
    }
    g_autoptr(GPtrArray) items = reminders(APP->state, APP->today, 2);
    if (!items->len) {
        if (force) app_toast("Nada vencendo nos próximos 2 dias");
        return;
    }
    g_autofree char *key = notice_key(items, APP->today);
    if (!force && g_strcmp0(APP->prefs->last_notified, key) == 0) return;
    g_autoptr(GNotification) n = build(APP->state, items, APP->today);
    g_application_send_notification(G_APPLICATION(APP->gapp), "vencimentos", n);
    g_free(APP->prefs->last_notified);
    APP->prefs->last_notified = g_steal_pointer(&key);
    prefs_save(APP->prefs, NULL);
}

static gboolean quit_later(gpointer app) {
    g_application_quit(app);
    return G_SOURCE_REMOVE;
}

int notify_headless(GApplication *app) {
    /* dá um instante para o serviço de notificações responder e então encerra */
    g_timeout_add(1500, quit_later, app);
    Prefs *pr = prefs_load(NULL);
    if (!pr->notifications) { prefs_free(pr); return 0; }
    g_autoptr(GError) e = NULL;
    Store *st = store_open(NULL, &e);
    if (!st || e) { store_free(st); prefs_free(pr); return 0; }
    AppState *s = store_load(st, NULL, &e);
    if (!s) { store_free(st); prefs_free(pr); return 0; }
    Day today = day_today();
    if (generate_recurring(s, today) > 0) store_save(st, s, NULL);
    GPtrArray *items = reminders(s, today, 2);
    if (items->len) {
        g_autofree char *key = notice_key(items, today);
        if (g_strcmp0(pr->last_notified, key) != 0) {
            GNotification *n = build(s, items, today);
            g_application_send_notification(app, "vencimentos", n);
            g_object_unref(n);
            GDBusConnection *c = g_application_get_dbus_connection(app);
            if (c) g_dbus_connection_flush_sync(c, NULL, NULL);
            g_free(pr->last_notified);
            pr->last_notified = g_steal_pointer(&key);
            prefs_save(pr, NULL);
        }
    }
    g_ptr_array_unref(items);
    app_state_free(s);
    store_free(st);
    prefs_free(pr);
    return 0;
}

static char *autostart_path(void) { return g_build_filename(g_get_user_config_dir(), "autostart", AUTOSTART_NAME, NULL); }

gboolean notify_autostart_enabled(void) {
    g_autofree char *p = autostart_path();
    return g_file_test(p, G_FILE_TEST_EXISTS);
}

gboolean notify_autostart_set(gboolean on, GError **error) {
    g_autofree char *p = autostart_path();
    if (!on) {
        if (g_file_test(p, G_FILE_TEST_EXISTS) && g_unlink(p) != 0) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "Não foi possível remover %s", p);
            return FALSE;
        }
        return TRUE;
    }
    g_autofree char *dir = g_path_get_dirname(p);
    g_mkdir_with_parents(dir, 0700);
    const char *content =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Finan+ (avisos de vencimento)\n"
        "Comment=Verifica contas a pagar, valores a receber e faturas ao entrar na sessão\n"
        "Exec=finan-plus --avisos\n"
        "Icon=" APP_ID "\n"
        "NoDisplay=true\n"
        "X-GNOME-Autostart-enabled=true\n"
        "X-GNOME-Autostart-Delay=60\n";
    return g_file_set_contents(p, content, -1, error);
}
