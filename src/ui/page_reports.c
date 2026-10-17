/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Resumo (antes "Relatórios"): o mesmo período da aba Lançamentos (‹ mês › e "Período e filtros"), só valores realizados.
 * Resumo com comparação justa (mês atual contra os mesmos dias do mês anterior), atalho para o
 * simulador "E se…?", despesas por categoria (rosca e barras, com limites) e evolução dos últimos 6 meses.
 * Sem nada realizado no período, mostra o que está pendente e leva ao calendário.
 * Em janelas largas (1.4.0): quatro indicadores no alto (receitas, despesas, resultado e gasto médio
 * por dia), categorias e fluxo do dinheiro lado a lado e, embaixo, a evolução e o saldo acumulado.
 */
#include "pages.h"
#include "charts.h"
#include "pdf.h"
#include "theme.h"
#include "widgets.h"
#include "core/finance.h"
#include "core/report.h"
#include "core/period.h"
#include <math.h>
#include <string.h>

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
    /* janelas largas: rosca e legenda à esquerda, barras à direita */
    gboolean side = APP->layout >= LAYOUT_WIDE;
    GtkWidget *bars = w_vbox(2);
    if (side) {
        gtk_widget_set_hexpand(legend, FALSE);
        gtk_widget_set_size_request(legend, 220, -1);
        GtkWidget *split = w_hbox(32);
        gtk_widget_set_hexpand(top, FALSE);
        gtk_widget_set_valign(top, GTK_ALIGN_START);
        w_add(split, top);
        gtk_widget_set_hexpand(bars, TRUE);
        w_add(split, bars);
        w_add(c, split);
    } else {
        w_add(c, top);
        w_add(c, bars);
    }

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
        w_add(bars, row);
        if (has_lim) {
            g_autofree char *lm = app_money(lim);
            g_autofree char *t = g_strdup_printf("%s limite mensal de %s", ct->value > lim ? "Acima do" : "Dentro do", lm);
            GtkWidget *ll = w_label(t, ct->value > lim ? "fin-red caption" : "fin-muted caption");
            gtk_widget_set_margin_start(ll, 140);
            w_add(bars, ll);
        }
    }
    return c;
}

/* ---------------------------------------------------------------- últimos 6 meses */

static GtkWidget *months_card(void) {
    GtkWidget *c = w_card(NULL);
    gtk_widget_set_valign(c, GTK_ALIGN_START);
    w_add(c, w_section_head("Evolução", "Últimos 6 meses"));
    Ym yms[6];
    Flow fl[6];
    Ym cur = day_ym(APP->today);
    GString *desc = g_string_new("Gráfico de receitas e despesas. ");
    gboolean empty = TRUE;
    for (int i = 0; i < 6; i++) {
        yms[i] = cur - 5 + i;
        fl[i] = month_flow(APP->state, yms[i]);
        if (fl[i].income || fl[i].expense) empty = FALSE;
        g_autofree char *my = br_month_year(yms[i]);
        if (app_hidden()) g_string_append_printf(desc, "%s; ", my);
        else {
            g_autofree char *a = money_fmt(fl[i].income), *b = money_fmt(fl[i].expense);
            g_string_append_printf(desc, "%s: receitas %s, despesas %s; ", my, a, b);
        }
    }
    if (empty) {
        /* sem nenhum mês com valores: texto no lugar do gráfico vazio */
        g_string_free(desc, TRUE);
        w_add(c, w_label_wrap("Aparece quando houver pelo menos um mês com valores realizados.", "fin-muted"));
        return c;
    }
    w_add(c, chart_month_bars_new(yms, fl, 6, 220, desc->str));
    g_string_free(desc, TRUE);
    w_add(c, w_label("Verde: receitas · Vermelho: despesas", "fin-muted caption"));
    return c;
}

