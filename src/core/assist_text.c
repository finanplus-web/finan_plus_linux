/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Normalização de texto usada por todo o assistente.
 * "PAG*Uber  Trip (2/3)" → fold = "pag uber trip 2 3" → tokens = [uber, trip]
 */
#include "assist.h"
#include <math.h>
#include <string.h>

/* Palavras sem significado para classificar (artigos, preposições e "ruído" de extrato bancário). */
static const char *const STOP[] = {
    /* português */
    "a", "o", "as", "os", "um", "uma", "de", "da", "do", "das", "dos", "em", "no", "na", "nos", "nas",
    "e", "com", "para", "pra", "por", "pelo", "pela", "ao", "aos", "meu", "minha",
    /* extrato / maquininha */
    "pag", "pagto", "compra", "compras", "cp", "deb", "debito", "cred", "credito", "cartao", "parcela",
    "ltda", "sa", "me", "eireli", "epp", "br", "www", "sem", "descricao",
};

gboolean text_is_stop(const char *w) {
    for (guint i = 0; i < G_N_ELEMENTS(STOP); i++) if (strcmp(STOP[i], w) == 0) return TRUE;
    return FALSE;
}

char *text_fold(const char *s) {
    if (!s) return g_strdup("");
    g_autofree char *valid = g_utf8_make_valid(s, -1);
    g_autofree char *nfd = g_utf8_normalize(valid, -1, G_NORMALIZE_NFD);
    if (!nfd) return g_strdup("");
    GString *out = g_string_new(NULL);
    gboolean space = FALSE;
    for (const char *p = nfd; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (g_unichar_type(c) == G_UNICODE_NON_SPACING_MARK) continue;
        gunichar l = g_unichar_tolower(c);
        if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9')) {
            if (space && out->len) g_string_append_c(out, ' ');
            space = FALSE;
            g_string_append_c(out, (char)l);
        } else space = TRUE;
    }
    return g_string_free(out, FALSE);
}

static gboolean all_digits(const char *w) {
    for (; *w; w++) if (!g_ascii_isdigit(*w)) return FALSE;
    return TRUE;
}

char **text_tokens(const char *s) {
    g_autofree char *f = text_fold(s);
    g_auto(GStrv) parts = g_strsplit(f, " ", -1);
    GPtrArray *out = g_ptr_array_new();
    for (char **p = parts; *p; p++)
        if (strlen(*p) >= 2 && !all_digits(*p) && !text_is_stop(*p)) g_ptr_array_add(out, g_strdup(*p));
    g_ptr_array_add(out, NULL);
    return (char **)g_ptr_array_free(out, FALSE);
}

char *text_key(const char *s) {
    g_auto(GStrv) t = text_tokens(s);
    GString *out = g_string_new(NULL);
    for (int i = 0; t[i]; i++) {
        gboolean dup = FALSE;
        for (int j = 0; j < i; j++) if (strcmp(t[i], t[j]) == 0) { dup = TRUE; break; }
        if (dup) continue;
        if (out->len) g_string_append_c(out, ' ');
        g_string_append(out, t[i]);
    }
    return g_string_free(out, FALSE);
}

gboolean text_same(const char *a, const char *b) {
    g_autofree char *x = text_fold(a);
    g_autofree char *y = text_fold(b);
    return strcmp(x, y) == 0;
}

char *text_clean_parcel(const char *desc) {
    /* remove \s*\(\d+/\d+\)\s*$ */
    gssize n = (gssize)strlen(desc);
    gssize e = n;
    while (e > 0 && g_ascii_isspace(desc[e - 1])) e--;
    if (e == 0 || desc[e - 1] != ')') return g_strdup(desc);
    gssize i = e - 2, d2 = 0;
    while (i >= 0 && g_ascii_isdigit(desc[i])) { i--; d2++; }
    if (d2 == 0 || i < 0 || desc[i] != '/') return g_strdup(desc);
    i--;
    gssize d1 = 0;
    while (i >= 0 && g_ascii_isdigit(desc[i])) { i--; d1++; }
    if (d1 == 0 || i < 0 || desc[i] != '(') return g_strdup(desc);
    while (i > 0 && g_ascii_isspace(desc[i - 1])) i--;
    return g_strndup(desc, (gsize)i);
}

char *br_pct(double v, char *buf) {
    g_snprintf(buf, 24, "%" G_GINT64_FORMAT "%%", (gint64)floor(fabs(v) + 0.5));
    return buf;
}

char *br_plural(int n, const char *one, const char *many) {
    return n == 1 ? g_strdup_printf("1 %s", one) : g_strdup_printf("%d %s", n, many);
}
