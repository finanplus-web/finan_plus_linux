/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "charts.h"
#include "app.h"
#include "theme.h"
#include "core/report.h"
#include <math.h>
#include <string.h>

/* texto pequeno dos eixos, na fonte da interface */
static PangoLayout *axis_layout(GtkWidget *w) {
    PangoLayout *lay = gtk_widget_create_pango_layout(w, NULL);
    PangoFontDescription *fd = pango_font_description_copy(pango_context_get_font_description(gtk_widget_get_pango_context(w)));
    pango_font_description_set_size(fd, 8 * PANGO_SCALE);
    pango_layout_set_font_description(lay, fd);
    pango_font_description_free(fd);
    return lay;
}

static void text_at(cairo_t *cr, PangoLayout *lay, const char *t, double x, double y, double align) {
    pango_layout_set_text(lay, t, -1);
    int tw, th;
    pango_layout_get_pixel_size(lay, &tw, &th);
    cairo_move_to(cr, x - tw * align, y - th / 2.0);
    pango_cairo_show_layout(cr, lay);
}

/* largura do maior rótulo do eixo Y (de [bottom] a [top], de [step] em [step]) */
static double axis_width(PangoLayout *lay, Cents bottom, Cents top, Cents step) {
    int mx = 0;
    for (Cents v = bottom; v <= top; v += step) {
        g_autofree char *t = report_compact(v);
        pango_layout_set_text(lay, t, -1);
        int tw, th;
        pango_layout_get_pixel_size(lay, &tw, &th);
        mx = MAX(mx, tw);
    }
    return mx + 14;
}

static void grid_line(cairo_t *cr, const FinPalette *pal, double x0, double x1, double y) {
    theme_cairo(cr, (pal->muted & 0x00FFFFFF) | 0x2E000000);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, x0, floor(y) + 0.5);
    cairo_line_to(cr, x1, floor(y) + 0.5);
    cairo_stroke(cr);
}

/* ---------------------------------------------------------------- linha */

typedef struct {
    int n, solid;
    Cents *v;
    char **labels;
} LineData;

static void line_free(gpointer p) {
    LineData *d = p;
    g_free(d->v);
    if (d->labels) for (int i = 0; i < d->n; i++) g_free(d->labels[i]);
    g_free(d->labels);
    g_free(d);
}

