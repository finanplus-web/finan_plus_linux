/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Dicionário inicial de palavras → categoria (arquivo aberto "dicionario.txt").
 *
 *   # comentário
 *   [despesa: Alimentação | Mercado | Comida]
 *   ifood, rappi, supermercado, pao de acucar
 *
 * Os nomes depois de "despesa:"/"receita:" são alternativas: vale o PRIMEIRO que existir na lista
 * de categorias do usuário (sem diferenciar acento e maiúscula). Se nenhum existir, a seção é
 * ignorada: o assistente nunca sugere uma categoria que o usuário não tem.
 */
#include "assist.h"
#include <string.h>

static void section_free(DictSection *s) {
    g_strfreev(s->names);
    g_strfreev(s->terms);
    g_free(s);
}

void dictionary_free(Dictionary *d) {
    if (!d) return;
    g_ptr_array_unref(d->sections);
    g_free(d);
}

static gboolean strv_has(GPtrArray *a, const char *s) {
    for (guint i = 0; i < a->len; i++) if (strcmp(a->pdata[i], s) == 0) return TRUE;
    return FALSE;
}

/* "[despesa: A | B]" → kind e nomes */
static gboolean parse_header(const char *l, Kind *kind, char ***names) {
    size_t n = strlen(l);
    if (n < 4 || l[0] != '[' || l[n - 1] != ']') return FALSE;
    g_autofree char *inner = g_strndup(l + 1, n - 2);
    char *colon = strchr(inner, ':');
    if (!colon) return FALSE;
    *colon = '\0';
    g_autofree char *k = g_ascii_strdown(g_strstrip(inner), -1);
    if (strcmp(k, "despesa") == 0) *kind = KIND_EXPENSE;
    else if (strcmp(k, "receita") == 0) *kind = KIND_INCOME;
    else return FALSE;
    char *rest = g_strstrip(colon + 1);
    if (!*rest) return FALSE;
    g_auto(GStrv) parts = g_strsplit(rest, "|", -1);
    GPtrArray *out = g_ptr_array_new();
    for (char **p = parts; *p; p++) {
        char *t = g_strstrip(*p);
        if (*t) g_ptr_array_add(out, g_strdup(t));
    }
    g_ptr_array_add(out, NULL);
    *names = (char **)g_ptr_array_free(out, FALSE);
    return TRUE;
}

Dictionary *dictionary_parse(const char *text) {
    Dictionary *d = g_new0(Dictionary, 1);
    d->sections = g_ptr_array_new_with_free_func((GDestroyNotify)section_free);
    g_auto(GStrv) lines = g_strsplit(text ? text : "", "\n", -1);
    gboolean have = FALSE;
    Kind kind = KIND_EXPENSE;
    char **names = NULL;
    GPtrArray *terms = NULL;
    int line0 = 0;

#define FLUSH()                                                                          \
    do {                                                                                 \
        if (have && terms && terms->len) {                                               \
            DictSection *s = g_new0(DictSection, 1);                                     \
            s->kind = kind; s->names = g_strdupv(names); s->line = line0;                \
            g_ptr_array_add(terms, NULL);                                                \
            s->terms = (char **)g_ptr_array_free(terms, FALSE);                          \
            terms = NULL;                                                                \
            g_ptr_array_add(d->sections, s);                                             \
        }                                                                                \
    } while (0)

    for (int i = 0; lines[i]; i++) {
        char *hash = strchr(lines[i], '#');
        if (hash) *hash = '\0';
        char *l = g_strstrip(lines[i]);
        if (!*l) continue;
        Kind k;
        char **nm = NULL;
        if (parse_header(l, &k, &nm)) {
            FLUSH();
            if (terms) g_ptr_array_free(terms, TRUE);
            g_strfreev(names);
            have = TRUE; kind = k; names = nm; line0 = i + 1;
            terms = g_ptr_array_new_with_free_func(g_free);
        } else if (have) {
            if (!terms) terms = g_ptr_array_new_with_free_func(g_free);
            g_auto(GStrv) parts = g_strsplit(l, ",", -1);
            for (char **p = parts; *p; p++) {
                char *f = text_fold(*p);
                if (strlen(f) >= 2 && !strv_has(terms, f)) g_ptr_array_add(terms, f);
                else g_free(f);
            }
        }
    }
    FLUSH();
#undef FLUSH
    if (terms) g_ptr_array_free(terms, TRUE);
    g_strfreev(names);
    return d;
}

