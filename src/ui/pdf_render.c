/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Relatório financeiro em PDF (A4 retrato), desenhado com Cairo e Pango: o mesmo layout do app
 * Android. Os números vêm de core/report.c (testado). Não depende da janela: os testes também geram PDFs.
 *
 * O layout roda duas vezes: a primeira só conta as páginas (para o rodapé "Página n de N"),
 * a segunda desenha.
 */
#include "pdf.h"
#include "theme.h"
#include "core/report.h"
#include <cairo-pdf.h>
#include <math.h>
#include <string.h>

#define W 595.0
#define H 842.0
#define M 36.0
#define CW (W - 2 * M)
#define FOOTER 28.0

/* cores fixas (sempre claro, pensado para tela e impressão) */
#define INK 0xFF182238
#define MUTED 0xFF5B6579
#define ACCENT 0xFF3A5FC8
#define GREEN 0xFF1B7351
#define RED 0xFFB03A4F
#define CARD 0xFFF3F6FD
#define LINE 0xFFDDE3EE
#define TRACK 0xFFE6EBF5
#define WHITE 0xFFFFFFFF

typedef enum { AL_LEFT, AL_RIGHT, AL_CENTER } Align;

typedef struct {
    cairo_t *cr;
    gboolean draw; /* FALSE na passada que só conta páginas */
    int pages, total;
    double y;
    const Report *r;
    PangoLayout *lay;
} Pen;

