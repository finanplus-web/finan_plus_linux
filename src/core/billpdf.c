/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Abre a fatura em PDF com o Poppler (poppler-glib, GPL-2.0 ou posterior) e devolve o texto, linha a linha.
 *
 * - O arquivo é lido só na memória (um arquivo anônimo, memfd, que nunca vai para o disco):
 *   nada é gravado no computador, nem uma cópia sem senha.
 * - PDF com senha de abertura: só abre com a senha que a pessoa digitou (ou pediu para lembrar).
 *   O app nunca tenta adivinhar a senha.
 * - PDF com "proteção" só contra cópia ou impressão abre sem senha: o texto é lido normalmente,
 *   porque é o documento da própria pessoa lido no computador dela.
 */
#define _GNU_SOURCE /* memfd_create */
#include "bill.h"
#include <poppler.h>
#include <math.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define MAX_PAGES 12 /* faturas têm poucas páginas; o resumo fica no começo */

typedef struct { gunichar c; double x1, y1, x2, y2; } Glyph;
typedef struct { double y, h; GArray *g; } Row;

static int by_x(gconstpointer a, gconstpointer b) {
    double d = ((const Glyph *)a)->x1 - ((const Glyph *)b)->x1;
    return d < 0 ? -1 : d > 0;
}
static int by_y(gconstpointer a, gconstpointer b) {
    double d = (*(Row *const *)a)->y - (*(Row *const *)b)->y;
    return d < 0 ? -1 : d > 0;
}

/*
 * Junta os caracteres pela posição na página: rótulo e valor na mesma linha visual
 * ("Total desta fatura ... R$ 1.655,40") ficam juntos, como no Android e na versão web.
 */
static void page_lines(PopplerPage *page, GString *out) {
    g_autofree char *text = poppler_page_get_text(page);
    PopplerRectangle *rects = NULL;
    guint n = 0;
    if (!text || !poppler_page_get_text_layout(page, &rects, &n)) return;
    GPtrArray *rows = g_ptr_array_new();
    const char *p = text;
    for (guint i = 0; i < n && *p; i++, p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (c == '\n' || c == '\r' || g_unichar_isspace(c)) continue;
        Glyph gl = { c, rects[i].x1, rects[i].y1, rects[i].x2, rects[i].y2 };
        double mid = (gl.y1 + gl.y2) / 2, h = fabs(gl.y2 - gl.y1);
        Row *row = NULL;
        for (guint r = 0; r < rows->len && !row; r++) {
            Row *cand = g_ptr_array_index(rows, r);
            if (fabs(cand->y - mid) <= MAX(2.0, MAX(h, cand->h) * 0.4)) row = cand;
        }
        if (!row) {
            row = g_new0(Row, 1);
            row->y = mid; row->h = h; row->g = g_array_new(FALSE, FALSE, sizeof(Glyph));
            g_ptr_array_add(rows, row);
        }
        g_array_append_val(row->g, gl);
    }
    g_free(rects);
    g_ptr_array_sort(rows, by_y);
    for (guint r = 0; r < rows->len; r++) {
        Row *row = g_ptr_array_index(rows, r);
        g_array_sort(row->g, by_x);
        double prev = -1e9;
        for (guint k = 0; k < row->g->len; k++) {
            Glyph *gl = &g_array_index(row->g, Glyph, k);
            double h = fabs(gl->y2 - gl->y1);
            /* espaço entre palavras: um vão maior que ~1/4 da altura da letra */
            if (k > 0 && gl->x1 - prev > MAX(1.0, h * 0.25)) g_string_append_c(out, ' ');
            g_string_append_unichar(out, gl->c);
            prev = gl->x2;
        }
        g_string_append_c(out, '\n');
        g_array_free(row->g, TRUE);
        g_free(row);
    }
    g_ptr_array_free(rows, TRUE);
}

/*
 * Abre o PDF a partir de um arquivo anônimo na memória (memfd: nada vai para o disco), pelo caminho
 * /proc/self/fd/N. Não usa poppler_document_new_from_bytes/_from_stream/_from_fd: no Poppler 24.02
 * (Ubuntu 24.04) eles releem um bloco já liberado quando a senha está errada (e podem travar o app),
 * porque tentam de novo com o mesmo stream. Com o caminho, cada tentativa abre o arquivo de novo.
 */
static PopplerDocument *open_doc(const char *data, gsize len, const char *password, GError **error) {
    int fd = memfd_create("finan-fatura", MFD_CLOEXEC);
    if (fd < 0) { g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "memfd"); return NULL; }
    gsize off = 0;
    while (off < len) {
        ssize_t w = write(fd, data + off, len - off);
        if (w <= 0) { close(fd); g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "memfd"); return NULL; }
        off += (gsize)w;
    }
    g_autofree char *uri = g_strdup_printf("file:///proc/self/fd/%d", fd);
    PopplerDocument *doc = poppler_document_new_from_file(uri, password, error);
    close(fd); /* o Poppler abriu o próprio descritor; o conteúdo some quando o último for fechado */
    return doc;
}

BillPdfStatus bill_pdf_read(GBytes *pdf, const char *password, char **text, char **message) {
    *text = NULL;
    if (message) *message = NULL;
    gsize len = 0;
    const char *data = pdf ? g_bytes_get_data(pdf, &len) : NULL;
    if (!data || len > BILL_PDF_MAX_BYTES) {
        if (message) *message = g_strdup("O arquivo é grande demais para uma fatura (mais de 25 MB).");
        return BILL_PDF_ERROR;
    }
    if (len < 5 || memcmp(data, "%PDF-", 5) != 0) {
        if (message) *message = g_strdup("Este arquivo não é um PDF.");
        return BILL_PDF_ERROR;
    }
    gboolean has_pw = password && *password;
    g_autoptr(GError) err = NULL;
    PopplerDocument *doc = open_doc(data, len, has_pw ? password : NULL, &err);
    if (!doc) {
        if (!has_pw && g_error_matches(err, POPPLER_ERROR, POPPLER_ERROR_ENCRYPTED)) return BILL_PDF_NEEDS_PASSWORD;
        if (has_pw) {
            /* confere se o erro é mesmo da senha (e não de um PDF danificado) */
            g_autoptr(GError) e2 = NULL;
            PopplerDocument *probe = open_doc(data, len, NULL, &e2);
            if (probe) g_object_unref(probe);
            else if (g_error_matches(e2, POPPLER_ERROR, POPPLER_ERROR_ENCRYPTED)) return BILL_PDF_WRONG_PASSWORD;
        }
        if (message) *message = g_strdup("Não foi possível ler este PDF. Ele pode estar danificado.");
        return BILL_PDF_ERROR;
    }
    GString *out = g_string_new(NULL);
    int pages = MIN(poppler_document_get_n_pages(doc), MAX_PAGES);
    for (int i = 0; i < pages; i++) {
        PopplerPage *page = poppler_document_get_page(doc, i);
        if (!page) continue;
        page_lines(page, out);
        g_object_unref(page);
    }
    g_object_unref(doc);
    gboolean blank = TRUE;
    for (const char *c = out->str; *c && blank; c++) if (!g_ascii_isspace(*c)) blank = FALSE;
    if (blank) { g_string_free(out, TRUE); return BILL_PDF_NO_TEXT; }
    *text = g_string_free(out, FALSE);
    return BILL_PDF_TEXT;
}
