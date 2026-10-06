/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Lançamentos: painel de período, filtros e totais à esquerda; lista à direita.
 * Em janelas estreitas o painel fica acima da lista e pode ser recolhido.
 */
#include "pages.h"
#include "widgets.h"
#include "core/finance.h"
#include <string.h>

#define PAGE_SIZE 300

static struct {
    GtkWidget *root, *left, *panel_rev, *panel_toggle, *panel_sw, *list_sw;
    GtkWidget *from, *to, *search, *kind, *paid;
    GtkWidget *totals; /* recalculado */
    GtkWidget *list, *count, *more, *empty;
    gboolean syncing;
    guint shown;
    GPtrArray *items; /* Tx* filtrados (do estado) */
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

GtkWidget *tx_row_new(const Tx *t) {
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
        /* duas linhas fixas: data e situação nunca são cortadas, mesmo com nomes longos */
        g_autofree char *l3 = g_strdup_printf("%s · %s", day_br(t->date, d), status);
        w_add(texts, w_label(l3, late ? "fin-late caption" : "fin-muted caption"));
    }
    w_add(row, texts);
    if (wide) {
        GtkWidget *dl = w_label(day_br(t->date, d), "fin-muted");
        gtk_widget_set_size_request(dl, 96, -1);
        w_add(row, dl);
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
    if (tx_is_card(t)) {
        GtkWidget *ic = w_icon("credit-card", 20);
        gtk_widget_set_size_request(ic, 34, -1);
        gtk_widget_set_tooltip_text(ic, "Compra no cartão: entra na fatura");
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

static void filters_changed(gpointer u);

static void read_inputs(void) {
    Filters *f = &APP->filters;
    Day d;
    if (date_field_get(M.from, &d)) f->from = d;
    if (date_field_get(M.to, &d)) f->to = d;
    g_free(f->query);
    f->query = g_strdup(gtk_editable_get_text(GTK_EDITABLE(M.search)));
    guint k = gtk_drop_down_get_selected(GTK_DROP_DOWN(M.kind));
    f->kind = k == 1 ? KIND_INCOME : k == 2 ? KIND_EXPENSE : -1;
    guint p = gtk_drop_down_get_selected(GTK_DROP_DOWN(M.paid));
    f->paid = p == 1 ? 1 : p == 2 ? 0 : -1;
}

static void write_inputs(void) {
    const Filters *f = &APP->filters;
    M.syncing = TRUE;
    Day cur;
    if (!date_field_get(M.from, &cur) || cur != f->from) date_field_set(M.from, f->from);
    if (!date_field_get(M.to, &cur) || cur != f->to) date_field_set(M.to, f->to);
    if (strcmp(gtk_editable_get_text(GTK_EDITABLE(M.search)), f->query) != 0) gtk_editable_set_text(GTK_EDITABLE(M.search), f->query);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(M.kind), f->kind == KIND_INCOME ? 1 : f->kind == KIND_EXPENSE ? 2 : 0);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(M.paid), f->paid == 1 ? 1 : f->paid == 0 ? 2 : 0);
    M.syncing = FALSE;
}

static void preset_month(gpointer u) { (void)u; filters_this_month(&APP->filters); write_inputs(); filters_changed(NULL); }
static void preset_30(gpointer u) {
    (void)u;
    APP->filters.to = APP->today;
    APP->filters.from = APP->today - 29;
    write_inputs();
    filters_changed(NULL);
}
static void preset_all(gpointer u) {
    (void)u;
    Day a = DAY_NONE, b = DAY_NONE;
    for (guint i = 0; i < APP->state->txs->len; i++) {
        Day d = ((Tx *)APP->state->txs->pdata[i])->date;
        if (a == DAY_NONE || d < a) a = d;
        if (b == DAY_NONE || d > b) b = d;
    }
    APP->filters.from = a;
    APP->filters.to = b;
    write_inputs();
    filters_changed(NULL);
}

/* ---------------------------------------------------------------- totais */

static GtkWidget *compare_bar(const char *label, double frac, const char *variant) {
    GtkWidget *r = w_hbox(8);
    GtkWidget *l = w_label(label, "caption");
    gtk_widget_set_size_request(l, 72, -1);
    w_add(r, l);
    w_add(r, w_level(frac, variant));
    char pct[16];
    g_snprintf(pct, sizeof pct, "%d%%", (int)(frac * 100));
    GtkWidget *p = w_label(app_hidden() ? "••" : pct, "caption heading");
    gtk_widget_set_size_request(p, 40, -1);
    gtk_label_set_xalign(GTK_LABEL(p), 1);
    w_add(r, p);
    return r;
}

static void build_totals(void) {
    w_clear(M.totals);
    Flow f = flow_of(M.items);
    Cents pend_in = 0, pend_out = 0;
    gboolean any_pending = FALSE;
    for (guint i = 0; i < M.items->len; i++) {
        Tx *t = M.items->pdata[i];
        if (!tx_is_flow(t) || t->paid) continue;
        any_pending = TRUE;
        if (t->kind == KIND_INCOME) pend_in += t->value; else pend_out += t->value;
    }
    GtkWidget *two = w_hbox(10);
    gtk_box_set_homogeneous(GTK_BOX(two), TRUE);
    GtkWidget *a = w_card("fin-tight fin-flat");
    w_add(a, w_icon_label("arrow-upward", "Receitas", "fin-muted caption"));
    w_add(a, w_money(f.income, "fin-money-mid fin-green"));
    GtkWidget *b = w_card("fin-tight fin-flat");
    w_add(b, w_icon_label("arrow-downward", "Despesas", "fin-muted caption"));
    w_add(b, w_money(f.expense, "fin-money-mid fin-red"));
    w_add(two, a);
    w_add(two, b);
    w_add(M.totals, two);

    GtkWidget *bal = w_card("fin-tight fin-flat");
    GtkWidget *br = w_hbox(8);
    GtkWidget *bl = w_label("Saldo do período", "fin-muted");
    gtk_widget_set_hexpand(bl, TRUE);
    w_add(br, bl);
    w_add(br, w_money(flow_balance(f), flow_balance(f) < 0 ? "fin-money-mid fin-red" : "fin-money-mid"));
    w_add(bal, br);
    w_add(M.totals, bal);

    GtkWidget *cmp = w_card("fin-flat");
    GtkWidget *head = w_hbox(8);
    GtkWidget *hh = w_section_head("Comparação", "Receitas × despesas");
    gtk_widget_set_hexpand(hh, TRUE);
    w_add(head, hh);
    char gasto[32] = "—";
    if (f.income > 0) g_snprintf(gasto, sizeof gasto, "%" G_GINT64_FORMAT "%% gasto", f.expense * 100 / f.income);
    w_add(head, w_label(app_hidden() ? "••" : gasto, "fin-muted heading"));
    w_add(cmp, head);
    Cents total = f.income + f.expense;
    w_add(cmp, compare_bar("Receitas", total > 0 ? (double)f.income / total : 0, "green"));
    w_add(cmp, compare_bar("Despesas", total > 0 ? (double)f.expense / total : 0, "red"));
    g_autofree char *summary = NULL;
    if (f.income > 0) {
        char *p = g_strdup_printf("%.1f", f.expense * 100.0 / f.income);
        for (char *q = p; *q; q++) if (*q == '.') *q = ',';
        summary = g_strdup_printf("As despesas representam %s%% das receitas do período.", app_hidden() ? "••" : p);
        g_free(p);
    } else if (f.expense > 0) summary = g_strdup("Há despesas, mas nenhuma receita neste período.");
    else summary = g_strdup("Nenhuma movimentação no período selecionado.");
    g_autofree char *pend = NULL;
    if (!any_pending) pend = g_strdup("");
    else if (app_hidden()) pend = g_strdup(" Há valores pendentes.");
    else {
        g_autofree char *x = money_fmt(pend_in), *y = money_fmt(pend_out);
        pend = g_strdup_printf(" Pendente: a receber %s · a pagar %s.", x, y);
    }
    g_autofree char *full = g_strconcat(summary, pend, NULL);
    w_add(cmp, w_label_wrap(full, "fin-muted caption"));
    w_add(M.totals, cmp);
}

/* ---------------------------------------------------------------- lista */

static void fill_list(guint upto) {
    while (M.shown < upto && M.shown < M.items->len) {
        Tx *t = M.items->pdata[M.shown++];
        gtk_list_box_append(GTK_LIST_BOX(M.list), tx_row_new(t));
    }
    gboolean more = M.shown < M.items->len;
    gtk_widget_set_visible(M.more, more);
    if (more) {
        g_autofree char *l = g_strdup_printf("Mostrar mais (%u de %u)", M.shown, M.items->len);
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
    char n[16];
    g_snprintf(n, sizeof n, "%u", M.items->len);
    gtk_label_set_text(GTK_LABEL(M.count), n);
    gtk_widget_set_visible(gtk_widget_get_parent(M.list), TRUE);
    /* aviso de lista vazia controlado aqui: gtk_list_box_remove_all() também descarta o placeholder da GtkListBox */
    gtk_widget_set_visible(M.list, M.items->len > 0);
    gtk_widget_set_visible(M.empty, M.items->len == 0);
    build_totals();
    if (keep_scroll) gtk_adjustment_set_value(adj, pos);
}

static void filters_changed(gpointer u) {
    (void)u;
    if (M.syncing) return;
    read_inputs();
    rebuild_list(FALSE);
    app_refresh_page(PAGE_REPORTS); /* Relatórios usam o mesmo período */
}

static void on_search(GtkSearchEntry *e, gpointer u) { (void)e; filters_changed(u); }
static void on_drop(GObject *o, GParamSpec *p, gpointer u) { (void)o; (void)p; filters_changed(u); }

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

void page_moves_focus_search(void) {
    if (APP->layout == LAYOUT_NARROW) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(M.panel_toggle), TRUE);
    gtk_widget_grab_focus(M.search);
}

void page_moves_refresh(void) {
    if (!M.root || !APP->state) return;
    gboolean narrow = APP->layout == LAYOUT_NARROW;
    gtk_orientable_set_orientation(GTK_ORIENTABLE(M.root), narrow ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
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
    write_inputs();
    rebuild_list(TRUE);
}

static GtkWidget *dropdown(const char *const *items, const char *label) {
    GtkWidget *d = gtk_drop_down_new_from_strings(items);
    gtk_widget_set_hexpand(d, TRUE);
    gtk_accessible_update_property(GTK_ACCESSIBLE(d), GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
    return d;
}

static GtkWidget *labeled(const char *label, GtkWidget *w) {
    GtkWidget *b = w_vbox(4);
    gtk_widget_set_hexpand(b, TRUE);
    w_add(b, w_label(label, "fin-muted caption"));
    w_add(b, w);
    return b;
}

GtkWidget *page_moves_new(void) {
    M.root = w_hbox(0);

    /* ---- painel ---- */
    GtkWidget *panel = w_vbox(12);
    gtk_widget_set_margin_start(panel, 20);
    gtk_widget_set_margin_end(panel, 12);
    gtk_widget_set_margin_top(panel, 18);
    gtk_widget_set_margin_bottom(panel, 18);

    GtkWidget *per = w_card("fin-flat");
    w_add(per, w_section_head("Movimentações", "Período"));
    GtkWidget *dates = w_hbox(8);
    gtk_box_set_homogeneous(GTK_BOX(dates), TRUE);
    M.from = date_field("De", APP->filters.from, filters_changed, NULL);
    M.to = date_field("Até", APP->filters.to, filters_changed, NULL);
    w_add(dates, labeled("De", M.from));
    w_add(dates, labeled("Até", M.to));
    w_add(per, dates);
    GtkWidget *pills = w_hbox(6);
    gtk_box_set_homogeneous(GTK_BOX(pills), TRUE);
    w_add(pills, w_pill("Este mês", preset_month, NULL, NULL));
    w_add(pills, w_pill("30 dias", preset_30, NULL, NULL));
    w_add(pills, w_pill("Tudo", preset_all, NULL, NULL));
    w_add(per, pills);
    w_add(panel, per);

    GtkWidget *flt = w_card("fin-flat");
    w_add(flt, w_eyebrow("Filtros da lista"));
    M.search = gtk_search_entry_new();
    g_object_set(M.search, "placeholder-text", "Descrição ou categoria", NULL);
    gtk_accessible_update_property(GTK_ACCESSIBLE(M.search), GTK_ACCESSIBLE_PROPERTY_LABEL, "Buscar lançamentos", -1);
    g_signal_connect(M.search, "search-changed", G_CALLBACK(on_search), NULL);
    w_add(flt, labeled("Buscar lançamentos (Ctrl+F)", M.search));
    GtkWidget *two = w_hbox(8);
    static const char *const kinds[] = {"Todos", "Receitas", "Despesas", NULL};
    static const char *const paid[] = {"Todos", "Realizados", "Pendentes", NULL};
    M.kind = dropdown(kinds, "Tipo");
    M.paid = dropdown(paid, "Situação");
    g_signal_connect(M.kind, "notify::selected", G_CALLBACK(on_drop), NULL);
    g_signal_connect(M.paid, "notify::selected", G_CALLBACK(on_drop), NULL);
    w_add(two, labeled("Tipo", M.kind));
    w_add(two, labeled("Situação", M.paid));
    w_add(flt, two);
    w_add(panel, flt);

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
    M.panel_toggle = gtk_toggle_button_new_with_label("Período, filtros e totais");
    gtk_widget_add_css_class(M.panel_toggle, "flat");
    gtk_widget_set_margin_start(M.panel_toggle, 12);
    gtk_widget_set_margin_end(M.panel_toggle, 12);
    gtk_widget_set_margin_top(M.panel_toggle, 6);
    g_signal_connect(M.panel_toggle, "toggled", G_CALLBACK(toggle_panel), NULL);
    w_add(left, M.panel_toggle);
    w_add(left, M.panel_rev);
    w_add(M.root, left);

    /* ---- lista ---- */
    GtkWidget *right = w_vbox(8);
    gtk_widget_set_hexpand(right, TRUE);
    gtk_widget_set_vexpand(right, TRUE);
    GtkWidget *head = w_hbox(8);
    gtk_widget_set_margin_start(head, 12);
    gtk_widget_set_margin_end(head, 24);
    gtk_widget_set_margin_top(head, 18);
    GtkWidget *hh = w_section_head("No período", "Todos os lançamentos");
    gtk_widget_set_hexpand(hh, TRUE);
    w_add(head, hh);
    M.count = w_badge("0", "accent");
    gtk_widget_set_tooltip_text(M.count, "Lançamentos na lista");
    w_add(head, M.count);
    w_add(right, head);

    GtkWidget *lbox = w_vbox(8);
    gtk_widget_set_margin_start(lbox, 12);
    gtk_widget_set_margin_end(lbox, 24);
    gtk_widget_set_margin_bottom(lbox, 24);
    M.list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(M.list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(M.list, "fin-list");
    gtk_widget_set_name(M.list, "fin-tx-list");
    gtk_accessible_update_property(GTK_ACCESSIBLE(M.list), GTK_ACCESSIBLE_PROPERTY_LABEL, "Lançamentos", -1);
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
    GtkWidget *et = w_label("Nenhum lançamento neste período", "title-3");
    w_add(empty, et);
    GtkWidget *ed = w_label_wrap("Altere as datas ou adicione uma movimentação (Ctrl+N).", "fin-muted");
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
    w_add(M.root, right);
    return M.root;
}