static void text(Pen *p, const char *t, double x, double base, double size, guint32 color, gboolean bold, Align al, double max_w) {
    PangoFontDescription *fd = pango_font_description_from_string(bold ? "Sans Bold" : "Sans");
    pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
    pango_layout_set_font_description(p->lay, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(p->lay, t, -1);
    pango_layout_set_width(p->lay, max_w > 0 ? (int)(max_w * PANGO_SCALE) : -1);
    pango_layout_set_ellipsize(p->lay, max_w > 0 ? PANGO_ELLIPSIZE_END : PANGO_ELLIPSIZE_NONE);
    if (!p->draw) return;
    int lw, lh;
    pango_layout_get_pixel_size(p->lay, &lw, &lh);
    double baseline = pango_layout_get_baseline(p->lay) / (double)PANGO_SCALE;
    double xx = al == AL_RIGHT ? x - lw : al == AL_CENTER ? x - lw / 2.0 : x;
    theme_cairo(p->cr, color);
    cairo_move_to(p->cr, xx, base - baseline);
    pango_cairo_show_layout(p->cr, p->lay);
}

static double measure(Pen *p, const char *t, double size) {
    PangoFontDescription *fd = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
    pango_layout_set_font_description(p->lay, fd);
    pango_font_description_free(fd);
    pango_layout_set_width(p->lay, -1);
    pango_layout_set_ellipsize(p->lay, PANGO_ELLIPSIZE_NONE);
    pango_layout_set_text(p->lay, t, -1);
    int lw, lh;
    pango_layout_get_pixel_size(p->lay, &lw, &lh);
    return lw;
}

static void rect(Pen *p, double x, double y0, double w, double h, guint32 color, double radius) {
    if (!p->draw || w <= 0 || h <= 0) return;
    theme_cairo(p->cr, color);
    if (radius > 0) {
        double r = MIN(radius, MIN(w, h) / 2);
        cairo_new_sub_path(p->cr);
        cairo_arc(p->cr, x + w - r, y0 + r, r, -G_PI / 2, 0);
        cairo_arc(p->cr, x + w - r, y0 + h - r, r, 0, G_PI / 2);
        cairo_arc(p->cr, x + r, y0 + h - r, r, G_PI / 2, G_PI);
        cairo_arc(p->cr, x + r, y0 + r, r, G_PI, 3 * G_PI / 2);
        cairo_close_path(p->cr);
    } else cairo_rectangle(p->cr, x, y0, w, h);
    cairo_fill(p->cr);
}

static void line(Pen *p, double x0, double y0, double x1, double y1) {
    if (!p->draw) return;
    theme_cairo(p->cr, LINE);
    cairo_set_line_width(p->cr, 0.7);
    cairo_move_to(p->cr, x0, y0);
    cairo_line_to(p->cr, x1, y1);
    cairo_stroke(p->cr);
}

static void finish_page(Pen *p) {
    if (p->pages == 0) return;
    text(p, "Gerado neste computador pelo Finan+ · os dados não saem do seu dispositivo", M, H - 18, 7.5, MUTED, FALSE, AL_LEFT, 0);
    g_autofree char *pg = g_strdup_printf("Página %d de %d", p->pages, p->total > 0 ? p->total : p->pages);
    text(p, pg, W - M, H - 18, 7.5, MUTED, FALSE, AL_RIGHT, 0);
    if (p->draw) cairo_show_page(p->cr);
}

static void new_page(Pen *p) {
    finish_page(p);
    p->pages++;
    if (p->pages == 1) p->y = 0;
    else {
        char a[11], b[11];
        g_autofree char *h = g_strdup_printf("Finan+ · Relatório financeiro · %s a %s", day_br(p->r->from, a), day_br(p->r->to, b));
        text(p, h, M, M + 4, 8, MUTED, FALSE, AL_LEFT, 0);
        line(p, M, M + 12, W - M, M + 12);
        p->y = M + 26;
    }
}

/* garante espaço vertical [h]; senão, nova página */
static void need(Pen *p, double h) { if (p->pages == 0 || p->y + h > H - M - FOOTER) new_page(p); }

static char *money(Cents c) { return money_fmt(c); }

/* "37,5%" */
static char *pct(double v) {
    double x = floor(v * 10 + 0.5) / 10.0;
    char *s = g_strdup_printf("%.1f%%", x);
    for (char *q = s; *q; q++) if (*q == '.') *q = ',';
    return s;
}

static char *change_text(gboolean ok, double v, const char *label) {
    if (!ok) return g_strdup("sem base no período anterior");
    g_autofree char *p = pct(fabs(v));
    return g_strdup_printf("%s%s vs. %s", v >= 0 ? "+" : "−", p, label);
}

void chart_donut(cairo_t *cr, double cx, double cy, double d, double thick, const Cents *values, int n, Cents total) {
    double r = d / 2 - thick / 2;
    double start = -G_PI / 2;
    cairo_set_line_width(cr, thick);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    if (total <= 0) {
        theme_cairo(cr, 0x33888888);
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, r, 0, 2 * G_PI);
        cairo_stroke(cr);
        return;
    }
    for (int i = 0; i < n; i++) {
        double sweep = 2 * G_PI * values[i] / total;
        if (sweep <= 0) continue;
        theme_cairo(cr, CHART_SERIES[i % 8]);
        cairo_new_path(cr); /* não liga o arco ao ponto atual (ex.: fim de um texto) */
        double gap = n > 1 ? 0.014 : 0;
        cairo_arc(cr, cx, cy, r, start, start + MAX(sweep - gap, 0.009));
        cairo_stroke(cr);
        start += sweep;
    }
}

/* ---------------------------------------------------------------- blocos */

static void para(Pen *p, const char *t) {
    g_auto(GStrv) words = g_strsplit(t, " ", -1);
    GString *ln = g_string_new(NULL);
    for (char **w = words; *w; w++) {
        g_autofree char *cand = ln->len ? g_strdup_printf("%s %s", ln->str, *w) : g_strdup(*w);
        if (measure(p, cand, 8.5) > CW && ln->len) {
            need(p, 13);
            text(p, ln->str, M, p->y + 10, 8.5, MUTED, FALSE, AL_LEFT, 0);
            p->y += 13;
            g_string_assign(ln, *w);
        } else g_string_assign(ln, cand);
    }
    if (ln->len) { need(p, 13); text(p, ln->str, M, p->y + 10, 8.5, MUTED, FALSE, AL_LEFT, 0); p->y += 13; }
    g_string_free(ln, TRUE);
    p->y += 3;
}

