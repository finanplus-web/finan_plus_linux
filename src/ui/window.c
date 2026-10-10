/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Janela principal, pensada para computador e notebook:
 * - barra lateral com as cinco seções (Início, Lançamentos, Relatórios, Assistente, Ajustes)
 *   e o saldo sempre à vista;
 * - área de conteúdo com várias colunas, que se reorganiza conforme a largura da janela
 *   (em telas estreitas a barra lateral vira uma página e as colunas viram uma só);
 * - atalhos de teclado para as ações principais;
 * - bloqueio por PIN, avisos de vencimento e virada automática do dia.
 */
#include "app.h"
#include "pages.h"
#include "theme.h"
#include "widgets.h"
#include "core/backup.h"
#include <stdlib.h>
#include <string.h>

App *APP = NULL;

static const struct { const char *title; const char *icon; const char *icon_on; } PAGES[PAGE_COUNT] = {
    {"Início", "home", "home-fill"},
    {"Lançamentos", "swap-horiz", "swap-horiz-fill"},
    {"Relatórios", "pie-chart", "pie-chart-fill"},
    {"Assistente", "auto-awesome", "auto-awesome"},
    {"Ajustes", "settings", "settings-fill"},
};

static struct {
    GtkWidget *root_stack;   /* "main" | "lock" | "problem" */
    AdwNavigationSplitView *split;
    GtkWidget *content_stack;
    GtkWidget *sidebar_list;
    GtkWidget *sidebar_total;
    AdwWindowTitle *title;
    GtkWidget *privacy_btn;
    GtkWidget *exp_btn, *inc_btn;
    GtkWidget *lock_btn;
    GtkWidget *nav_icons[PAGE_COUNT];
    guint timer;
} W;

/* ------------------------------------------------------------------ dados */

gboolean app_save(GError **error) {
    if (APP->problem) {
        g_set_error(error, STORE_ERROR, STORE_ERROR_UNREADABLE, "Os dados salvos não foram abertos; nada foi gravado.");
        return FALSE;
    }
    return store_save(APP->store, APP->state, error);
}

static void refresh_sidebar(void);

void app_refresh_page(PageId p) {
    if (APP->page_refresh[p]) APP->page_refresh[p]();
}

void app_refresh(void) {
    for (int p = 0; p < PAGE_COUNT; p++) app_refresh_page((PageId)p);
    refresh_sidebar();
}

void app_commit(void) {
    g_autoptr(GError) e = NULL;
    if (!app_save(&e)) dlg_notice("Não foi possível salvar", e ? e->message : "Erro desconhecido ao gravar os dados.");
    app_refresh();
}

gboolean app_hidden(void) { return APP && APP->state && APP->state->privacy; }

MoneyFmt app_money_fmt(void) { return app_hidden() ? money_fmt_hidden : money_fmt; }

char *app_money(Cents c) { return app_money_fmt()(c); }

void app_toast(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    g_autofree char *msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    AdwToast *t = adw_toast_new(msg);
    adw_toast_set_timeout(t, 3);
    adw_toast_overlay_add_toast(APP->toasts, t);
}

void filters_this_month(Filters *f) {
    Ym ym = day_ym(APP->today);
    f->from = ym_first(ym);
    f->to = ym_last(ym);
}

/* ------------------------------------------------------------------ navegação */

static void update_nav_icons(void) {
    for (int p = 0; p < PAGE_COUNT; p++) {
        g_autofree char *n = icon_name(p == (int)APP->page ? PAGES[p].icon_on : PAGES[p].icon);
        gtk_image_set_from_icon_name(GTK_IMAGE(W.nav_icons[p]), n);
    }
}

void app_show_page(PageId p) {
    APP->page = p;
    gtk_stack_set_visible_child(GTK_STACK(W.content_stack), APP->page_root[p]);
    adw_window_title_set_title(W.title, PAGES[p].title);
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(W.sidebar_list), (int)p);
    if (row && gtk_list_box_get_selected_row(GTK_LIST_BOX(W.sidebar_list)) != row)
        gtk_list_box_select_row(GTK_LIST_BOX(W.sidebar_list), row);
    adw_navigation_split_view_set_show_content(W.split, TRUE);
    update_nav_icons();
}