/* saldo das contas no fim de cada um dos últimos 6 meses (o mês atual, até hoje) */
static GtkWidget *wealth_card(void) {
    GtkWidget *c = w_card(NULL);
    gtk_widget_set_valign(c, GTK_ALIGN_START);
    w_add(c, w_section_head("Saldo", "Saldo acumulado · 6 meses"));
    Cents v[6];
    char lab[6][12];
    const char *labels[6];
    Ym cur = day_ym(APP->today);
    GString *desc = g_string_new("Saldo das contas no fim de cada mês. ");
    for (int i = 0; i < 6; i++) {
        Ym m = cur - 5 + i;
        v[i] = balance_on(APP->state, i == 5 ? APP->today : ym_last(m));
        g_snprintf(lab[i], sizeof lab[i], "%s/%02d", BR_MONTHS_SHORT[ym_month(m) - 1], ym_year(m) % 100);
        labels[i] = lab[i];
        if (!app_hidden()) {
            g_autofree char *mv = money_fmt(v[i]);
            g_string_append_printf(desc, "%s: %s; ", lab[i], mv);
        }
    }
    gboolean flat = TRUE;
    for (int i = 0; i < 6; i++) if (v[i] != 0) flat = FALSE;
    if (flat) {
        g_string_free(desc, TRUE);
        w_add(c, w_label_wrap("Aparece quando houver saldo nas contas ou lançamentos pagos.", "fin-muted"));
        return c;
    }
    w_add(c, chart_line_new(v, 6, 5, labels, 220, desc->str));
    g_string_free(desc, TRUE);
    w_add(c, w_label("Só o que já foi pago ou recebido. O último ponto é o saldo de hoje.", "fin-muted caption"));
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

/* ---------------------------------------------------------------- fluxo do dinheiro */

typedef struct { int n; Cents scale; Cents v[64]; guint32 color[64]; } FlowBar;

static void draw_flow(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer p) {
    (void)a;
    FlowBar *fb = p;
    const FinPalette *pal = theme_palette();
    double r = h / 2.0, x = 0;
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_arc(cr, r, r, r, G_PI / 2, 3 * G_PI / 2);
    cairo_arc(cr, w - r, r, r, -G_PI / 2, G_PI / 2);
    cairo_close_path(cr);
    cairo_clip(cr);
    theme_cairo(cr, pal->track);
    cairo_paint(cr);
    for (int i = 0; i < fb->n; i++) {
        double ww = (double)w * fb->v[i] / (fb->scale > 0 ? fb->scale : 1);
        theme_cairo(cr, fb->color[i]);
        cairo_rectangle(cr, x, 0, ww, h);
        cairo_fill(cr);
        x += ww;
    }
    cairo_restore(cr);
}

static GtkWidget *flow_bar(FlowBar *fb) {
    GtkWidget *da = gtk_drawing_area_new();
    gtk_widget_set_size_request(da, -1, 14);
    gtk_widget_set_hexpand(da, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), draw_flow, fb, g_free);
    if (app_hidden()) gtk_widget_set_opacity(da, 0.25);
    gtk_accessible_update_property(GTK_ACCESSIBLE(da), GTK_ACCESSIBLE_PROPERTY_LABEL, "Gráfico de barras proporcionais do fluxo de dinheiro", -1);
    return da;
}

static void dot_draw(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer p) {
    (void)a;
    theme_cairo(cr, GPOINTER_TO_UINT(p));
    cairo_arc(cr, w / 2.0, h / 2.0, MIN(w, h) / 2.0, 0, 2 * G_PI);
    cairo_fill(cr);
}

static void flow_legend(GtkWidget *c, FlowBar *fb, char **names, Cents total) {
    for (int i = 0; i < fb->n; i++) {
        GtkWidget *row = w_hbox(8);
        GtkWidget *dot = gtk_drawing_area_new();
        gtk_widget_set_size_request(dot, 12, 12);
        gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(dot), dot_draw, GUINT_TO_POINTER(fb->color[i]), NULL);
        w_add(row, dot);
        GtkWidget *n = w_label(names[i], NULL);
        gtk_label_set_ellipsize(GTK_LABEL(n), PANGO_ELLIPSIZE_END);
        gtk_widget_set_hexpand(n, TRUE);
        gtk_label_set_xalign(GTK_LABEL(n), 0);
        w_add(row, n);
        w_add(row, w_money(fb->v[i], "heading"));
        g_autofree char *pct = app_hidden() || total <= 0 ? g_strdup("••%") : g_strdup_printf("%d%%", (int)MIN(100, fb->v[i] * 100 / total));
        GtkWidget *pl = w_label(pct, "fin-muted caption");
        gtk_widget_set_size_request(pl, 40, -1);
        gtk_label_set_xalign(GTK_LABEL(pl), 1);
        w_add(row, pl);
        w_add(c, row);
    }
}