/* Título de seção. [keep] = altura mínima do conteúdo que precisa caber junto (o título nunca fica sozinho no pé da página). */
static void section(Pen *p, const char *title, const char *sub, double keep) {
    need(p, 56 + keep);
    p->y += 12;
    text(p, title, M, p->y + 14, 14, INK, TRUE, AL_LEFT, 0);
    p->y += 22;
    if (sub && *sub) para(p, sub);
    p->y += 2;
}

static void note(Pen *p, const char *t) {
    need(p, 20);
    text(p, t, M, p->y + 12, 9.5, MUTED, FALSE, AL_LEFT, 0);
    p->y += 22;
}

static void header(Pen *p) {
    const Report *r = p->r;
    rect(p, 0, 0, W, 112, ACCENT, 0);
    text(p, "FINAN+", M, 40, 10, 0xCCFFFFFF, TRUE, AL_LEFT, 0);
    text(p, "Relatório financeiro", M, 68, 24, WHITE, TRUE, AL_LEFT, 0);
    char a[11], b[11];
    g_autofree char *per = g_strdup_printf("%s a %s · %d dia(s)", day_br(r->from, a), day_br(r->to, b), r->days);
    text(p, per, M, 92, 12, WHITE, FALSE, AL_LEFT, 0);
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    Day today = day_today();
    g_autofree char *gen = g_strdup_printf("Gerado em %s às %02d:%02d", day_br(today, a), g_date_time_get_hour(now), g_date_time_get_minute(now));
    text(p, gen, W - M, 40, 9, 0xDDFFFFFF, FALSE, AL_RIGHT, 0);
    p->y = 132;
}

static void kpis(Pen *p) {
    const Report *r = p->r;
    need(p, 150);
    double gap = 10, w3 = (CW - 2 * gap) / 3;
    double c1, c2, sr;
    gboolean ok1 = report_change(r->income, r->prev_income, &c1), ok2 = report_change(r->expense, r->prev_expense, &c2);
    g_autofree char *s1 = change_text(ok1, c1, "período anterior"), *s2 = change_text(ok2, c2, "período anterior");
    g_autofree char *srt = NULL;
    if (report_savings_rate(r, &sr)) { g_autofree char *x = pct(sr); srt = g_strdup_printf("%s das receitas", x); }
    else srt = g_strdup("sem receitas no período");
    struct { const char *t; Cents v; guint32 c; const char *sub; } big[3] = {
        {"Receitas", r->income, GREEN, s1},
        {"Despesas", r->expense, RED, s2},
        {"Saldo do período", report_balance(r), report_balance(r) < 0 ? RED : ACCENT, srt},
    };
    for (int i = 0; i < 3; i++) {
        double x = M + i * (w3 + gap);
        rect(p, x, p->y, w3, 72, CARD, 12);
        text(p, big[i].t, x + 12, p->y + 20, 9, MUTED, FALSE, AL_LEFT, 0);
        g_autofree char *m = money(big[i].v);
        text(p, m, x + 12, p->y + 44, 16, big[i].c, TRUE, AL_LEFT, w3 - 24);
        text(p, big[i].sub, x + 12, p->y + 61, 7.5, MUTED, FALSE, AL_LEFT, w3 - 24);
    }
    p->y += 82;
    double w4 = (CW - 3 * gap) / 4;
    g_autofree char *a = money(r->pending_income), *b = money(r->pending_expense), *c = money(report_daily_average(r));
    g_autofree char *d = g_strdup_printf("%u", r->txs->len);
    const char *labels[4] = {"A receber (pendente)", "A pagar (pendente)", "Média diária de gastos", "Lançamentos"};
    const char *vals[4] = {a, b, c, d};
    for (int i = 0; i < 4; i++) {
        double x = M + i * (w4 + gap);
        rect(p, x, p->y, w4, 48, CARD, 10);
        text(p, labels[i], x + 10, p->y + 17, 8, MUTED, FALSE, AL_LEFT, w4 - 20);
        text(p, vals[i], x + 10, p->y + 36, 12, INK, TRUE, AL_LEFT, w4 - 20);
    }
    p->y += 60;
}