void app_open_moves(const char *query, Day from, Day to, int kind) {
    Filters *f = &APP->filters;
    g_free(f->query);
    f->query = g_strdup(query ? query : "");
    f->from = from;
    f->to = to;
    f->kind = kind;
    f->paid = -1;
    app_refresh_page(PAGE_MOVES);
    app_refresh_page(PAGE_REPORTS);
    app_show_page(PAGE_MOVES);
}

void app_open_moves_pending(void) {
    Filters *f = &APP->filters;
    g_free(f->query);
    f->query = g_strdup("");
    f->kind = -1;
    f->paid = 0;
    app_refresh_page(PAGE_MOVES);
    app_refresh_page(PAGE_REPORTS);
    app_show_page(PAGE_MOVES);
}

static void on_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer u) {
    (void)box; (void)u;
    if (!row) return;
    int i = gtk_list_box_row_get_index(row);
    if (i >= 0 && i < PAGE_COUNT && (PageId)i != APP->page) app_show_page((PageId)i);
    else adw_navigation_split_view_set_show_content(W.split, TRUE);
}

static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer u) {
    (void)box; (void)u;
    int i = gtk_list_box_row_get_index(row);
    if (i >= 0 && i < PAGE_COUNT) app_show_page((PageId)i);
}

static void refresh_sidebar(void) {
    if (!W.sidebar_total) return;
    w_clear(W.sidebar_total);
    Cents bal = current_balance(APP->state);
    Cents fut = future_balance(APP->state, ym_last(day_ym(APP->today)), APP->today);
    w_add(W.sidebar_total, w_eyebrow("Saldo atual"));
    w_add(W.sidebar_total, w_money(bal, bal < 0 ? "fin-money-mid fin-red" : "fin-money-mid"));
    GtkWidget *l = w_label("Previsto no fim do mês", "fin-muted caption");
    gtk_widget_set_margin_top(l, 6);
    w_add(W.sidebar_total, l);
    w_add(W.sidebar_total, w_money(fut, fut < 0 ? "fin-red" : "fin-accent"));
    gtk_widget_set_tooltip_text(W.sidebar_total, app_hidden() ? "Valores ocultos (Ctrl+H para mostrar)" : NULL);
    g_autofree char *pi = icon_name(app_hidden() ? "visibility-off" : "visibility");
    gtk_button_set_icon_name(GTK_BUTTON(W.privacy_btn), pi);
    gtk_widget_set_tooltip_text(W.privacy_btn, app_hidden() ? "Mostrar valores (Ctrl+H)" : "Ocultar valores (Ctrl+H)");
    gtk_widget_set_visible(W.lock_btn, app_lock_enabled());
    g_autofree char *sub = br_full_date(APP->today);
    adw_window_title_set_subtitle(W.title, sub);
}

/* ------------------------------------------------------------------ layout responsivo */

static void on_width(int width) {
    Layout l = width < 720 ? LAYOUT_NARROW : width < 1080 ? LAYOUT_MEDIUM : LAYOUT_WIDE;
    if (l == APP->layout) return;
    APP->layout = l;
    /* em janelas estreitas os botões do topo ficam só com o ícone */
    gboolean compact = l == LAYOUT_NARROW;
    adw_button_content_set_label(ADW_BUTTON_CONTENT(gtk_button_get_child(GTK_BUTTON(W.exp_btn))), compact ? "" : "Despesa");
    adw_button_content_set_label(ADW_BUTTON_CONTENT(gtk_button_get_child(GTK_BUTTON(W.inc_btn))), compact ? "" : "Receita");
    app_refresh();
}

/* ------------------------------------------------------------------ bloqueio */

gboolean app_lock_enabled(void) { return prefs_has_pin(APP->prefs); }

void app_lock(void) {
    if (!app_lock_enabled() || APP->problem) return;
    /* fecha seletores de arquivo abertos (backup, CSV, PDF, restaurar) */
    g_cancellable_cancel(APP->files);
    g_object_unref(APP->files);
    APP->files = g_cancellable_new();
    APP->locked = TRUE;
    lock_page_reset();
    gtk_stack_set_visible_child_name(GTK_STACK(W.root_stack), "lock");
    /* fecha diálogos abertos para não deixar dados à mostra */
    AdwDialog *d;
    while ((d = adw_application_window_get_visible_dialog(ADW_APPLICATION_WINDOW(APP->window)))) adw_dialog_force_close(d);
}

