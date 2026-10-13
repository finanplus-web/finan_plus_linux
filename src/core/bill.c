/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Ler fatura ou boleto: boleto, Pix, leitura do texto e comparação com a fatura do cartão.
 * Mesmo cálculo de core/bill/Bill.kt (Android) e js/bill.js (web). Ver FATURAS.md.
 */
#include "bill.h"
#include "finance.h"
#include <string.h>
#include <stdlib.h>

const char *bill_source_label(BillSource s) {
    switch (s) {
    case BILL_SRC_BOLETO: return "código do boleto";
    case BILL_SRC_PIX: return "Pix copia e cola";
    default: return "texto da fatura";
    }
}

void bill_info_clear(BillInfo *i) {
    g_clear_pointer(&i->amount_excerpt, g_free);
    g_clear_pointer(&i->due_excerpt, g_free);
    g_clear_pointer(&i->payee, g_free);
    g_clear_pointer(&i->segment, g_free);
    memset(i, 0, sizeof *i);
    i->due = DAY_NONE;
}

/* "trecho de até 90 caracteres" com espaços simples */
static char *shorten(const char *s) {
    GString *g = g_string_new(NULL);
    gboolean sp = FALSE;
    for (const char *p = s; *p; p++) {
        if (g_ascii_isspace(*p)) { sp = TRUE; continue; }
        if (sp && g->len) g_string_append_c(g, ' ');
        sp = FALSE;
        g_string_append_c(g, *p);
    }
    char *v = g_utf8_make_valid(g->str, -1);
    g_string_free(g, TRUE);
    if (g_utf8_strlen(v, -1) > 90) {
        char *cut = g_utf8_substring(v, 0, 87);
        char *r = g_strconcat(cut, "…", NULL);
        g_free(cut); g_free(v);
        return r;
    }
    return v;
}

/* --------------------------------------------------------------------------------------- boleto */
int boleto_mod10(const char *s, int n) {
    int sum = 0, w = 2;
    for (int i = n - 1; i >= 0; i--) {
        int p = (s[i] - '0') * w;
        if (p > 9) p = p / 10 + p % 10;
        sum += p; w = w == 2 ? 1 : 2;
    }
    return (10 - sum % 10) % 10;
}
static int weighted11(const char *s, int n) {
    int sum = 0, w = 2;
    for (int i = n - 1; i >= 0; i--) { sum += (s[i] - '0') * w; w = w == 9 ? 2 : w + 1; }
    return sum;
}
/* DV geral do boleto bancário: 0, 10 e 11 viram 1 */
int boleto_mod11_bank(const char *s, int n) { int dv = 11 - weighted11(s, n) % 11; return (dv == 0 || dv == 10 || dv == 11) ? 1 : dv; }
/* DV do boleto de arrecadação: resto 0 ou 1 vira 0; resto 10 vira 1 */
int boleto_mod11_utility(const char *s, int n) { int r = weighted11(s, n) % 11; return (r == 0 || r == 1) ? 0 : 11 - r; }

Day boleto_due_from_factor(int factor, Day today) {
    if (factor <= 0) return DAY_NONE;
    /* dias desde 07/10/1997; chegou a 9999 em 21/02/2025 e recomeçou em 1000 em 22/02/2025 (base 29/05/2022) */
    Day a = day_from_ymd(1997, 10, 7) + factor, b = day_from_ymd(2022, 5, 29) + factor;
    return abs(a - today) <= abs(b - today) ? a : b;
}

static const char *const SEGMENTS[] = {
    NULL, "Prefeitura", "Água e esgoto", "Energia elétrica e gás", "Telefone e internet", "Órgão do governo",
    "Carnê ou cobrança", "Multa de trânsito",
};
static const struct { const char *code, *name; } BANKS[] = {
    {"001", "Banco do Brasil"}, {"004", "Banco do Nordeste"}, {"021", "Banestes"}, {"033", "Santander"}, {"041", "Banrisul"},
    {"070", "BRB"}, {"077", "Inter"}, {"104", "Caixa"}, {"197", "Stone"}, {"208", "BTG Pactual"}, {"212", "Banco Original"},
    {"237", "Bradesco"}, {"260", "Nubank"}, {"290", "PagBank"}, {"323", "Mercado Pago"}, {"336", "C6 Bank"}, {"341", "Itaú"},
    {"380", "PicPay"}, {"389", "Mercantil do Brasil"}, {"403", "Cora"}, {"422", "Safra"}, {"655", "Banco Votorantim"},
    {"748", "Sicredi"}, {"756", "Sicoob"},
};
static const char *bank_name(const char *code3) {
    for (gsize i = 0; i < G_N_ELEMENTS(BANKS); i++) if (strncmp(BANKS[i].code, code3, 3) == 0) return BANKS[i].name;
    return NULL;
}

