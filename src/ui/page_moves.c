/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Lançamentos: chave Lista | Calendário no alto.
 * Lista: ‹ mês › com "Período e filtros", busca, filtros de um toque (Todos, Receitas, Despesas,
 * Pendentes) e o resumo do período à esquerda; os lançamentos agrupados por dia, com o saldo do dia,
 * à direita. Em janelas estreitas o painel fica acima da lista e pode ser recolhido.
 * Calendário: page_calendar.c.
 */
#include "pages.h"
#include "widgets.h"
#include "core/finance.h"
#include "core/period.h"
#include <string.h>

#define PAGE_SIZE 300

static struct {
    GtkWidget *root, *stack, *view_list, *view_cal;
    GtkWidget *body, *left, *panel_rev, *panel_toggle, *panel_sw, *list_sw;
    GtkWidget *period_label, *tune, *search;
    GtkWidget *chip_all, *chip_inc, *chip_exp, *chip_pend;
    GtkWidget *totals; /* recalculado */
    GtkWidget *list, *more, *empty;
    gboolean syncing;
    guint shown;
    GPtrArray *items; /* Tx* filtrados (do estado) */
    /* "Período e filtros" aberto (NULL se fechado) */
    AdwDialog *dlg;
    GtkWidget *dlg_from, *dlg_to, *dlg_paid;
} M;

/* ---------------------------------------------------------------- linha de lançamento */

const char *tx_status(const Tx *t, gboolean *late) {
    if (late) *late = FALSE;
    if (!tx_is_flow(t)) return "Pag. fatura";
    if (tx_is_card(t)) return "Cartão";
    if (t->paid) return t->kind == KIND_INCOME ? "Recebido" : "Pago";
    if (t->date < APP->today) { if (late) *late = TRUE; return "Em atraso"; }
    return t->kind == KIND_INCOME ? "A receber" : "A pagar";
}

static void toggle_paid(gpointer id) {
    Tx *t = app_tx(APP->state, id);
    if (!t) return;
    ops_toggle_paid(APP->state, id);
    gboolean paid = t->paid;
    gboolean inc = t->kind == KIND_INCOME;
    app_commit();
    app_toast(paid ? (inc ? "Marcado como recebido" : "Marcado como pago") : (inc ? "Marcado como a receber" : "Marcado como a pagar"));
}