void app_unlocked(void) {
    APP->locked = FALSE;
    pin_throttle_reset(&APP->throttle);
    gtk_stack_set_visible_child_name(GTK_STACK(W.root_stack), "main");
    app_refresh();
}

/* ------------------------------------------------------------------ ações */

static void act(GSimpleAction *a, GVariant *p, gpointer u);

static void new_expense(void) { if (!APP->locked) editor_tx(KIND_EXPENSE, NULL); }
static void new_income(void) { if (!APP->locked) editor_tx(KIND_INCOME, NULL); }

static void toggle_privacy(void) {
    APP->state->privacy = !APP->state->privacy;
    app_commit();
    app_toast(APP->state->privacy ? "Valores ocultos" : "Valores visíveis");
}

static void act(GSimpleAction *a, GVariant *p, gpointer u) {
    (void)p; (void)u;
    const char *n = g_action_get_name(G_ACTION(a));
    if (APP->locked && strcmp(n, "quit") != 0 && strcmp(n, "about") != 0) return;
    if (APP->problem && strcmp(n, "quit") != 0 && strcmp(n, "about") != 0 && strcmp(n, "shortcuts") != 0) return;
    /* com um diálogo aberto, os atalhos globais esperam (evita abrir um editor por cima do outro) */
    if (adw_application_window_get_visible_dialog(ADW_APPLICATION_WINDOW(APP->window)) && strcmp(n, "quit") != 0) return;
    if (!strcmp(n, "new-expense")) new_expense();
    else if (!strcmp(n, "new-income")) new_income();
    else if (!strcmp(n, "new-goal")) editor_goal(NULL);
    else if (!strcmp(n, "page-home")) app_show_page(PAGE_HOME);
    else if (!strcmp(n, "page-moves")) app_show_page(PAGE_MOVES);
    else if (!strcmp(n, "page-reports")) app_show_page(PAGE_REPORTS);
    else if (!strcmp(n, "page-assist")) app_show_page(PAGE_ASSIST);
    else if (!strcmp(n, "page-settings")) app_show_page(PAGE_SETTINGS);
    else if (!strcmp(n, "search")) { app_show_page(PAGE_MOVES); page_moves_focus_search(); }
    else if (!strcmp(n, "ask")) { app_show_page(PAGE_ASSIST); page_assist_focus_question(); }
    else if (!strcmp(n, "privacy")) toggle_privacy();
    else if (!strcmp(n, "lock")) app_lock();
    else if (!strcmp(n, "pdf")) report_pdf_dialog(APP->filters.from, APP->filters.to);
    else if (!strcmp(n, "csv")) data_export_csv();
    else if (!strcmp(n, "backup")) data_export_backup();
    else if (!strcmp(n, "restore")) data_restore_backup();
    else if (!strcmp(n, "shortcuts")) show_shortcuts();
    else if (!strcmp(n, "about")) show_about();
    else if (!strcmp(n, "quit")) gtk_window_close(APP->window);
}

static void install_actions(GtkApplication *app) {
    static const struct { const char *name; const char *accels[3]; } A[] = {
        {"new-expense", {"<Control>n", NULL}}, {"new-income", {"<Control><Shift>n", NULL}}, {"new-goal", {"<Control>m", NULL}},
        {"page-home", {"<Control>1", NULL}}, {"page-moves", {"<Control>2", NULL}}, {"page-reports", {"<Control>3", NULL}},
        {"page-assist", {"<Control>4", NULL}}, {"page-settings", {"<Control>5", "<Control>comma", NULL}},
        {"search", {"<Control>f", NULL}}, {"ask", {"<Control>k", NULL}}, {"privacy", {"<Control>h", NULL}},
        {"lock", {"<Control>l", NULL}}, {"pdf", {"<Control>p", NULL}}, {"csv", {"<Control>e", NULL}},
        {"backup", {"<Control>s", NULL}}, {"restore", {"<Control>o", NULL}}, {"shortcuts", {"<Control>question", NULL}},
        {"about", {NULL}}, {"quit", {"<Control>q", NULL}},
    };
    for (guint i = 0; i < G_N_ELEMENTS(A); i++) {
        GSimpleAction *sa = g_simple_action_new(A[i].name, NULL);
        g_signal_connect(sa, "activate", G_CALLBACK(act), NULL);
        g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(sa));
        g_object_unref(sa);
        g_autofree char *full = g_strdup_printf("app.%s", A[i].name);
        if (A[i].accels[0]) gtk_application_set_accels_for_action(app, full, A[i].accels);
    }
}