static void draw_line(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer p) {
    LineData *d = p;
    const FinPalette *pal = theme_palette();
    gboolean hidden = app_hidden();
    PangoLayout *lay = axis_layout(GTK_WIDGET(a));
    Cents lo = d->v[0], hi = d->v[0];
    for (int i = 1; i < d->n; i++) { lo = MIN(lo, d->v[i]); hi = MAX(hi, d->v[i]); }
    /* começa do zero quando o saldo é sempre positivo e a variação é grande; senão foca na variação */
    if (lo > 0 && lo < hi / 2) lo = 0;
    Cents step = report_nice_step(MAX(hi - lo, 100), 3);
    Cents bottom = (Cents)floor((double)lo / step) * step, top = (Cents)ceil((double)hi / step) * step;
    if (top <= bottom) top = bottom + step;
    double left = hidden ? 8 : axis_width(lay, bottom, top, step), right = w - 8, ytop = 22, ybot = h - 22;
    double ch = ybot - ytop;
#define Y(v) (ybot - ch * (double)((v) - bottom) / (double)(top - bottom))
#define X(i) (d->n > 1 ? left + (right - left) * (i) / (double)(d->n - 1) : (left + right) / 2)
    for (Cents v = bottom; v <= top; v += step) {
        grid_line(cr, pal, left, right, Y(v));
        if (!hidden) {
            g_autofree char *t = report_compact(v);
            theme_cairo(cr, pal->muted);
            text_at(cr, lay, t, left - 8, Y(v), 1);
        }
    }
    for (int i = 0; i < d->n; i++) {
        if (!d->labels || !d->labels[i]) continue;
        theme_cairo(cr, pal->muted);
        text_at(cr, lay, d->labels[i], X(i), ybot + 12, i == 0 ? 0 : i == d->n - 1 ? 1 : 0.5);
    }
    if (hidden) {
        /* "Ocultar valores": só a forma geral não aparece; fica uma linha neutra */
        theme_cairo(cr, (pal->muted & 0x00FFFFFF) | 0x66000000);
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, left, (ytop + ybot) / 2);
        cairo_line_to(cr, right, (ytop + ybot) / 2);
        cairo_stroke(cr);
        g_object_unref(lay);
        return;
    }
    int solid = CLAMP(d->solid, 0, d->n - 1);
    /* área sob a parte realizada */
    cairo_pattern_t *grad = cairo_pattern_create_linear(0, ytop, 0, ybot);
    guint32 ac = pal->accent;
    cairo_pattern_add_color_stop_rgba(grad, 0, ((ac >> 16) & 255) / 255.0, ((ac >> 8) & 255) / 255.0, (ac & 255) / 255.0, 0.22);
    cairo_pattern_add_color_stop_rgba(grad, 1, ((ac >> 16) & 255) / 255.0, ((ac >> 8) & 255) / 255.0, (ac & 255) / 255.0, 0.02);
    cairo_move_to(cr, X(0), ybot);
    for (int i = 0; i <= solid; i++) cairo_line_to(cr, X(i), Y(d->v[i]));
    cairo_line_to(cr, X(solid), ybot);
    cairo_close_path(cr);
    cairo_set_source(cr, grad);
    cairo_fill(cr);
    cairo_pattern_destroy(grad);
    /* linha realizada */
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    theme_cairo(cr, pal->accent);
    cairo_set_line_width(cr, 2.5);
    for (int i = 0; i <= solid; i++) (i ? cairo_line_to : cairo_move_to)(cr, X(i), Y(d->v[i]));
    cairo_stroke(cr);
    /* previsão (tracejada) */
    if (solid < d->n - 1) {
        double dash[] = {5, 5};
        cairo_set_dash(cr, dash, 2, 0);
        theme_cairo(cr, (pal->accent & 0x00FFFFFF) | 0x99000000);
        cairo_set_line_width(cr, 2);
        for (int i = solid; i < d->n; i++) (i > solid ? cairo_line_to : cairo_move_to)(cr, X(i), Y(d->v[i]));
        cairo_stroke(cr);
        cairo_set_dash(cr, NULL, 0, 0);
    }
    /* ponto de hoje com o valor */
    double px = X(solid), py = Y(d->v[solid]);
    theme_cairo(cr, pal->accent);
    cairo_arc(cr, px, py, 4.5, 0, 2 * G_PI);
    cairo_fill(cr);
    g_autofree char *now = report_compact(d->v[solid]);
    pango_layout_set_text(lay, now, -1);
    int tw, th;
    pango_layout_get_pixel_size(lay, &tw, &th);
    /* rótulo à esquerda do ponto, para não cruzar a linha da previsão */
    double tx = CLAMP(px - tw - 8, left + 2, right - tw), ty = py - th - 8 < 0 ? py + 8 : py - th - 6;
    theme_cairo(cr, pal->accent);
    cairo_move_to(cr, tx, ty);
    pango_cairo_show_layout(cr, lay);
#undef X
#undef Y
    g_object_unref(lay);
}

GtkWidget *chart_line_new(const Cents *values, int n, int solid, const char *const *labels, int height, const char *a11y) {
    LineData *d = g_new0(LineData, 1);
    d->n = MAX(n, 1);
    d->solid = solid;
    d->v = g_new0(Cents, d->n);
    if (n > 0) memcpy(d->v, values, sizeof(Cents) * n);
    if (labels) {
        d->labels = g_new0(char *, d->n);
        for (int i = 0; i < n; i++) d->labels[i] = g_strdup(labels[i]);
    }
    GtkWidget *da = gtk_drawing_area_new();
    gtk_widget_set_size_request(da, -1, height);
    gtk_widget_set_hexpand(da, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), draw_line, d, line_free);
    gtk_accessible_update_property(GTK_ACCESSIBLE(da), GTK_ACCESSIBLE_PROPERTY_LABEL, a11y ? a11y : "Gráfico de linha", -1);
    return da;
}

/* ---------------------------------------------------------------- barras por mês */

typedef struct { int n; Ym ym[12]; Flow f[12]; } Bars;