static gint64 num(const char *s, int n) { gint64 v = 0; for (int i = 0; i < n; i++) v = v * 10 + (s[i] - '0'); return v; }

static gboolean bank_barcode(const char *b, Day today, Boleto *out) {
    if (b[3] != '9') return FALSE; /* só real */
    char rest[44];
    memcpy(rest, b, 4); memcpy(rest + 4, b + 5, 39);
    if (boleto_mod11_bank(rest, 43) != b[4] - '0') return FALSE;
    memset(out, 0, sizeof *out);
    out->type = BOLETO_BANK;
    memcpy(out->barcode, b, 44); out->barcode[44] = '\0';
    out->amount = num(b + 9, 10);
    out->has_amount = out->amount > 0;
    out->due = boleto_due_from_factor((int)num(b + 5, 4), today);
    out->bank = bank_name(b);
    return TRUE;
}
static gboolean bank_line(const char *d, Day today, Boleto *out) {
    if (boleto_mod10(d, 9) != d[9] - '0' || boleto_mod10(d + 10, 10) != d[20] - '0' || boleto_mod10(d + 21, 10) != d[31] - '0') return FALSE;
    char b[45];
    memcpy(b, d, 4); b[4] = d[32]; memcpy(b + 5, d + 33, 14); memcpy(b + 19, d + 4, 5); memcpy(b + 24, d + 10, 10); memcpy(b + 34, d + 21, 10);
    b[44] = '\0';
    return bank_barcode(b, today, out);
}
static int utility_dv(char ident, const char *s, int n) {
    if (ident == '6' || ident == '7') return boleto_mod10(s, n);
    if (ident == '8' || ident == '9') return boleto_mod11_utility(s, n);
    return -1;
}
static gboolean utility_barcode(const char *b, Boleto *out) {
    char rest[44];
    memcpy(rest, b, 3); memcpy(rest + 3, b + 4, 40);
    int dv = utility_dv(b[2], rest, 43);
    if (dv < 0 || dv != b[3] - '0') return FALSE;
    memset(out, 0, sizeof *out);
    out->type = BOLETO_UTILITY;
    memcpy(out->barcode, b, 44); out->barcode[44] = '\0';
    /* 6 e 8: valor em reais; 7 e 9: valor de referência (outra moeda/índice), não é dinheiro */
    if (b[2] == '6' || b[2] == '8') { out->amount = num(b + 4, 11); out->has_amount = out->amount > 0; }
    out->due = DAY_NONE;
    int seg = b[1] - '0';
    out->segment = seg >= 1 && seg <= 7 ? SEGMENTS[seg] : NULL;
    return TRUE;
}
static gboolean utility_line(const char *d, Boleto *out) {
    char b[45];
    for (int i = 0; i < 4; i++) {
        const char *bl = d + i * 12;
        int dv = utility_dv(d[2], bl, 11);
        if (dv < 0 || dv != bl[11] - '0') return FALSE;
        memcpy(b + i * 11, bl, 11);
    }
    b[44] = '\0';
    return utility_barcode(b, out);
}
static gboolean boleto_digits(const char *d, gsize n, Day today, Boleto *out) {
    if (n == 47) return bank_line(d, today, out);
    if (n == 48) return d[0] == '8' && utility_line(d, out);
    if (n == 44) return d[0] == '8' ? utility_barcode(d, out) : bank_barcode(d, today, out);
    return FALSE;
}
gboolean boleto_parse(const char *input, Day today, Boleto *out) {
    char d[64];
    gsize n = 0;
    for (const char *p = input ? input : ""; *p; p++) {
        if (*p < '0' || *p > '9') continue;
        if (n >= sizeof d - 1) return FALSE;
        d[n++] = *p;
    }
    d[n] = '\0';
    return boleto_digits(d, n, today, out);
}