/* ------------------------------------------------------------------ relógio: dia, bloqueio e avisos */

static gboolean tick(gpointer u) {
    (void)u;
    Day t = day_today();
    if (t != APP->today) {
        /* virou o dia: saldos previstos, faturas, "Em atraso", resumo e recorrências do mês novo */
        APP->today = t;
        if (generate_recurring(APP->state, t) > 0) app_commit();
        else app_refresh();
    }
    /* bloqueio automático: janela sem foco (ou minimizada) pelo tempo escolhido */
    int mins = APP->state->auto_lock;
    if (!APP->locked && app_lock_enabled() && mins > 0 && APP->inactive_since > 0 &&
        g_get_monotonic_time() - APP->inactive_since >= (gint64)mins * 60 * G_USEC_PER_SEC)
        app_lock();
    notify_check(FALSE);
    return G_SOURCE_CONTINUE;
}

static void on_active(GObject *win, GParamSpec *ps, gpointer u) {
    (void)ps; (void)u;
    if (gtk_window_is_active(GTK_WINDOW(win))) {
        APP->inactive_since = 0;
        tick(NULL);
    } else if (!APP->inactive_since) APP->inactive_since = g_get_monotonic_time();
}

static gboolean on_close(GtkWindow *win, gpointer u) {
    (void)u;
    int w = 0, h = 0;
    gtk_window_get_default_size(win, &w, &h);
    APP->prefs->window_maximized = gtk_window_is_maximized(win);
    if (!APP->prefs->window_maximized && w > 0 && h > 0) {
        APP->prefs->window_width = w;
        APP->prefs->window_height = h;
    }
    prefs_save(APP->prefs, NULL);
    if (W.timer) g_source_remove(W.timer);
    W.timer = 0;
    return FALSE;
}

/* ------------------------------------------------------------------ problema ao abrir os dados */

static void problem_quarantine(GtkButton *b, gpointer u) {
    (void)b; (void)u;
    g_autoptr(GError) e = NULL;
    g_autofree char *moved = store_quarantine(APP->store, &e);
    if (!moved) { dlg_notice("Não foi possível mover o arquivo", e->message); return; }
    app_state_free(APP->state);
    APP->state = app_state_new();
    APP->problem = FALSE;
    app_save(NULL);
    gtk_stack_set_visible_child_name(GTK_STACK(W.root_stack), "main");
    app_refresh();
    g_autofree char *msg = g_strdup_printf("O arquivo antigo foi guardado em:\n%s\n\nNada foi apagado. Se você tiver um backup JSON, "
                                           "restaure em Ajustes › Dados.", moved);
    dlg_notice("Começando do zero", msg);
}

static void problem_quit(GtkButton *b, gpointer u) { (void)b; (void)u; gtk_window_close(APP->window); }

static GtkWidget *problem_page(const char *message) {
    GtkWidget *sp = adw_status_page_new();
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(sp), "fin-warning-symbolic");
    adw_status_page_set_title(ADW_STATUS_PAGE(sp), "Não foi possível abrir seus dados");
    g_autofree char *desc = g_strdup_printf(
        "%s\n\nIsso acontece se o chaveiro do sistema foi apagado ou trocado, ou se o arquivo foi danificado. "
        "Você pode guardar o arquivo à parte (nada é apagado) e começar do zero, restaurando depois um backup JSON.", message);
    adw_status_page_set_description(ADW_STATUS_PAGE(sp), desc);
    GtkWidget *box = w_hbox(12);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    GtkWidget *q = gtk_button_new_with_label("Sair");
    gtk_widget_add_css_class(q, "pill");
    g_signal_connect(q, "clicked", G_CALLBACK(problem_quit), NULL);
    GtkWidget *ok = gtk_button_new_with_label("Guardar o arquivo à parte e começar do zero");
    gtk_widget_add_css_class(ok, "pill");
    gtk_widget_add_css_class(ok, "destructive-action");
    g_signal_connect(ok, "clicked", G_CALLBACK(problem_quarantine), NULL);
    w_add(box, q);
    w_add(box, ok);
    adw_status_page_set_child(ADW_STATUS_PAGE(sp), box);
    return sp;
}

/* ------------------------------------------------------------------ construção da janela */