static void draw_bars(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer p) {
    Bars *m = p;
    const FinPalette *pal = theme_palette();
    gboolean hidden = app_hidden();
    Cents max = 0;
    for (int i = 0; i < m->n; i++) max = MAX(max, MAX(m->f[i].income, m->f[i].expense));
    Cents step = report_nice_step(MAX(max, 100), 3);
    Cents top = MAX(step, ((max + step - 1) / step) * step);
    PangoLayout *lay = axis_layout(GTK_WIDGET(a));
    double left = hidden ? 8 : axis_width(lay, 0, top, step), bottom = h - 22, ch = bottom - 10;
    for (Cents v = 0; v <= top; v += step) {
        double y = bottom - ch * v / top;
        grid_line(cr, pal, left, w - 4, y);
        if (!hidden) {
            g_autofree char *t = report_compact(v);
            theme_cairo(cr, pal->muted);
            text_at(cr, lay, t, left - 8, y, 1);
        }
    }
    double slot = (w - 4 - left) / m->n;
    double bw = MIN(24, slot * 0.28);
    for (int i = 0; i < m->n; i++) {
        double x = left + i * slot + slot / 2;
        double hi = hidden ? 0 : ch * m->f[i].income / top, he = hidden ? 0 : ch * m->f[i].expense / top;
        /* mês sem valor não ganha barra; valor muito pequeno ganha um traço mínimo para aparecer */
        if (m->f[i].income > 0 || hidden) {
            theme_cairo(cr, hidden ? pal->track : pal->green);
            double hh = hidden ? ch * 0.5 : MAX(hi, 3);
            cairo_new_sub_path(cr);
            cairo_arc(cr, x - bw - 2 + 4, bottom - hh + 4, 4, G_PI, 1.5 * G_PI);
            cairo_arc(cr, x - 2 - 4, bottom - hh + 4, 4, 1.5 * G_PI, 2 * G_PI);
            cairo_line_to(cr, x - 2, bottom);
            cairo_line_to(cr, x - bw - 2, bottom);
            cairo_close_path(cr);
            cairo_fill(cr);
        }
        if (m->f[i].expense > 0 || hidden) {
            theme_cairo(cr, hidden ? pal->track : pal->red);
            double hh = hidden ? ch * 0.35 : MAX(he, 3);
            cairo_new_sub_path(cr);
            cairo_arc(cr, x + 2 + 4, bottom - hh + 4, 4, G_PI, 1.5 * G_PI);
            cairo_arc(cr, x + 2 + bw - 4, bottom - hh + 4, 4, 1.5 * G_PI, 2 * G_PI);
            cairo_line_to(cr, x + 2 + bw, bottom);
            cairo_line_to(cr, x + 2, bottom);
            cairo_close_path(cr);
            cairo_fill(cr);
        }
        g_autofree char *lab = g_strdup_printf("%s/%02d", BR_MONTHS_SHORT[ym_month(m->ym[i]) - 1], ym_year(m->ym[i]) % 100);
        theme_cairo(cr, pal->muted);
        text_at(cr, lay, lab, x, bottom + 12, 0.5);
    }
    g_object_unref(lay);
}

GtkWidget *chart_month_bars_new(const Ym *yms, const Flow *flows, int n, int height, const char *a11y) {
    Bars *m = g_new0(Bars, 1);
    m->n = CLAMP(n, 1, 12);
    for (int i = 0; i < m->n; i++) { m->ym[i] = yms[i]; m->f[i] = flows[i]; }
    GtkWidget *da = gtk_drawing_area_new();
    gtk_widget_set_size_request(da, -1, height);
    gtk_widget_set_hexpand(da, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), draw_bars, m, g_free);
    gtk_accessible_update_property(GTK_ACCESSIBLE(da), GTK_ACCESSIBLE_PROPERTY_LABEL, a11y ? a11y : "Gráfico de barras", -1);
    return da;
}

/* ---------------------------------------------------------------- miniatura do tema */

static void rounded(cairo_t *cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 1.5 * G_PI);
    cairo_close_path(cr);
}