/* ------------------------------------------------------------------------------------------ Pix */
char *pix_crc16(const char *s, gsize len, char out[5]) {
    guint crc = 0xFFFF;
    for (gsize i = 0; i < len; i++) {
        crc ^= ((guint)(guchar)s[i]) << 8;
        for (int k = 0; k < 8; k++) crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
    }
    g_snprintf(out, 5, "%04X", crc);
    return out;
}

void pix_clear(Pix *p) { g_clear_pointer(&p->name, g_free); g_clear_pointer(&p->city, g_free); memset(p, 0, sizeof *p); }

/* TLV do BR Code: id (2) + tamanho em caracteres (2) + valor. Devolve os valores por id (g_hash_table). */
static GHashTable *tlv(const char *s) {
    GHashTable *h = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    const char *p = s;
    while (*p) {
        if (strlen(p) < 4 || !g_ascii_isdigit(p[2]) || !g_ascii_isdigit(p[3])) { g_hash_table_unref(h); return NULL; }
        char id[3] = { p[0], p[1], 0 };
        long len = (p[2] - '0') * 10 + (p[3] - '0');
        const char *v = p + 4;
        if (g_utf8_strlen(v, -1) < len) { g_hash_table_unref(h); return NULL; }
        const char *end = g_utf8_offset_to_pointer(v, len);
        g_hash_table_replace(h, g_strdup(id), g_strndup(v, (gsize)(end - v)));
        p = end;
    }
    return h;
}

gboolean pix_parse(const char *input, Pix *out) {
    memset(out, 0, sizeof *out);
    if (!input || !g_utf8_validate(input, -1, NULL)) return FALSE;
    g_autofree char *s = g_strstrip(g_strdup(input));
    gsize n = strlen(s);
    if (!g_str_has_prefix(s, "000201") || n < 14) return FALSE;
    gsize at = n - 8;
    if (strncmp(s + at, "6304", 4) != 0) return FALSE;
    char crc[5];
    pix_crc16(s, at + 4, crc);
    if (g_ascii_strcasecmp(crc, s + at + 4) != 0) return FALSE;
    g_autoptr(GHashTable) tags = tlv(s);
    if (!tags) return FALSE;
    const char *acc = g_hash_table_lookup(tags, "26");
    if (!acc) return FALSE;
    g_autoptr(GHashTable) sub = tlv(acc);
    if (!sub) return FALSE;
    const char *gui = g_hash_table_lookup(sub, "00");
    if (!gui || g_ascii_strcasecmp(gui, "br.gov.bcb.pix") != 0) return FALSE;
    const char *a = g_hash_table_lookup(tags, "54");
    if (a && g_regex_match_simple("^\\d{1,13}(\\.\\d{1,2})?$", a, 0, 0)) {
        const char *dot = strchr(a, '.');
        gint64 whole = g_ascii_strtoll(a, NULL, 10), frac = 0;
        if (dot) { frac = (dot[1] - '0') * 10; if (dot[2]) frac += dot[2] - '0'; }
        out->amount = whole * 100 + frac;
        out->has_amount = out->amount > 0;
    }
    const char *nm = g_hash_table_lookup(tags, "59"), *ct = g_hash_table_lookup(tags, "60");
    if (nm) { char *t = g_strstrip(g_strdup(nm)); if (*t) out->name = t; else g_free(t); }
    if (ct) { char *t = g_strstrip(g_strdup(ct)); if (*t) out->city = t; else g_free(t); }
    out->dynamic = g_hash_table_contains(sub, "25");
    return TRUE;
}

/* --------------------------------------------------------------------------------------- leitor */
static void from_pix(const Pix *p, const char *raw, BillInfo *out) {
    bill_info_clear(out);
    if (p->has_amount) { out->has_amount = TRUE; out->amount = p->amount; out->amount_src = BILL_SRC_PIX; out->amount_excerpt = shorten(raw); }
    out->payee = g_strdup(p->name);
    out->dynamic_pix = p->dynamic && !p->has_amount;
}
static void from_boleto(const Boleto *b, const char *raw, BillInfo *out) {
    bill_info_clear(out);
    if (b->has_amount) { out->has_amount = TRUE; out->amount = b->amount; out->amount_src = BILL_SRC_BOLETO; out->amount_excerpt = shorten(raw); }
    if (b->due != DAY_NONE) { out->has_due = TRUE; out->due = b->due; out->due_src = BILL_SRC_BOLETO; out->due_excerpt = shorten(raw); }
    out->payee = g_strdup(b->bank);
    out->segment = g_strdup(b->segment);
}

