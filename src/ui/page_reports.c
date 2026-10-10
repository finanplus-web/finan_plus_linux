/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Relatórios: o mesmo período da aba Lançamentos (‹ mês › e "Período e filtros"), só valores realizados.
 * Resumo com comparação justa (mês atual contra os mesmos dias do mês anterior), atalho para o
 * simulador "E se…?", despesas por categoria (rosca e barras, com limites) e evolução dos últimos 6 meses.
 * Sem nada realizado no período, mostra o que está pendente e leva ao calendário.
 */
#include "pages.h"
#include "pdf.h"
#include "theme.h"
#include "widgets.h"
#include "core/finance.h"
#include "core/report.h"
#include "core/period.h"
#include <math.h>

static GtkWidget *body;

/* ---------------------------------------------------------------- por categoria */

typedef struct { Cents values[8]; int n; Cents total; } DonutData;

static void draw_donut(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer p) {
    (void)a;
    DonutData *d = p;
    double size = MIN(w, h);
    if (app_hidden()) {
        Cents one = 1;
        chart_donut(cr, w / 2.0, h / 2.0, size, 26, &one, 1, 1);
        return;
    }
    chart_donut(cr, w / 2.0, h / 2.0, size, 26, d->values, d->n, d->total);
}

static GtkWidget *categories_card(void) {
    const AppState *s = APP->state;
    const Filters *f = &APP->filters;
    GtkWidget *c = w_card(NULL);
    gtk_widget_set_valign(c, GTK_ALIGN_START);
    w_add(c, w_section_head("Despesas", "Por categoria"));
    g_autoptr(GPtrArray) cats = category_totals(s, f->from, f->to);
    Cents total = 0;
    for (guint i = 0; i < cats->len; i++) total += ((CatTotal *)cats->pdata[i])->value;
    if (!cats->len) { w_add(c, w_label_wrap("Sem despesas realizadas no período.", "fin-muted")); return c; }

    /* rosca com as 7 maiores + "Outras" */
    DonutData *dd = g_new0(DonutData, 1);
    dd->total = total;
    for (guint i = 0; i < cats->len; i++) {
        Cents v = ((CatTotal *)cats->pdata[i])->value;
        if (i < 7) dd->values[dd->n++] = v;
        else { if (dd->n == 7) dd->values[dd->n++] = 0; dd->values[7] += v; }
    }
    GtkWidget *top = w_hbox(18);
    GtkWidget *overlay = gtk_overlay_new();
    GtkWidget *da = gtk_drawing_area_new();
    gtk_widget_set_size_request(da, 170, 170);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), draw_donut, dd, g_free);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), da);
    GtkWidget *center = w_vbox(0);
    gtk_widget_set_halign(center, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(center, GTK_ALIGN_CENTER);
    GtkWidget *tl = w_label("Total", "fin-muted caption");
    gtk_label_set_xalign(GTK_LABEL(tl), 0.5);
    w_add(center, tl);
    g_autofree char *compact = app_hidden() ? g_strdup("R$ ••••") : report_compact(total);
    GtkWidget *cl = w_label(compact, "fin-money");
    gtk_label_set_xalign(GTK_LABEL(cl), 0.5);
    w_add(center, cl);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), center);
    gtk_widget_set_valign(overlay, GTK_ALIGN_START);
    w_add(top, overlay);
    /* descrição do gráfico para leitores de tela */
    GString *desc = g_string_new("Gráfico de despesas por categoria. ");

    GtkWidget *legend = w_vbox(6);
    gtk_widget_set_hexpand(legend, TRUE);
    gtk_widget_set_valign(legend, GTK_ALIGN_CENTER);
    for (int i = 0; i < dd->n; i++) {
        const char *name = i < 7 ? ((CatTotal *)cats->pdata[i])->name : "Outras";
        GtkWidget *r = w_hbox(8);
        GtkWidget *dot = gtk_label_new("●");
        g_autofree char *colored = g_strdup_printf("<span foreground=\"#%06X\">●</span>", CHART_SERIES[i % 8] & 0xFFFFFF);
        gtk_label_set_markup(GTK_LABEL(dot), colored);
        w_add(r, dot);
        GtkWidget *nl = w_label(name, NULL);
        gtk_widget_set_hexpand(nl, TRUE);
        w_add(r, nl);
        char pct[24];
        g_snprintf(pct, sizeof pct, "%.0f%%", dd->values[i] * 100.0 / total);
        w_add(r, w_label(app_hidden() ? "••" : pct, "fin-muted caption"));
        w_add(legend, r);
        if (!app_hidden()) g_string_append_printf(desc, "%s %s; ", name, pct);
    }
    w_add(top, legend);
    gtk_accessible_update_property(GTK_ACCESSIBLE(da), GTK_ACCESSIBLE_PROPERTY_LABEL, desc->str, -1);
    g_string_free(desc, TRUE);
    w_add(c, top);

    /* barras com todas as categorias e os limites */
    for (guint i = 0; i < cats->len; i++) {
        CatTotal *ct = cats->pdata[i];
        Cents lim = 0;
        gboolean has_lim = app_limit(s, ct->name, &lim);
        GtkWidget *row = w_hbox(10);
        gtk_widget_set_margin_top(row, 4);
        GtkWidget *nl = w_label(ct->name, NULL);
        gtk_widget_set_size_request(nl, 130, -1);
        w_add(row, nl);
        w_add(row, w_level(total > 0 ? (double)ct->value / total : 0, has_lim && ct->value > lim ? "over" : "red"));
        GtkWidget *vl = w_money(ct->value, NULL);
        gtk_widget_set_size_request(vl, 110, -1);
        gtk_label_set_xalign(GTK_LABEL(vl), 1);
        w_add(row, vl);
        w_add(c, row);
        if (has_lim) {
            g_autofree char *lm = app_money(lim);
            g_autofree char *t = g_strdup_printf("%s limite mensal de %s", ct->value > lim ? "Acima do" : "Dentro do", lm);
            GtkWidget *ll = w_label(t, ct->value > lim ? "fin-red caption" : "fin-muted caption");
            gtk_widget_set_margin_start(ll, 140);
            w_add(c, ll);
        }
    }
    return c;
}