void dict_match_free(DictMatch *m) {
    if (!m) return;
    g_free(m->category);
    g_strfreev(m->terms);
    g_free(m);
}

typedef struct {
    const char *category;
    GPtrArray *terms; /* const char* (do dicionário) */
    const DictSection *section;
    int weight;
} Acc;

static int term_weight(const char *t) {
    int w = 1;
    for (; *t; t++) if (*t == ' ') w++;
    return w;
}

DictMatch *dictionary_match(const Dictionary *d, const char *desc, Kind kind, GPtrArray *categories) {
    if (!d) return NULL;
    g_autofree char *f = text_fold(desc);
    g_autofree char *padded = g_strdup_printf(" %s ", f);
    g_auto(GStrv) words = g_strsplit(f, " ", -1);
    GArray *acc = g_array_new(FALSE, TRUE, sizeof(Acc));

    for (guint i = 0; i < d->sections->len; i++) {
        const DictSection *s = d->sections->pdata[i];
        if (s->kind != kind) continue;
        const char *cat = NULL;
        for (char **n = s->names; *n && !cat; n++)
            for (guint j = 0; j < categories->len && !cat; j++)
                if (text_same(categories->pdata[j], *n)) cat = categories->pdata[j];
        if (!cat) continue;
        for (char **t = s->terms; *t; t++) {
            g_autofree char *needle = g_strdup_printf(" %s ", *t);
            gboolean hit = strstr(padded, needle) != NULL;
            size_t tl = strlen(*t);
            if (!hit && tl >= 5 && !strchr(*t, ' ')) {
                /* plural/variação simples: "supermercados" casa com "supermercado" */
                for (char **w = words; *w && !hit; w++)
                    if (g_str_has_prefix(*w, *t) && strlen(*w) - tl <= 2) hit = TRUE;
            }
            if (!hit) continue;
            /* várias seções podem apontar para a mesma categoria do usuário: soma os termos */
            Acc *a = NULL;
            for (guint k = 0; k < acc->len; k++)
                if (g_array_index(acc, Acc, k).category == cat) { a = &g_array_index(acc, Acc, k); break; }
            if (!a) {
                Acc n = {cat, g_ptr_array_new(), s, 0};
                g_array_append_val(acc, n);
                a = &g_array_index(acc, Acc, acc->len - 1);
            }
            gboolean dup = FALSE;
            for (guint k = 0; k < a->terms->len; k++) if (strcmp(a->terms->pdata[k], *t) == 0) dup = TRUE;
            if (!dup) { g_ptr_array_add(a->terms, *t); a->weight += term_weight(*t); }
        }
    }
    DictMatch *m = NULL;
    if (acc->len) {
        /* maior peso vence; empate entre as duas primeiras = ambíguo = sem sugestão */
        guint best = 0;
        for (guint k = 1; k < acc->len; k++)
            if (g_array_index(acc, Acc, k).weight > g_array_index(acc, Acc, best).weight) best = k;
        int bw = g_array_index(acc, Acc, best).weight;
        gboolean tie = FALSE;
        for (guint k = 0; k < acc->len; k++) if (k != best && g_array_index(acc, Acc, k).weight == bw) tie = TRUE;
        if (!tie) {
            Acc *a = &g_array_index(acc, Acc, best);
            m = g_new0(DictMatch, 1);
            m->category = g_strdup(a->category);
            m->section = a->section;
            GPtrArray *t = g_ptr_array_new();
            for (guint k = 0; k < a->terms->len; k++) g_ptr_array_add(t, g_strdup(a->terms->pdata[k]));
            g_ptr_array_add(t, NULL);
            m->terms = (char **)g_ptr_array_free(t, FALSE);
        }
    }
    for (guint k = 0; k < acc->len; k++) g_ptr_array_unref(g_array_index(acc, Acc, k).terms);
    g_array_unref(acc);
    return m;
}