static GtkWidget *build_sidebar(void) {
    GtkWidget *tv = adw_toolbar_view_new();
    GtkWidget *hb = adw_header_bar_new();
    GtkWidget *brand = w_hbox(8);
    GtkWidget *logo = gtk_image_new_from_icon_name(APP_ID);
    gtk_image_set_pixel_size(GTK_IMAGE(logo), 22);
    w_add(brand, logo);
    w_add(brand, w_label("Finan+", "heading"));
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(hb), brand);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), hb);

    GtkWidget *box = w_vbox(0);
    GtkWidget *list = gtk_list_box_new();
    gtk_widget_add_css_class(list, "navigation-sidebar");
    gtk_accessible_update_property(GTK_ACCESSIBLE(list), GTK_ACCESSIBLE_PROPERTY_LABEL, "Seções", -1);
    static const char *const keys[PAGE_COUNT] = {"Ctrl+1", "Ctrl+2", "Ctrl+3", "Ctrl+4", "Ctrl+5"};
    for (int p = 0; p < PAGE_COUNT; p++) {
        GtkWidget *row = w_hbox(12);
        W.nav_icons[p] = w_icon(PAGES[p].icon, 20);
        w_add(row, W.nav_icons[p]);
        w_add(row, w_label(PAGES[p].title, NULL));
        gtk_list_box_append(GTK_LIST_BOX(list), row);
        GtkWidget *r = gtk_widget_get_parent(row);
        g_autofree char *tip = g_strdup_printf("%s (%s)", PAGES[p].title, keys[p]);
        gtk_widget_set_tooltip_text(r, tip);
    }
    g_signal_connect(list, "row-selected", G_CALLBACK(on_row_selected), NULL);
    g_signal_connect(list, "row-activated", G_CALLBACK(on_row_activated), NULL);
    W.sidebar_list = list;
    gtk_widget_set_vexpand(list, TRUE);
    w_add(box, list);
    W.sidebar_total = w_vbox(2);
    gtk_widget_add_css_class(W.sidebar_total, "fin-sidebar-total");
    w_add(box, W.sidebar_total);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), box);
    return tv;
}

static GtkWidget *build_menu_button(void) {
    GMenu *m = g_menu_new();
    GMenu *s1 = g_menu_new();
    g_menu_append(s1, "Nova despesa", "app.new-expense");
    g_menu_append(s1, "Nova receita", "app.new-income");
    g_menu_append(s1, "Nova meta", "app.new-goal");
    GMenu *s2 = g_menu_new();
    g_menu_append(s2, "Relatório em PDF…", "app.pdf");
    g_menu_append(s2, "Exportar CSV…", "app.csv");
    g_menu_append(s2, "Salvar backup JSON…", "app.backup");
    g_menu_append(s2, "Restaurar backup…", "app.restore");
    GMenu *s3 = g_menu_new();
    g_menu_append(s3, "Atalhos de teclado", "app.shortcuts");
    g_menu_append(s3, "Sobre o Finan+", "app.about");
    g_menu_append_section(m, NULL, G_MENU_MODEL(s1));
    g_menu_append_section(m, NULL, G_MENU_MODEL(s2));
    g_menu_append_section(m, NULL, G_MENU_MODEL(s3));
    GtkWidget *mb = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(mb), "fin-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(mb), G_MENU_MODEL(m));
    gtk_menu_button_set_primary(GTK_MENU_BUTTON(mb), TRUE);
    gtk_widget_set_tooltip_text(mb, "Menu principal (F10)");
    g_object_unref(m); g_object_unref(s1); g_object_unref(s2); g_object_unref(s3);
    return mb;
}

static void do_action(gpointer name) { g_action_group_activate_action(G_ACTION_GROUP(APP->gapp), name, NULL); }