static char *strip_chars(const char *s, const char *drop) {
    GString *g = g_string_new(NULL);
    for (const char *p = s; *p; p++) if (!strchr(drop, *p)) g_string_append_c(g, *p);
    return g_string_free(g, FALSE);
}

gboolean bill_from_code(const char *input, Day today, BillInfo *out) {
    bill_info_clear(out);
    if (!input) return FALSE;
    g_autofree char *t = g_strstrip(g_strdup(input));
    Pix p;
    if (pix_parse(t, &p)) { from_pix(&p, t, out); pix_clear(&p); return TRUE; }
    g_autofree char *joined = strip_chars(t, "\r\n");
    if (pix_parse(joined, &p)) { from_pix(&p, t, out); pix_clear(&p); return TRUE; }
    Boleto b;
    if (boleto_parse(t, today, &b)) { from_boleto(&b, t, out); return TRUE; }
    return FALSE;
}

static GRegex *re_code, *re_money, *re_num_date, *re_name_date;
static void init_regex(void) {
    static gsize once = 0;
    if (g_once_init_enter(&once)) {
        re_code = g_regex_new("\\d[\\d .\\-]{41,80}\\d", 0, 0, NULL);
        /* procurado no texto já em minúsculas e sem acento ("r$") */
        re_money = g_regex_new("(?<![\\d,.])(?:r\\$\\s*)?(\\d{1,3}(?:\\.\\d{3})+|\\d+),(\\d{2})(?!\\d)", 0, 0, NULL);
        re_num_date = g_regex_new("(?<!\\d)(\\d{1,2})[/.\\-](\\d{1,2})[/.\\-](\\d{4}|\\d{2})(?!\\d)", 0, 0, NULL);
        re_name_date = g_regex_new("(?<!\\d)(\\d{1,2})(?:\\s+de)?\\s+(jan|fev|mar|abr|mai|jun|jul|ago|set|out|nov|dez)[a-z]*\\.?(?:(?:\\s+de)?\\s+(\\d{4}))?", 0, 0, NULL);
        g_once_init_leave(&once, 1);
    }
}

/* minúsculas e sem acento, mantendo a pontuação ("Mínimo R$" → "minimo r$") */
static char *fold(const char *s) {
    g_autofree char *valid = g_utf8_make_valid(s ? s : "", -1);
    g_autofree char *nfd = g_utf8_normalize(valid, -1, G_NORMALIZE_NFD);
    if (!nfd) return g_strdup("");
    GString *out = g_string_new(NULL);
    for (const char *p = nfd; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (g_unichar_type(c) == G_UNICODE_NON_SPACING_MARK) continue;
        g_string_append_unichar(out, g_unichar_tolower(c));
    }
    return g_string_free(out, FALSE);
}

static gboolean find_boleto(const char *text, Day today, BillInfo *out) {
    g_autofree char *flat = g_strdup(text);
    for (char *p = flat; *p; p++) if (*p == '\n' || *p == '\r') *p = ' ';
    gboolean have = FALSE;
    GMatchInfo *mi = NULL;
    g_regex_match(re_code, flat, 0, &mi);
    while (g_match_info_matches(mi)) {
        g_autofree char *m = g_match_info_fetch(mi, 0);
        char d[128];
        gsize n = 0;
        for (const char *p = m; *p && n < sizeof d - 1; p++) if (*p >= '0' && *p <= '9') d[n++] = *p;
        d[n] = '\0';
        static const gsize LENS[] = { 47, 48, 44 };
        for (int li = 0; li < 3; li++) {
            gsize len = LENS[li];
            for (gsize st = 0; st + len <= n; st++) {
                char w[64];
                memcpy(w, d + st, len); w[len] = '\0';
                Boleto b;
                if (!boleto_digits(w, len, today, &b)) continue;
                g_autofree char *raw = g_strstrip(g_strdup(m));
                if (b.has_amount) { from_boleto(&b, raw, out); g_match_info_free(mi); return TRUE; }
                if (!have) { from_boleto(&b, raw, out); have = TRUE; }
            }
        }
        g_match_info_next(mi, NULL);
    }
    g_match_info_free(mi);
    return have;
}

