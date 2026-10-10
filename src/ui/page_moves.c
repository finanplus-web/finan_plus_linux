/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Lançamentos, com a chave Lista | Calendário (as mesmas telas do app Android e da versão web).
 * Lista: ‹ mês ›, busca, filtros de um toque e resumo à esquerda (acima, em janelas estreitas);
 * lançamentos agrupados por dia à direita. Calendário: o mês em grade com o saldo de cada dia.
 * Regras em core/period.c; detalhes em CALENDARIO.md.
 */
#include "pages.h"
#include "widgets.h"
#include "core/finance.h"
#include "core/period.h"
#include <math.h>
#include <string.h>

#define PAGE_SIZE 300

static struct {
    GtkWidget *root, *stack, *view_list, *view_cal, *seg_list, *seg_cal;
    GtkWidget *list_root, *left, *panel_sw, *list_sw;
    GtkWidget *period_label, *tune, *search, *chips[4];
    GtkWidget *totals; /* recalculado */
    GtkWidget *list, *count, *more, *empty;
    GtkWidget *cal_body; /* calendário (redesenhado) */
    GtkWidget *cal_btns[31], *cal_day_box; /* botões dos dias e painel do dia escolhido */
    CalMonth *cal; /* mês mostrado */
    gboolean syncing, suppress_click;
    guint shown;
    GPtrArray *items; /* Tx* filtrados (do estado) */
    GHashTable *day_net; /* Day → saldo do dia (lista agrupada) */
    GHashTable *day_cash; /* Day → há lançamento fora do cartão */
    /* folha "Período e filtros" aberta */
    GtkWidget *f_from, *f_to, *f_paid;
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

GtkWidget *tx_row_new(const Tx *t) { return tx_row_new_full(t, TRUE); }

GtkWidget *tx_row_new_full(const Tx *t, gboolean show_date) {
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
        g_autofree char *l3 = show_date ? g_strdup_printf("%s · %s", day_br(t->date, d), status) : g_strdup(status);
        w_add(texts, w_label(l3, late ? "fin-late caption" : "fin-muted caption"));
    }
    w_add(row, texts);
    if (wide) {
        if (show_date) {
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
    g_object_set_data(G_OBJECT(row), "date", GINT_TO_POINTER(t->date));
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

static void rebuild_list(gboolean keep_scroll);

/* o período ou um filtro mudou: lista, totais e Relatórios (que usam o mesmo período) */
static void filters_changed(gpointer u) {
    (void)u;
    if (M.syncing) return;
    rebuild_list(FALSE);
    app_refresh_page(PAGE_REPORTS);
}

static void shift_period(gpointer d) {
    Filters *f = &APP->filters;
    period_shift(f->from, f->to, GPOINTER_TO_INT(d), APP->today, &f->from, &f->to);
    filters_changed(NULL);
}

/* filtros de um toque: Receitas/Despesas combinam com Pendentes; "Realizados" fica em Período e filtros */
static void chip(gpointer c) {
    Filters *f = &APP->filters;
    switch (GPOINTER_TO_INT(c)) {
    case 0: f->kind = -1; f->paid = -1; break;
    case 1: f->kind = f->kind == KIND_INCOME ? -1 : KIND_INCOME; break;
    case 2: f->kind = f->kind == KIND_EXPENSE ? -1 : KIND_EXPENSE; break;
    default: f->paid = f->paid == 0 ? -1 : 0;
    }
    filters_changed(NULL);
}

static void on_search(GtkSearchEntry *e, gpointer u) {
    (void)u;
    if (M.syncing) return;
    g_free(APP->filters.query);
    APP->filters.query = g_strdup(gtk_editable_get_text(GTK_EDITABLE(e)));
    filters_changed(NULL);
}

/* ---- folha "Período e filtros": datas livres, atalhos e situação (inclui "Realizados") ---- */

static void sheet_sync(void) {
    if (!M.f_from) return;
    M.syncing = TRUE;
    date_field_set(M.f_from, APP->filters.from);
    date_field_set(M.f_to, APP->filters.to);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(M.f_paid), APP->filters.paid == 1 ? 1 : APP->filters.paid == 0 ? 2 : 0);
    M.syncing = FALSE;
}

static void sheet_dates(gpointer u) {
    (void)u;
    if (M.syncing || !M.f_from) return;
    Day d;
    if (date_field_get(M.f_from, &d)) APP->filters.from = d;
    if (date_field_get(M.f_to, &d)) APP->filters.to = d;
    filters_changed(NULL);
}

static void sheet_paid(GObject *o, GParamSpec *p, gpointer u) {
    (void)p; (void)u;
    if (M.syncing) return;
    guint k = gtk_drop_down_get_selected(GTK_DROP_DOWN(o));
    APP->filters.paid = k == 1 ? 1 : k == 2 ? 0 : -1;
    filters_changed(NULL);
}

static void preset(gpointer w) {
    Filters *f = &APP->filters;
    int which = GPOINTER_TO_INT(w);
    if (which == 0) filters_this_month(f);
    else if (which == 1) { f->to = APP->today; f->from = APP->today - 29; }
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
    sheet_sync();
    filters_changed(NULL);
}

static void sheet_closed(AdwDialog *d, gpointer u) { (void)d; (void)u; M.f_from = M.f_to = M.f_paid = NULL; }

static GtkWidget *labeled(const char *label, GtkWidget *w) {
    GtkWidget *b = w_vbox(4);
    gtk_widget_set_hexpand(b, TRUE);
    w_add(b, w_label(label, "fin-muted caption"));
    w_add(b, w);
    return b;
}

static void open_filters(gpointer u) {
    (void)u;
    AdwDialog *d = adw_dialog_new();
    adw_dialog_set_title(d, "Período e filtros");
    adw_dialog_set_content_width(d, 440);
    GtkWidget *tv = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), adw_header_bar_new());
    GtkWidget *c = w_vbox(14);
    gtk_widget_set_margin_start(c, 18);
    gtk_widget_set_margin_end(c, 18);
    gtk_widget_set_margin_bottom(c, 18);
    w_add(c, w_label_wrap("O período também vale para Relatórios. As mudanças valem na hora.", "fin-muted"));
    GtkWidget *dates = w_hbox(8);
    gtk_box_set_homogeneous(GTK_BOX(dates), TRUE);
    M.f_from = date_field("De", APP->filters.from, sheet_dates, NULL);
    M.f_to = date_field("Até", APP->filters.to, sheet_dates, NULL);
    w_add(dates, labeled("De", M.f_from));
    w_add(dates, labeled("Até", M.f_to));
    w_add(c, dates);
    GtkWidget *pills = w_hbox(6);
    gtk_box_set_homogeneous(GTK_BOX(pills), TRUE);
    w_add(pills, w_pill("Este mês", preset, GINT_TO_POINTER(0), NULL));
    w_add(pills, w_pill("30 dias", preset, GINT_TO_POINTER(1), NULL));
    w_add(pills, w_pill("Tudo", preset, GINT_TO_POINTER(2), NULL));
    w_add(c, pills);
    static const char *const paid[] = {"Todos", "Realizados", "Pendentes", NULL};
    M.f_paid = gtk_drop_down_new_from_strings(paid);
    gtk_accessible_update_property(GTK_ACCESSIBLE(M.f_paid), GTK_ACCESSIBLE_PROPERTY_LABEL, "Situação", -1);
    w_add(c, labeled("Situação", M.f_paid));
    sheet_sync();
    g_signal_connect(M.f_paid, "notify::selected", G_CALLBACK(sheet_paid), NULL);
    GtkWidget *ok = gtk_button_new_with_label("Pronto");
    gtk_widget_add_css_class(ok, "suggested-action");
    gtk_widget_add_css_class(ok, "pill");
    g_signal_connect_swapped(ok, "clicked", G_CALLBACK(adw_dialog_close), d);
    w_add(c, ok);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), c);
    adw_dialog_set_child(d, tv);
    g_signal_connect(d, "closed", G_CALLBACK(sheet_closed), NULL);
    adw_dialog_present(d, GTK_WIDGET(APP->window));
}