/* ---------------------------------------------------------------- últimos 6 meses */

typedef struct { Ym ym[6]; Flow f[6]; } SixMonths;

static void draw_bars(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer p) {
    (void)a;
    SixMonths *m = p;
    const FinPalette *pal = theme_palette();
    Cents max = 0;
    for (int i = 0; i < 6; i++) max = MAX(max, MAX(m->f[i].income, m->f[i].expense));
    Cents step = report_nice_step(max, 4);
    Cents top = MAX(step, ((max + step - 1) / step) * step);
    double left = 64, bottom = h - 22, ch = bottom - 8;
    PangoLayout *lay = gtk_widget_create_pango_layout(GTK_WIDGET(a), NULL);
    PangoFontDescription *fd = pango_font_description_from_string("Sans 8");
    pango_layout_set_font_description(lay, fd);
    /* linhas de grade e eixo */
    for (Cents v = 0; v <= top; v += step) {
        double y = bottom - ch * v / top;
        theme_cairo(cr, (pal->muted & 0x00FFFFFF) | 0x33000000);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, left, y + 0.5);
        cairo_line_to(cr, w - 4, y + 0.5);
        cairo_stroke(cr);
        g_autofree char *t = app_hidden() ? g_strdup("") : report_compact(v);
        pango_layout_set_text(lay, t, -1);
        int tw, th;
        pango_layout_get_pixel_size(lay, &tw, &th);
        theme_cairo(cr, pal->muted);
        cairo_move_to(cr, left - 8 - tw, y - th / 2.0);
        pango_cairo_show_layout(cr, lay);
    }
    double slot = (w - 4 - left) / 6;
    double bw = MIN(26, slot * 0.3);
    for (int i = 0; i < 6; i++) {
        double x = left + i * slot + slot / 2;
        double hi = app_hidden() ? 0 : ch * m->f[i].income / top, he = app_hidden() ? 0 : ch * m->f[i].expense / top;
        /* mês sem valor não ganha barra; valor muito pequeno ganha um traço mínimo de 2 px para aparecer */
        if (m->f[i].income > 0) {
            theme_cairo(cr, pal->green);
            cairo_rectangle(cr, x - bw - 2, bottom - MAX(hi, 2), bw, MAX(hi, 2));
            cairo_fill(cr);
        }
        if (m->f[i].expense > 0) {
            theme_cairo(cr, pal->red);
            cairo_rectangle(cr, x + 2, bottom - MAX(he, 2), bw, MAX(he, 2));
            cairo_fill(cr);
        }
        g_autofree char *lab = g_strdup_printf("%s/%02d", BR_MONTHS_SHORT[ym_month(m->ym[i]) - 1], ym_year(m->ym[i]) % 100);
        pango_layout_set_text(lay, lab, -1);
        int tw, th;
        pango_layout_get_pixel_size(lay, &tw, &th);
        theme_cairo(cr, pal->muted);
        cairo_move_to(cr, x - tw / 2.0, bottom + 5);
        pango_cairo_show_layout(cr, lay);
    }
    pango_font_description_free(fd);
    g_object_unref(lay);
}

