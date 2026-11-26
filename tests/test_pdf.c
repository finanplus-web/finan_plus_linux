/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Testes do relatório em PDF: gera arquivos reais com Cairo e confere a estrutura.
 * Com um JSON como argumento (tests/test-pdf arquivo.json saída.pdf), só gera o PDF (usado na documentação).
 */
#include "core/backup.h"
#include "ui/pdf.h"
#include "ui/shared_receipt.h"
#include "core/shared.h"
#include <glib/gstdio.h>
#include <string.h>

static int count(const char *hay, gsize n, const char *needle) {
    int c = 0;
    size_t l = strlen(needle);
    for (gsize i = 0; i + l <= n; i++) if (memcmp(hay + i, needle, l) == 0) c++;
    return c;
}

static char *render(const AppState *s, Day from, Day to, gboolean txs, gsize *len) {
    g_autofree char *dir = g_dir_make_tmp("finan-pdf-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(dir, "r.pdf", NULL);
    g_autoptr(GError) e = NULL;
    g_assert_true(pdf_report_render(s, path, from, to, txs, from, &e));
    g_assert_no_error(e);
    char *data = NULL;
    g_assert_true(g_file_get_contents(path, &data, len, NULL));
    g_unlink(path);
    g_rmdir(dir);
    return data;
}

static void test_empty(void) {
    g_autoptr(AppState) s = app_state_new();
    Day a, b;
    day_parse_iso("2026-10-01", &a);
    day_parse_iso("2026-10-31", &b);
    gsize n;
    g_autofree char *pdf = render(s, a, b, TRUE, &n);
    g_assert_true(g_str_has_prefix(pdf, "%PDF-"));
    g_assert_cmpint(count(pdf, n, "/Type /Page\n") + count(pdf, n, "/Type /Page "), >=, 1);
}

static void test_many_pages(void) {
    g_autoptr(AppState) s = app_state_new();
    Day a, b;
    day_parse_iso("2026-01-01", &a);
    day_parse_iso("2026-12-31", &b);
    for (int i = 0; i < 400; i++) {
        g_autofree char *id = g_strdup_printf("t%d", i);
        g_autofree char *desc = g_strdup_printf("Lançamento de teste número %d com descrição longa para cortar", i);
        g_ptr_array_add(s->txs, tx_new(id, i % 5 ? KIND_EXPENSE : KIND_INCOME, 1000 + i * 37, a + i % 365, desc,
                                       i % 5 ? s->cats[KIND_EXPENSE]->pdata[i % 7] : "Salário", i % 3 != 0, "main", ""));
    }
    gsize n1, n2;
    g_autofree char *with = render(s, a, b, TRUE, &n1);
    g_autofree char *without = render(s, a, b, FALSE, &n2);
    int p1 = count(with, n1, "/Type /Page\n") + count(with, n1, "/Type /Page ");
    int p2 = count(without, n2, "/Type /Page\n") + count(without, n2, "/Type /Page ");
    g_assert_cmpint(p1, >, p2); /* a lista de lançamentos ocupa páginas a mais */
    g_assert_cmpint(p1, >=, 10);
}

/* comprovante de despesa compartilhada (imagem e PDF) e o relatório com o saldo acumulado */
static void test_shared_receipt(void) {
    g_autoptr(AppState) s = app_state_new();
    SharedPersonDraft p[3] = {{"", "Eu", ""}, {"", "Ana", ""}, {"", "Pedro", ""}};
    Day d;
    day_parse_iso("2026-11-21", &d);
    SharedDraft dr = {"Churrasco", "450", d, SPLIT_EQUAL, p, 3, 0, ""};
    g_assert_true(shared_save(s, NULL, &dr, NULL));
    SharedExpense *e = s->shared->pdata[0];
    g_assert_true(shared_add_payment(s, e->id, ((SharedPerson *)e->people->pdata[1])->id, "150", d, NULL));
    g_autofree char *dir = g_dir_make_tmp("finan-rcpt-XXXXXX", NULL);
    g_autofree char *png = g_build_filename(dir, "r.png", NULL), *pdf = g_build_filename(dir, "r.pdf", NULL);
    g_autoptr(GError) err = NULL;
    g_assert_true(shared_receipt_write(e, png, FALSE, &err));
    g_assert_no_error(err);
    g_assert_true(shared_receipt_write(e, pdf, TRUE, &err));
    g_autofree char *a = NULL, *b = NULL;
    gsize na = 0, nb = 0;
    g_assert_true(g_file_get_contents(png, &a, &na, NULL));
    g_assert_true(g_file_get_contents(pdf, &b, &nb, NULL));
    g_assert_true(na > 8 && memcmp(a, "\x89PNG", 4) == 0);
    g_assert_true(g_str_has_prefix(b, "%PDF-"));
    if (g_getenv("FINAN_RECEIPT_OUT")) g_file_set_contents(g_getenv("FINAN_RECEIPT_OUT"), a, (gssize)na, NULL);
    g_unlink(png); g_unlink(pdf); g_rmdir(dir);
}

static void test_accumulated_pdf(void) {
    g_autoptr(AppState) s = app_state_new();
    Day a, b;
    day_parse_iso("2026-09-01", &a);
    day_parse_iso("2026-11-30", &b);
    g_ptr_array_add(s->txs, tx_new("x", KIND_INCOME, 300000, a + 4, "Salário", "Salário", TRUE, "main", ""));
    g_autofree char *dir = g_dir_make_tmp("finan-pdf-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(dir, "r.pdf", NULL);
    g_autoptr(GError) e = NULL;
    g_assert_true(pdf_report_render_ext(s, path, a, b, FALSE, b, TRUE, &e));
    g_assert_no_error(e);
    g_unlink(path);
    g_rmdir(dir);
}

int main(int argc, char **argv) {
    if (argc == 3 && g_str_has_suffix(argv[1], ".json")) {
        g_autofree char *text = NULL;
        gsize len;
        if (!g_file_get_contents(argv[1], &text, &len, NULL)) return 1;
        AppState *s = backup_parse(text, (gssize)len, NULL, NULL);
        Day a, b, today = day_today();
        a = ym_first(day_ym(today) - 3);
        b = ym_last(day_ym(today));
        g_autoptr(GError) e = NULL;
        if (!pdf_report_render(s, argv[2], a, b, TRUE, today, &e)) { g_printerr("%s\n", e->message); return 1; }
        app_state_free(s);
        return 0;
    }
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/pdf/empty", test_empty);
    g_test_add_func("/pdf/many-pages", test_many_pages);
    g_test_add_func("/pdf/shared-receipt", test_shared_receipt);
    g_test_add_func("/pdf/accumulated", test_accumulated_pdf);
    return g_test_run();
}
