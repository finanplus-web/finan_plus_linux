/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Calendário da aba Lançamentos (visão "Calendário"): o mês em grade com o saldo de cada dia e,
 * ao lado, o dia escolhido com os lançamentos dele. As regras ficam em core/period.c (testadas);
 * aqui só a interface. Mesmo comportamento do app Android 1.3.0 e do Finan+ web 1.2.0.
 *
 * - um clique escolhe o dia; clicar de novo no dia escolhido, dar dois cliques, clicar com o botão
 *   direito ou segurar abre um lançamento novo já com a data (numa data futura ele começa pendente);
 * - Page Up / Page Down e as setas ‹ › trocam de mês; "Voltar para hoje" fora do mês atual.
 */
#include "pages.h"
#include "widgets.h"
#include "core/finance.h"
#include "core/period.h"
#include <string.h>

static struct {
    GtkWidget *root, *body, *left, *right;
    gboolean built;
} C;

static Ym cur_ym(void) {
    if (APP->cal_ym == YM_NONE) { APP->cal_ym = day_ym(APP->today); APP->cal_day = APP->today; }
    return APP->cal_ym;
}

static void shift_month(gpointer delta) {
    APP->cal_ym = cur_ym() + GPOINTER_TO_INT(delta);
    /* o dia escolhido só vale dentro do mês mostrado */
    if (APP->cal_day != DAY_NONE && day_ym(APP->cal_day) != APP->cal_ym) APP->cal_day = DAY_NONE;
    page_calendar_refresh();
}

static void go_today(gpointer u) {
    (void)u;
    APP->cal_ym = day_ym(APP->today);
    APP->cal_day = APP->today;
    page_calendar_refresh();
}

static void new_on(Kind k, Day d) { editor_tx_on(k, d, NULL); }
static void new_income(gpointer d) { new_on(KIND_INCOME, GPOINTER_TO_INT(d)); }
static void new_expense(gpointer d) { new_on(KIND_EXPENSE, GPOINTER_TO_INT(d)); }

/* ---------------------------------------------------------------- dia (célula) */

static void select_day(Day d) {
    if (APP->cal_day == d) { new_on(KIND_EXPENSE, d); return; } /* clicar de novo: lançar nesta data */
    APP->cal_day = d;
    page_calendar_refresh();
}

static void on_cell(GtkButton *b, gpointer u) {
    (void)u;
    select_day(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "day")));
}

/* botão direito ou segurar: lançamento novo com a data (dois cliques = escolher + clicar de novo) */
static void on_cell_secondary(GtkGesture *g, int n, double x, double y, gpointer u) {
    (void)n; (void)x; (void)y; (void)u;
    GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
    Day d = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "day"));
    gtk_gesture_set_state(g, GTK_EVENT_SEQUENCE_CLAIMED);
    APP->cal_day = d;
    new_on(KIND_EXPENSE, d);
    page_calendar_refresh();
}

static void on_cell_long(GtkGestureLongPress *g, double x, double y, gpointer u) {
    on_cell_secondary(GTK_GESTURE(g), 1, x, y, u);
}

static GtkWidget *dot(const char *kind) {
    GtkWidget *d = gtk_drawing_area_new();
    gtk_widget_set_size_request(d, 7, 7);
    gtk_widget_add_css_class(d, "fin-dot");
    gtk_widget_add_css_class(d, kind);
    gtk_widget_set_valign(d, GTK_ALIGN_CENTER);
    return d;
}