static GtkWidget *build_content(void) {
    GtkWidget *tv = adw_toolbar_view_new();
    GtkWidget *hb = adw_header_bar_new();
    W.title = ADW_WINDOW_TITLE(adw_window_title_new("Início", ""));
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(hb), GTK_WIDGET(W.title));

    GtkWidget *exp = w_button("Despesa", "remove", "suggested-action", do_action, "new-expense", NULL);
    gtk_widget_set_tooltip_text(exp, "Nova despesa (Ctrl+N)");
    GtkWidget *inc = w_button("Receita", "add", NULL, do_action, "new-income", NULL);
    gtk_widget_set_tooltip_text(inc, "Nova receita (Ctrl+Shift+N)");
    adw_header_bar_pack_start(ADW_HEADER_BAR(hb), exp);
    adw_header_bar_pack_start(ADW_HEADER_BAR(hb), inc);
    W.exp_btn = exp;
    W.inc_btn = inc;

    adw_header_bar_pack_end(ADW_HEADER_BAR(hb), build_menu_button());
    W.lock_btn = w_button(NULL, "lock", "flat", do_action, "lock", NULL);
    gtk_widget_set_tooltip_text(W.lock_btn, "Bloquear agora (Ctrl+L)");
    adw_header_bar_pack_end(ADW_HEADER_BAR(hb), W.lock_btn);
    W.privacy_btn = w_button(NULL, "visibility", "flat", do_action, "privacy", NULL);
    adw_header_bar_pack_end(ADW_HEADER_BAR(hb), W.privacy_btn);
    GtkWidget *search = w_button(NULL, "search", "flat", do_action, "search", NULL);
    gtk_widget_set_tooltip_text(search, "Buscar lançamentos (Ctrl+F)");
    adw_header_bar_pack_end(ADW_HEADER_BAR(hb), search);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), hb);

    W.content_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(W.content_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(W.content_stack), 120);
    GtkWidget *(*makers[PAGE_COUNT])(void) = {page_home_new, page_moves_new, page_reports_new, page_assist_new, page_settings_new};
    void (*refreshers[PAGE_COUNT])(void) = {page_home_refresh, page_moves_refresh, page_reports_refresh, page_assist_refresh,
                                            page_settings_refresh};
    for (int p = 0; p < PAGE_COUNT; p++) {
        APP->page_root[p] = makers[p]();
        APP->page_refresh[p] = refreshers[p];
        gtk_stack_add_child(GTK_STACK(W.content_stack), APP->page_root[p]);
    }
    GtkWidget *bin = fin_width_bin_new(W.content_stack, on_width);
    gtk_widget_add_css_class(bin, "fin-content");
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), bin);
    return tv;
}

static GtkWidget *build_window(GtkApplication *app) {
    GtkWidget *win = adw_application_window_new(app);
    APP->window = GTK_WINDOW(win);
    gtk_window_set_title(GTK_WINDOW(win), "Finan+");
    gtk_window_set_icon_name(GTK_WINDOW(win), APP_ID);
    gtk_window_set_default_size(GTK_WINDOW(win), MAX(APP->prefs->window_width, 360), MAX(APP->prefs->window_height, 480));
    gtk_widget_set_size_request(win, 360, 480);
    if (APP->prefs->window_maximized) gtk_window_maximize(GTK_WINDOW(win));

    GtkWidget *split = adw_navigation_split_view_new();
    W.split = ADW_NAVIGATION_SPLIT_VIEW(split);
    AdwNavigationPage *side = adw_navigation_page_new(build_sidebar(), "Finan+");
    AdwNavigationPage *content = adw_navigation_page_new(build_content(), "Finan+");
    adw_navigation_split_view_set_sidebar(W.split, side);
    adw_navigation_split_view_set_content(W.split, content);
    adw_navigation_split_view_set_min_sidebar_width(W.split, 220);
    adw_navigation_split_view_set_max_sidebar_width(W.split, 260);

    /* janela estreita: a barra lateral vira uma página */
    AdwBreakpoint *bp = adw_breakpoint_new(adw_breakpoint_condition_parse("max-width: 720sp"));
    GValue v = G_VALUE_INIT;
    g_value_init(&v, G_TYPE_BOOLEAN);
    g_value_set_boolean(&v, TRUE);
    adw_breakpoint_add_setter(bp, G_OBJECT(split), "collapsed", &v);
    g_value_unset(&v);
    adw_application_window_add_breakpoint(ADW_APPLICATION_WINDOW(win), bp);

    W.root_stack = gtk_stack_new();
    gtk_stack_add_named(GTK_STACK(W.root_stack), split, "main");
    gtk_stack_add_named(GTK_STACK(W.root_stack), lock_page_new(), "lock");
    GtkWidget *overlay = adw_toast_overlay_new();
    APP->toasts = ADW_TOAST_OVERLAY(overlay);
    adw_toast_overlay_set_child(APP->toasts, W.root_stack);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), overlay);

    g_signal_connect(win, "notify::is-active", G_CALLBACK(on_active), NULL);
    g_signal_connect(win, "close-request", G_CALLBACK(on_close), NULL);
    return win;
}