/* Linha de lançamento. [with_date]: FALSE quando o dia já aparece no título do grupo (lista por dia e calendário). */
GtkWidget *tx_row_full(const Tx *t, gboolean with_date) {
    const AppState *s = APP->state;
    gboolean wide = APP->layout == LAYOUT_WIDE;
    gboolean late;
    const char *status = tx_status(t, &late);
    g_autofree char *where = NULL;
    if (tx_is_card(t)) { Card *c = app_card(s, t->card_id); where = g_strdup_printf("Cartão %s", c ? c->name : ""); }
    else { Account *a = app_account(s, t->account_id); where = g_strdup(a ? a->name : "Conta"); }

    GtkWidget *row = w_hbox(12);
    gtk_widget_add_css_class(row, "fin-row");
    w_add(row, w_category_glyph(t->category));
    GtkWidget *texts = w_vbox(2);
    gtk_widget_set_hexpand(texts, TRUE);
    w_add(texts, w_label(t->desc, "heading"));
    g_autofree char *l2 = g_strdup_printf("%s · %s", t->category, where);
    w_add(texts, w_label(l2, "fin-muted caption"));
    char d[11];
    if (!wide) {
        /* data e situação numa linha própria: nunca são cortadas, mesmo com nomes longos */
        g_autofree char *l3 = with_date ? g_strdup_printf("%s · %s", day_br(t->date, d), status) : g_strdup(status);
        w_add(texts, w_label(l3, late ? "fin-late caption" : "fin-muted caption"));
    }
    w_add(row, texts);
    if (wide) {
        if (with_date) {
            GtkWidget *dl = w_label(day_br(t->date, d), "fin-muted");
            gtk_widget_set_size_request(dl, 96, -1);
            w_add(row, dl);
        }
        GtkWidget *sl = w_label(status, late ? "fin-late" : "fin-muted");
        gtk_widget_set_size_request(sl, 100, -1);
        w_add(row, sl);
    }
    g_autofree char *v = app_money(t->value);
    g_autofree char *vs = g_strdup_printf("%s %s", t->kind == KIND_EXPENSE ? "−" : "+", v);
    GtkWidget *vl = w_label(vs, !tx_is_flow(t) ? "fin-money fin-muted" : t->kind == KIND_EXPENSE ? "fin-money fin-red" : "fin-money fin-green");
    gtk_label_set_xalign(GTK_LABEL(vl), 1);
    gtk_widget_set_size_request(vl, wide ? 140 : 110, -1);
    gtk_label_set_ellipsize(GTK_LABEL(vl), PANGO_ELLIPSIZE_NONE);
    w_add(row, vl);
    if (!ops_can_toggle_paid(t)) {
        /* compra no cartão ou pagamento de fatura: não alterna pago/pendente */
        GtkWidget *ic = w_icon(tx_is_card(t) ? "credit-card" : "check", 20);
        gtk_widget_set_size_request(ic, 34, -1);
        gtk_widget_set_tooltip_text(ic, tx_is_card(t) ? "Compra no cartão: entra na fatura" : "Pagamento de fatura");
        w_add(row, ic);
    } else {
        GtkWidget *chk = w_button(NULL, t->paid ? "check" : NULL, t->paid ? "fin-check on" : "fin-check", toggle_paid, g_strdup(t->id), g_free);
        if (!t->paid) gtk_button_set_label(GTK_BUTTON(chk), "");
        gtk_widget_set_valign(chk, GTK_ALIGN_CENTER);
        g_autofree char *tip = g_strdup_printf("%s: %s", t->kind == KIND_INCOME ? "Recebida" : "Paga", t->desc);
        gtk_widget_set_tooltip_text(chk, t->paid ? (t->kind == KIND_INCOME ? "Recebido — clique para marcar como a receber" : "Pago — clique para marcar como a pagar")
                                                 : (t->kind == KIND_INCOME ? "Marcar como recebido" : "Marcar como pago"));
        gtk_accessible_update_property(GTK_ACCESSIBLE(chk), GTK_ACCESSIBLE_PROPERTY_LABEL, tip, -1);
        w_add(row, chk);
    }
    g_object_set_data_full(G_OBJECT(row), "id", g_strdup(t->id), g_free);
    return row;
}

GtkWidget *tx_row_new(const Tx *t) { return tx_row_full(t, TRUE); }

/* ---------------------------------------------------------------- filtros */

static gboolean match(const Tx *t, const char *q) {
    const Filters *f = &APP->filters;
    if (f->from != DAY_NONE && t->date < f->from) return FALSE;
    if (f->to != DAY_NONE && t->date > f->to) return FALSE;
    if (f->paid >= 0 && (t->paid ? 1 : 0) != f->paid) return FALSE;
    if (f->kind >= 0 && (int)t->kind != f->kind) return FALSE;
    if (*q) {
        /* busca sem diferenciar acento: "cafe" encontra "Café" */
        g_autofree char *dc = g_strdup_printf("%s %s", t->desc, t->category);
        g_autofree char *fd = text_fold(dc);
        if (!strstr(fd, q)) return FALSE;
    }
    return TRUE;
}

static gint cmp_desc_date(gconstpointer a, gconstpointer b) {
    const Tx *x = *(Tx *const *)a, *y = *(Tx *const *)b;
    if (x->date != y->date) return (y->date > x->date) - (y->date < x->date);
    return strcmp(y->id, x->id);
}

static void rebuild_list(gboolean keep_scroll);

/* tudo que muda o período ou os filtros passa por aqui (Relatórios usam o mesmo período) */
static void filters_changed(void) {
    if (M.syncing) return;
    rebuild_list(FALSE);
    app_refresh_page(PAGE_REPORTS);
}

/* período livre ou "Realizados" ligado: o botão "Período e filtros" fica destacado */
static gboolean filters_custom(void) {
    const Filters *f = &APP->filters;
    return !period_full_month(f->from, f->to, NULL) || f->paid == 1;
}