/* ---------------------------------------------------------------- resumo do período */

static GtkWidget *sum_col(const char *label, Cents v, const char *cls, const char *sub, Cents sv, const char *scls, gboolean show) {
    GtkWidget *b = w_vbox(2);
    gtk_widget_set_hexpand(b, TRUE);
    w_add(b, w_label(label, "fin-muted caption"));
    w_add(b, w_money(v, cls));
    if (show) {
        w_add(b, w_label(sub, "fin-muted caption"));
        w_add(b, w_money(sv, scls));
    }
    return b;
}

static void build_totals(void) {
    w_clear(M.totals);
    Flow f = flow_of(M.items);
    Pending p = period_pending(M.items);
    Cents bal = flow_balance(f), forecast = bal + p.to_receive - p.to_pay;
    GtkWidget *card = w_card("fin-tight fin-flat");
    GtkWidget *row = w_hbox(8);
    gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
    w_add(row, sum_col("Receitas", f.income, "fin-money-mid fin-green", "a receber", p.to_receive, "caption fin-green", p.to_receive > 0));
    w_add(row, sum_col("Despesas", f.expense, "fin-money-mid fin-red", "a pagar", p.to_pay, "caption fin-red", p.to_pay > 0));
    w_add(row, sum_col("Saldo", bal, bal < 0 ? "fin-money-mid fin-red" : "fin-money-mid", "previsto", forecast,
                       forecast < 0 ? "caption fin-red" : "caption fin-accent", p.to_receive > 0 || p.to_pay > 0));
    w_add(card, row);
    if (f.income > 0 && !app_hidden()) {
        char *pc = g_strdup_printf("%.1f", f.expense * 100.0 / f.income);
        for (char *q = pc; *q; q++) if (*q == '.') *q = ',';
        g_autofree char *t = g_strdup_printf("As despesas são %s%% das receitas do período.", pc);
        g_free(pc);
        GtkWidget *l = w_label_wrap(t, "fin-muted caption");
        gtk_widget_set_margin_top(l, 6);
        w_add(card, l);
    }
    w_add(M.totals, card);
}

