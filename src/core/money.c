/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Dinheiro em centavos (int64). Formatação e leitura próprias, em português,
 * sem depender do idioma do sistema.
 */
#include "model.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

char *money_format(Cents c, char *buf) {
    gboolean neg = c < 0;
    guint64 a = neg ? (guint64)(-(c + 1)) + 1 : (guint64)c;
    char reais[32];
    g_snprintf(reais, sizeof reais, "%" G_GUINT64_FORMAT, a / 100);
    size_t n = strlen(reais);
    char grouped[48];
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (i > 0 && (n - i) % 3 == 0) grouped[j++] = '.';
        grouped[j++] = reais[i];
    }
    grouped[j] = '\0';
    g_snprintf(buf, 32, "%sR$ %s,%02u", neg ? "-" : "", grouped, (unsigned)(a % 100));
    return buf;
}

char *money_fmt(Cents c) {
    char b[32];
    return g_strdup(money_format(c, b));
}

char *money_fmt_hidden(Cents c) {
    (void)c;
    return g_strdup("R$ ••••");
}

char *money_input(Cents c, char *buf) {
    gboolean neg = c < 0;
    guint64 a = neg ? (guint64)(-(c + 1)) + 1 : (guint64)c;
    g_snprintf(buf, 32, "%s%" G_GUINT64_FORMAT ",%02u", neg ? "-" : "", a / 100, (unsigned)(a % 100));
    return buf;
}

char *money_reais_json(Cents c, char *buf) {
    gboolean neg = c < 0;
    guint64 a = neg ? (guint64)(-(c + 1)) + 1 : (guint64)c;
    g_snprintf(buf, 32, "%s%" G_GUINT64_FORMAT ".%02u", neg ? "-" : "", a / 100, (unsigned)(a % 100));
    return buf;
}

Cents money_from_reais(double d) {
    if (!isfinite(d)) return 0;
    double x = floor(d * 100.0 + 0.5);
    if (x > 9.0e15) x = 9.0e15;
    if (x < -9.0e15) x = -9.0e15;
    return (Cents)x;
}

static int count_char(const char *s, char c) {
    int n = 0;
    for (; *s; s++) if (*s == c) n++;
    return n;
}

/* "1.234.567" (grupos de milhar com ponto, sem decimais) */
static gboolean thousands_with_dots(const char *s) {
    const char *p = s;
    int lead = 0;
    while (g_ascii_isdigit(*p)) { lead++; p++; }
    if (lead < 1 || lead > 3 || *p != '.') return FALSE;
    while (*p == '.') {
        p++;
        for (int i = 0; i < 3; i++, p++) if (!g_ascii_isdigit(*p)) return FALSE;
    }
    return *p == '\0';
}

static void remove_char(char *s, char c) {
    char *w = s;
    for (char *r = s; *r; r++) if (*r != c) *w++ = *r;
    *w = '\0';
}

gboolean money_parse(const char *input, Cents *out) {
    if (!input) return FALSE;
    /* tira "R$" e espaços (inclusive o espaço não separável) */
    GString *b = g_string_new(NULL);
    for (const char *p = input; *p;) {
        if (p[0] == 'R' && p[1] == '$') { p += 2; continue; }
        gunichar ch = g_utf8_get_char_validated(p, -1);
        if (ch == (gunichar)-1 || ch == (gunichar)-2) { g_string_free(b, TRUE); return FALSE; }
        if (!g_unichar_isspace(ch) && ch != 0x00A0 && ch != 0x202F) g_string_append_unichar(b, ch);
        p = g_utf8_next_char(p);
    }
    g_autofree char *s0 = g_string_free(b, FALSE);
    char *s = s0;
    if (!*s) return FALSE;
    gboolean neg = *s == '-';
    if (neg || *s == '+') s++;
    if (!*s) return FALSE;
    for (char *p = s; *p; p++) if (!g_ascii_isdigit(*p) && *p != '.' && *p != ',') return FALSE;

    char *lc = strrchr(s, ','), *ld = strrchr(s, '.');
    if (lc && ld) {
        char dec = lc > ld ? ',' : '.';
        char thou = dec == ',' ? '.' : ',';
        remove_char(s, thou);
        if (count_char(s, dec) > 1) return FALSE;
        for (char *p = s; *p; p++) if (*p == dec) *p = '.';
    } else if (lc) {
        if (count_char(s, ',') > 1) return FALSE;
        *lc = '.';
    } else if (ld && thousands_with_dots(s)) {
        remove_char(s, '.');
    }
    if (count_char(s, '.') > 1) return FALSE;

    char *dot = strchr(s, '.');
    const char *int_part = s;
    const char *frac = "";
    if (dot) { *dot = '\0'; frac = dot + 1; }
    if (dot && *frac == '\0' && *int_part == '\0') return FALSE;
    if (*int_part == '\0') int_part = "0";
    if (strlen(int_part) > 13) return FALSE;
    gint64 whole = g_ascii_strtoll(int_part, NULL, 10);
    /* arredonda a partir da 3ª casa, como o Finan+ web (Math.round) */
    char f3[4] = "000";
    for (int i = 0; i < 3 && frac[i]; i++) f3[i] = frac[i];
    int f = atoi(f3);
    Cents cents = whole * 100 + (f + 5) / 10;
    if (out) *out = neg ? -cents : cents;
    return TRUE;
}