static void sync_controls(void) {
    const Filters *f = &APP->filters;
    M.syncing = TRUE;
    g_autofree char *label = period_label(f->from, f->to);
    gtk_label_set_text(GTK_LABEL(M.period_label), label);
    if (filters_custom()) gtk_widget_add_css_class(M.tune, "on");
    else gtk_widget_remove_css_class(M.tune, "on");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(M.chip_all), f->kind < 0 && f->paid < 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(M.chip_inc), f->kind == KIND_INCOME);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(M.chip_exp), f->kind == KIND_EXPENSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(M.chip_pend), f->paid == 0);
    if (strcmp(gtk_editable_get_text(GTK_EDITABLE(M.search)), f->query) != 0) gtk_editable_set_text(GTK_EDITABLE(M.search), f->query);
    if (M.dlg) {
        Day cur;
        if (!date_field_get(M.dlg_from, &cur) || cur != f->from) date_field_set(M.dlg_from, f->from);
        if (!date_field_get(M.dlg_to, &cur) || cur != f->to) date_field_set(M.dlg_to, f->to);
        adw_combo_row_set_selected(ADW_COMBO_ROW(M.dlg_paid), f->paid == 1 ? 1 : f->paid == 0 ? 2 : 0);
    }
    M.syncing = FALSE;
}

static void shift(gpointer delta) {
    Filters *f = &APP->filters;
    period_shift(f->from, f->to, GPOINTER_TO_INT(delta), APP->today, &f->from, &f->to);
    sync_controls();
    filters_changed();
}

/* Todos / Receitas / Despesas / Pendentes: Receitas e Despesas combinam com Pendentes */
static void on_chip(GtkToggleButton *b, gpointer which) {
    if (M.syncing) return;
    Filters *f = &APP->filters;
    int w = GPOINTER_TO_INT(which);
    if (w == 0) { f->kind = -1; f->paid = -1; }
    else if (w == 3) f->paid = f->paid == 0 ? -1 : 0;
    else {
        int k = w == 1 ? KIND_INCOME : KIND_EXPENSE;
        f->kind = f->kind == k ? -1 : k;
    }
    (void)b;
    sync_controls();
    filters_changed();
}

static void on_search(GtkSearchEntry *e, gpointer u) {
    (void)u;
    if (M.syncing) return;
    g_free(APP->filters.query);
    APP->filters.query = g_strdup(gtk_editable_get_text(GTK_EDITABLE(e)));
    filters_changed();
}

/* ---------------------------------------------------------------- folha "Período e filtros" */

static void preset(gpointer key) {
    Filters *f = &APP->filters;
    const char *k = key;
    if (!strcmp(k, "mes")) filters_this_month(f);
    else if (!strcmp(k, "30")) { f->to = APP->today; f->from = APP->today - 29; }
    else {
        Day a = DAY_NONE, b = DAY_NONE;
        for (guint i = 0; i < APP->state->txs->len; i++) {
            Day d = ((Tx *)APP->state->txs->pdata[i])->date;
            if (a == DAY_NONE || d < a) a = d;
            if (b == DAY_NONE || d > b) b = d;
        }
        f->from = a;
        f->to = b;
    }
    sync_controls();
    filters_changed();
}

static void dlg_dates_changed(gpointer u) {
    (void)u;
    if (M.syncing || !M.dlg) return;
    Day d;
    if (date_field_get(M.dlg_from, &d)) APP->filters.from = d;
    if (date_field_get(M.dlg_to, &d)) APP->filters.to = d;
    sync_controls();
    filters_changed();
}

static void dlg_paid_changed(GObject *o, GParamSpec *p, gpointer u) {
    (void)o; (void)p; (void)u;
    if (M.syncing || !M.dlg) return;
    guint sel = adw_combo_row_get_selected(ADW_COMBO_ROW(M.dlg_paid));
    APP->filters.paid = sel == 1 ? 1 : sel == 2 ? 0 : -1;
    sync_controls();
    filters_changed();
}

static void dlg_closed(AdwDialog *d, gpointer u) {
    (void)d; (void)u;
    M.dlg = NULL;
    M.dlg_from = M.dlg_to = M.dlg_paid = NULL;
}