/* Rosca com as 7 maiores categorias + "Outras", e legenda ao lado. */
static void donut(Pen *p, GPtrArray *rows, Cents total) {
    need(p, 160);
    Cents values[8] = {0};
    const char *names[8] = {0};
    int n = 0;
    guint rest = 0;
    for (guint i = 0; i < rows->len; i++) {
        CategoryRow *c = rows->pdata[i];
        if (i < 7) { values[n] = c->value; names[n++] = c->name; }
        else { rest++; values[7] += c->value; }
    }
    g_autofree char *others = rest ? g_strdup_printf("Outras (%u)", rest) : NULL;
    if (rest) { names[7] = others; n = 8; }
    double d = 130, cx = M + d / 2 + 6, cy = p->y + d / 2 + 6;
    if (p->draw) chart_donut(p->cr, cx, cy, d, 22, values, n, total);
    text(p, "Total", cx, cy - 4, 8, MUTED, FALSE, AL_CENTER, 0);
    g_autofree char *tc = report_compact(total);
    text(p, tc, cx, cy + 11, 11, INK, TRUE, AL_CENTER, 0);
    double lx = M + d + 30, ly = p->y + 14;
    for (int i = 0; i < n; i++) {
        rect(p, lx, ly - 8, 9, 9, CHART_SERIES[i % 8], 2);
        text(p, names[i], lx + 16, ly, 9.5, INK, FALSE, AL_LEFT, 200);
        g_autofree char *m = money(values[i]);
        text(p, m, W - M - 52, ly, 9.5, INK, TRUE, AL_RIGHT, 0);
        g_autofree char *pc = pct(total > 0 ? values[i] * 100.0 / total : 0);
        text(p, pc, W - M, ly, 9, MUTED, FALSE, AL_RIGHT, 0);
        ly += 16;
    }
    p->y += MAX(d + 16, ly - p->y + 4);
}

typedef struct { const char *t; double w; } Col;

static void table_header(Pen *p, const Col *cols, int n, int right_from) {
    need(p, 40);
    rect(p, M, p->y, CW, 18, CARD, 4);
    double x = M + 6;
    for (int i = 0; i < n; i++) {
        if (i >= right_from) text(p, cols[i].t, x + cols[i].w - 8, p->y + 12.5, 8, MUTED, TRUE, AL_RIGHT, 0);
        else text(p, cols[i].t, x, p->y + 12.5, 8, MUTED, TRUE, AL_LEFT, 0);
        x += cols[i].w;
    }
    p->y += 22;
}

static gboolean row_break(Pen *p, double h, const Col *cols, int n, int right) {
    if (p->y + h > H - M - FOOTER) { new_page(p); table_header(p, cols, n, right); return TRUE; }
    return FALSE;
}

