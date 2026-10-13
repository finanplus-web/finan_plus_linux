/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Ler fatura ou boleto: os mesmos casos do app Android (BillTest.kt) e da versão web (bill.test.mjs),
 * mais a leitura dos PDFs de exemplo (tests/fixtures/faturas, fatura fictícia) com o Poppler
 * e as senhas lembradas no armazenamento cifrado.
 */
#include "core/bill.h"
#include "core/finance.h"
#include "core/store.h"
#include <glib/gstdio.h>
#include <string.h>

#define D(y, m, d) day_from_ymd(y, m, d)
static const char *ITAU = "34191.79001 01043.510047 91020.150008 5 84380000026000";
static const char *BB = "00190500954014481606906809350314337370000000100";
static const char *LUZ = "836200000005 667800481000 180975657313 001589636081";
static const char *PIX = "00020126580014br.gov.bcb.pix0136123e4567-e12b-12d1-a456-42665544000052040000530398654071234.565802BR5913Fulano de Tal6008BRASILIA62070503***63046EE5";
static Day TODAY;

static char *replace(const char *s, const char *a, const char *b) {
    g_auto(GStrv) parts = g_strsplit(s, a, 2);
    return g_strjoinv(b, parts);
}

static void test_boleto_bancario(void) {
    Boleto b;
    g_assert_true(boleto_parse(ITAU, D(2020, 11, 1), &b));
    g_assert_cmpint(b.type, ==, BOLETO_BANK);
    g_assert_cmpint(b.amount, ==, 26000);
    g_assert_cmpint(b.due, ==, D(2020, 11, 13));
    g_assert_cmpstr(b.bank, ==, "Itaú");
    g_assert_cmpstr(b.barcode, ==, "34195843800000260001790001043510049102015000");
    Boleto c;
    g_assert_true(boleto_parse(b.barcode, D(2020, 11, 1), &c));
    g_assert_cmpint(c.amount, ==, 26000);
    g_assert_true(boleto_parse(BB, TODAY, &c));
    g_assert_cmpstr(c.bank, ==, "Banco do Brasil");
    g_assert_cmpint(c.amount, ==, 100);
}

static void test_boleto_errado(void) {
    Boleto b;
    g_autofree char *v = replace(ITAU, "84380000026000", "84380000026100");
    g_assert_false(boleto_parse(v, TODAY, &b));
    g_autofree char *c1 = replace(ITAU, "79001", "79002");
    g_assert_false(boleto_parse(c1, TODAY, &b));
    g_assert_false(boleto_parse("1234", TODAY, &b));
    g_autofree char *l = replace(LUZ, "667800481000", "667800481001");
    g_assert_false(boleto_parse(l, TODAY, &b));
}

static void test_fator(void) {
    g_assert_cmpint(boleto_due_from_factor(9999, D(2025, 2, 1)), ==, D(2025, 2, 21));
    g_assert_cmpint(boleto_due_from_factor(1000, D(2025, 2, 1)), ==, D(2025, 2, 22));
    g_assert_cmpint(boleto_due_from_factor(1626, TODAY), ==, D(2026, 11, 10));
    g_assert_cmpint(boleto_due_from_factor(0, TODAY), ==, DAY_NONE);
}

static void test_arrecadacao(void) {
    Boleto b;
    g_assert_true(boleto_parse(LUZ, TODAY, &b));
    g_assert_cmpint(b.type, ==, BOLETO_UTILITY);
    g_assert_cmpint(b.amount, ==, 6678);
    g_assert_cmpstr(b.segment, ==, "Energia elétrica e gás");
    g_assert_cmpint(b.due, ==, DAY_NONE);
    g_assert_true(boleto_parse("846700000017435900240209024050002435842210108119", TODAY, &b));
    g_assert_cmpint(b.amount, ==, 14359);
    /* identificador 8: módulo 11 */
    const char *body = "848" "00000012345" "0001" "2026111500000000000000000";
    char bc[45];
    memcpy(bc, body, 3); bc[3] = (char)('0' + boleto_mod11_utility(body, 43)); memcpy(bc + 4, body + 3, 40); bc[44] = 0;
    char line[49];
    for (int i = 0; i < 4; i++) { memcpy(line + i * 12, bc + i * 11, 11); line[i * 12 + 11] = (char)('0' + boleto_mod11_utility(bc + i * 11, 11)); }
    line[48] = 0;
    g_assert_true(boleto_parse(line, TODAY, &b));
    g_assert_cmpint(b.amount, ==, 12345);
    g_assert_cmpstr(b.segment, ==, "Telefone e internet");
}