static void open_filters(gpointer u) {
    (void)u;
    if (M.dlg) return;
    AdwDialog *d = M.dlg = adw_dialog_new();
    adw_dialog_set_title(d, "Período e filtros");
    adw_dialog_set_content_width(d, 460);
    GtkWidget *tv = adw_toolbar_view_new();
    GtkWidget *hb = adw_header_bar_new();
    adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(hb), FALSE);
    GtkWidget *done = gtk_button_new_with_label("Pronto");
    gtk_widget_add_css_class(done, "suggested-action");
    g_signal_connect_swapped(done, "clicked", G_CALLBACK(adw_dialog_close), d);
    adw_header_bar_pack_end(ADW_HEADER_BAR(hb), done);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), hb);
    GtkWidget *c = w_vbox(14);
    gtk_widget_set_margin_start(c, 18);
    gtk_widget_set_margin_end(c, 18);
    gtk_widget_set_margin_top(c, 8);
    gtk_widget_set_margin_bottom(c, 18);
    w_add(c, w_label_wrap("O período também vale para Relatórios. As mudanças valem na hora.", "fin-muted"));
    GtkWidget *dates = w_hbox(10);
    gtk_box_set_homogeneous(GTK_BOX(dates), TRUE);
    M.dlg_from = date_field("De", APP->filters.from, dlg_dates_changed, NULL);
    M.dlg_to = date_field("Até", APP->filters.to, dlg_dates_changed, NULL);
    GtkWidget *a = w_vbox(4), *b = w_vbox(4);
    w_add(a, w_label("De", "fin-muted caption"));
    w_add(a, M.dlg_from);
    w_add(b, w_label("Até", "fin-muted caption"));
    w_add(b, M.dlg_to);
    w_add(dates, a);
    w_add(dates, b);
    w_add(c, dates);
    GtkWidget *pills = w_hbox(6);
    gtk_box_set_homogeneous(GTK_BOX(pills), TRUE);
    w_add(pills, w_pill("Este mês", preset, "mes", NULL));
    w_add(pills, w_pill("30 dias", preset, "30", NULL));
    w_add(pills, w_pill("Tudo", preset, "tudo", NULL));
    w_add(c, pills);
    GtkWidget *g = adw_preferences_group_new();
    static const char *const st[] = {"Todos", "Realizados", "Pendentes"};
    M.dlg_paid = row_combo("Situação", st, 3, APP->filters.paid == 1 ? 1 : APP->filters.paid == 0 ? 2 : 0);
    g_signal_connect(M.dlg_paid, "notify::selected", G_CALLBACK(dlg_paid_changed), NULL);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), M.dlg_paid);
    w_add(c, g);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), c);
    adw_dialog_set_child(d, tv);
    g_signal_connect(d, "closed", G_CALLBACK(dlg_closed), NULL);
    adw_dialog_present(d, GTK_WIDGET(APP->window));
}

/* ---------------------------------------------------------------- resumo do período */

static GtkWidget *sum_col(const char *label, Cents v, const char *cls, const char *sub_label, Cents sub, const char *sub_cls, gboolean show_sub) {
    GtkWidget *b = w_vbox(3);
    gtk_widget_set_hexpand(b, TRUE);
    w_add(b, w_label(label, "fin-muted caption"));
    GtkWidget *m = w_money(v, cls);
    gtk_label_set_xalign(GTK_LABEL(m), 0);
    w_add(b, m);
    if (show_sub) {
        w_add(b, w_label(sub_label, "fin-muted caption"));
        w_add(b, w_money(sub, sub_cls));
    }
    return b;
}