/* De onde veio (receitas realizadas por categoria) e para onde foi (despesas por categoria e a sobra).
 * Não liga uma receita a uma despesa específica (igual ao Android 1.6.0). */
static GtkWidget *flow_card(Flow fl) {
    const Filters *f = &APP->filters;
    static const guint32 COLORS[6] = {0xFF00AFC1, 0, 0xFF278AE5, 1, 0xFF8C9499, 2}; /* 0/1/2 = vermelho, verde, destaque do tema */
    const FinPalette *pal = theme_palette();
#define FLOW_COLOR(i) (COLORS[(i) % 6] > 2 ? COLORS[(i) % 6] : COLORS[(i) % 6] == 0 ? pal->red : COLORS[(i) % 6] == 1 ? pal->green : pal->accent)
    GtkWidget *c = w_card(NULL);
    gtk_widget_set_valign(c, GTK_ALIGN_START);
    w_add(c, w_section_head("Análises", "Fluxo do dinheiro"));
    w_add(c, w_label_wrap("De onde veio (em cima) e para onde foi (embaixo).", "fin-muted caption"));
    Cents scale = MAX(MAX(fl.income, fl.expense), 1);

    /* receitas realizadas (sem pagamento de fatura) por categoria, maiores primeiro */
    g_autoptr(GPtrArray) txs = in_range(f->from, f->to);
    g_autoptr(GHashTable) by = g_hash_table_new(g_str_hash, g_str_equal);
    g_autoptr(GPtrArray) order = g_ptr_array_new();
    for (guint i = 0; i < txs->len; i++) {
        Tx *t = txs->pdata[i];
        if (t->kind != KIND_INCOME || !t->paid || !tx_is_flow(t)) continue;
        const char *k = t->category[0] ? t->category : "Sem categoria";
        gpointer cur = g_hash_table_lookup(by, k);
        if (!cur && !g_hash_table_contains(by, k)) g_ptr_array_add(order, (gpointer)k);
        Cents *slot = cur;
        if (!slot) { slot = g_new0(Cents, 1); g_hash_table_insert(by, (gpointer)k, slot); }
        *slot += t->value;
    }
    FlowBar *in = g_new0(FlowBar, 1);
    char *in_names[64];
    /* ordena por valor decrescente */
    for (guint i = 0; i < order->len && in->n < 64; i++) {
        const char *best = NULL; Cents bv = -1; guint bi = 0;
        for (guint j = 0; j < order->len; j++) {
            const char *k = order->pdata[j];
            if (!k) continue;
            Cents v = *(Cents *)g_hash_table_lookup(by, k);
            if (v > bv) { bv = v; best = k; bi = j; }
        }
        if (!best) break;
        order->pdata[bi] = NULL;
        in_names[in->n] = (char *)best;
        in->v[in->n] = bv;
        in->color[in->n] = FLOW_COLOR(in->n);
        in->n++;
    }
    in->scale = scale;
    w_add(c, w_label("De onde veio", "heading"));
    w_add(c, flow_bar(in));
    flow_legend(c, in, in_names, fl.income);
    GHashTableIter it; gpointer kk, vv;
    g_hash_table_iter_init(&it, by);
    while (g_hash_table_iter_next(&it, &kk, &vv)) g_free(vv);

    g_autofree char *inc_txt = money_fmt(fl.income);
    g_autofree char *mid = app_hidden() ? g_strdup("Entradas ocultas") : g_strdup_printf("Entrou %s", inc_txt);
    GtkWidget *pill = w_label(mid, "fin-flow-pill");
    gtk_widget_set_halign(pill, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(pill, 8);
    gtk_widget_set_margin_bottom(pill, 8);
    w_add(c, pill);

    w_add(c, w_label("Para onde foi", "heading"));
    g_autoptr(GPtrArray) cats = category_totals(APP->state, f->from, f->to);
    FlowBar *out = g_new0(FlowBar, 1);
    char *out_names[64];
    for (guint i = 0; i < cats->len && out->n < 63; i++) {
        CatTotal *ct = cats->pdata[i];
        out_names[out->n] = ct->name;
        out->v[out->n] = ct->value;
        out->color[out->n] = FLOW_COLOR(out->n + 1);
        out->n++;
    }
    Cents left = MAX(0, fl.income - fl.expense);
    if (left > 0) { out_names[out->n] = "Sobrou"; out->v[out->n] = left; out->color[out->n] = pal->green; out->n++; }
    out->scale = scale;
    if (!out->n) {
        g_free(out);
        w_add(c, w_label_wrap("Nenhuma despesa realizada; todo o valor recebido permaneceu disponível.", "fin-muted caption"));
    } else {
        int n = out->n;
        Cents vals[64]; guint32 cols[64];
        memcpy(vals, out->v, sizeof(Cents) * n); memcpy(cols, out->color, sizeof(guint32) * n);
        w_add(c, flow_bar(out)); /* out passa a ser da área de desenho */
        FlowBar tmp = {.n = n, .scale = scale};
        memcpy(tmp.v, vals, sizeof(Cents) * n); memcpy(tmp.color, cols, sizeof(guint32) * n);
        flow_legend(c, &tmp, out_names, MAX(fl.income, fl.expense));
    }
    if (fl.expense > fl.income) w_add(c, w_label_wrap("As despesas passaram o valor recebido neste período.", "fin-red caption"));
    w_add(c, w_label_wrap("Considera apenas lançamentos pagos ou recebidos; não associa uma entrada a uma despesa específica.", "fin-muted caption"));
#undef FLOW_COLOR
    return c;
}

/* atalho para o simulador */
static GtkWidget *what_if(void) {
    GtkWidget *btn = w_button("Simular meu dinheiro", NULL, "fin-row fin-whatif", open_sim, NULL, NULL);
    GtkWidget *row = w_hbox(12);
    w_add(row, w_icon("auto-awesome", 24));
    GtkWidget *t = w_vbox(2);
    gtk_widget_set_hexpand(t, TRUE);
    w_add(t, w_label("Simular meu dinheiro", "heading"));
    w_add(t, w_label_wrap("Simule economizar, comprar algo, uma mudança na renda ou antecipar uma dívida, sem mexer nos seus dados.", "fin-muted caption"));
    w_add(row, t);
    w_add(row, w_icon("chevron-right", 20));
    gtk_button_set_child(GTK_BUTTON(btn), row);
    gtk_widget_set_tooltip_text(btn, "Abrir o simulador (nada é gravado)");
    return btn;
}

/* ---------------------------------------------------------------- montagem */

static void export_pdf(gpointer u) { (void)u; report_pdf_dialog(APP->filters.from, APP->filters.to); }

/* gasto médio por dia no período, até hoje (FALSE se o período ainda não começou) */
static gboolean daily_average(Flow fl, Cents *avg, int *days) {
    const Filters *f = &APP->filters;
    Day from = f->from, to = f->to == DAY_NONE || f->to > APP->today ? APP->today : f->to;
    if (from == DAY_NONE || from > to) return FALSE;
    *days = (int)(to - from) + 1;
    *avg = fl.expense / *days;
    return TRUE;
}

/* indicadores do alto: receitas e despesas (com a comparação), resultado e gasto médio por dia */
static GtkWidget *kpis(Flow fl, int cols) {
    const Filters *f = &APP->filters;
    Compare cmp;
    gboolean has_cmp = period_compare(f->from, f->to, APP->today, &cmp);
    Flow prev = {0, 0};
    if (has_cmp) {
        g_autoptr(GPtrArray) pt = in_range(cmp.from, cmp.to);
        prev = flow_of(pt);
    }
    g_autofree char *ci = app_hidden() ? g_strdup("Variação oculta") : has_cmp ? period_compare_text(fl.income, prev.income, &cmp) : NULL;
    g_autofree char *ce = app_hidden() ? g_strdup("Variação oculta") : has_cmp ? period_compare_text(fl.expense, prev.expense, &cmp) : NULL;
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g), 18);
    gtk_grid_set_row_spacing(GTK_GRID(g), 18);
    gtk_grid_set_column_homogeneous(GTK_GRID(g), TRUE);
    gtk_accessible_update_property(GTK_ACCESSIBLE(g), GTK_ACCESSIBLE_PROPERTY_LABEL, "Resumo do período", -1);
    GtkWidget *items[4];
    items[0] = sum_box("arrow-upward", "Receitas", fl.income, "fin-money-big fin-green", ci);
    items[1] = sum_box("arrow-downward", "Despesas", fl.expense, "fin-money-big fin-red", ce);
    Cents res = flow_balance(fl);
    g_autofree char *rt = app_hidden() ? NULL : result_text(fl);
    items[2] = sum_box("account-balance-wallet", "Resultado do período", res, res < 0 ? "fin-money-big fin-red" : "fin-money-big fin-accent", rt);
    Cents avg = 0;
    int days = 0;
    gboolean has_avg = daily_average(fl, &avg, &days);
    g_autofree char *dt = has_avg ? g_strdup_printf(days == 1 ? "%d dia até agora" : "%d dias até agora", days) : g_strdup("O período ainda não começou");
    items[3] = sum_box("calendar-month", "Gasto médio por dia", avg, "fin-money-big", dt);
    for (int i = 0; i < 4; i++) {
        gtk_widget_set_valign(items[i], GTK_ALIGN_FILL);
        gtk_grid_attach(GTK_GRID(g), items[i], i % cols, i / cols, 1, 1);
    }
    return g;
}