static void category_table(Pen *p, GPtrArray *rows, gboolean with_limit) {
    Col lim[7] = {{"Categoria", 128}, {"", 92}, {"%", 46}, {"Lanç.", 38}, {"Média/mês", 74}, {"Limite/mês", 70}, {"Valor", CW - 448}};
    Col plain[5] = {{"Categoria", 150}, {"", 150}, {"%", 50}, {"Lanç.", 50}, {"Valor", CW - 400}};
    const Col *cols = with_limit ? lim : plain;
    int n = with_limit ? 7 : 5;
    table_header(p, cols, n, 2);
    for (guint i = 0; i < rows->len; i++) {
        CategoryRow *row = rows->pdata[i];
        row_break(p, 18, cols, n, 2);
        double base = p->y + 11, x = M + 6;
        guint32 color = CHART_SERIES[MIN(i, 7) % 8];
        text(p, row->name, x, base, 9, INK, FALSE, AL_LEFT, cols[0].w - 8);
        x += cols[0].w;
        rect(p, x, p->y + 5, cols[1].w - 10, 6, TRACK, 3);
        rect(p, x, p->y + 5, MAX(2, (cols[1].w - 10) * row->percent / 100.0), 6, color, 3);
        x += cols[1].w;
        g_autofree char *pc = pct(row->percent);
        text(p, pc, x + cols[2].w - 8, base, 8.5, MUTED, FALSE, AL_RIGHT, 0);
        x += cols[2].w;
        g_autofree char *cnt = g_strdup_printf("%d", row->count);
        text(p, cnt, x + cols[3].w - 8, base, 8.5, MUTED, FALSE, AL_RIGHT, 0);
        x += cols[3].w;
        if (with_limit) {
            gboolean over = category_over_limit(row);
            g_autofree char *avg = money(row->monthly_average);
            text(p, avg, x + cols[4].w - 8, base, 8.5, over ? RED : INK, FALSE, AL_RIGHT, 0);
            x += cols[4].w;
            g_autofree char *lm = row->has_limit ? money(row->monthly_limit) : NULL;
            g_autofree char *lt = row->has_limit ? g_strdup_printf("%s%s", lm, over ? " · acima" : "") : g_strdup("—");
            text(p, lt, x + cols[5].w - 8, base, 8, over ? RED : MUTED, FALSE, AL_RIGHT, cols[5].w - 6);
            x += cols[5].w;
        }
        g_autofree char *v = money(row->value);
        text(p, v, x + cols[n - 1].w - 8, base, 9, INK, TRUE, AL_RIGHT, 0);
        line(p, M, p->y + 17, W - M, p->y + 17);
        p->y += 18;
    }
    p->y += 6;
}

static char *month_label(Ym ym) { return g_strdup_printf("%s/%02d", BR_MONTHS_SHORT[ym_month(ym) - 1], ym_year(ym) % 100); }

static void month_chart(Pen *p) {
    const Report *r = p->r;
    guint total_months = r->months->len;
    guint first = total_months > REPORT_MAX_CHART_MONTHS ? total_months - REPORT_MAX_CHART_MONTHS : 0;
    guint n = total_months - first;
    double ch = 150;
    need(p, ch + 40);
    Cents max = 0;
    for (guint i = first; i < total_months; i++) {
        MonthRow *m = &g_array_index(r->months, MonthRow, i);
        max = MAX(max, MAX(m->income, m->expense));
    }
    Cents step = report_nice_step(max, 4);
    Cents top = MAX(step, ((max + step - 1) / step) * step);
    double left = M + 52, right = W - M, y0 = p->y + 6, y1 = y0 + ch;
    for (Cents v = 0; v <= top; v += step) {
        double yy = y1 - ch * v / top;
        line(p, left, yy, right, yy);
        g_autofree char *t = report_compact(v);
        text(p, t, left - 6, yy + 3, 7.5, MUTED, FALSE, AL_RIGHT, 0);
    }
    double slot = (right - left) / n, bw = MIN(14, slot * 0.32);
    for (guint i = 0; i < n; i++) {
        MonthRow *m = &g_array_index(r->months, MonthRow, first + i);
        double x = left + i * slot + slot / 2;
        double hi = ch * m->income / top, he = ch * m->expense / top;
        if (hi > 0) rect(p, x - bw - 1, y1 - hi, bw, hi, GREEN, 2);
        if (he > 0) rect(p, x + 1, y1 - he, bw, he, RED, 2);
        if (n <= 12 || i % 2 == 0) { g_autofree char *ml = month_label(m->ym); text(p, ml, x, y1 + 12, 7.5, MUTED, FALSE, AL_CENTER, 0); }
    }
    p->y = y1 + 22;
    rect(p, left, p->y - 7, 8, 8, GREEN, 2);
    text(p, "Receitas", left + 12, p->y, 8, MUTED, FALSE, AL_LEFT, 0);
    rect(p, left + 70, p->y - 7, 8, 8, RED, 2);
    text(p, "Despesas", left + 82, p->y, 8, MUTED, FALSE, AL_LEFT, 0);
    if (total_months > n) {
        g_autofree char *t = g_strdup_printf("Gráfico com os últimos %u meses do período; a tabela abaixo traz todos.", n);
        text(p, t, right, p->y, 7.5, MUTED, FALSE, AL_RIGHT, 0);
    }
    p->y += 14;
}