static void build_totals(void) {
    w_clear(M.totals);
    Flow f = flow_of(M.items);
    Pending p = period_pending(M.items);
    Cents bal = flow_balance(f), forecast = bal + p.to_receive - p.to_pay;
    gboolean has_pend = p.to_receive > 0 || p.to_pay > 0;
    GtkWidget *card = w_card("fin-flat");
    gtk_accessible_update_property(GTK_ACCESSIBLE(card), GTK_ACCESSIBLE_PROPERTY_LABEL, "Resumo do período", -1);
    GtkWidget *row = w_hbox(10);
    gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
    w_add(row, sum_col("Receitas", f.income, "fin-money-mid fin-green", "a receber", p.to_receive, "caption fin-green", p.to_receive > 0));
    w_add(row, sum_col("Despesas", f.expense, "fin-money-mid fin-red", "a pagar", p.to_pay, "caption fin-red", p.to_pay > 0));
    w_add(row, sum_col("Saldo", bal, bal < 0 ? "fin-money-mid fin-red" : "fin-money-mid", "previsto", forecast,
                       forecast < 0 ? "caption fin-red" : "caption fin-accent", has_pend));
    w_add(card, row);
    /* com "Ocultar valores" a porcentagem também some */
    if (f.income > 0 && !app_hidden()) {
        g_autofree char *pct = g_strdup_printf("%.1f", f.expense * 100.0 / f.income);
        for (char *q = pct; *q; q++) if (*q == '.') *q = ',';
        g_autofree char *note = g_strdup_printf("As despesas são %s%% das receitas do período.", pct);
        GtkWidget *nl = w_label_wrap(note, "fin-muted caption");
        gtk_widget_set_margin_top(nl, 6);
        w_add(card, nl);
    }
    w_add(M.totals, card);
}

/* ---------------------------------------------------------------- lista agrupada por dia */

static GtkWidget *day_head(Day d, GPtrArray *txs) {
    GtkWidget *h = w_hbox(8);
    gtk_widget_add_css_class(h, "fin-day-head");
    g_autofree char *t = cal_day_title(d, APP->today);
    g_autofree char *title = d == APP->today ? g_strdup_printf("Hoje · %s", t) : g_strdup(t);
    GtkWidget *tl = w_label(title, "heading");
    gtk_widget_set_hexpand(tl, TRUE);
    w_add(h, tl);
    /* saldo do dia pela mesma regra do calendário; só quando há dinheiro das contas no dia */
    gboolean cash = FALSE;
    for (guint i = 0; i < txs->len; i++) if (!tx_is_card((Tx *)txs->pdata[i])) cash = TRUE;
    if (cash && !app_hidden()) {
        Cents net = period_cash_net(txs);
        char buf[40];
        g_autofree char *s = g_strdup_printf("%s%s", net > 0 ? "+ " : net < 0 ? "− " : "", money_format(net < 0 ? -net : net, buf));
        w_add(h, w_label(s, net < 0 ? "heading fin-red" : "heading fin-green"));
    }
    return h;
}

static void append_head(Day d, GPtrArray *group) {
    GtkWidget *r = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(r), FALSE);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(r), FALSE);
    gtk_widget_set_focusable(r, FALSE);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(r), day_head(d, group));
    gtk_list_box_append(GTK_LIST_BOX(M.list), r);
}

static void fill_list(guint upto) {
    /* acrescenta grupos de dias até [upto] lançamentos; um dia nunca fica partido entre páginas */
    while (M.shown < M.items->len && M.shown < upto) {
        Day d = ((Tx *)M.items->pdata[M.shown])->date;
        g_autoptr(GPtrArray) group = g_ptr_array_new();
        guint j = M.shown;
        while (j < M.items->len && ((Tx *)M.items->pdata[j])->date == d) g_ptr_array_add(group, M.items->pdata[j++]);
        append_head(d, group);
        for (guint i = 0; i < group->len; i++) gtk_list_box_append(GTK_LIST_BOX(M.list), tx_row_full(group->pdata[i], FALSE));
        M.shown = j;
    }
    gboolean more = M.shown < M.items->len;
    gtk_widget_set_visible(M.more, more);
    if (more) {
        g_autofree char *l = g_strdup_printf("Mostrar mais (%u restantes)", M.items->len - M.shown);
        gtk_button_set_label(GTK_BUTTON(M.more), l);
    }
}

static void show_more(GtkButton *b, gpointer u) { (void)b; (void)u; fill_list(M.shown + PAGE_SIZE); }