void page_reports_refresh(void) {
    if (!body || !APP->state) return;
    w_clear(body);
    GtkWidget *head = w_hbox(12);
    GtkWidget *t = w_label("Resumo", "fin-page-title");
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

    g_autoptr(GPtrArray) rt = in_range(APP->filters.from, APP->filters.to);
    Flow fl = flow_of(rt);
    gboolean any = fl.income > 0 || fl.expense > 0;
    Layout l = APP->layout;
    GtkWidget *top = any ? kpis(fl, l == LAYOUT_NARROW ? 1 : l == LAYOUT_MEDIUM ? 2 : 4) : summary();
    GtkWidget *flw = any ? flow_card(fl) : NULL;
    GtkWidget *sim = what_if(), *cat = categories_card(), *mon = months_card(), *wl = wealth_card();
    w_add(body, top);
    if (l == LAYOUT_NARROW) {
        w_add(body, sim);
        if (flw) w_add(body, flw);
        w_add(body, cat);
        w_add(body, mon);
        w_add(body, wl);
        return;
    }
    /* à esquerda (mais larga): categorias e a evolução; à direita: fluxo do dinheiro, simulador e saldo acumulado */
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g), 18);
    gtk_grid_set_column_homogeneous(GTK_GRID(g), TRUE);
    int cols = l == LAYOUT_XWIDE ? 3 : 2;
    GtkWidget *main_col = w_vbox(18), *side = w_vbox(18);
    gtk_widget_set_valign(main_col, GTK_ALIGN_START);
    gtk_widget_set_valign(side, GTK_ALIGN_START);
    w_add(main_col, cat);
    w_add(main_col, mon);
    if (flw) w_add(side, flw);
    w_add(side, sim);
    w_add(side, wl);
    gtk_grid_attach(GTK_GRID(g), main_col, 0, 0, cols - 1, 1);
    gtk_grid_attach(GTK_GRID(g), side, cols - 1, 0, 1, 1);
    w_add(body, g);
}

GtkWidget *page_reports_new(void) {
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 2400);
    adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp), 2000);
    body = w_vbox(18);
    gtk_widget_set_margin_start(body, 24);
    gtk_widget_set_margin_end(body, 24);
    gtk_widget_set_margin_top(body, 20);
    gtk_widget_set_margin_bottom(body, 32);
    adw_clamp_set_child(ADW_CLAMP(clamp), body);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), clamp);
    return sw;
}