static void test_pix(void) {
    char crc[5];
    g_assert_cmpstr(pix_crc16("123456789", 9, crc), ==, "29B1");
    Pix p;
    g_assert_true(pix_parse("00020126580014br.gov.bcb.pix0136123e4567-e12b-12d1-a456-4266554400005204000053039865802BR5913Fulano de Tal6008BRASILIA62070503***63041D3D", &p));
    g_assert_false(p.has_amount);
    g_assert_cmpstr(p.name, ==, "Fulano de Tal");
    pix_clear(&p);
    g_assert_true(pix_parse(PIX, &p));
    g_assert_cmpint(p.amount, ==, 123456);
    pix_clear(&p);
    g_autofree char *bad = replace(PIX, "1234.56", "1234.57");
    g_assert_false(pix_parse(bad, &p));
}

static void test_codigo_colado(void) {
    BillInfo i = { .due = DAY_NONE };
    g_autofree char *in = g_strdup_printf(" %s \n", ITAU);
    g_assert_true(bill_from_code(in, D(2020, 11, 1), &i));
    g_assert_cmpint(i.amount, ==, 26000);
    g_assert_cmpint(i.amount_src, ==, BILL_SRC_BOLETO);
    g_assert_cmpint(i.due, ==, D(2020, 11, 13));
    g_assert_false(bill_from_code("qualquer coisa", TODAY, &i));
    bill_info_clear(&i);
}

static void test_rotulos(void) {
    BillInfo i = { .due = DAY_NONE };
    bill_from_text("BANCO EXEMPLO S.A.\nResumo da fatura\nTotal da fatura anterior R$ 1.020,00\nPagamento mínimo R$ 98,31\n"
                   "Total desta fatura\nR$ 1.655,40\nData de vencimento: 15/11/2026\nPróximo vencimento 15/12/2026\nLimite disponível R$ 3.000,00\n", TODAY, &i);
    g_assert_true(i.has_amount);
    g_assert_cmpint(i.amount, ==, 165540);
    g_assert_cmpint(i.amount_src, ==, BILL_SRC_TEXT);
    g_assert_cmpint(i.due, ==, D(2026, 11, 15));
    bill_from_text("Total da fatura R$ 655,40   Pagamento mínimo R$ 98,31\nVencimento 15 NOV\n", TODAY, &i);
    g_assert_cmpint(i.amount, ==, 65540);
    g_assert_cmpint(i.due, ==, D(2026, 11, 15));
    g_assert_cmpint(bill_date_in("vence em 5 de janeiro", TODAY), ==, D(2027, 1, 5));
    g_assert_cmpint(bill_date_in("01/12/26", TODAY), ==, D(2026, 12, 1));
    g_assert_cmpint(bill_date_in("31/02/2026", TODAY), ==, DAY_NONE);
    bill_from_text("Pagamento mínimo do total da fatura R$ 98,31\nValor total R$ 980,00", TODAY, &i);
    g_assert_cmpint(i.amount, ==, 98000);
    g_autofree char *t = g_strdup_printf("Valor total R$ 999,99\nVencimento 01/01/2027\nPague com o código:\n%s\n", ITAU);
    bill_from_text(t, D(2020, 11, 1), &i);
    g_assert_cmpint(i.amount, ==, 26000);
    g_assert_cmpint(i.amount_src, ==, BILL_SRC_BOLETO);
    g_assert_cmpint(i.due, ==, D(2020, 11, 13));
    bill_from_text("Olá, este documento não tem valores.", TODAY, &i);
    g_assert_true(bill_info_empty(&i));
    g_assert_false(i.dynamic_pix);
    bill_info_clear(&i);
}