static GtkWidget *months_card(void) {
    GtkWidget *c = w_card(NULL);
    gtk_widget_set_valign(c, GTK_ALIGN_START);
    w_add(c, w_section_head("Evolução", "Últimos 6 meses"));
    SixMonths *m = g_new0(SixMonths, 1);
    Ym cur = day_ym(APP->today);
    GString *desc = g_string_new("Gráfico de receitas e despesas. ");
    for (int i = 0; i < 6; i++) {
        m->ym[i] = cur - 5 + i;
        m->f[i] = month_flow(APP->state, m->ym[i]);
        g_autofree char *my = br_month_year(m->ym[i]);
        if (app_hidden()) g_string_append_printf(desc, "%s; ", my);
        else {
            g_autofree char *a = money_fmt(m->f[i].income), *b = money_fmt(m->f[i].expense);
            g_string_append_printf(desc, "%s: receitas %s, despesas %s; ", my, a, b);
        }
    }
    gboolean empty = TRUE;
    for (int i = 0; i < 6; i++) if (m->f[i].income || m->f[i].expense) empty = FALSE;
    if (empty) {
        /* sem nenhum mês com valores: texto no lugar do gráfico vazio */
        g_free(m);
        g_string_free(desc, TRUE);
        w_add(c, w_label_wrap("Aparece quando houver pelo menos um mês com valores realizados.", "fin-muted"));
        return c;
    }
    GtkWidget *da = gtk_drawing_area_new();
    gtk_widget_set_size_request(da, -1, 220);
    gtk_widget_set_hexpand(da, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), draw_bars, m, g_free);
    gtk_accessible_update_property(GTK_ACCESSIBLE(da), GTK_ACCESSIBLE_PROPERTY_LABEL, desc->str, -1);
    g_string_free(desc, TRUE);
    w_add(c, da);
    w_add(c, w_label("Verde: receitas · Vermelho: despesas", "fin-muted caption"));
    return c;
}

/* ---------------------------------------------------------------- resumo do período */

/* lançamentos do estado entre [from] e [to] (DAY_NONE = sem limite) */
static GPtrArray *in_range(Day from, Day to) {
    GPtrArray *a = g_ptr_array_new();
    for (guint i = 0; i < APP->state->txs->len; i++) {
        Tx *t = APP->state->txs->pdata[i];
        if ((from == DAY_NONE || t->date >= from) && (to == DAY_NONE || t->date <= to)) g_ptr_array_add(a, t);
    }
    return a;
}