/* ---------------------------------------------------------------- lista agrupada por dia */

static void update_header(GtkListBoxRow *row, GtkListBoxRow *before, gpointer u) {
    (void)u;
    Day d = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(gtk_list_box_row_get_child(row)), "date"));
    if (before && GPOINTER_TO_INT(g_object_get_data(G_OBJECT(gtk_list_box_row_get_child(before)), "date")) == d) {
        gtk_list_box_row_set_header(row, NULL);
        return;
    }
    GtkWidget *h = w_hbox(8);
    gtk_widget_add_css_class(h, "fin-day-head");
    g_autofree char *t = cal_day_title(d, APP->today);
    g_autofree char *tt = d == APP->today ? g_strdup_printf("Hoje · %s", t) : g_strdup(t);
    GtkWidget *l = w_label(tt, "heading");
    gtk_widget_set_hexpand(l, TRUE);
    w_add(h, l);
    if (M.day_cash && g_hash_table_contains(M.day_cash, GINT_TO_POINTER(d)) && !app_hidden()) {
        Cents net = (Cents)(gintptr)0;
        gpointer v = g_hash_table_lookup(M.day_net, GINT_TO_POINTER(d));
        if (v) net = *(Cents *)v;
        g_autofree char *m = money_fmt(net < 0 ? -net : net);
        g_autofree char *s = g_strdup_printf("%s%s", net > 0 ? "+ " : net < 0 ? "− " : "", m);
        w_add(h, w_label(s, net < 0 ? "fin-money fin-red" : "fin-money fin-green"));
    }
    gtk_list_box_row_set_header(row, h);
}

static void fill_list(guint upto) {
    while (M.shown < upto && M.shown < M.items->len) {
        Tx *t = M.items->pdata[M.shown++];
        gtk_list_box_append(GTK_LIST_BOX(M.list), tx_row_new_full(t, FALSE));
    }
    gboolean more = M.shown < M.items->len;
    gtk_widget_set_visible(M.more, more);
    if (more) {
        g_autofree char *l = g_strdup_printf("Mostrar mais (%u de %u)", M.shown, M.items->len);
        gtk_button_set_label(GTK_BUTTON(M.more), l);
    }
}

static void show_more(GtkButton *b, gpointer u) { (void)b; (void)u; fill_list(M.shown + PAGE_SIZE); }

static void sync_panel(void) {
    const Filters *f = &APP->filters;
    M.syncing = TRUE;
    g_autofree char *lbl = period_label(f->from, f->to);
    gtk_label_set_text(GTK_LABEL(M.period_label), lbl);
    gboolean custom = period_full_month(f->from, f->to) == YM_NONE || f->paid == 1;
    if (custom) gtk_widget_add_css_class(M.tune, "on"); else gtk_widget_remove_css_class(M.tune, "on");
    if (strcmp(gtk_editable_get_text(GTK_EDITABLE(M.search)), f->query) != 0) gtk_editable_set_text(GTK_EDITABLE(M.search), f->query);
    gboolean on[4] = {f->kind < 0 && f->paid < 0, f->kind == KIND_INCOME, f->kind == KIND_EXPENSE, f->paid == 0};
    for (int i = 0; i < 4; i++) {
        if (on[i]) gtk_widget_add_css_class(M.chips[i], "selected"); else gtk_widget_remove_css_class(M.chips[i], "selected");
        gtk_accessible_update_state(GTK_ACCESSIBLE(M.chips[i]), GTK_ACCESSIBLE_STATE_PRESSED, on[i] ? GTK_ACCESSIBLE_TRISTATE_TRUE : GTK_ACCESSIBLE_TRISTATE_FALSE, -1);
    }
    M.syncing = FALSE;
}

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
    /* saldo de cada dia (mesma regra do calendário) para os títulos dos grupos */
    g_hash_table_remove_all(M.day_net);
    g_hash_table_remove_all(M.day_cash);
    for (guint i = 0; i < M.items->len; i++) {
        Tx *t = M.items->pdata[i];
        Cents *n = g_hash_table_lookup(M.day_net, GINT_TO_POINTER(t->date));
        if (!n) { n = g_new0(Cents, 1); g_hash_table_insert(M.day_net, GINT_TO_POINTER(t->date), n); }
        if (!tx_is_card(t)) {
            *n += t->kind == KIND_INCOME ? t->value : -t->value;
            g_hash_table_add(M.day_cash, GINT_TO_POINTER(t->date));
        }
    }
    gtk_list_box_remove_all(GTK_LIST_BOX(M.list));
    M.shown = 0;
    fill_list(keep);
    char n[16];
    g_snprintf(n, sizeof n, "%u", M.items->len);
    gtk_label_set_text(GTK_LABEL(M.count), n);
    /* aviso de lista vazia controlado aqui: gtk_list_box_remove_all() também descarta o placeholder da GtkListBox */
    gtk_widget_set_visible(M.list, M.items->len > 0);
    gtk_widget_set_visible(M.empty, M.items->len == 0);
    build_totals();
    sync_panel();
    if (keep_scroll) gtk_adjustment_set_value(adj, pos);
}

