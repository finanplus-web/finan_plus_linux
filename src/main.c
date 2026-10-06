/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Finan+ para Linux: controle financeiro pessoal, offline, com dados criptografados no computador.
 *
 * Uso:
 *   finan-plus            abre o aplicativo
 *   finan-plus --avisos   só verifica vencimentos, mostra uma notificação e sai
 *   finan-plus --diagnostico  mostra versões e configurações que afetam os temas
 *                         (usado pelo início automático da sessão, se ativado em Ajustes)
 */
#include "ui/app.h"
#include "ui/pages.h"
#include <locale.h>

static int on_command_line(GApplication *app, GApplicationCommandLine *cl, gpointer u) {
    (void)u;
    GVariantDict *opts = g_application_command_line_get_options_dict(cl);
    if (g_variant_dict_contains(opts, "avisos")) {
        if (APP && APP->window) { notify_check(TRUE); return 0; }
        return notify_headless(app);
    }
    g_application_activate(app);
    return 0;
}

static void on_activate(GApplication *app, gpointer u) {
    (void)u;
    app_activate(GTK_APPLICATION(app));
}

/* finan-plus --diagnostico: informações para investigar problemas de tema e de dados */
static int diagnostics(void) {
    const char *gt = g_getenv("GTK_THEME");
    g_autofree char *ucss = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "gtk.css", NULL);
    g_print("Finan+ %s\nGTK %u.%u.%u · libadwaita (compilado) %d.%d\nGTK_THEME=%s\n%s: %s\nXDG_SESSION_TYPE=%s · XDG_CURRENT_DESKTOP=%s\n",
            APP_VERSION, gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(), ADW_MAJOR_VERSION, ADW_MINOR_VERSION,
            gt ? gt : "(não definido)", ucss, g_file_test(ucss, G_FILE_TEST_EXISTS) ? "existe" : "não existe",
            g_getenv("XDG_SESSION_TYPE") ? g_getenv("XDG_SESSION_TYPE") : "?", g_getenv("XDG_CURRENT_DESKTOP") ? g_getenv("XDG_CURRENT_DESKTOP") : "?");
    return 0;
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");
    for (int i = 1; i < argc; i++) if (g_str_equal(argv[i], "--diagnostico")) return diagnostics();
    /* GTK_THEME força um tema GTK por cima do libadwaita e impede os temas do Finan+ (vale só para este programa) */
    if (g_getenv("GTK_THEME")) g_unsetenv("GTK_THEME");
    g_set_application_name("Finan+");
    AdwApplication *app = adw_application_new(APP_ID, G_APPLICATION_HANDLES_COMMAND_LINE);
    const GOptionEntry entries[] = {
        {"avisos", 0, 0, G_OPTION_ARG_NONE, NULL, "Verifica vencimentos, mostra uma notificação e sai", NULL},
        {"version", 'v', 0, G_OPTION_ARG_NONE, NULL, "Mostra a versão", NULL},
        G_OPTION_ENTRY_NULL,
    };
    g_application_add_main_option_entries(G_APPLICATION(app), entries);
    g_signal_connect(app, "command-line", G_CALLBACK(on_command_line), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    for (int i = 1; i < argc; i++)
        if (g_str_equal(argv[i], "--version") || g_str_equal(argv[i], "-v")) { g_print("Finan+ %s\n", APP_VERSION); return 0; }
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
