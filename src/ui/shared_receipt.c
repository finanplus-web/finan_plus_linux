/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Comprovante de despesa compartilhada (Android 1.9): imagem e PDF na identidade do Finan+ (ícone e nome do app,
 * fundo azul-claro com brilhos suaves, cartão com o total, quem pagou, recebido, pendente e quem deve e quanto).
 * Desenhado com Cairo e Pango; sem dependência da janela (testado em tests/test_pdf.c).
 */
#include "shared_receipt.h"
#include "theme.h"
#include "core/shared.h"
#include <cairo-pdf.h>
#include <pango/pangocairo.h>
#include <string.h>

/* ================================================================ desenho do comprovante (PDF e imagem) */

static void text_at(cairo_t *cr, PangoLayout *lay, const char *t, double x, double y, double size, gboolean bold,
                    guint32 color, double max_w, gboolean right) {
    PangoFontDescription *fd = pango_font_description_from_string(bold ? "Sans Bold" : "Sans");
    pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
    pango_layout_set_font_description(lay, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(lay, t, -1);
    pango_layout_set_width(lay, max_w > 0 ? (int)(max_w * PANGO_SCALE) : -1);
    pango_layout_set_ellipsize(lay, max_w > 0 ? PANGO_ELLIPSIZE_END : PANGO_ELLIPSIZE_NONE);
    /* largura da página inteira (x = 0) = rodapé centralizado */
    pango_layout_set_alignment(lay, right ? PANGO_ALIGN_RIGHT : x == 0 && max_w > 0 ? PANGO_ALIGN_CENTER : PANGO_ALIGN_LEFT);
    theme_cairo(cr, color);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, lay);
}

static void rrect(cairo_t *cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}