static void on_activated(GtkListBox *box, GtkListBoxRow *row, gpointer u) {
    (void)box; (void)u;
    const char *id = g_object_get_data(G_OBJECT(gtk_list_box_row_get_child(row)), "id");
    Tx *t = id ? app_tx(APP->state, id) : NULL;
    if (t) editor_tx(t->kind, t->id);
}

void page_moves_focus_search(void) {
    if (APP->moves_view != 0) { APP->moves_view = 0; page_moves_refresh(); }
    gtk_widget_grab_focus(M.search);
}

/* ================================================================ calendário */

static void cal_build(void);
static void cal_select(Day d);

static void cal_shift(gpointer d) {
    APP->cal_month += GPOINTER_TO_INT(d);
    cal_build();
}

static void cal_today(gpointer u) {
    (void)u;
    APP->cal_month = day_ym(APP->today);
    APP->cal_day = APP->today;
    cal_build();
}

static void cal_new(gpointer kind) { editor_tx_on(GPOINTER_TO_INT(kind), NULL, APP->cal_day); }

/* 1º clique escolhe o dia; clicar de novo no dia escolhido abre um lançamento novo nessa data */
static void cal_day_clicked(GtkButton *b, gpointer u) {
    (void)u;
    if (M.suppress_click) { M.suppress_click = FALSE; return; }
    Day d = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "day"));
    if (APP->cal_day == d) { editor_tx_on(KIND_EXPENSE, NULL, d); return; }
    cal_select(d);
}

/* tocar e segurar (ou clique direito): lançamento novo nesse dia */
static void cal_day_hold(GtkGesture *g, double x, double y, gpointer u) {
    (void)x; (void)y; (void)u;
    GtkWidget *b = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
    Day d = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "day"));
    gtk_gesture_set_state(g, GTK_EVENT_SEQUENCE_CLAIMED);
    M.suppress_click = TRUE;
    cal_select(d);
    editor_tx_on(KIND_EXPENSE, NULL, d);
}

static void cal_right_click(GtkGestureClick *g, int n, double x, double y, gpointer u) {
    (void)n;
    M.suppress_click = FALSE;
    cal_day_hold(GTK_GESTURE(g), x, y, u);
    M.suppress_click = FALSE;
}

static void cal_swipe(GtkGestureSwipe *g, double vx, double vy, gpointer u) {
    (void)g; (void)u;
    if (fabs(vx) > 700 && fabs(vx) > 2 * fabs(vy)) cal_shift(GINT_TO_POINTER(vx < 0 ? 1 : -1));
}

static void open_invoice(gpointer card_id) { editor_pay_invoice(card_id); }

static GtkWidget *day_cell(Day d, const CalDay *cd) {
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "fin-cal-day");
    if (d == APP->today) gtk_widget_add_css_class(b, "today");
    GtkWidget *v = w_vbox(2);
    gtk_widget_set_valign(v, GTK_ALIGN_START);
    char num[4];
    g_snprintf(num, sizeof num, "%d", day_dom(d));
    GtkWidget *top = w_hbox(2);
    gtk_widget_set_halign(top, GTK_ALIGN_CENTER);
    w_add(top, w_label(num, d < APP->today ? "fin-cal-num fin-muted" : "fin-cal-num"));
    if (cd && cd->overdue) {
        GtkWidget *ic = w_icon("warning", 12);
        gtk_widget_add_css_class(ic, "fin-cal-late");
        w_add(top, ic);
    }
    w_add(v, top);
    if (cd && (cd->income || cd->expense) && !app_hidden()) {
        char s[32];
        Cents net = cal_day_net(cd);
        GtkWidget *l = w_label(cal_signed(net, s), net < 0 ? "fin-cal-val fin-red" : net > 0 ? "fin-cal-val fin-green" : "fin-cal-val fin-muted");
        gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_NONE);
        gtk_label_set_xalign(GTK_LABEL(l), 0.5);
        gtk_widget_set_halign(l, GTK_ALIGN_CENTER);
        w_add(v, l);
    }
    if (cd && cd->marks) {
        GtkWidget *dots = w_hbox(3);
        gtk_widget_set_halign(dots, GTK_ALIGN_CENTER);
        if (cd->marks & MARK_INCOME) { GtkWidget *x = w_box(GTK_ORIENTATION_HORIZONTAL, 0); w_classes(x, "fin-dot inc"); w_add(dots, x); }
        if (cd->marks & MARK_EXPENSE) { GtkWidget *x = w_box(GTK_ORIENTATION_HORIZONTAL, 0); w_classes(x, "fin-dot exp"); w_add(dots, x); }
        if (cd->marks & MARK_CARD) { GtkWidget *x = w_box(GTK_ORIENTATION_HORIZONTAL, 0); w_classes(x, "fin-dot card"); w_add(dots, x); }
        w_add(v, dots);
    }
    gtk_button_set_child(GTK_BUTTON(b), v);
    g_autofree char *desc = cal_describe(d, cd, APP->today, app_hidden());
    gtk_accessible_update_property(GTK_ACCESSIBLE(b), GTK_ACCESSIBLE_PROPERTY_LABEL, desc, -1);
    gtk_widget_set_tooltip_text(b, desc);
    g_object_set_data(G_OBJECT(b), "day", GINT_TO_POINTER(d));
    g_signal_connect(b, "clicked", G_CALLBACK(cal_day_clicked), NULL);
    GtkGesture *lp = gtk_gesture_long_press_new();
    g_signal_connect(lp, "pressed", G_CALLBACK(cal_day_hold), NULL);
    gtk_widget_add_controller(b, GTK_EVENT_CONTROLLER(lp));
    GtkGesture *rc = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rc), GDK_BUTTON_SECONDARY);
    g_signal_connect(rc, "pressed", G_CALLBACK(cal_right_click), NULL);
    gtk_widget_add_controller(b, GTK_EVENT_CONTROLLER(rc));
    return b;
}

