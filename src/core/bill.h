/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * "Ler fatura ou boleto" (FATURAS.md): tira valor e vencimento de uma fatura em PDF (texto), de uma
 * linha digitável de boleto ou de um Pix copia e cola. Mesmo cálculo do app Android (core/bill/Bill.kt)
 * e do Finan+ web (js/bill.js). O PDF é aberto com o Poppler em bill_pdf_read().
 *
 * O resultado é sempre uma sugestão: a tela mostra de onde cada valor veio e a pessoa confirma.
 */
#pragma once
#include "model.h"

G_BEGIN_DECLS

/* de onde veio a informação, em ordem de confiança: o código é exato, o texto é heurística */
typedef enum { BILL_SRC_BOLETO, BILL_SRC_PIX, BILL_SRC_TEXT } BillSource;
const char *bill_source_label(BillSource s);

typedef struct {
    gboolean has_amount;
    Cents amount;
    BillSource amount_src;
    char *amount_excerpt;
    gboolean has_due;
    Day due;
    BillSource due_src;
    char *due_excerpt;
    /* recebedor do Pix, ou banco que emitiu o boleto (nem sempre é quem cobra); NULL se não houver */
    char *payee;
    /* tipo de conta de um boleto de arrecadação ("Energia elétrica e gás"…); NULL se não houver */
    char *segment;
    /* Pix dinâmico: o valor fica no banco de quem cobra e não vem no código */
    gboolean dynamic_pix;
} BillInfo;

void bill_info_clear(BillInfo *i);
#define bill_info_empty(i) (!(i)->has_amount && !(i)->has_due)

/* ---- boleto (padrão Febraban; todos os dígitos verificadores são conferidos) */
typedef enum { BOLETO_BANK, BOLETO_UTILITY } BoletoType;
typedef struct {
    BoletoType type;
    char barcode[45];
    gboolean has_amount;
    Cents amount;
    Day due;             /* DAY_NONE se não houver */
    const char *bank;    /* banco emissor (bancário) ou NULL */
    const char *segment; /* segmento (arrecadação) ou NULL */
} Boleto;

/* linha digitável (47 ou 48 números) ou código de barras (44), com pontos, espaços e quebras */
gboolean boleto_parse(const char *input, Day today, Boleto *out);
int boleto_mod10(const char *s, int n);
int boleto_mod11_bank(const char *s, int n);
int boleto_mod11_utility(const char *s, int n);
/* fator de vencimento (com a virada de 22/02/2025): das duas datas possíveis, a mais próxima de hoje */
Day boleto_due_from_factor(int factor, Day today);

/* ---- Pix copia e cola (BR Code; o CRC é conferido) */
typedef struct { gboolean has_amount; Cents amount; char *name; char *city; gboolean dynamic; } Pix;
gboolean pix_parse(const char *input, Pix *out);
void pix_clear(Pix *p);
/* CRC16-CCITT (0x1021, início 0xFFFF): 4 dígitos hexadecimais maiúsculos em out[5] */
char *pix_crc16(const char *s, gsize len, char out[5]);

/* ---- leitura */
/* código colado; FALSE = não reconhecido */
gboolean bill_from_code(const char *input, Day today, BillInfo *out);
/* texto extraído da fatura: linha digitável impressa > Pix > rótulos ("Total da fatura", "Vencimento") */
void bill_from_text(const char *text, Day today, BillInfo *out);
/* primeira data do trecho (15/11/2026, 15.11.26, "15 de novembro de 2026", "15 NOV"); DAY_NONE se não houver */
Day bill_date_in(const char *s, Day today);

/* ---- fatura do cartão */
/* mês da fatura do cartão que vence no mês de [due] (DAY_NONE: a fatura em aberto, ou a do mês) */
Ym bill_invoice_ym(const AppState *s, const Card *c, Day due, Day today);
/* total da fatura [ym] no Finan+ (compras do cartão naquele mês de fatura) */
Cents bill_app_total(const AppState *s, const Card *c, Ym ym);
/* data de uma compra que cai na fatura [ym]: o dia do fechamento */
Day bill_date_in_invoice(const Card *c, Ym ym);

/* ---- PDF (Poppler), só na memória */
typedef enum { BILL_PDF_TEXT, BILL_PDF_NEEDS_PASSWORD, BILL_PDF_WRONG_PASSWORD, BILL_PDF_NO_TEXT, BILL_PDF_ERROR } BillPdfStatus;
#define BILL_PDF_MAX_BYTES (25 * 1024 * 1024)
/* [text] (g_free) só com BILL_PDF_TEXT; [message] (g_free) só com BILL_PDF_ERROR */
BillPdfStatus bill_pdf_read(GBytes *pdf, const char *password, char **text, char **message);

G_END_DECLS