static void month_table(Pen *p) {
    const Report *r = p->r;
    Col cols[4] = {{"Mês", 160}, {"Receitas", 120}, {"Despesas", 120}, {"Saldo", CW - 400}};
    table_header(p, cols, 4, 1);
    for (guint i = 0; i < r->months->len; i++) {
        MonthRow *m = &g_array_index(r->months, MonthRow, i);
        row_break(p, 18, cols, 4, 1);
        double base = p->y + 11, x = M + 6;
        g_autofree char *my = br_month_year(m->ym);
        my[0] = g_ascii_toupper(my[0]);
        text(p, my, x, base, 9, INK, FALSE, AL_LEFT, 0);
        x += cols[0].w;
        g_autofree char *a = money(m->income), *b = money(m->expense), *c = money(m->income - m->expense);
        text(p, a, x + cols[1].w - 8, base, 9, GREEN, FALSE, AL_RIGHT, 0);
        x += cols[1].w;
        text(p, b, x + cols[2].w - 8, base, 9, RED, FALSE, AL_RIGHT, 0);
        x += cols[2].w;
        text(p, c, x + cols[3].w - 8, base, 9, m->income - m->expense < 0 ? RED : INK, TRUE, AL_RIGHT, 0);
        line(p, M, p->y + 17, W - M, p->y + 17);
        p->y += 18;
    }
    p->y += 6;
}

static void top_table(Pen *p) {
    Col cols[4] = {{"Data", 70}, {"Descrição", 230}, {"Categoria", 120}, {"Valor", CW - 420}};
    table_header(p, cols, 4, 3);
    for (guint i = 0; i < p->r->top_expenses->len; i++) {
        Tx *t = p->r->top_expenses->pdata[i];
        row_break(p, 18, cols, 4, 3);
        double base = p->y + 11, x = M + 6;
        char d[11];
        text(p, day_br(t->date, d), x, base, 8.5, MUTED, FALSE, AL_LEFT, 0);
        x += cols[0].w;
        text(p, t->desc, x, base, 9, INK, FALSE, AL_LEFT, cols[1].w - 8);
        x += cols[1].w;
        text(p, t->category, x, base, 8.5, MUTED, FALSE, AL_LEFT, cols[2].w - 8);
        x += cols[2].w;
        g_autofree char *v = money(t->value);
        text(p, v, x + cols[3].w - 8, base, 9, RED, TRUE, AL_RIGHT, 0);
        line(p, M, p->y + 17, W - M, p->y + 17);
        p->y += 18;
    }
    p->y += 6;
}