static GtkWidget *round_btn(const char *icon, const char *tip, FinFn fn, gpointer data) {
    GtkWidget *b = w_button(NULL, icon, "fin-round", fn, data, NULL);
    gtk_widget_set_tooltip_text(b, tip);
    gtk_accessible_update_property(GTK_ACCESSIBLE(b), GTK_ACCESSIBLE_PROPERTY_LABEL, tip, -1);
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    return b;
}

static GtkWidget *total_col(const char *label, Cents v, const char *cls) {
    GtkWidget *b = w_vbox(2);
    gtk_widget_set_hexpand(b, TRUE);
    w_add(b, w_label(label, "fin-muted caption"));
    w_add(b, w_money(v, cls));
    return b;
}

static void cal_build(void) {
    if (!M.cal_body || !APP->state) return;
    w_clear(M.cal_body);
    const AppState *s = APP->state;
    if (APP->cal_month == YM_NONE) APP->cal_month = day_ym(APP->today);
    if (APP->cal_day == DAY_NONE) APP->cal_day = APP->today;
    Ym ym = APP->cal_month;
    cal_month_free(M.cal);
    CalMonth *m = M.cal = cal_month_build(s, ym, APP->today);
    memset(M.cal_btns, 0, sizeof M.cal_btns);

    GtkWidget *grid_card = w_card("fin-flat");
    GtkWidget *bar = w_hbox(6);
    w_add(bar, round_btn("chevron-left", "Mês anterior", cal_shift, GINT_TO_POINTER(-1)));
    GtkWidget *tbox = w_vbox(0);
    gtk_widget_set_hexpand(tbox, TRUE);
    g_autofree char *title = cal_month_title(ym);
    GtkWidget *tl = w_label(title, "title-4");
    gtk_label_set_xalign(GTK_LABEL(tl), 0.5);
    w_add(tbox, tl);
    if (ym != day_ym(APP->today)) {
        GtkWidget *back = w_button("Voltar para hoje", NULL, "flat fin-link", cal_today, NULL, NULL);
        gtk_widget_set_halign(back, GTK_ALIGN_CENTER);
        w_add(tbox, back);
    }
    w_add(bar, tbox);
    w_add(bar, round_btn("chevron-right", "Próximo mês", cal_shift, GINT_TO_POINTER(1)));
    w_add(grid_card, bar);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    gtk_widget_set_margin_top(grid, 8);
    static const char *const WD[7] = {"DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SÁB"};
    for (int i = 0; i < 7; i++) {
        GtkWidget *l = w_label(WD[i], "fin-cal-wd");
        gtk_label_set_xalign(GTK_LABEL(l), 0.5);
        gtk_grid_attach(GTK_GRID(grid), l, i, 0, 1, 1);
    }
    Day cells[42];
    int n = cal_cells(ym, cells);
    for (int i = 0; i < n; i++) {
        if (cells[i] == DAY_NONE) continue;
        GtkWidget *cell = M.cal_btns[day_dom(cells[i]) - 1] = day_cell(cells[i], cal_month_day(m, cells[i]));
        gtk_grid_attach(GTK_GRID(grid), cell, i % 7, 1 + i / 7, 1, 1);
    }
    GtkGesture *sw = gtk_gesture_swipe_new();
    g_signal_connect(sw, "swipe", G_CALLBACK(cal_swipe), NULL);
    gtk_widget_add_controller(grid, GTK_EVENT_CONTROLLER(sw));
    w_add(grid_card, grid);
    GtkWidget *legend = w_label_wrap("Clique num dia para ver os lançamentos; clique de novo (ou segure, ou use o botão direito) para lançar nessa data.", "fin-muted caption");
    gtk_widget_set_margin_top(legend, 6);
    w_add(grid_card, legend);
    w_add(M.cal_body, grid_card);

    /* totais do mês: soma dos dias (inclui pendências e faturas) */
    Cents ti, te;
    cal_month_totals(m, &ti, &te);
    GtkWidget *tot = w_card("fin-tight fin-flat");
    GtkWidget *tr = w_hbox(8);
    gtk_box_set_homogeneous(GTK_BOX(tr), TRUE);
    w_add(tr, total_col("Entradas", ti, "fin-money-mid fin-green"));
    w_add(tr, total_col("Saídas", te, "fin-money-mid fin-red"));
    w_add(tr, total_col("Resultado", ti - te, ti - te < 0 ? "fin-money-mid fin-red" : "fin-money-mid"));
    w_add(tot, tr);
    w_add(tot, w_label_wrap("Inclui o que está pendente e as faturas no vencimento.", "fin-muted caption"));
    w_add(M.cal_body, tot);

    M.cal_day_box = w_vbox(0);
    w_add(M.cal_body, M.cal_day_box);
    cal_select(APP->cal_day);
}