static GtkWidget *day_cell(Day d, const CalDay *day) {
    Day today = APP->today;
    gboolean sel = d == APP->cal_day;
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "fin-cal-day");
    if (sel) gtk_widget_add_css_class(b, "sel");
    if (d == today) gtk_widget_add_css_class(b, "today");
    if (d < today) gtk_widget_add_css_class(b, "past");
    gtk_widget_set_hexpand(b, TRUE);
    gtk_widget_set_size_request(b, -1, 64);
    GtkWidget *v = w_vbox(2);
    GtkWidget *top = w_hbox(2);
    char n[4];
    g_snprintf(n, sizeof n, "%d", day_dom(d));
    GtkWidget *nl = w_label(n, "fin-cal-n");
    gtk_widget_set_hexpand(nl, TRUE);
    w_add(top, nl);
    if (day && day->overdue) {
        GtkWidget *wi = w_icon("warning", 12);
        gtk_widget_add_css_class(wi, "fin-red");
        w_add(top, wi);
    }
    w_add(v, top);
    if (day && !app_hidden() && (day->income || day->expense)) {
        g_autofree char *val = cal_signed(cal_day_net(day));
        GtkWidget *vl = w_label(val, cal_day_net(day) < 0 ? "fin-cal-v fin-red" : "fin-cal-v fin-green");
        /* o valor do dia nunca é cortado com "…": cal_signed já é curto */
        gtk_label_set_ellipsize(GTK_LABEL(vl), PANGO_ELLIPSIZE_NONE);
        w_add(v, vl);
    }
    if (day && day->marks) {
        GtkWidget *dots = w_hbox(3);
        if (day->marks & MARK_INCOME) w_add(dots, dot("income"));
        if (day->marks & MARK_EXPENSE) w_add(dots, dot("expense"));
        if (day->marks & MARK_CARD) w_add(dots, dot("card"));
        w_add(v, dots);
    }
    gtk_button_set_child(GTK_BUTTON(b), v);
    g_object_set_data(G_OBJECT(b), "day", GINT_TO_POINTER(d));
    g_signal_connect(b, "clicked", G_CALLBACK(on_cell), NULL);
    GtkGesture *rc = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rc), GDK_BUTTON_SECONDARY);
    g_signal_connect(rc, "pressed", G_CALLBACK(on_cell_secondary), NULL);
    gtk_widget_add_controller(b, GTK_EVENT_CONTROLLER(rc));
    GtkGesture *lp = gtk_gesture_long_press_new();
    g_signal_connect(lp, "pressed", G_CALLBACK(on_cell_long), NULL);
    gtk_widget_add_controller(b, GTK_EVENT_CONTROLLER(lp));
    /* leitor de tela: o dia como frase completa ("6 de outubro, terça-feira, 1 lançamento, …") */
    g_autofree char *desc = cal_describe(d, day, today, app_hidden());
    g_autofree char *full = sel ? g_strdup_printf("%s. Clique de novo para lançar nesta data", desc) : g_strdup(desc);
    gtk_accessible_update_property(GTK_ACCESSIBLE(b), GTK_ACCESSIBLE_PROPERTY_LABEL, full, -1);
    gtk_accessible_update_state(GTK_ACCESSIBLE(b), GTK_ACCESSIBLE_STATE_PRESSED, sel ? GTK_ACCESSIBLE_TRISTATE_TRUE : GTK_ACCESSIBLE_TRISTATE_FALSE, -1);
    gtk_widget_set_tooltip_text(b, full);
    return b;
}

/* ---------------------------------------------------------------- mês */

static GtkWidget *round_btn(const char *icon, const char *tip, FinFn fn, gpointer data) {
    GtkWidget *b = w_button(NULL, icon, "fin-round", fn, data, NULL);
    gtk_widget_set_tooltip_text(b, tip);
    gtk_accessible_update_property(GTK_ACCESSIBLE(b), GTK_ACCESSIBLE_PROPERTY_LABEL, tip, -1);
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    return b;
}

static GtkWidget *legend_item(GtkWidget *mark, const char *text) {
    GtkWidget *b = w_hbox(5);
    w_add(b, mark);
    w_add(b, w_label(text, "fin-muted caption"));
    return b;
}