static void accounts_and_goals(Pen *p) {
    const Report *r = p->r;
    Col cols[2] = {{"Conta", 300}, {"Saldo atual", CW - 300}};
    table_header(p, cols, 2, 1);
    for (guint i = 0; i < r->accounts->len; i++) {
        AccountRow *a = &g_array_index(r->accounts, AccountRow, i);
        row_break(p, 18, cols, 2, 1);
        text(p, a->name, M + 6, p->y + 11, 9, INK, FALSE, AL_LEFT, 290);
        g_autofree char *m = money(a->balance);
        text(p, m, W - M - 8, p->y + 11, 9, a->balance < 0 ? RED : INK, TRUE, AL_RIGHT, 0);
        line(p, M, p->y + 17, W - M, p->y + 17);
        p->y += 18;
    }
    p->y += 8;
    if (!r->goals->len) { note(p, "Nenhuma meta cadastrada."); return; }
    for (guint i = 0; i < r->goals->len; i++) {
        GoalRow *g = &g_array_index(r->goals, GoalRow, i);
        need(p, 34);
        text(p, g->name, M, p->y + 11, 9.5, INK, TRUE, AL_LEFT, 260);
        g_autofree char *a = money(g->saved), *b = money(g->target), *pc = pct(goal_row_percent(g));
        char d[11];
        g_autofree char *dl = g->deadline != DAY_NONE ? g_strdup_printf(" · até %s", day_br(g->deadline, d)) : g_strdup("");
        g_autofree char *t = g_strdup_printf("%s de %s · %s%s", a, b, pc, dl);
        text(p, t, W - M, p->y + 11, 8.5, MUTED, FALSE, AL_RIGHT, 260);
        rect(p, M, p->y + 17, CW, 7, TRACK, 3.5);
        rect(p, M, p->y + 17, MAX(3, CW * goal_row_percent(g) / 100), 7, ACCENT, 3.5);
        p->y += 32;
    }
}

static void tx_table(Pen *p, const AppState *s) {
    Col cols[6] = {{"Data", 58}, {"Descrição", 150}, {"Categoria", 90}, {"Conta / cartão", 86}, {"Situação", 62}, {"Valor", CW - 446}};
    table_header(p, cols, 6, 5);
    for (guint i = 0; i < p->r->txs->len; i++) {
        Tx *t = p->r->txs->pdata[i];
        row_break(p, 17, cols, 6, 5);
        if (i % 2 == 1) rect(p, M, p->y, CW, 17, 0xFFF8FAFE, 0);
        double base = p->y + 11.5, x = M + 6;
        gboolean payment = !tx_is_flow(t);
        g_autofree char *where = NULL;
        if (tx_is_card(t)) { Card *c = app_card(s, t->card_id); where = g_strdup_printf("Cartão %s", c ? c->name : ""); }
        else { Account *a = app_account(s, t->account_id); where = g_strdup(a ? a->name : ""); }
        const char *status = payment ? "Pag. fatura" : tx_is_card(t) ? "Cartão" : t->paid ? (t->kind == KIND_INCOME ? "Recebido" : "Pago")
                                                                                         : (t->kind == KIND_INCOME ? "A receber" : "A pagar");
        char d[11];
        text(p, day_br(t->date, d), x, base, 8, MUTED, FALSE, AL_LEFT, 0);
        x += cols[0].w;
        text(p, t->desc, x, base, 8.5, payment ? MUTED : INK, FALSE, AL_LEFT, cols[1].w - 8);
        x += cols[1].w;
        text(p, t->category, x, base, 8, MUTED, FALSE, AL_LEFT, cols[2].w - 8);
        x += cols[2].w;
        text(p, where, x, base, 8, MUTED, FALSE, AL_LEFT, cols[3].w - 8);
        x += cols[3].w;
        text(p, status, x, base, 8, !t->paid && !payment ? ACCENT : MUTED, FALSE, AL_LEFT, cols[4].w - 6);
        x += cols[4].w;
        guint32 color = payment ? MUTED : t->kind == KIND_INCOME ? GREEN : RED;
        g_autofree char *m = money(t->value);
        g_autofree char *v = g_strdup_printf("%s %s", t->kind == KIND_INCOME ? "+" : "−", m);
        text(p, v, x + cols[5].w - 8, base, 8.5, color, !payment, AL_RIGHT, 0);
        p->y += 17;
    }
    line(p, M, p->y, W - M, p->y);
    p->y += 8;
}