/* escolhe o dia sem refazer a grade (só troca o destaque e o painel do dia) */
static void cal_select(Day d) {
    const AppState *s = APP->state;
    CalMonth *m = M.cal;
    if (!m || !M.cal_day_box) return;
    Ym ym = m->ym;
    if (APP->cal_day != DAY_NONE && day_ym(APP->cal_day) == ym && M.cal_btns[day_dom(APP->cal_day) - 1])
        gtk_widget_remove_css_class(M.cal_btns[day_dom(APP->cal_day) - 1], "sel");
    APP->cal_day = d;
    w_clear(M.cal_day_box);
    if (d == DAY_NONE || day_ym(d) != ym) return;
    gtk_widget_add_css_class(M.cal_btns[day_dom(d) - 1], "sel");
    const CalDay *cd = cal_month_day(m, d);
    GtkWidget *dc = w_card("fin-flat");
    GtkWidget *hr = w_hbox(8);
    g_autofree char *dt = cal_day_title(d, APP->today);
    g_autofree char *dtt = d == APP->today ? g_strdup_printf("Hoje · %s", dt) : g_strdup(dt);
    GtkWidget *hl = w_label(dtt, "title-4");
    gtk_widget_set_hexpand(hl, TRUE);
    w_add(hr, hl);
    if (cd && (cd->income || cd->expense)) {
        Cents net = cal_day_net(cd);
        w_add(hr, w_money(net, net < 0 ? "fin-money-mid fin-red" : "fin-money-mid fin-green"));
    }
    w_add(dc, hr);
    if (d >= APP->today) {
        GtkWidget *fr = w_hbox(6);
        w_add(fr, w_label("Saldo previsto ao fim do dia:", "fin-muted caption"));
        Cents fb = future_balance(s, d, APP->today);
        w_add(fr, w_money(fb, fb < 0 ? "caption fin-red" : "caption fin-accent"));
        w_add(dc, fr);
    }
    GtkWidget *btns = w_hbox(8);
    gtk_box_set_homogeneous(GTK_BOX(btns), TRUE);
    gtk_widget_set_margin_top(btns, 6);
    w_add(btns, w_button("Receita", "add", "fin-quick", cal_new, GINT_TO_POINTER(KIND_INCOME), NULL));
    w_add(btns, w_button("Despesa", "remove", "fin-quick", cal_new, GINT_TO_POINTER(KIND_EXPENSE), NULL));
    w_add(dc, btns);
    if (!cd) w_add(dc, w_label_wrap("Nenhum lançamento neste dia.", "fin-muted"));
    else {
        for (guint j = 0; j < cd->invoices->len; j++) {
            InvoiceDue *iv = &g_array_index(cd->invoices, InvoiceDue, j);
            GtkWidget *row = w_hbox(12);
            gtk_widget_add_css_class(row, "fin-row");
            GtkWidget *ic = w_icon("credit-card", 20);
            gtk_widget_add_css_class(ic, "fin-card-purple");
            w_add(row, ic);
            GtkWidget *tx = w_vbox(2);
            gtk_widget_set_hexpand(tx, TRUE);
            g_autofree char *nm = g_strdup_printf("Fatura %s", iv->card_name);
            w_add(tx, w_label(nm, "heading"));
            w_add(tx, w_label(iv->overdue ? "Vencida · em aberto" : "Vence neste dia · em aberto", iv->overdue ? "fin-late caption" : "fin-muted caption"));
            w_add(row, tx);
            w_add(row, w_money(iv->amount, "fin-money fin-red"));
            GtkWidget *pay = w_button("Pagar", NULL, "fin-pill", open_invoice, g_strdup(iv->card_id), g_free);
            gtk_widget_set_valign(pay, GTK_ALIGN_CENTER);
            w_add(row, pay);
            gtk_widget_set_margin_top(row, 6);
            w_add(dc, row);
        }
        if (cd->txs->len) {
            GtkWidget *lb = gtk_list_box_new();
            gtk_list_box_set_selection_mode(GTK_LIST_BOX(lb), GTK_SELECTION_NONE);
            gtk_widget_add_css_class(lb, "fin-list");
            gtk_widget_set_margin_top(lb, 4);
            for (guint j = 0; j < cd->txs->len; j++) gtk_list_box_append(GTK_LIST_BOX(lb), tx_row_new_full(cd->txs->pdata[j], FALSE));
            g_signal_connect(lb, "row-activated", G_CALLBACK(on_activated), NULL);
            w_add(dc, lb);
        }
    }
    w_add(M.cal_day_box, dc);
}