static GtkWidget *month_card(const CalMonth *m) {
    Ym ym = m->ym;
    GtkWidget *c = w_card(NULL);
    gtk_widget_set_valign(c, GTK_ALIGN_START);
    g_autofree char *title = cal_month_title(ym);
    g_autofree char *aria = g_strdup_printf("Calendário de %s", title);
    gtk_accessible_update_property(GTK_ACCESSIBLE(c), GTK_ACCESSIBLE_PROPERTY_LABEL, aria, -1);

    GtkWidget *head = w_hbox(8);
    w_add(head, round_btn("chevron-left", "Mês anterior (Page Up)", shift_month, GINT_TO_POINTER(-1)));
    GtkWidget *mid = w_vbox(0);
    gtk_widget_set_hexpand(mid, TRUE);
    GtkWidget *tl = w_label(title, "title-4");
    gtk_label_set_xalign(GTK_LABEL(tl), 0.5f);
    w_add(mid, tl);
    if (ym != day_ym(APP->today)) {
        GtkWidget *back = w_button("Voltar para hoje", NULL, "flat fin-link", go_today, NULL, NULL);
        gtk_widget_set_halign(back, GTK_ALIGN_CENTER);
        w_add(mid, back);
    }
    w_add(head, mid);
    w_add(head, round_btn("chevron-right", "Próximo mês (Page Down)", shift_month, GINT_TO_POINTER(1)));
    w_add(c, head);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_widget_set_margin_top(grid, 6);
    for (int i = 0; i < 7; i++) {
        GtkWidget *h = w_label(CAL_WEEK_HEADER[i], "fin-muted caption heading");
        gtk_label_set_xalign(GTK_LABEL(h), 0.5f);
        gtk_accessible_update_state(GTK_ACCESSIBLE(h), GTK_ACCESSIBLE_STATE_HIDDEN, TRUE, -1);
        gtk_grid_attach(GTK_GRID(grid), h, i, 0, 1, 1);
    }
    Day cells[42];
    int n = cal_cells(ym, cells);
    for (int i = 0; i < n; i++) {
        if (cells[i] == DAY_NONE) continue;
        gtk_grid_attach(GTK_GRID(grid), day_cell(cells[i], cal_get(m, cells[i])), i % 7, 1 + i / 7, 1, 1);
    }
    w_add(c, grid);

    GtkWidget *legend = w_hbox(14);
    gtk_widget_set_margin_top(legend, 8);
    gtk_widget_set_halign(legend, GTK_ALIGN_CENTER);
    gtk_accessible_update_state(GTK_ACCESSIBLE(legend), GTK_ACCESSIBLE_STATE_HIDDEN, TRUE, -1);
    w_add(legend, legend_item(dot("income"), "Receita"));
    w_add(legend, legend_item(dot("expense"), "Despesa"));
    w_add(legend, legend_item(dot("card"), "Cartão"));
    GtkWidget *wi = w_icon("warning", 13);
    gtk_widget_add_css_class(wi, "fin-red");
    w_add(legend, legend_item(wi, "Em atraso"));
    w_add(c, legend);
    w_add(c, w_label_wrap("Clique num dia para ver os lançamentos. Clique de novo no dia escolhido, dê dois cliques ou use o botão direito para lançar nessa data.",
                          "fin-muted caption"));
    return c;
}

static GtkWidget *total_box(const char *label, Cents v, const char *cls) {
    GtkWidget *b = w_card("fin-tight fin-flat");
    gtk_widget_set_hexpand(b, TRUE);
    w_add(b, w_label(label, "fin-muted caption"));
    w_add(b, w_money(v, cls));
    return b;
}

static GtkWidget *month_totals(const CalMonth *m) {
    Cents inc, exp;
    cal_totals(m, &inc, &exp);
    GtkWidget *v = w_vbox(6);
    gtk_accessible_update_property(GTK_ACCESSIBLE(v), GTK_ACCESSIBLE_PROPERTY_LABEL, "Totais do mês", -1);
    GtkWidget *row = w_hbox(10);
    gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
    w_add(row, total_box("Entradas", inc, "fin-money-mid fin-green"));
    w_add(row, total_box("Saídas", exp, "fin-money-mid fin-red"));
    w_add(row, total_box("Resultado", inc - exp, inc - exp < 0 ? "fin-money-mid fin-red" : "fin-money-mid fin-accent"));
    w_add(v, row);
    w_add(v, w_label_wrap("Inclui o que ainda está pendente e as faturas no dia do vencimento. Compras no cartão aparecem no dia, mas só contam na fatura.",
                          "fin-muted caption"));
    return v;
}

