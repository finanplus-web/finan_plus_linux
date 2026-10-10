/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Janela "Sobre" e lista de atalhos de teclado.
 */
#include "pages.h"

void show_about(void) {
    AdwDialog *d = adw_about_dialog_new();
    AdwAboutDialog *a = ADW_ABOUT_DIALOG(d);
    adw_about_dialog_set_application_name(a, "Finan+");
    adw_about_dialog_set_application_icon(a, APP_ID);
    adw_about_dialog_set_version(a, APP_VERSION);
    adw_about_dialog_set_developer_name(a, "Juscelino Be");
    adw_about_dialog_set_comments(a, "Controle financeiro pessoal: simples, privado e offline. "
                                     "Seus dados ficam neste computador, criptografados.");
    adw_about_dialog_set_copyright(a, "© 2026 Juscelino Be");
    adw_about_dialog_set_license_type(a, GTK_LICENSE_GPL_3_0);
    const char *devs[] = {"Juscelino Be (autor e idealizador)", "Desenvolvido com auxílio de inteligência artificial", NULL};
    adw_about_dialog_set_developers(a, devs);
    adw_about_dialog_add_legal_section(a, "Ícones Material Symbols", "© Google LLC", GTK_LICENSE_APACHE_2_0, NULL);
    adw_about_dialog_add_legal_section(a, "GTK 4 e libadwaita", "© The GNOME Project", GTK_LICENSE_LGPL_2_1, NULL);
    adw_about_dialog_add_legal_section(a, "libsodium", "© Frank Denis", GTK_LICENSE_CUSTOM,
                                       "Licença ISC: https://opensource.org/license/isc-license-txt");
    adw_about_dialog_add_legal_section(a, "libsecret e json-glib", "© The GNOME Project", GTK_LICENSE_LGPL_2_1, NULL);
    adw_about_dialog_set_release_notes_version(a, APP_VERSION);
    adw_about_dialog_set_release_notes(a,
        "<p>1.2.0: o que chegou ao app Android e ao Finan+ web:</p>"
        "<ul><li>Calendário de lançamentos (Lançamentos › Calendário), com o saldo de cada dia e lançar direto numa data</li>"
        "<li>Simulador \u201cE se…?\u201d em Relatórios: economizar, comprar, mudança na renda e antecipar uma dívida, sem mexer nos dados</li>"
        "<li>Relatórios com o mesmo ‹ mês › da Lista e comparação justa com os mesmos dias do mês anterior</li>"
        "<li>Início e Lista mais enxutos: a receber e a pagar, assistente em 2 frases, \u201cComece por aqui\u201d, filtros de um toque e lançamentos por dia</li>"
        "<li>Limite de tentativas do PIN gravado no computador, bloqueio automático com \u201cImediatamente\u201d e \u201cSó ao abrir o app\u201d e \u201cDescartar alterações?\u201d nos editores</li></ul>"
        "<p>1.1.8: o assistente não avisa mais que as despesas vão passar das receitas com base em uma ou duas compras (mínimo de 5 despesas no mês, 3 por categoria com limite; compra grande isolada conta uma vez).</p>"
        "<p>1.1.7: recorrência reativada retoma no mês atual; pagamento de fatura não pode ser desmarcado; backups com valores gigantes são recusados.</p>"
        "<p>1.1.6: listas arredondadas (como em Sobre) sem o quadrado claro atrás dos cantos quando o sistema usa tema próprio.</p>"
        "<p>1.1.5: acabamento visual — aviso de lista vazia em Lançamentos, cores do tema também em botões, seleção e campos quando o sistema usa tema próprio (ex.: KDE), só ícones Material.</p>"
        "<p>1.1.4: Relatórios não fecha mais o app quando não há lançamentos.</p>"
        "<p>1.1.3: botões de fechar, minimizar e maximizar também na tela de bloqueio.</p>"
        "<p>1.1.2: os temas voltam a funcionar com o sistema em português (as cores saíam com vírgula decimal).</p>"
        "<p>1.1.1: os temas voltam a trocar em sistemas que forçam cores do libadwaita (gtk.css do usuário, GTK_THEME ou libadwaita 1.6+).</p>"
        "<p>Primeira versão para Linux, com todas as funções do app Android:</p>"
        "<ul><li>Janela para computador e notebook, com barra lateral e várias colunas</li>"
        "<li>Dados criptografados com chave no chaveiro do sistema</li>"
        "<li>Assistente no computador, relatório em PDF e avisos de vencimento</li>"
        "<li>Atalhos de teclado (Ctrl+?)</li></ul>");
    adw_dialog_present(d, GTK_WIDGET(APP->window));
}

static const char *SHORTCUTS_UI =
    "<interface>"
    "<object class='GtkShortcutsWindow' id='w'><property name='modal'>1</property><property name='title'>Atalhos de teclado</property>"
    "<child><object class='GtkShortcutsSection'><property name='section-name'>main</property>"
    "<child><object class='GtkShortcutsGroup'><property name='title'>Lançamentos</property>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Nova despesa</property><property name='accelerator'>&lt;Control&gt;n</property></object></child>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Nova receita</property><property name='accelerator'>&lt;Control&gt;&lt;Shift&gt;n</property></object></child>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Nova meta</property><property name='accelerator'>&lt;Control&gt;m</property></object></child>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Buscar lançamentos</property><property name='accelerator'>&lt;Control&gt;f</property></object></child>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Perguntar ao assistente</property><property name='accelerator'>&lt;Control&gt;k</property></object></child>"
    "</object></child>"
    "<child><object class='GtkShortcutsGroup'><property name='title'>Navegação</property>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Início</property><property name='accelerator'>&lt;Control&gt;1</property></object></child>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Lançamentos</property><property name='accelerator'>&lt;Control&gt;2</property></object></child>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Relatórios</property><property name='accelerator'>&lt;Control&gt;3</property></object></child>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Assistente</property><property name='accelerator'>&lt;Control&gt;4</property></object></child>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Ajustes</property><property name='accelerator'>&lt;Control&gt;5 &lt;Control&gt;comma</property></object></child>"
    "</object></child>"
    "<child><object class='GtkShortcutsGroup'><property name='title'>Privacidade e dados</property>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Ocultar ou mostrar valores</property><property name='accelerator'>&lt;Control&gt;h</property></object></child>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Bloquear agora (com PIN)</property><property name='accelerator'>&lt;Control&gt;l</property></object></child>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Relatório em PDF</property><property name='accelerator'>&lt;Control&gt;p</property></object></child>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Exportar CSV</property><property name='accelerator'>&lt;Control&gt;e</property></object></child>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Salvar backup JSON</property><property name='accelerator'>&lt;Control&gt;s</property></object></child>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Restaurar backup</property><property name='accelerator'>&lt;Control&gt;o</property></object></child>"
    "</object></child>"
    "<child><object class='GtkShortcutsGroup'><property name='title'>Geral</property>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Atalhos de teclado</property><property name='accelerator'>&lt;Control&gt;question</property></object></child>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Menu principal</property><property name='accelerator'>F10</property></object></child>"
    "<child><object class='GtkShortcutsShortcut'><property name='title'>Sair</property><property name='accelerator'>&lt;Control&gt;q</property></object></child>"
    "</object></child>"
    "</object></child></object></interface>";

void show_shortcuts(void) {
    GtkBuilder *b = gtk_builder_new_from_string(SHORTCUTS_UI, -1);
    GtkWindow *w = GTK_WINDOW(gtk_builder_get_object(b, "w"));
    gtk_window_set_transient_for(w, APP->window);
    gtk_window_present(w);
    g_object_unref(b);
}