static void test_pix_no_texto(void) {
    GString *g = g_string_new("Pague com Pix copia e cola:\n");
    for (gsize k = 0; k < strlen(PIX); k += 40) { g_string_append_len(g, PIX + k, MIN(40, strlen(PIX) - k)); g_string_append_c(g, '\n'); }
    BillInfo i = { .due = DAY_NONE };
    bill_from_text(g->str, TODAY, &i);
    g_assert_cmpint(i.amount, ==, 123456);
    g_assert_cmpint(i.amount_src, ==, BILL_SRC_PIX);
    g_assert_cmpstr(i.payee, ==, "Fulano de Tal");
    bill_info_clear(&i);
    g_string_free(g, TRUE);
}

static void test_fatura_cartao(void) {
    g_autoptr(AppState) s = app_state_new();
    Card *c = card_new("c", "Cartão X", 500000, 5, 12);
    g_ptr_array_add(s->cards, c);
    g_ptr_array_add(s->txs, tx_new("1", KIND_EXPENSE, 10000, D(2026, 10, 3), "A", "Outros", TRUE, "main", "c"));
    g_ptr_array_add(s->txs, tx_new("2", KIND_EXPENSE, 20000, D(2026, 10, 9), "B", "Outros", TRUE, "main", "c"));
    g_ptr_array_add(s->txs, tx_new("3", KIND_EXPENSE, 5000, D(2026, 11, 2), "C", "Outros", TRUE, "main", "c"));
    Ym ym = bill_invoice_ym(s, c, D(2026, 11, 12), TODAY);
    g_assert_cmpint(ym, ==, ym_make(2026, 11));
    g_assert_cmpint(bill_app_total(s, c, ym), ==, 25000);
    g_assert_cmpint(invoice_ym(c, bill_date_in_invoice(c, ym)), ==, ym);
    Card *late = card_new("l", "Late", 0, 25, 5);
    g_ptr_array_add(s->cards, late);
    g_assert_cmpint(bill_invoice_ym(s, late, D(2026, 11, 5), TODAY), ==, ym_make(2026, 10));
}

/* ------------------------------------------------------------------ PDFs de exemplo */
static BillPdfStatus read_fixture(const char *name, const char *pw, char **text) {
    g_autofree char *path = g_build_filename(FIXTURES, name, NULL), *raw = NULL;
    gsize n = 0;
    g_assert_true(g_file_get_contents(path, &raw, &n, NULL));
    g_autoptr(GBytes) b = g_bytes_new(raw, n);
    g_autofree char *msg = NULL;
    return bill_pdf_read(b, pw, text, &msg);
}