static gboolean find_pix(const char *text, BillInfo *out) {
    char *variants[2] = { strip_chars(text, "\r\n"), strip_chars(text, " \t\r\n\f\v") };
    gboolean found = FALSE;
    for (int v = 0; v < 2 && !found; v++) {
        const char *s = variants[v];
        gsize n = strlen(s);
        for (const char *i = strstr(s, "000201"); i && !found; i = strstr(i + 6, "000201")) {
            /* o "6304" pode aparecer dentro do código: tenta cada ocorrência até o CRC conferir */
            for (const char *k = strstr(i, "6304"); k && (gsize)(k - s) + 8 <= n; k = strstr(k + 1, "6304")) {
                g_autofree char *cand = g_strndup(i, (gsize)(k - i) + 8);
                Pix p;
                if (pix_parse(cand, &p)) { from_pix(&p, cand, out); pix_clear(&p); found = TRUE; break; }
            }
        }
    }
    g_free(variants[0]); g_free(variants[1]);
    return found;
}

static const struct { const char *label; int w; } AMOUNT_LABELS[] = {
    {"total desta fatura", 100}, {"total da fatura", 100}, {"valor total da fatura", 100}, {"total da sua fatura", 100},
    {"valor da fatura", 95}, {"valor total a pagar", 95}, {"total a pagar", 90}, {"valor a pagar", 90},
    {"valor do documento", 85}, {"valor cobrado", 80}, {"pagamento total", 80}, {"valor total", 60}, {"total geral", 60},
};
/* palavras de outro valor (mínimo, limite, fatura anterior, juros…) */
static const char *const AMOUNT_SKIP[] = {
    "minimo", "limite", "anterior", "parcelamento", "parcelar", "juros", "encargo", "multa", "iof", "cet ",
    "rotativo", "disponivel", "saque", "desconto", "proxima", "proximo",
};
static gboolean skips(const char *s, gsize len) {
    g_autofree char *t = g_strndup(s, len);
    for (gsize i = 0; i < G_N_ELEMENTS(AMOUNT_SKIP); i++) if (strstr(t, AMOUNT_SKIP[i])) return TRUE;
    return FALSE;
}

/* dinheiro a partir de [from] em [l]: devolve o início do achado em *at */
static gboolean money_from(const char *l, gssize from, Cents *v, gint *at) {
    GMatchInfo *mi = NULL;
    gboolean ok = g_regex_match_full(re_money, l, -1, from, 0, &mi, NULL);
    if (ok) {
        g_autofree char *a = g_match_info_fetch(mi, 1), *b = g_match_info_fetch(mi, 2);
        g_autofree char *whole = strip_chars(a, ".");
        *v = g_ascii_strtoll(whole, NULL, 10) * 100 + g_ascii_strtoll(b, NULL, 10);
        gint st, en;
        g_match_info_fetch_pos(mi, 0, &st, &en);
        *at = st;
    }
    g_match_info_free(mi);
    return ok;
}

/* início da linha seguinte (com recuo de 25 bytes, sem cortar um caractere ao meio) */
static gsize back25(const char *l, gsize at) {
    if (at <= 25) return 0;
    const char *p = g_utf8_find_prev_char(l, l + at - 24);
    return p ? (gsize)(p - l) : 0;
}