/* ‹ mês › com o botão de período livre, para os Relatórios (o período é o mesmo da aba Lançamentos) */
GtkWidget *moves_period_bar(void) {
    const Filters *f = &APP->filters;
    GtkWidget *bar = w_hbox(4);
    w_add(bar, round_btn("chevron-left", "Mês anterior", shift_period, GINT_TO_POINTER(-1)));
    g_autofree char *lbl = period_label(f->from, f->to);
    GtkWidget *l = w_label(lbl, "title-4");
    gtk_label_set_xalign(GTK_LABEL(l), 0.5);
    gtk_widget_set_hexpand(l, TRUE);
    w_add(bar, l);
    w_add(bar, round_btn("chevron-right", "Próximo mês", shift_period, GINT_TO_POINTER(1)));
    GtkWidget *tune = round_btn("tune", "Período e filtros (datas livres e situação)", open_filters, NULL);
    if (period_full_month(f->from, f->to) == YM_NONE || f->paid == 1) gtk_widget_add_css_class(tune, "on");
    w_add(bar, tune);
    return bar;
}

/* Relatórios › "Ver no calendário": abre o calendário no mês do período */
void moves_open_calendar(void) {
    APP->moves_view = 1;
    APP->cal_month = day_ym(APP->filters.from != DAY_NONE ? APP->filters.from : APP->today);
    if (day_ym(APP->cal_day) != APP->cal_month) APP->cal_day = APP->cal_month == day_ym(APP->today) ? APP->today : ym_first(APP->cal_month);
    app_refresh_page(PAGE_MOVES);
    app_show_page(PAGE_MOVES);
}

/* ================================================================ montagem */

static void set_view(GtkToggleButton *b, gpointer u) {
    (void)u;
    if (M.syncing || !gtk_toggle_button_get_active(b)) return;
    APP->moves_view = GTK_WIDGET(b) == M.seg_cal ? 1 : 0;
    page_moves_refresh();
}

void page_moves_refresh(void) {
    if (!M.root || !APP->state) return;
    M.syncing = TRUE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(APP->moves_view ? M.seg_cal : M.seg_list), TRUE);
    M.syncing = FALSE;
    gtk_stack_set_visible_child(GTK_STACK(M.stack), APP->moves_view ? M.view_cal : M.view_list);
    if (APP->moves_view) { cal_build(); return; }
    gboolean narrow = APP->layout == LAYOUT_NARROW;
    gtk_orientable_set_orientation(GTK_ORIENTABLE(M.list_root), narrow ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
    int pw = APP->layout == LAYOUT_WIDE ? 400 : 350;
    gtk_widget_set_size_request(M.left, narrow ? -1 : pw, -1);
    gtk_widget_set_vexpand(M.panel_sw, !narrow);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(M.panel_sw), GTK_POLICY_NEVER, narrow ? GTK_POLICY_NEVER : GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(M.panel_sw), narrow);
    rebuild_list(TRUE);
}