/* ---------------------------------------------------------------- dia escolhido */

static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer u) {
    (void)box; (void)u;
    GtkWidget *child = gtk_list_box_row_get_child(row);
    const char *card = g_object_get_data(G_OBJECT(child), "card");
    if (card) { editor_pay_invoice(card); return; }
    const char *id = g_object_get_data(G_OBJECT(child), "id");
    Tx *t = id ? app_tx(APP->state, id) : NULL;
    if (t) editor_tx(t->kind, t->id);
}

/* fatura em aberto que vence no dia: abre "Pagar fatura" */
static GtkWidget *invoice_row(const InvoiceDue *i) {
    GtkWidget *row = w_hbox(12);
    gtk_widget_add_css_class(row, "fin-row");
    GtkWidget *ic = w_icon("credit-card", 22);
    gtk_widget_add_css_class(ic, "fin-glyph");
    w_add(row, ic);
    GtkWidget *t = w_vbox(2);
    gtk_widget_set_hexpand(t, TRUE);
    g_autofree char *title = g_strdup_printf("Fatura %s", i->card_name);
    w_add(t, w_label(title, "heading"));
    w_add(t, w_label(i->overdue ? "Vencida · em aberto" : "Vence neste dia · clique para pagar", i->overdue ? "fin-late caption" : "fin-muted caption"));
    w_add(row, t);
    g_autofree char *v = app_money(i->amount);
    g_autofree char *vs = g_strdup_printf("− %s", v);
    GtkWidget *vl = w_label(vs, "fin-money");
    gtk_label_set_ellipsize(GTK_LABEL(vl), PANGO_ELLIPSIZE_NONE);
    w_add(row, vl);
    g_object_set_data_full(G_OBJECT(row), "card", g_strdup(i->card_id), g_free);
    gtk_widget_set_tooltip_text(row, "Pagar fatura");
    return row;
}

static GtkWidget *day_box(Day d, const CalDay *day) {
    Day today = APP->today;
    GtkWidget *c = w_vbox(10);
    gtk_accessible_update_property(GTK_ACCESSIBLE(c), GTK_ACCESSIBLE_PROPERTY_LABEL, "Lançamentos do dia", -1);
    int n = day ? cal_day_count(day) : 0;
    w_add(c, w_eyebrow(d == today ? "Hoje" : d < today ? "Dia escolhido" : "Previsto"));
    g_autofree char *title = cal_day_title(d, today);
    w_add(c, w_title(title));
    GString *sub = g_string_new(n == 0 ? "Sem lançamentos" : n == 1 ? "1 lançamento" : NULL);
    if (n > 1) g_string_printf(sub, "%d lançamentos", n);
    if (day && !app_hidden() && (day->income || day->expense)) {
        Cents net = cal_day_net(day);
        char buf[40];
        g_string_append_printf(sub, " · saldo do dia %s%s", net > 0 ? "+ " : net < 0 ? "− " : "", money_format(net < 0 ? -net : net, buf));
    }
    w_add(c, w_label(sub->str, "fin-muted"));
    g_string_free(sub, TRUE);

    GtkWidget *btns = w_hbox(8);
    gtk_box_set_homogeneous(GTK_BOX(btns), TRUE);
    w_add(btns, w_button("Receita", "add", "fin-pill", new_income, GINT_TO_POINTER(d), NULL));
    w_add(btns, w_button("Despesa", "remove", "fin-pill", new_expense, GINT_TO_POINTER(d), NULL));
    w_add(c, btns);

    if (d >= today) {
        Cents f = future_balance(APP->state, d, today);
        GtkWidget *fr = w_card("fin-tight fin-flat");
        GtkWidget *r = w_hbox(8);
        GtkWidget *l = w_label("Saldo previsto ao fim do dia", "fin-muted");
        gtk_widget_set_hexpand(l, TRUE);
        w_add(r, l);
        w_add(r, w_money(f, f < 0 ? "fin-money-mid fin-red" : "fin-money-mid"));
        w_add(fr, r);
        w_add(c, fr);
    }

    if (!n) {
        GtkWidget *e = w_card("fin-flat");
        w_add(e, w_label("Nada neste dia", "heading"));
        w_add(e, w_label_wrap("Use Receita ou Despesa para lançar algo com esta data.", "fin-muted"));
        w_add(c, e);
        return c;
    }
    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(list, "fin-list");
    g_signal_connect(list, "row-activated", G_CALLBACK(on_row_activated), NULL);
    for (guint i = 0; i < day->txs->len; i++) gtk_list_box_append(GTK_LIST_BOX(list), tx_row_full(day->txs->pdata[i], FALSE));
    for (guint i = 0; i < day->invoices->len; i++) gtk_list_box_append(GTK_LIST_BOX(list), invoice_row(&g_array_index(day->invoices, InvoiceDue, i)));
    w_add(c, list);
    return c;
}