static void find_amount(char **lines, char **norm, guint n, BillInfo *out) {
    int best_w = -1;
    for (guint i = 0; i < n; i++) {
        const char *l = norm[i];
        for (gsize k = 0; k < G_N_ELEMENTS(AMOUNT_LABELS); k++) {
            int w = AMOUNT_LABELS[k].w;
            if (w <= best_w) continue;
            const char *hit = strstr(l, AMOUNT_LABELS[k].label);
            if (!hit) continue;
            gsize at = (gsize)(hit - l), end = at + strlen(AMOUNT_LABELS[k].label);
            /* "Total da fatura anterior", "Pagamento mínimo do total da fatura": é de outro valor */
            gsize b25 = back25(l, at);
            if (skips(l + b25, at - b25)) continue;
            Cents v = 0; gint mat = 0;
            char *ex = NULL;
            if (money_from(l, (gssize)end, &v, &mat)) {
                if (!skips(l + end, (gsize)mat - end)) ex = g_strdup(lines[i]);
            } else {
                for (guint j = 1; j <= 2 && i + j < n && !ex; j++) {
                    const char *nl = norm[i + j];
                    if (!money_from(nl, 0, &v, &mat)) continue;
                    if (!skips(nl, (gsize)mat)) ex = g_strconcat(lines[i], " ", lines[i + j], NULL);
                }
            }
            if (ex && v > 0) {
                g_free(out->amount_excerpt);
                out->has_amount = TRUE; out->amount = v; out->amount_src = BILL_SRC_TEXT; out->amount_excerpt = shorten(ex);
                best_w = w;
            }
            g_free(ex);
        }
    }
}

static Day safe_date(int y, int m, int d) {
    if (y < 2000 || y > 2100 || !day_is_valid_ymd(y, m, d)) return DAY_NONE;
    return day_from_ymd(y, m, d);
}

static const char *const MONTH_ABBR[] = { "jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez" };

Day bill_date_in(const char *s, Day today) {
    init_regex();
    g_autofree char *f = fold(s);
    GMatchInfo *a = NULL, *b = NULL;
    gboolean ha = g_regex_match(re_num_date, f, 0, &a), hb = g_regex_match(re_name_date, f, 0, &b);
    gint as = G_MAXINT, bs = G_MAXINT, e;
    if (ha) g_match_info_fetch_pos(a, 0, &as, &e);
    if (hb) g_match_info_fetch_pos(b, 0, &bs, &e);
    Day r = DAY_NONE;
    if (ha && as <= bs) {
        g_autofree char *dd = g_match_info_fetch(a, 1), *mm = g_match_info_fetch(a, 2), *yy = g_match_info_fetch(a, 3);
        int y = atoi(yy); if (y < 100) y += 2000;
        r = safe_date(y, atoi(mm), atoi(dd));
    } else if (hb) {
        g_autofree char *dd = g_match_info_fetch(b, 1), *mon = g_match_info_fetch(b, 2), *yy = g_match_info_fetch(b, 3);
        int m = 0;
        for (int i = 0; i < 12; i++) if (strcmp(mon, MONTH_ABBR[i]) == 0) m = i + 1;
        int d = atoi(dd);
        if (yy && *yy) r = safe_date(atoi(yy), m, d);
        else {
            int y0 = day_year(today);
            for (int y = y0 - 1; y <= y0 + 1; y++) { Day c = safe_date(y, m, d); if (c != DAY_NONE && c >= today - 60) { r = c; break; } }
        }
    }
    g_match_info_free(a); g_match_info_free(b);
    return r;
}

static const char *const DUE_LABELS[] = { "data de vencimento", "vencimento", "vence em", "pague ate", "pagar ate", "pagamento ate" };

static void find_due(char **lines, char **norm, guint n, Day today, BillInfo *out) {
    for (guint i = 0; i < n; i++) {
        const char *l = norm[i];
        for (gsize k = 0; k < G_N_ELEMENTS(DUE_LABELS); k++) {
            const char *hit = strstr(l, DUE_LABELS[k]);
            if (!hit) continue;
            gsize at = (gsize)(hit - l), len = strlen(DUE_LABELS[k]);
            /* "Próximo vencimento", "vencimento da fatura anterior": é de outra fatura */
            gsize b25 = back25(l, at);
            g_autofree char *before = g_strndup(l + b25, at - b25);
            if (strstr(before, "proxim") || strstr(before, "anterior") || g_str_has_prefix(hit + len, " da fatura anterior")) break;
            Day d = bill_date_in(hit + len, today);
            if (d != DAY_NONE) {
                out->has_due = TRUE; out->due = d; out->due_src = BILL_SRC_TEXT; out->due_excerpt = shorten(lines[i]);
                return;
            }
            for (guint j = 1; j <= 2 && i + j < n; j++) {
                if (strstr(norm[i + j], "proxim")) break;
                d = bill_date_in(lines[i + j], today);
                if (d != DAY_NONE) {
                    g_autofree char *ex = g_strconcat(lines[i], " ", lines[i + j], NULL);
                    out->has_due = TRUE; out->due = d; out->due_src = BILL_SRC_TEXT; out->due_excerpt = shorten(ex);
                    return;
                }
            }
            break;
        }
    }
}