static void layout(Pen *p, const AppState *s, gboolean include_txs) {
    const Report *r = p->r;
    new_page(p);
    header(p);
    kpis(p);
    section(p, "Despesas por categoria", "Valores realizados. Média mensal comparada com o limite mensal definido em Ajustes.", 160);
    if (!r->expense_by_category->len) note(p, "Sem despesas realizadas no período.");
    else { donut(p, r->expense_by_category, r->expense); category_table(p, r->expense_by_category, TRUE); }

    gboolean multi = r->months->len > 1;
    section(p, "Evolução mensal", multi ? "Receitas e despesas realizadas por mês." : "Receitas e despesas realizadas no mês.", multi ? 200 : 60);
    if (multi) month_chart(p);
    month_table(p);

    section(p, "Receitas por categoria", "Valores recebidos no período.", 60);
    if (!r->income_by_category->len) note(p, "Sem receitas recebidas no período.");
    else category_table(p, r->income_by_category, FALSE);

    g_autofree char *top_sub = g_strdup_printf("As %d maiores despesas realizadas do período.", REPORT_TOP);
    section(p, "Maiores despesas", top_sub, 60);
    if (!r->top_expenses->len) note(p, "Sem despesas realizadas no período.");
    else top_table(p);

    section(p, "Contas e metas", "Saldos e metas na data de hoje (não dependem do período).", 60);
    accounts_and_goals(p);

    if (include_txs) {
        g_autofree char *sub = g_strdup_printf("%u lançamento(s), incluindo pendentes. Pagamentos de fatura aparecem, mas não contam como despesa.", r->txs->len);
        section(p, "Lançamentos do período", sub, 60);
        if (!r->txs->len) note(p, "Nenhum lançamento no período.");
        else tx_table(p, s);
    }
    section(p, "Como ler este relatório", "", 40);
    char a[11], b[11];
    g_autofree char *cmp = g_strdup_printf("• A comparação usa o período anterior com o mesmo número de dias (%s a %s).", day_br(r->prev_from, a), day_br(r->prev_to, b));
    para(p, "• Receitas e despesas consideram só o que foi pago ou recebido. O que está pendente aparece em “A receber” e “A pagar”.");
    para(p, "• Compras no cartão contam na data da compra; o pagamento da fatura não é uma despesa nova.");
    para(p, cmp);
    para(p, "• Média mensal = valor da categoria ÷ meses do período (meses incompletos contam pela fração de dias).");
    finish_page(p);
}

gboolean pdf_report_render(const AppState *s, const char *path, Day from, Day to, gboolean include_txs, Day today, GError **error) {
    Report *r = report_build(s, from, to, today);
    if (!r) { g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Período inválido."); return FALSE; }
    /* 1ª passada: conta as páginas */
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 4, 4);
    cairo_t *mcr = cairo_create(img);
    Pen count = {.cr = mcr, .draw = FALSE, .r = r, .lay = pango_cairo_create_layout(mcr)};
    layout(&count, s, include_txs);
    g_object_unref(count.lay);
    cairo_destroy(mcr);
    cairo_surface_destroy(img);
    /* 2ª passada: desenha */
    cairo_surface_t *pdf = cairo_pdf_surface_create(path, W, H);
    cairo_pdf_surface_restrict_to_version(pdf, CAIRO_PDF_VERSION_1_4); /* compatível com qualquer leitor */
    cairo_pdf_surface_set_metadata(pdf, CAIRO_PDF_METADATA_TITLE, "Relatório financeiro — Finan+");
    cairo_pdf_surface_set_metadata(pdf, CAIRO_PDF_METADATA_CREATOR, "Finan+ (Linux)");
    cairo_t *cr = cairo_create(pdf);
    Pen pen = {.cr = cr, .draw = TRUE, .total = count.pages, .r = r, .lay = pango_cairo_create_layout(cr)};
    layout(&pen, s, include_txs);
    g_object_unref(pen.lay);
    cairo_destroy(cr);
    cairo_surface_finish(pdf);
    cairo_status_t st = cairo_surface_status(pdf);
    cairo_surface_destroy(pdf);
    report_free(r);
    if (st != CAIRO_STATUS_SUCCESS) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Erro ao gravar o PDF: %s", cairo_status_to_string(st));
        return FALSE;
    }
    return TRUE;
}