static void open_calendar(gpointer u) {
    (void)u;
    const Filters *f = &APP->filters;
    app_open_calendar(day_ym(f->from != DAY_NONE ? f->from : APP->today));
}

static GtkWidget *sum_box(const char *icon, const char *label, Cents v, const char *cls, const char *change) {
    GtkWidget *b = w_card("fin-tight");
    gtk_widget_set_hexpand(b, TRUE);
    w_add(b, w_icon_label(icon, label, "fin-muted caption"));
    w_add(b, w_money(v, cls));
    if (change) w_add(b, w_label(change, "fin-muted caption"));
    return b;
}

static GtkWidget *summary(void) {
    const Filters *f = &APP->filters;
    g_autoptr(GPtrArray) txs = in_range(f->from, f->to);
    Flow fl = flow_of(txs);
    if (fl.income == 0 && fl.expense == 0) {
        /* nada realizado: em vez de zeros, o que está pendente e o caminho para o calendário */
        Pending p = period_pending(txs);
        Ym ym;
        g_autofree char *title = g_strdup_printf("Nada realizado em %s ainda", period_full_month(f->from, f->to, &ym) ? br_month(ym) : "este período");
        GtkWidget *c = w_card(NULL);
        w_add(c, w_title(title));
        if (p.to_receive > 0 || p.to_pay > 0) {
            w_add(c, w_label_wrap("Os relatórios mostram o que já foi pago ou recebido. Por enquanto, está pendente:", "fin-muted"));
            GtkWidget *row = w_hbox(10);
            gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
            GtkWidget *a = w_vbox(2), *b = w_vbox(2);
            w_add(a, w_label("A receber", "fin-muted caption"));
            w_add(a, w_money(p.to_receive, "fin-money-mid fin-green"));
            w_add(b, w_label("A pagar", "fin-muted caption"));
            w_add(b, w_money(p.to_pay, "fin-money-mid fin-red"));
            w_add(row, a);
            w_add(row, b);
            w_add(c, row);
        } else {
            w_add(c, w_label_wrap("Os relatórios mostram o que já foi pago ou recebido. Troque o período ou marque lançamentos como pagos.", "fin-muted"));
        }
        GtkWidget *link = w_button("Ver no calendário", "calendar-month", "flat fin-link", open_calendar, NULL, NULL);
        gtk_widget_set_halign(link, GTK_ALIGN_START);
        w_add(c, link);
        return c;
    }
    Compare cmp;
    gboolean has_cmp = period_compare(f->from, f->to, APP->today, &cmp);
    Flow prev = {0, 0};
    if (has_cmp) {
        g_autoptr(GPtrArray) pt = in_range(cmp.from, cmp.to);
        prev = flow_of(pt);
    }
    /* com "Ocultar valores", a variação também fica oculta (revelaria a proporção entre os períodos) */
    g_autofree char *ci = app_hidden() ? g_strdup("Variação oculta") : has_cmp ? period_compare_text(fl.income, prev.income, &cmp) : NULL;
    g_autofree char *ce = app_hidden() ? g_strdup("Variação oculta") : has_cmp ? period_compare_text(fl.expense, prev.expense, &cmp) : NULL;
    GtkWidget *row = w_hbox(12);
    gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
    gtk_accessible_update_property(GTK_ACCESSIBLE(row), GTK_ACCESSIBLE_PROPERTY_LABEL, "Resumo do período", -1);
    w_add(row, sum_box("arrow-upward", "Receitas", fl.income, "fin-money-big fin-green", ci));
    w_add(row, sum_box("arrow-downward", "Despesas", fl.expense, "fin-money-big fin-red", ce));
    return row;
}

static void open_sim(gpointer u) { (void)u; simulator_open(); }

