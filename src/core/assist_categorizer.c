/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Sugere a categoria de um lançamento a partir da descrição, em três etapas
 * (a primeira que responder vence):
 *
 * 1. Mesma descrição — o usuário já lançou algo com a mesma descrição: usa a categoria escolhida.
 * 2. Aprendizado — classificador Naive Bayes treinado só com os lançamentos do próprio usuário
 *    (palavras da descrição → categoria). Só sugere com confiança ≥ CAT_MIN_CONFIDENCE.
 * 3. Dicionário — termos conhecidos (ifood, sabesp, farmácia…) do arquivo aberto dicionario.txt.
 *
 * Nada é guardado à parte: o "aprendizado" é recalculado a partir dos lançamentos que já existem.
 * Apagar ou corrigir um lançamento corrige o aprendizado.
 */
#include "assist.h"
#include <math.h>
#include <string.h>

typedef struct { int count; Day last; } Seen;

struct Categorizer {
    Kind kind;
    GPtrArray *categories;   /* char* (cópias) */
    const Dictionary *dict;
    GHashTable *exact;       /* chave → (GHashTable categoria → Seen*) */
    GHashTable *cat_docs;    /* categoria → int */
    GHashTable *word_docs;   /* palavra → (GHashTable categoria → int) */
    GHashTable *cat_words;   /* categoria → int */
    GPtrArray *cat_order;    /* categorias com documentos, na ordem em que apareceram */
    int docs;
};

static GHashTable *str_int_table(void) { return g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL); }
static int get_int(GHashTable *t, const char *k) { return GPOINTER_TO_INT(g_hash_table_lookup(t, k)); }
static void add_int(GHashTable *t, const char *k, int v) { g_hash_table_insert(t, g_strdup(k), GINT_TO_POINTER(get_int(t, k) + v)); }

static gboolean in_list(GPtrArray *a, const char *s) {
    for (guint i = 0; i < a->len; i++) if (strcmp(a->pdata[i], s) == 0) return TRUE;
    return FALSE;
}

/* palavras sem repetição, na ordem */
static GPtrArray *distinct(char **toks) {
    GPtrArray *o = g_ptr_array_new();
    for (char **t = toks; *t; t++) if (!in_list(o, *t)) g_ptr_array_add(o, *t);
    return o;
}

static char *join(GPtrArray *a) {
    GString *s = g_string_new(NULL);
    for (guint i = 0; i < a->len; i++) { if (i) g_string_append_c(s, ' '); g_string_append(s, a->pdata[i]); }
    return g_string_free(s, FALSE);
}

Categorizer *categorizer_build(const AppState *s, Kind kind, const Dictionary *dict) {
    Categorizer *c = g_new0(Categorizer, 1);
    c->kind = kind;
    c->dict = dict;
    c->categories = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < s->cats[kind]->len; i++) g_ptr_array_add(c->categories, g_strdup(s->cats[kind]->pdata[i]));
    c->exact = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_hash_table_unref);
    c->cat_docs = str_int_table();
    c->word_docs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_hash_table_unref);
    c->cat_words = str_int_table();
    c->cat_order = g_ptr_array_new_with_free_func(g_free);

    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        /* treino: mesmo tipo, com descrição, em categoria que ainda existe, sem pagamento de fatura */
        if (t->kind != kind || !tx_is_flow(t) || !in_list(c->categories, t->category) || strcmp(t->desc, "Sem descrição") == 0)
            continue;
        g_autofree char *clean = text_clean_parcel(t->desc);
        g_auto(GStrv) toks = text_tokens(clean);
        if (!toks[0]) continue;
        c->docs++;
        g_autoptr(GPtrArray) dist = distinct(toks);
        g_autofree char *k = join(dist);
        GHashTable *m = g_hash_table_lookup(c->exact, k);
        if (!m) {
            m = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
            g_hash_table_insert(c->exact, g_strdup(k), m);
        }
        Seen *sn = g_hash_table_lookup(m, t->category);
        if (!sn) { sn = g_new0(Seen, 1); sn->last = INT32_MIN; g_hash_table_insert(m, g_strdup(t->category), sn); }
        sn->count++;
        if (t->date > sn->last) sn->last = t->date;
        if (!g_hash_table_contains(c->cat_docs, t->category)) g_ptr_array_add(c->cat_order, g_strdup(t->category));
        add_int(c->cat_docs, t->category, 1);
        add_int(c->cat_words, t->category, (int)g_strv_length(toks));
        for (guint w = 0; w < dist->len; w++) {
            GHashTable *wm = g_hash_table_lookup(c->word_docs, dist->pdata[w]);
            if (!wm) { wm = str_int_table(); g_hash_table_insert(c->word_docs, g_strdup(dist->pdata[w]), wm); }
            add_int(wm, t->category, 1);
        }
    }
    return c;
}