/* ---------------------------------------------------------------- montagem */

void page_calendar_refresh(void) {
    if (!C.built || !APP->state) return;
    Ym ym = cur_ym();
    g_autoptr(CalMonth) m = cal_build(APP->state, ym, APP->today);
    gboolean narrow = APP->layout == LAYOUT_NARROW;
    gtk_orientable_set_orientation(GTK_ORIENTABLE(C.body), narrow ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
    gtk_box_set_homogeneous(GTK_BOX(C.body), !narrow);
    w_clear(C.left);
    w_clear(C.right);
    w_add(C.left, month_card(m));
    w_add(C.left, month_totals(m));
    Day sel = APP->cal_day != DAY_NONE && day_ym(APP->cal_day) == ym ? APP->cal_day : DAY_NONE;
    if (sel != DAY_NONE) w_add(C.right, day_box(sel, cal_get(m, sel)));
    else {
        GtkWidget *e = w_card("fin-flat");
        w_add(e, w_label_wrap("Clique num dia para ver os lançamentos dele.", "fin-muted"));
        w_add(C.right, e);
    }
}

/* Page Up / Page Down trocam de mês */
static gboolean on_key(GtkEventControllerKey *k, guint key, guint code, GdkModifierType st, gpointer u) {
    (void)k; (void)code; (void)st; (void)u;
    if (key == GDK_KEY_Page_Up) { shift_month(GINT_TO_POINTER(-1)); return TRUE; }
    if (key == GDK_KEY_Page_Down) { shift_month(GINT_TO_POINTER(1)); return TRUE; }
    return FALSE;
}

/* mostra o calendário no mês de [ym] (atalho "Ver no calendário" dos Relatórios) */
void app_open_calendar(Ym ym) {
    APP->cal_ym = ym;
    APP->cal_day = ym == day_ym(APP->today) ? APP->today : DAY_NONE;
    APP->moves_view = MOVES_CALENDAR;
    app_refresh_page(PAGE_MOVES);
    app_show_page(PAGE_MOVES);
}

GtkWidget *page_calendar_new(void) {
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 1400);
    C.body = w_hbox(18);
    gtk_widget_set_margin_start(C.body, 20);
    gtk_widget_set_margin_end(C.body, 24);
    gtk_widget_set_margin_top(C.body, 12);
    gtk_widget_set_margin_bottom(C.body, 24);
    C.left = w_vbox(12);
    C.right = w_vbox(12);
    gtk_widget_set_hexpand(C.left, TRUE);
    gtk_widget_set_hexpand(C.right, TRUE);
    w_add(C.body, C.left);
    w_add(C.body, C.right);
    adw_clamp_set_child(ADW_CLAMP(clamp), C.body);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), clamp);
    GtkEventController *k = gtk_event_controller_key_new();
    g_signal_connect(k, "key-pressed", G_CALLBACK(on_key), NULL);
    gtk_widget_add_controller(sw, k);
    C.root = sw;
    C.built = TRUE;
    return sw;
}