static GtkWidget *build_list_view(void) {
    M.list_root = w_hbox(0);

    /* ---- painel: ‹ mês ›, busca, filtros de um toque e resumo ---- */
    GtkWidget *panel = w_vbox(10);
    gtk_widget_set_margin_start(panel, 20);
    gtk_widget_set_margin_end(panel, 12);
    gtk_widget_set_margin_top(panel, 6);
    gtk_widget_set_margin_bottom(panel, 12);

    GtkWidget *bar = w_hbox(4);
    w_add(bar, round_btn("chevron-left", "Mês anterior", shift_period, GINT_TO_POINTER(-1)));
    M.period_label = w_label("", "title-4");
    gtk_label_set_xalign(GTK_LABEL(M.period_label), 0.5);
    gtk_widget_set_hexpand(M.period_label, TRUE);
    gtk_accessible_update_property(GTK_ACCESSIBLE(M.period_label), GTK_ACCESSIBLE_PROPERTY_LABEL, "Período", -1);
    w_add(bar, M.period_label);
    w_add(bar, round_btn("chevron-right", "Próximo mês", shift_period, GINT_TO_POINTER(1)));
    M.tune = round_btn("tune", "Período e filtros (datas livres e situação)", open_filters, NULL);
    w_add(bar, M.tune);
    w_add(panel, bar);

    M.search = gtk_search_entry_new();
    g_object_set(M.search, "placeholder-text", "Buscar descrição ou categoria", NULL);
    gtk_accessible_update_property(GTK_ACCESSIBLE(M.search), GTK_ACCESSIBLE_PROPERTY_LABEL, "Buscar lançamentos (Ctrl+F)", -1);
    g_signal_connect(M.search, "search-changed", G_CALLBACK(on_search), NULL);
    w_add(panel, M.search);

    GtkWidget *chips = w_hbox(6);
    static const char *const CH[4] = {"Todos", "Receitas", "Despesas", "Pendentes"};
    for (int i = 0; i < 4; i++) {
        M.chips[i] = w_pill(CH[i], chip, GINT_TO_POINTER(i), NULL);
        w_add(chips, M.chips[i]);
    }
    w_add(panel, chips);

    M.totals = w_vbox(10);
    w_add(panel, M.totals);

    M.panel_sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(M.panel_sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(M.panel_sw), panel);
    M.left = w_vbox(0);
    w_add(M.left, M.panel_sw);
    w_add(M.list_root, M.left);

    /* ---- lista ---- */
    GtkWidget *right = w_vbox(4);
    gtk_widget_set_hexpand(right, TRUE);
    gtk_widget_set_vexpand(right, TRUE);
    GtkWidget *head = w_hbox(8);
    gtk_widget_set_margin_start(head, 12);
    gtk_widget_set_margin_end(head, 24);
    gtk_widget_set_margin_top(head, 6);
    GtkWidget *hh = w_section_head("No período", "Lançamentos por dia");
    gtk_widget_set_hexpand(hh, TRUE);
    w_add(head, hh);
    M.count = w_badge("0", "accent");
    gtk_widget_set_tooltip_text(M.count, "Lançamentos na lista");
    gtk_widget_set_valign(M.count, GTK_ALIGN_END);
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
    gtk_list_box_set_header_func(GTK_LIST_BOX(M.list), update_header, NULL, NULL);
    g_signal_connect(M.list, "row-activated", G_CALLBACK(on_activated), NULL);
    /* aviso de lista vazia, irmão da lista (não placeholder: remove_all() o descartaria) */
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
    w_add(M.list_root, right);
    return M.list_root;
}

static GtkWidget *build_cal_view(void) {
    M.cal_body = w_vbox(12);
    gtk_widget_set_margin_start(M.cal_body, 16);
    gtk_widget_set_margin_end(M.cal_body, 16);
    gtk_widget_set_margin_top(M.cal_body, 6);
    gtk_widget_set_margin_bottom(M.cal_body, 24);
    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 760);
    adw_clamp_set_child(ADW_CLAMP(clamp), M.cal_body);
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), clamp);
    gtk_widget_set_vexpand(sw, TRUE);
    return sw;
}

static GtkWidget *seg_button(const char *icon, const char *label) {
    GtkWidget *b = gtk_toggle_button_new();
    GtkWidget *c = adw_button_content_new();
    g_autofree char *n = icon_name(icon);
    adw_button_content_set_icon_name(ADW_BUTTON_CONTENT(c), n);
    adw_button_content_set_label(ADW_BUTTON_CONTENT(c), label);
    gtk_button_set_child(GTK_BUTTON(b), c);
    gtk_widget_set_hexpand(b, TRUE);
    return b;
}

GtkWidget *page_moves_new(void) {
    M.day_net = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    M.day_cash = g_hash_table_new(g_direct_hash, g_direct_equal);
    M.root = w_vbox(6);
    /* chave Lista | Calendário */
    GtkWidget *seg = w_hbox(0);
    gtk_widget_add_css_class(seg, "linked");
    gtk_widget_set_margin_top(seg, 14);
    gtk_widget_set_margin_start(seg, 20);
    gtk_widget_set_margin_end(seg, 20);
    gtk_widget_set_halign(seg, GTK_ALIGN_FILL);
    gtk_widget_set_size_request(seg, -1, 40);
    M.seg_list = seg_button("view-list", "Lista");
    M.seg_cal = seg_button("calendar-month", "Calendário");
    gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(M.seg_cal), GTK_TOGGLE_BUTTON(M.seg_list));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(M.seg_list), TRUE);
    g_signal_connect(M.seg_list, "toggled", G_CALLBACK(set_view), NULL);
    g_signal_connect(M.seg_cal, "toggled", G_CALLBACK(set_view), NULL);
    w_add(seg, M.seg_list);
    w_add(seg, M.seg_cal);
    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 520);
    adw_clamp_set_child(ADW_CLAMP(clamp), seg);
    gtk_widget_set_halign(clamp, APP->layout == LAYOUT_NARROW ? GTK_ALIGN_FILL : GTK_ALIGN_START);
    w_add(M.root, clamp);

    M.stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(M.stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_vexpand(M.stack, TRUE);
    M.view_list = build_list_view();
    M.view_cal = build_cal_view();
    gtk_stack_add_child(GTK_STACK(M.stack), M.view_list);
    gtk_stack_add_child(GTK_STACK(M.stack), M.view_cal);
    w_add(M.root, M.stack);
    return M.root;
}