static void glow(cairo_t *cr, double cx, double cy, double r, guint32 argb) {
    cairo_pattern_t *p = cairo_pattern_create_radial(cx, cy, 0, cx, cy, r);
    double R = ((argb >> 16) & 255) / 255.0, G = ((argb >> 8) & 255) / 255.0, B = (argb & 255) / 255.0;
    cairo_pattern_add_color_stop_rgba(p, 0, R, G, B, 0.55);
    cairo_pattern_add_color_stop_rgba(p, 1, R, G, B, 0);
    cairo_set_source(cr, p);
    cairo_arc(cr, cx, cy, r, 0, 2 * G_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
}

#define INK 0xFF182238
#define MUTED 0xFF5B6579
#define ACCENT 0xFF3A5FC8
#define GREEN 0xFF1B7351
#define RED 0xFFB03A4F

/* altura do desenho para [e] na largura 600 */
static double receipt_height(const SharedExpense *e) { return 470 + 44.0 * e->people->len; }

/* O comprovante, na identidade do Finan+ (fundo azul-claro com brilhos, cartão branco, ícone e nome do app),
 * em coordenadas de 600 de largura. Mesmo conteúdo do Android: total, quem pagou, recebido, pendente e quem deve. */
static void receipt_draw(cairo_t *cr, const SharedExpense *e, gboolean hide) {
    double W = 600, H = receipt_height(e);
    cairo_pattern_t *bg = cairo_pattern_create_linear(0, 0, 0, H);
    cairo_pattern_add_color_stop_rgb(bg, 0, 0xEE / 255.0, 0xF4 / 255.0, 1);
    cairo_pattern_add_color_stop_rgb(bg, 1, 0xDF / 255.0, 0xE8 / 255.0, 1);
    cairo_set_source(cr, bg);
    cairo_paint(cr);
    cairo_pattern_destroy(bg);
    glow(cr, 80, 60, 220, 0xFFD9E5FF);
    glow(cr, W - 40, H - 80, 260, 0xFFFFE7ED);
    PangoLayout *lay = pango_cairo_create_layout(cr);
    /* ícone e nome do app */
    g_autoptr(GBytes) png = g_resources_lookup_data("/com/finanplus/FinanPlus/icons/256x256/apps/com.finanplus.FinanPlus.png", 0, NULL);
    if (png) {
        g_autoptr(GdkTexture) tex = gdk_texture_new_from_bytes(png, NULL);
        if (tex) {
            int tw = gdk_texture_get_width(tex), th = gdk_texture_get_height(tex);
            cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
            cairo_surface_flush(img);
            gdk_texture_download(tex, cairo_image_surface_get_data(img), cairo_image_surface_get_stride(img));
            cairo_surface_mark_dirty(img);
            cairo_save(cr);
            cairo_translate(cr, 36, 30);
            cairo_scale(cr, 44.0 / tw, 44.0 / th);
            cairo_set_source_surface(cr, img, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
            cairo_surface_destroy(img);
        }
    }
    text_at(cr, lay, "Finan+", 92, 34, 20, TRUE, INK, 0, FALSE);
    text_at(cr, lay, "Despesa compartilhada", 92, 58, 12, FALSE, MUTED, 0, FALSE);

    /* cartão */
    double cx = 30, cy = 96, cw = W - 60, ch = H - 96 - 64;
    rrect(cr, cx, cy, cw, ch, 22);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.94);
    cairo_fill(cr);
    SharedSummary st;
    shared_summary(e, &st);
    char b1[32], d[11];
    double x = cx + 26, y = cy + 22, iw = cw - 52;
    text_at(cr, lay, e->name, x, y, 22, TRUE, INK, iw, FALSE);
    y += 32;
    g_autofree char *when = br_full_date(e->date);
    text_at(cr, lay, when, x, y, 12, FALSE, MUTED, iw, FALSE);
    y += 34;
    text_at(cr, lay, "Total", x, y, 12, FALSE, MUTED, 0, FALSE);
    text_at(cr, lay, hide ? "R$ ••••" : money_format(e->total, b1), x, y + 16, 30, TRUE, ACCENT, 0, FALSE);
    const SharedPerson *payer = NULL;
    for (guint i = 0; i < e->people->len; i++) if (strcmp(((SharedPerson *)e->people->pdata[i])->id, e->payer_id) == 0) payer = e->people->pdata[i];
    g_autofree char *pl = g_strdup_printf("Pago por %s", payer ? payer->name : "—");
    text_at(cr, lay, pl, x, y + 58, 13, FALSE, INK, iw, FALSE);
    y += 96;
    /* recebido e pendente */
    double half = iw / 2;
    text_at(cr, lay, "Recebido", x, y, 12, FALSE, MUTED, 0, FALSE);
    text_at(cr, lay, hide ? "R$ ••••" : money_format(st.received, b1), x, y + 16, 18, TRUE, GREEN, 0, FALSE);
    text_at(cr, lay, "Pendente", x + half, y, 12, FALSE, MUTED, 0, FALSE);
    text_at(cr, lay, hide ? "R$ ••••" : money_format(summary_pending(&st), b1), x + half, y + 16, 18, TRUE,
            summary_pending(&st) > 0 ? RED : GREEN, 0, FALSE);
    y += 58;
    cairo_set_source_rgba(cr, 0.35, 0.40, 0.48, 0.18);
    cairo_rectangle(cr, x, y, iw, 1);
    cairo_fill(cr);
    y += 14;
    text_at(cr, lay, "Quem deve e quanto", x, y, 13, TRUE, INK, 0, FALSE);
    y += 26;
    for (guint i = 0; i < st.people->len; i++) {
        const PersonStatus *p = &g_array_index(st.people, PersonStatus, i);
        PersonState ps = person_state(p);
        text_at(cr, lay, p->person->name, x, y, 14, TRUE, INK, half, FALSE);
        g_autofree char *sl = p->is_payer ? g_strdup("Pagou a despesa")
            : ps == PERSON_PAID ? g_strdup("Quitado")
            : hide ? g_strdup(person_state_label(ps))
            : g_strdup_printf("%s · deve %s", person_state_label(ps), money_format(person_remaining(p), b1));
        text_at(cr, lay, sl, x, y + 19, 11, FALSE, ps == PERSON_PENDING || ps == PERSON_PARTIAL ? RED : MUTED, half, FALSE);
        text_at(cr, lay, hide ? "R$ ••••" : money_format(p->person->share, b1), x + half, y + 4, 14, TRUE, INK, half, TRUE);
        y += 44;
    }
    shared_summary_clear(&st);
    /* rodapé */
    g_autofree char *foot = g_strdup_printf("Feito com Finan+ · finanplus-web.github.io · %s", day_br(e->date, d));
    text_at(cr, lay, foot, 0, H - 44, 11, FALSE, MUTED, W, FALSE);
    g_object_unref(lay);
}

/* PNG (2× = 1200 px de largura) ou PDF (do tamanho do desenho) */
gboolean shared_receipt_write(const SharedExpense *e, const char *path, gboolean pdf, GError **error) {
    double W = 600, H = receipt_height(e);
    cairo_surface_t *sf = pdf ? cairo_pdf_surface_create(path, W, H) : cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)W * 2, (int)H * 2);
    cairo_t *cr = cairo_create(sf);
    if (!pdf) cairo_scale(cr, 2, 2);
    if (pdf) cairo_pdf_surface_set_metadata(sf, CAIRO_PDF_METADATA_CREATOR, "Finan+ para Linux");
    receipt_draw(cr, e, FALSE);
    cairo_destroy(cr);
    cairo_status_t s;
    if (pdf) { cairo_surface_finish(sf); s = cairo_surface_status(sf); }
    else s = cairo_surface_write_to_png(sf, path);
    cairo_surface_destroy(sf);
    if (s != CAIRO_STATUS_SUCCESS) { g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_IO, "%s", cairo_status_to_string(s)); return FALSE; }
    return TRUE;
}