void categorizer_free(Categorizer *c) {
    if (!c) return;
    g_ptr_array_unref(c->categories);
    g_hash_table_unref(c->exact);
    g_hash_table_unref(c->cat_docs);
    g_hash_table_unref(c->word_docs);
    g_hash_table_unref(c->cat_words);
    g_ptr_array_unref(c->cat_order);
    g_free(c);
}

int categorizer_training_size(const Categorizer *c) { return c->docs; }

void suggestion_free(Suggestion *s) {
    if (!s) return;
    g_free(s->category);
    g_free(s->why);
    g_free(s);
}

static Suggestion *suggestion_new(const char *cat, SuggestSource src, char *why, double conf) {
    Suggestion *s = g_new0(Suggestion, 1);
    s->category = g_strdup(cat);
    s->source = src;
    s->why = why;
    s->confidence = conf;
    return s;
}

static Suggestion *same_description(const Categorizer *c, GPtrArray *dist) {
    g_autofree char *k = join(dist);
    GHashTable *m = g_hash_table_lookup(c->exact, k);
    if (!m) return NULL;
    int total = 0;
    const char *best = NULL;
    Seen *bs = NULL;
    GHashTableIter it;
    gpointer key, val;
    g_hash_table_iter_init(&it, m);
    while (g_hash_table_iter_next(&it, &key, &val)) {
        Seen *sn = val;
        total += sn->count;
        /* mais vezes; empate: a usada mais recentemente; ainda empatado: ordem alfabética (estável) */
        if (!bs || sn->count > bs->count || (sn->count == bs->count && sn->last > bs->last) ||
            (sn->count == bs->count && sn->last == bs->last && strcmp(key, best) < 0)) {
            bs = sn;
            best = key;
        }
    }
    double share = (double)bs->count / total;
    if (share < 0.6) return NULL; /* a mesma descrição já foi usada em categorias diferentes: não decide */
    int n = bs->count;
    g_autofree char *times = n == 1 ? g_strdup("1 vez") : g_strdup_printf("%d vezes", n);
    g_autofree char *extra = total > n ? g_strdup_printf(" (e %d vez(es) em outra categoria).", total - n) : g_strdup(".");
    char *why = g_strdup_printf("Você já lançou esta descrição %s como %s%s", times, best, extra);
    return suggestion_new(best, SOURCE_SAME_DESCRIPTION, why, share);
}

static Suggestion *learned(const Categorizer *c, GPtrArray *dist) {
    guint ncat = c->cat_order->len;
    if (c->docs < CAT_MIN_DOCS || ncat < 2) return NULL;
    g_autoptr(GPtrArray) known = g_ptr_array_new();
    for (guint i = 0; i < dist->len; i++) if (g_hash_table_contains(c->word_docs, dist->pdata[i])) g_ptr_array_add(known, dist->pdata[i]);
    if (!known->len) return NULL;
    double vocab = g_hash_table_size(c->word_docs);
    double k = ncat;
    double *scores = g_new(double, ncat);
    double max = -INFINITY;
    /* log P(c) + Σ log P(palavra | c). Suavização leve (ALPHA) porque descrições são curtas;
     * palavras nunca vistas são ignoradas (não indicam nada). */
    for (guint i = 0; i < ncat; i++) {
        const char *cat = c->cat_order->pdata[i];
        double sc = log((get_int(c->cat_docs, cat) + 1.0) / (c->docs + k));
        for (guint w = 0; w < known->len; w++) {
            GHashTable *wm = g_hash_table_lookup(c->word_docs, known->pdata[w]);
            sc += log((get_int(wm, cat) + CAT_ALPHA) / (get_int(c->cat_words, cat) + CAT_ALPHA * vocab));
        }
        scores[i] = sc;
        if (sc > max) max = sc;
    }
    double sum = 0;
    guint bi = 0;
    for (guint i = 0; i < ncat; i++) {
        sum += exp(scores[i] - max);
        if (scores[i] > scores[bi]) bi = i;
    }
    double p = exp(scores[bi] - max) / sum;
    g_free(scores);
    if (p < CAT_MIN_CONFIDENCE) return NULL;
    const char *cat = c->cat_order->pdata[bi];
    /* palavra que mais sustenta a decisão */
    const char *word = NULL;
    int wd = -1;
    for (guint w = 0; w < known->len; w++) {
        int n = get_int(g_hash_table_lookup(c->word_docs, known->pdata[w]), cat);
        if (n > wd) { wd = n; word = known->pdata[w]; }
    }
    if (wd < CAT_MIN_WORD_DOCS) return NULL;
    GHashTable *wm = g_hash_table_lookup(c->word_docs, word);
    int total_w = 0;
    GHashTableIter it;
    gpointer key, val;
    g_hash_table_iter_init(&it, wm);
    while (g_hash_table_iter_next(&it, &key, &val)) total_w += GPOINTER_TO_INT(val);
    char pct[24];
    g_autofree char *of = total_w > wd ? g_strdup_printf(" (de %d com essa palavra)", total_w) : g_strdup("");
    char *why = g_strdup_printf("A palavra “%s” aparece em %d lançamento(s) seus de %s%s. Confiança: %s.", word, wd, cat, of, br_pct(p * 100, pct));
    return suggestion_new(cat, SOURCE_LEARNED, why, p);
}