/* ------------------------------------------------------------------ inicialização */

static void on_dark_changed(AdwStyleManager *sm, GParamSpec *p, gpointer u) {
    (void)sm; (void)p; (void)u;
    if (APP->state->theme == THEME_AUTO) { theme_apply(THEME_AUTO); app_refresh(); }
}

static Dictionary *load_dictionary(void) {
    g_autoptr(GBytes) b = g_resources_lookup_data("/com/finanplus/FinanPlus/dicionario.txt", 0, NULL);
    if (!b) return dictionary_parse("");
    gsize n;
    const char *data = g_bytes_get_data(b, &n);
    g_autofree char *text = g_strndup(data, n);
    return dictionary_parse(text);
}

void app_activate(GtkApplication *gapp) {
    if (APP && APP->window) { gtk_window_present(APP->window); return; }
    APP = g_new0(App, 1);
    APP->gapp = ADW_APPLICATION(gapp);
    APP->today = day_today();
    APP->prefs = prefs_load(NULL);
    APP->dict = load_dictionary();
    APP->filters.kind = -1;
    APP->filters.paid = -1;
    APP->filters.query = g_strdup("");
    filters_this_month(&APP->filters);
    APP->layout = LAYOUT_WIDE;
    APP->moves_view = MOVES_LIST;
    APP->cal_ym = YM_NONE;
    APP->cal_day = DAY_NONE;
    APP->files = g_cancellable_new();

    gtk_icon_theme_add_resource_path(gtk_icon_theme_get_for_display(gdk_display_get_default()), "/com/finanplus/FinanPlus/icons");

    g_autoptr(GError) e = NULL;
    APP->store = store_open(NULL, &e);
    char *problem = NULL;
    if (!APP->store) {
        dlg_notice("Erro ao iniciar", e ? e->message : "Falha desconhecida.");
        return;
    }
    if (e) problem = g_strdup(e->message);
    g_clear_error(&e);
    if (!problem) {
        Dropped dr = {0};
        APP->state = store_load(APP->store, &dr, &e);
        if (e) problem = g_strdup(e->message);
    }
    if (!APP->state) APP->state = app_state_new();
    if (!problem && generate_recurring(APP->state, APP->today) > 0) app_save(NULL);

    install_actions(gapp);
    theme_apply(APP->state->theme);
    g_signal_connect(adw_style_manager_get_default(), "notify::dark", G_CALLBACK(on_dark_changed), NULL);

    GtkWidget *win = build_window(gapp);
    if (problem) {
        APP->problem = TRUE;
        GtkWidget *pbox = w_vbox(0);
        GtkWidget *phb = adw_header_bar_new(); /* botões da janela também na tela de problema */
        adw_header_bar_set_show_title(ADW_HEADER_BAR(phb), FALSE);
        gtk_widget_add_css_class(phb, "flat");
        w_add(pbox, phb);
        GtkWidget *pp = problem_page(problem);
        gtk_widget_set_vexpand(pp, TRUE);
        w_add(pbox, pp);
        gtk_stack_add_named(GTK_STACK(W.root_stack), pbox, "problem");
        gtk_stack_set_visible_child_name(GTK_STACK(W.root_stack), "problem");
        g_free(problem);
    } else if (app_lock_enabled()) {
        app_lock();
    }
    app_show_page(PAGE_HOME);
    /* FINAN_PLUS_START_PAGE=0..4 abre direto numa seção (usado para capturas de tela da documentação) */
    const char *sp = g_getenv("FINAN_PLUS_START_PAGE");
    if (sp && *sp) app_show_page((PageId)CLAMP(atoi(sp), 0, PAGE_COUNT - 1));
    /* FINAN_PLUS_START_VIEW=calendario abre Lançamentos no calendário (também só para capturas) */
    const char *sv = g_getenv("FINAN_PLUS_START_VIEW");
    if (sv && !strcmp(sv, "calendario")) APP->moves_view = MOVES_CALENDAR;
    app_refresh();
    gtk_window_present(GTK_WINDOW(win));
    W.timer = g_timeout_add_seconds(20, tick, NULL);
    if (!problem) notify_check(FALSE);
}