/* atalho para o simulador "E se…?" */
static GtkWidget *what_if(void) {
    GtkWidget *btn = w_button("E se…?", NULL, "fin-row fin-whatif", open_sim, NULL, NULL);
    GtkWidget *row = w_hbox(12);
    w_add(row, w_icon("auto-awesome", 24));
    GtkWidget *t = w_vbox(2);
    gtk_widget_set_hexpand(t, TRUE);
    w_add(t, w_label("E se…?", "heading"));
    w_add(t, w_label_wrap("Simule economizar, comprar algo, uma mudança na renda ou antecipar uma dívida, sem mexer nos seus dados.", "fin-muted caption"));
    w_add(row, t);
    w_add(row, w_icon("chevron-right", 20));
    gtk_button_set_child(GTK_BUTTON(btn), row);
    gtk_widget_set_tooltip_text(btn, "Abrir o simulador \"E se…?\"");
    return btn;
}

/* ---------------------------------------------------------------- montagem */

static void export_pdf(gpointer u) { (void)u; report_pdf_dialog(APP->filters.from, APP->filters.to); }

void page_reports_refresh(void) {
    if (!body || !APP->state) return;
    w_clear(body);
    GtkWidget *head = w_hbox(12);
    GtkWidget *t = w_label("Relatórios", "fin-page-title");
    gtk_widget_set_hexpand(t, TRUE);
    gtk_label_set_xalign(GTK_LABEL(t), 0);
    w_add(head, t);
    GtkWidget *pdf = w_button("PDF", "picture-as-pdf", "fin-pill", export_pdf, NULL, NULL);
    gtk_widget_set_tooltip_text(pdf, "Exportar relatório em PDF (Ctrl+P)");
    gtk_accessible_update_property(GTK_ACCESSIBLE(pdf), GTK_ACCESSIBLE_PROPERTY_LABEL, "Exportar relatório em PDF", -1);
    gtk_widget_set_valign(pdf, GTK_ALIGN_CENTER);
    w_add(head, pdf);
    w_add(body, head);
    GtkWidget *bar = period_bar_new();
    GtkWidget *bar_wrap = w_vbox(4);
    w_add(bar_wrap, bar);
    w_add(bar_wrap, w_label_wrap("Só valores realizados (pagos ou recebidos). O período é o mesmo da aba Lançamentos.", "fin-muted caption"));
    if (APP->layout != LAYOUT_NARROW) gtk_widget_set_size_request(bar, 420, -1), gtk_widget_set_halign(bar_wrap, GTK_ALIGN_START);
    w_add(body, bar_wrap);

    GtkWidget *sum = summary(), *sim = what_if(), *cat = categories_card(), *mon = months_card();
    if (APP->layout == LAYOUT_NARROW) {
        w_add(body, sum);
        w_add(body, sim);
        w_add(body, cat);
        w_add(body, mon);
        return;
    }
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g), 18);
    gtk_grid_set_column_homogeneous(GTK_GRID(g), TRUE);
    GtkWidget *left = w_vbox(18), *right = w_vbox(18);
    gtk_widget_set_valign(left, GTK_ALIGN_START);
    gtk_widget_set_valign(right, GTK_ALIGN_START);
    w_add(left, sum);
    w_add(left, sim);
    w_add(left, mon);
    w_add(right, cat);
    gtk_grid_attach(GTK_GRID(g), left, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), right, 1, 0, 1, 1);
    w_add(body, g);
}

GtkWidget *page_reports_new(void) {
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 1500);
    adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp), 1200);
    body = w_vbox(18);
    gtk_widget_set_margin_start(body, 24);
    gtk_widget_set_margin_end(body, 24);
    gtk_widget_set_margin_top(body, 20);
    gtk_widget_set_margin_bottom(body, 32);
    adw_clamp_set_child(ADW_CLAMP(clamp), body);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), clamp);
    return sw;
}