static Suggestion *dictionary(const Categorizer *c, const char *desc) {
    DictMatch *m = dictionary_match(c->dict, desc, c->kind, c->categories);
    if (!m) return NULL;
    GString *terms = g_string_new(NULL);
    for (char **t = m->terms; *t; t++) {
        if (terms->len) g_string_append(terms, ", ");
        g_string_append_printf(terms, "“%s”", *t);
    }
    char *why = g_strdup_printf("%s está no dicionário aberto do assistente, na seção de %s (linha %d de dicionario.txt).",
                                terms->str, m->category, m->section->line);
    g_string_free(terms, TRUE);
    Suggestion *s = suggestion_new(m->category, SOURCE_DICTIONARY, why, 0.6);
    dict_match_free(m);
    return s;
}

Suggestion *categorizer_suggest(const Categorizer *c, const char *desc) {
    g_autofree char *clean = text_clean_parcel(desc ? desc : "");
    g_auto(GStrv) toks = text_tokens(clean);
    if (!toks[0]) return NULL;
    g_autoptr(GPtrArray) dist = distinct(toks);
    Suggestion *s = same_description(c, dist);
    if (!s) s = learned(c, dist);
    if (!s) s = dictionary(c, desc);
    return s;
}

static void learned_category_free(LearnedCategory *l) {
    for (guint i = 0; i < l->words->len; i++) g_free(g_array_index(l->words, LearnedWord, i).word);
    g_array_unref(l->words);
    g_free(l->category);
    g_free(l);
}

static gint cmp_lw(gconstpointer a, gconstpointer b) {
    const LearnedWord *x = a, *y = b;
    if (x->count != y->count) return y->count - x->count;
    return strcmp(x->word, y->word);
}

GPtrArray *categorizer_learned_words(const Categorizer *c, int per_category) {
    GPtrArray *out = g_ptr_array_new_with_free_func((GDestroyNotify)learned_category_free);
    for (guint i = 0; i < c->categories->len; i++) {
        const char *cat = c->categories->pdata[i];
        GArray *words = g_array_new(FALSE, FALSE, sizeof(LearnedWord));
        GHashTableIter it;
        gpointer key, val;
        g_hash_table_iter_init(&it, c->word_docs);
        while (g_hash_table_iter_next(&it, &key, &val)) {
            int n = get_int(val, cat);
            if (n >= CAT_MIN_WORD_DOCS) { LearnedWord w = {g_strdup(key), n}; g_array_append_val(words, w); }
        }
        g_array_sort(words, cmp_lw);
        while ((int)words->len > per_category) {
            g_free(g_array_index(words, LearnedWord, words->len - 1).word);
            g_array_remove_index(words, words->len - 1);
        }
        if (!words->len) { g_array_unref(words); continue; }
        LearnedCategory *l = g_new0(LearnedCategory, 1);
        l->category = g_strdup(cat);
        l->words = words;
        g_ptr_array_add(out, l);
    }
    return out;
}