static void test_pdf(void) {
    char *text = NULL;
    g_assert_cmpint(read_fixture("fatura.pdf", NULL, &text), ==, BILL_PDF_TEXT);
    g_assert_nonnull(strstr(text, "Total desta fatura R$ 1.655,40"));
    BillInfo i = { .due = DAY_NONE };
    bill_from_text(text, TODAY, &i);
    g_assert_cmpint(i.amount, ==, 165540);
    g_assert_cmpint(i.amount_src, ==, BILL_SRC_BOLETO);
    g_assert_cmpint(i.due, ==, D(2026, 11, 15));
    g_clear_pointer(&text, g_free);

    g_assert_cmpint(read_fixture("fatura-senha.pdf", NULL, &text), ==, BILL_PDF_NEEDS_PASSWORD);
    g_assert_cmpint(read_fixture("fatura-senha.pdf", "999", &text), ==, BILL_PDF_WRONG_PASSWORD);
    g_assert_cmpint(read_fixture("fatura-senha.pdf", "12345", &text), ==, BILL_PDF_TEXT);
    bill_from_text(text, TODAY, &i);
    g_assert_cmpint(i.amount, ==, 165540);
    g_clear_pointer(&text, g_free);
    g_assert_cmpint(read_fixture("fatura-senha-rc4.pdf", "12345", &text), ==, BILL_PDF_TEXT);
    g_clear_pointer(&text, g_free);
    g_assert_cmpint(read_fixture("fatura-restrita.pdf", NULL, &text), ==, BILL_PDF_TEXT);
    bill_from_text(text, TODAY, &i);
    g_assert_cmpint(i.amount, ==, 165540);
    g_clear_pointer(&text, g_free);
    g_assert_cmpint(read_fixture("imagem.pdf", NULL, &text), ==, BILL_PDF_NO_TEXT);
    g_autoptr(GBytes) junk = g_bytes_new_static("não é pdf", 10);
    g_autofree char *msg = NULL;
    g_assert_cmpint(bill_pdf_read(junk, NULL, &text, &msg), ==, BILL_PDF_ERROR);
    bill_info_clear(&i);
}

static void test_senha_lembrada(void) {
    g_autoptr(GError) e = NULL;
    g_autofree char *dir = g_dir_make_tmp("finan-test-XXXXXX", &e);
    g_assert_no_error(e);
    Store *st = store_open(dir, &e);
    g_assert_no_error(e);
    g_assert_null(store_bill_password(st, "c1"));
    g_assert_true(store_set_bill_password(st, "c1", "12345", &e));
    g_assert_true(store_set_bill_password(st, "c2", "999", &e));
    g_autofree char *a = store_bill_password(st, "c1");
    g_assert_cmpstr(a, ==, "12345");
    /* a senha não aparece no arquivo */
    g_autofree char *path = g_build_filename(dir, "faturas.fin", NULL), *raw = NULL;
    gsize n = 0;
    g_assert_true(g_file_get_contents(path, &raw, &n, NULL));
    g_assert_null(memmem(raw, n, "12345", 5));
    /* esquecer uma não apaga a outra; esquecer todas apaga o arquivo */
    g_assert_true(store_set_bill_password(st, "c1", NULL, &e));
    g_assert_null(store_bill_password(st, "c1"));
    g_autofree char *b = store_bill_password(st, "c2");
    g_assert_cmpstr(b, ==, "999");
    g_assert_true(store_set_bill_password(st, "c2", "", &e));
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    /* "Apagar tudo" apaga junto */
    g_assert_true(store_set_bill_password(st, "c3", "abc", &e));
    g_assert_true(store_wipe(st, &e));
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    store_free(st);
    GDir *d = g_dir_open(dir, 0, NULL);
    const char *nm;
    while (d && (nm = g_dir_read_name(d))) { g_autofree char *p = g_build_filename(dir, nm, NULL); g_unlink(p); }
    if (d) g_dir_close(d);
    g_rmdir(dir);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_setenv("FINAN_PLUS_NO_KEYRING", "1", TRUE);
    TODAY = D(2026, 10, 13);
    g_test_add_func("/bill/boleto-bancario", test_boleto_bancario);
    g_test_add_func("/bill/boleto-errado", test_boleto_errado);
    g_test_add_func("/bill/fator", test_fator);
    g_test_add_func("/bill/arrecadacao", test_arrecadacao);
    g_test_add_func("/bill/pix", test_pix);
    g_test_add_func("/bill/codigo-colado", test_codigo_colado);
    g_test_add_func("/bill/rotulos", test_rotulos);
    g_test_add_func("/bill/pix-no-texto", test_pix_no_texto);
    g_test_add_func("/bill/fatura-cartao", test_fatura_cartao);
    g_test_add_func("/bill/pdf", test_pdf);
    g_test_add_func("/bill/senha-lembrada", test_senha_lembrada);
    return g_test_run();
}