static void rebuild_list(gboolean keep_scroll) {
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(M.list_sw));
    double pos = keep_scroll ? gtk_adjustment_get_value(adj) : 0;
    guint keep = keep_scroll ? MAX(M.shown, PAGE_SIZE) : PAGE_SIZE;
    if (M.items) g_ptr_array_unref(M.items);
    M.items = g_ptr_array_new();
    g_autofree char *q = text_fold(APP->filters.query);
    for (guint i = 0; i < APP->state->txs->len; i++) {
        Tx *t = APP->state->txs->pdata[i];
        if (match(t, q)) g_ptr_array_add(M.items, t);
    }
    g_ptr_array_sort(M.items, cmp_desc_date);
    gtk_list_box_remove_all(GTK_LIST_BOX(M.list));
    M.shown = 0;
    fill_list(keep);
    /* aviso de lista vazia controlado aqui: gtk_list_box_remove_all() também descarta o placeholder da GtkListBox */
    gtk_widget_set_visible(M.list, M.items->len > 0);
    gtk_widget_set_visible(M.empty, M.items->len == 0);
    build_totals();
    if (keep_scroll) gtk_adjustment_set_value(adj, pos);
}

static void on_activated(GtkListBox *box, GtkListBoxRow *row, gpointer u) {
    (void)box; (void)u;
    const char *id = g_object_get_data(G_OBJECT(gtk_list_box_row_get_child(row)), "id");
    Tx *t = id ? app_tx(APP->state, id) : NULL;
    if (t) editor_tx(t->kind, t->id);
}

static void toggle_panel(GtkToggleButton *b, gpointer u) {
    (void)u;
    gtk_revealer_set_reveal_child(GTK_REVEALER(M.panel_rev), gtk_toggle_button_get_active(b));
}

/* ---------------------------------------------------------------- Lista | Calendário */

static void sync_view(void) {
    gboolean cal = APP->moves_view == MOVES_CALENDAR;
    M.syncing = TRUE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(M.view_list), !cal);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(M.view_cal), cal);
    M.syncing = FALSE;
    gtk_stack_set_visible_child_name(GTK_STACK(M.stack), cal ? "calendar" : "list");
}

static void on_view(GtkToggleButton *b, gpointer which) {
    if (M.syncing || !gtk_toggle_button_get_active(b)) return;
    APP->moves_view = GPOINTER_TO_INT(which);
    sync_view();
    if (APP->moves_view == MOVES_CALENDAR) page_calendar_refresh();
}

void page_moves_focus_search(void) {
    if (APP->moves_view != MOVES_LIST) { APP->moves_view = MOVES_LIST; sync_view(); }
    if (APP->layout == LAYOUT_NARROW) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(M.panel_toggle), TRUE);
    gtk_widget_grab_focus(M.search);
}

void page_moves_refresh(void) {
    if (!M.root || !APP->state) return;
    gboolean narrow = APP->layout == LAYOUT_NARROW;
    gtk_orientable_set_orientation(GTK_ORIENTABLE(M.body), narrow ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_visible(M.panel_toggle, narrow);
    int pw = APP->layout == LAYOUT_WIDE ? 380 : 330;
    gtk_widget_set_size_request(M.left, narrow ? -1 : pw, -1);
    gtk_widget_set_hexpand(M.left, FALSE);
    gtk_widget_set_vexpand(M.panel_rev, !narrow);
    gtk_widget_set_vexpand(M.panel_sw, !narrow);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(M.panel_sw), narrow);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(M.panel_sw), narrow ? 360 : -1);
    if (!narrow) gtk_revealer_set_reveal_child(GTK_REVEALER(M.panel_rev), TRUE);
    else gtk_revealer_set_reveal_child(GTK_REVEALER(M.panel_rev), gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(M.panel_toggle)));
    sync_view();
    sync_controls();
    rebuild_list(TRUE);
    page_calendar_refresh();
}

static GtkWidget *round_btn(const char *icon, const char *tip, FinFn fn, gpointer data) {
    GtkWidget *b = w_button(NULL, icon, "fin-round", fn, data, NULL);
    gtk_widget_set_tooltip_text(b, tip);
    gtk_accessible_update_property(GTK_ACCESSIBLE(b), GTK_ACCESSIBLE_PROPERTY_LABEL, tip, -1);
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    return b;
}

