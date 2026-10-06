/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Relatórios: despesas por categoria (rosca e barras, com limites), evolução dos últimos 6 meses
 * e este mês × mês anterior. Usa o período escolhido em Lançamentos e só valores realizados.
 */
#include "pages.h"
#include "pdf.h"
#include "theme.h"
#include "widgets.h"
#include "core/finance.h"
#include "core/report.h"
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
    if (!cats->len) { w_add(c, w_label_wrap("Sem despesas no período.", "fin-muted")); return c; }

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

/* ---------------------------------------------------------------- este mês × anterior */

static char *chg(Cents a, Cents b) {
    if (b == 0) return g_strdup("Sem base");
    double c = (a - b) * 100.0 / b;
    return g_strdup_printf("%s%.0f%% vs. mês anterior", c >= 0 ? "+" : "−", fabs(c));
}

static GtkWidget *compare_card(void) {
    Ym ym = day_ym(APP->today);
    Flow cur = month_flow(APP->state, ym), prev = month_flow(APP->state, ym - 1);
    GtkWidget *c = w_card(NULL);
    gtk_widget_set_valign(c, GTK_ALIGN_START);
    w_add(c, w_section_head("Comparação", "Este mês × mês anterior"));
    GtkWidget *row = w_hbox(10);
    gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
    const char *labels[2] = {"Receitas", "Despesas"};
    Cents vals[2] = {cur.income, cur.expense}, prevs[2] = {prev.income, prev.expense};
    for (int i = 0; i < 2; i++) {
        GtkWidget *b = w_vbox(4);
        gtk_widget_add_css_class(b, "fin-soft");
        w_add(b, w_label(labels[i], "fin-muted caption"));
        w_add(b, w_money(vals[i], "fin-money-mid"));
        g_autofree char *t = app_hidden() ? g_strdup("••") : chg(vals[i], prevs[i]);
        w_add(b, w_label(t, "fin-muted caption"));
        w_add(row, b);
    }
    w_add(c, row);
    return c;
}

/* ---------------------------------------------------------------- montagem */

static void export_pdf(gpointer u) { (void)u; report_pdf_dialog(APP->filters.from, APP->filters.to); }

void page_reports_refresh(void) {
    if (!body || !APP->state) return;
    w_clear(body);
    const Filters *f = &APP->filters;
    GtkWidget *head = w_hbox(12);
    GtkWidget *t = w_vbox(2);
    gtk_widget_set_hexpand(t, TRUE);
    w_add(t, w_eyebrow("Análise"));
    w_add(t, w_label("Relatórios", "fin-page-title"));
    char a[11], b[11];
    g_autofree char *period = f->from == DAY_NONE && f->to == DAY_NONE
                                  ? g_strdup("Todo o histórico.")
                                  : g_strdup_printf("Período: %s a %s (datas da aba Lançamentos).", f->from == DAY_NONE ? "início" : day_br(f->from, a),
                                                    f->to == DAY_NONE ? "hoje" : day_br(f->to, b));
    g_autofree char *sub = g_strdup_printf("%s Considera só valores realizados.", period);
    w_add(t, w_label_wrap(sub, "fin-muted"));
    w_add(head, t);
    GtkWidget *pdf = w_button("Exportar relatório em PDF", "picture-as-pdf", "suggested-action pill", export_pdf, NULL, NULL);
    gtk_widget_set_tooltip_text(pdf, "Relatório completo do período em PDF (Ctrl+P)");
    gtk_widget_set_valign(pdf, GTK_ALIGN_CENTER);
    w_add(head, pdf);
    if (APP->layout == LAYOUT_NARROW) gtk_orientable_set_orientation(GTK_ORIENTABLE(head), GTK_ORIENTATION_VERTICAL);
    w_add(body, head);

    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g), 18);
    gtk_grid_set_row_spacing(GTK_GRID(g), 18);
    gtk_grid_set_column_homogeneous(GTK_GRID(g), TRUE);
    GtkWidget *cat = categories_card(), *mon = months_card(), *cmp = compare_card();
    if (APP->layout == LAYOUT_NARROW) {
        gtk_grid_attach(GTK_GRID(g), cat, 0, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(g), mon, 0, 1, 1, 1);
        gtk_grid_attach(GTK_GRID(g), cmp, 0, 2, 1, 1);
    } else {
        gtk_grid_attach(GTK_GRID(g), cat, 0, 0, 1, 2);
        gtk_grid_attach(GTK_GRID(g), mon, 1, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(g), cmp, 1, 1, 1, 1);
    }
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