void bill_from_text(const char *text, Day today, BillInfo *out) {
    init_regex();
    bill_info_clear(out);
    g_autofree char *valid = g_utf8_make_valid(text ? text : "", -1);
    /* linhas limpas (sem espaços repetidos), sem as vazias */
    GPtrArray *lines = g_ptr_array_new_with_free_func(g_free), *norm = g_ptr_array_new_with_free_func(g_free);
    char **raw = g_strsplit(valid, "\n", -1);
    for (char **p = raw; *p; p++) {
        GString *g = g_string_new(NULL);
        gboolean sp = FALSE;
        for (const char *c = *p; *c; c = g_utf8_next_char(c)) {
            gunichar u = g_utf8_get_char(c);
            if (u == ' ' || u == '\t' || u == 0xA0 || u == '\r') { sp = TRUE; continue; }
            if (sp && g->len) g_string_append_c(g, ' ');
            sp = FALSE;
            g_string_append_unichar(g, u);
        }
        if (g->len) { g_ptr_array_add(norm, fold(g->str)); g_ptr_array_add(lines, g_string_free(g, FALSE)); }
        else g_string_free(g, TRUE);
    }
    g_strfreev(raw);

    BillInfo boleto = { .due = DAY_NONE }, pix = { .due = DAY_NONE }, txt = { .due = DAY_NONE };
    gboolean hb = find_boleto(valid, today, &boleto);
    gboolean hp = !(hb && boleto.has_amount) && find_pix(valid, &pix);
    find_amount((char **)lines->pdata, (char **)norm->pdata, lines->len, &txt);
    find_due((char **)lines->pdata, (char **)norm->pdata, lines->len, today, &txt);

    /* valor: boleto > Pix > texto; vencimento: boleto > texto */
    const BillInfo *src = hb && boleto.has_amount ? &boleto : hp && pix.has_amount ? &pix : txt.has_amount ? &txt : NULL;
    if (src) { out->has_amount = TRUE; out->amount = src->amount; out->amount_src = src->amount_src; out->amount_excerpt = g_strdup(src->amount_excerpt); }
    const BillInfo *dsrc = hb && boleto.has_due ? &boleto : txt.has_due ? &txt : NULL;
    if (dsrc) { out->has_due = TRUE; out->due = dsrc->due; out->due_src = dsrc->due_src; out->due_excerpt = g_strdup(dsrc->due_excerpt); }
    out->payee = g_strdup(hp && pix.payee ? pix.payee : hb ? boleto.payee : NULL);
    out->segment = g_strdup(hb ? boleto.segment : NULL);
    out->dynamic_pix = hp && pix.dynamic_pix && !out->has_amount;

    bill_info_clear(&boleto); bill_info_clear(&pix); bill_info_clear(&txt);
    g_ptr_array_unref(lines); g_ptr_array_unref(norm);
}

/* ------------------------------------------------------------------------------- fatura do cartão */
Ym bill_invoice_ym(const AppState *s, const Card *c, Day due, Day today) {
    if (due != DAY_NONE) {
        Ym target = day_ym(due);
        for (Ym ym = target; ym >= target - 2; ym--) if (day_ym(invoice_due(c, ym)) == target) return ym;
    }
    CardStatus st;
    card_status(s, c, today, &st);
    const Invoice *cur = card_status_current(&st);
    Ym r = cur ? cur->ym : invoice_ym(c, today);
    card_status_clear(&st);
    return r;
}

Cents bill_app_total(const AppState *s, const Card *c, Ym ym) {
    Cents t = 0;
    for (guint i = 0; i < s->txs->len; i++) {
        const Tx *x = g_ptr_array_index(s->txs, i);
        if (strcmp(x->card_id, c->id) == 0 && x->kind == KIND_EXPENSE && invoice_ym(c, x->date) == ym) t += x->value;
    }
    return t;
}

Day bill_date_in_invoice(const Card *c, Ym ym) { return invoice_close(c, ym); }