static GtkWidget *chip(const char *label, int which) {
    GtkWidget *b = gtk_toggle_button_new_with_label(label);
    gtk_widget_add_css_class(b, "fin-pill");
    g_signal_connect(b, "toggled", G_CALLBACK(on_chip), GINT_TO_POINTER(which));
    return b;
}

static GtkWidget *view_btn(const char *icon, const char *label, int which) {
    GtkWidget *b = gtk_toggle_button_new();
    GtkWidget *c = adw_button_content_new();
    g_autofree char *n = icon_name(icon);
    adw_button_content_set_icon_name(ADW_BUTTON_CONTENT(c), n);
    adw_button_content_set_label(ADW_BUTTON_CONTENT(c), label);
    gtk_button_set_child(GTK_BUTTON(b), c);
    gtk_widget_add_css_class(b, "fin-pill");
    g_signal_connect(b, "toggled", G_CALLBACK(on_view), GINT_TO_POINTER(which));
    return b;
}

GtkWidget *page_moves_new(void) {
    M.root = w_vbox(0);

    /* ---- título e chave Lista | Calendário ---- */
    GtkWidget *top = w_hbox(10);
    gtk_widget_set_margin_start(top, 20);
    gtk_widget_set_margin_end(top, 24);
    gtk_widget_set_margin_top(top, 16);
    GtkWidget *title = w_label("Lançamentos", "title-2");
    gtk_widget_set_hexpand(title, TRUE);
    w_add(top, title);
    GtkWidget *sw = w_hbox(4);
    gtk_widget_add_css_class(sw, "fin-segment");
    gtk_accessible_update_property(GTK_ACCESSIBLE(sw), GTK_ACCESSIBLE_PROPERTY_LABEL, "Modo de exibição", -1);
    M.view_list = view_btn("view-list", "Lista", MOVES_LIST);
    M.view_cal = view_btn("calendar-month", "Calendário", MOVES_CALENDAR);
    gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(M.view_cal), GTK_TOGGLE_BUTTON(M.view_list));
    w_add(sw, M.view_list);
    w_add(sw, M.view_cal);
    w_add(top, sw);
    w_add(M.root, top);

    M.stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(M.stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_vexpand(M.stack, TRUE);
    w_add(M.root, M.stack);
    M.body = w_hbox(0);
    gtk_stack_add_named(GTK_STACK(M.stack), M.body, "list");
    gtk_stack_add_named(GTK_STACK(M.stack), page_calendar_new(), "calendar");

    /* ---- painel ---- */
    GtkWidget *panel = w_vbox(12);
    gtk_widget_set_margin_start(panel, 20);
    gtk_widget_set_margin_end(panel, 12);
    gtk_widget_set_margin_top(panel, 12);
    gtk_widget_set_margin_bottom(panel, 18);

    GtkWidget *pbar = w_hbox(6);
    w_add(pbar, round_btn("chevron-left", "Mês anterior", shift, GINT_TO_POINTER(-1)));
    M.period_label = w_label("", "title-4");
    gtk_label_set_xalign(GTK_LABEL(M.period_label), 0.5f);
    gtk_label_set_ellipsize(GTK_LABEL(M.period_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(M.period_label, TRUE);
    gtk_accessible_update_property(GTK_ACCESSIBLE(M.period_label), GTK_ACCESSIBLE_PROPERTY_LABEL, "Período", -1);
    w_add(pbar, M.period_label);
    w_add(pbar, round_btn("chevron-right", "Próximo mês", shift, GINT_TO_POINTER(1)));
    M.tune = round_btn("tune", "Período e filtros", open_filters, NULL);
    w_add(pbar, M.tune);
    w_add(panel, pbar);

    M.search = gtk_search_entry_new();
    g_object_set(M.search, "placeholder-text", "Buscar descrição ou categoria", NULL);
    gtk_accessible_update_property(GTK_ACCESSIBLE(M.search), GTK_ACCESSIBLE_PROPERTY_LABEL, "Buscar lançamentos (descrição ou categoria)", -1);
    gtk_widget_set_tooltip_text(M.search, "Buscar lançamentos (Ctrl+F)");
    g_signal_connect(M.search, "search-changed", G_CALLBACK(on_search), NULL);
    w_add(panel, M.search);

    GtkWidget *chips = w_hbox(6);
    gtk_accessible_update_property(GTK_ACCESSIBLE(chips), GTK_ACCESSIBLE_PROPERTY_LABEL, "Filtros", -1);
    w_add(chips, M.chip_all = chip("Todos", 0));
    w_add(chips, M.chip_inc = chip("Receitas", 1));
    w_add(chips, M.chip_exp = chip("Despesas", 2));
    w_add(chips, M.chip_pend = chip("Pendentes", 3));
    w_add(panel, chips);

    M.totals = w_vbox(10);
    w_add(panel, M.totals);

    M.panel_sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(M.panel_sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(M.panel_sw), panel);
    M.panel_rev = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(M.panel_rev), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_child(GTK_REVEALER(M.panel_rev), M.panel_sw);
    gtk_revealer_set_reveal_child(GTK_REVEALER(M.panel_rev), TRUE);

    GtkWidget *left = M.left = w_vbox(0);
    M.panel_toggle = gtk_toggle_button_new_with_label("Período, filtros e resumo");
    gtk_widget_add_css_class(M.panel_toggle, "flat");
    gtk_widget_set_margin_start(M.panel_toggle, 12);
    gtk_widget_set_margin_end(M.panel_toggle, 12);
    gtk_widget_set_margin_top(M.panel_toggle, 6);
    g_signal_connect(M.panel_toggle, "toggled", G_CALLBACK(toggle_panel), NULL);
    w_add(left, M.panel_toggle);
    w_add(left, M.panel_rev);
    w_add(M.body, left);

    /* ---- lista ---- */
    GtkWidget *right = w_vbox(8);
    gtk_widget_set_hexpand(right, TRUE);
    gtk_widget_set_vexpand(right, TRUE);
    GtkWidget *lbox = w_vbox(8);
    gtk_widget_set_margin_start(lbox, 12);
    gtk_widget_set_margin_end(lbox, 24);
    gtk_widget_set_margin_top(lbox, 12);
    gtk_widget_set_margin_bottom(lbox, 24);
    M.list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(M.list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(M.list, "fin-list");
    gtk_widget_set_name(M.list, "fin-tx-list");
    gtk_accessible_update_property(GTK_ACCESSIBLE(M.list), GTK_ACCESSIBLE_PROPERTY_LABEL, "Lançamentos do período, por dia", -1);
    g_signal_connect(M.list, "row-activated", G_CALLBACK(on_activated), NULL);
    /* aviso de lista vazia, irmão da lista (não placeholder: remove_all() o descartaria;
       e um AdwStatusPage aqui ficava com altura zero, pois tem rolagem própria) */
    GtkWidget *empty = M.empty = w_vbox(8);
    gtk_widget_add_css_class(empty, "fin-empty");
    gtk_widget_set_margin_top(empty, 48);
    gtk_widget_set_margin_bottom(empty, 48);
    gtk_widget_set_halign(empty, GTK_ALIGN_CENTER);
    GtkWidget *eic = w_icon("swap-horiz", 48);
    gtk_widget_add_css_class(eic, "fin-muted");
    w_add(empty, eic);
    w_add(empty, w_label("Nenhum lançamento neste período", "title-3"));
    GtkWidget *ed = w_label_wrap("Troque o mês, ajuste os filtros ou adicione uma movimentação (Ctrl+N).", "fin-muted");
    gtk_label_set_justify(GTK_LABEL(ed), GTK_JUSTIFY_CENTER);
    w_add(empty, ed);
    w_add(lbox, M.list);
    w_add(lbox, empty);
    M.more = gtk_button_new_with_label("Mostrar mais");
    gtk_widget_add_css_class(M.more, "fin-pill");
    gtk_widget_set_halign(M.more, GTK_ALIGN_CENTER);
    g_signal_connect(M.more, "clicked", G_CALLBACK(show_more), NULL);
    w_add(lbox, M.more);
    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 1200);
    adw_clamp_set_child(ADW_CLAMP(clamp), lbox);
    M.list_sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(M.list_sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(M.list_sw), clamp);
    gtk_widget_set_vexpand(M.list_sw, TRUE);
    w_add(right, M.list_sw);
    w_add(M.body, right);
    return M.root;
}