static void paint_window(cairo_t *cr, const FinPalette *p, double x, double y, double w, double h) {
    cairo_save(cr);
    rounded(cr, x, y, w, h, 10);
    cairo_clip(cr);
    theme_cairo(cr, p->bg);
    cairo_paint(cr);
    if (!p->dark) {
        /* os brilhos suaves do fundo do tema claro */
        cairo_pattern_t *g = cairo_pattern_create_linear(x, y, x + w, y + h);
        cairo_pattern_add_color_stop_rgba(g, 0, ((p->glow_a >> 16) & 255) / 255.0, ((p->glow_a >> 8) & 255) / 255.0, (p->glow_a & 255) / 255.0, 1);
        cairo_pattern_add_color_stop_rgba(g, 1, ((p->glow_b >> 16) & 255) / 255.0, ((p->glow_b >> 8) & 255) / 255.0, (p->glow_b & 255) / 255.0, 1);
        cairo_set_source(cr, g);
        cairo_paint_with_alpha(cr, 0.8);
        cairo_pattern_destroy(g);
    }
    /* barra lateral com o item escolhido */
    theme_cairo(cr, p->dark ? 0x14FFFFFF : 0x80FFFFFF);
    cairo_rectangle(cr, x, y, w * 0.24, h);
    cairo_fill(cr);
    theme_cairo(cr, (p->accent & 0x00FFFFFF) | 0x40000000);
    rounded(cr, x + 4, y + 8, w * 0.24 - 8, 7, 3.5);
    cairo_fill(cr);
    for (int i = 0; i < 3; i++) {
        theme_cairo(cr, (p->muted & 0x00FFFFFF) | 0x55000000);
        rounded(cr, x + 6, y + 20 + i * 9, w * 0.24 - 16, 4, 2);
        cairo_fill(cr);
    }
    /* dois cartões e uma barra de destaque */
    double cx = x + w * 0.24 + 6, cw = w - (cx - x) - 6;
    guint32 card = p->dark ? (p->surface | 0xFF000000) : 0xFFFFFFFF;
    theme_cairo(cr, card);
    rounded(cr, cx, y + 8, cw, h * 0.42, 6);
    cairo_fill(cr);
    rounded(cr, cx, y + 12 + h * 0.42, cw * 0.55, h - h * 0.42 - 20, 6);
    cairo_fill(cr);
    rounded(cr, cx + cw * 0.55 + 4, y + 12 + h * 0.42, cw * 0.45 - 4, h - h * 0.42 - 20, 6);
    cairo_fill(cr);
    theme_cairo(cr, p->accent);
    rounded(cr, cx + 6, y + 16, cw * 0.45, 6, 3);
    cairo_fill(cr);
    theme_cairo(cr, p->green);
    rounded(cr, cx + 6, y + 28, cw * 0.3, 4, 2);
    cairo_fill(cr);
    theme_cairo(cr, p->red);
    rounded(cr, cx + 6 + cw * 0.34, y + 28, cw * 0.2, 4, 2);
    cairo_fill(cr);
    cairo_restore(cr);
}

static void draw_preview(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer p) {
    (void)a;
    ThemeId t = GPOINTER_TO_INT(p);
    if (t == THEME_AUTO) {
        /* "Sistema": metade clara, metade escura */
        cairo_save(cr);
        cairo_rectangle(cr, 0, 0, w / 2.0, h);
        cairo_clip(cr);
        paint_window(cr, theme_palette_for(THEME_LIGHT, FALSE), 0, 0, w, h);
        cairo_restore(cr);
        cairo_save(cr);
        cairo_rectangle(cr, w / 2.0, 0, w / 2.0, h);
        cairo_clip(cr);
        paint_window(cr, theme_palette_for(THEME_OLED, TRUE), 0, 0, w, h);
        cairo_restore(cr);
        return;
    }
    paint_window(cr, theme_palette_for(t, FALSE), 0, 0, w, h);
}

GtkWidget *theme_preview_new(ThemeId t) {
    GtkWidget *da = gtk_drawing_area_new();
    gtk_widget_set_size_request(da, 120, 72);
    gtk_widget_set_hexpand(da, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), draw_preview, GINT_TO_POINTER(t), NULL);
    gtk_accessible_update_state(GTK_ACCESSIBLE(da), GTK_ACCESSIBLE_STATE_HIDDEN, TRUE, -1);
    return da;
}
