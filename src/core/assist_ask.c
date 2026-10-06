/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Perguntas rápidas em português, respondidas com cálculo exato sobre os lançamentos.
 * Não é um modelo de linguagem: é um interpretador de palavras-chave. Por isso a resposta
 * sempre mostra "Como entendi" — o usuário vê exatamente o que foi considerado.
 *
 * Exemplos: "quanto gastei com mercado em agosto?", "maior gasto da semana",
 * "quanto recebi este ano", "saldo do mês passado", "quantas vezes usei uber nos últimos 30 dias".
 */
#include "assist.h"
#include <string.h>
#include <stdlib.h>

const char *const ASK_EXAMPLES[6] = {
    "Quanto gastei este mês?",
    "Quanto gastei com mercado no mês passado?",
    "Maior gasto da semana",
    "Quanto recebi este ano?",
    "Saldo do mês passado",
    "Quantas vezes usei uber nos últimos 30 dias?",
};

static const char *const INTENT_LABEL[] = {"total", "maior lançamento", "quantidade", "média por dia", "saldo"};

static const char *const EXPENSE_W[] = {"gastei", "gasto", "gastos", "gastou", "gastamos", "despesa", "despesas", "paguei",
                                        "pagamos", "saiu", "sairam", "custou", "custaram", "gastar", NULL};
static const char *const INCOME_W[] = {"recebi", "recebemos", "receita", "receitas", "ganhei", "ganho", "ganhos", "entrou",
                                       "entraram", "entrada", "entradas", "renda", NULL};
static const char *const BALANCE_W[] = {"saldo", "sobrou", "sobra", "economizei", "guardei", "balanco", "lucro", NULL};
static const char *const MAX_W[] = {"maior", "maiores", "caro", "cara", NULL};
static const char *const COUNT_W[] = {"quantas", "quantos", "vezes", "frequencia", NULL};
static const char *const AVG_W[] = {"media", "medio", NULL};
static const char *const FILLER[] = {
    "quanto", "quanta", "qual", "quais", "foi", "foram", "eu", "nos", "meu", "minha", "meus", "minhas", "total", "valor", "lancamento",
    "lancamentos", "usei", "fiz", "tive", "tem", "teve", "ja", "ate", "agora", "mes", "ano", "semana", "dia", "dias", "ultimos", "ultimas",
    "ultimo", "ultima", "passado", "passada", "este", "esta", "esse", "essa", "neste", "nesta", "nesse", "nessa", "deste", "desta",
    "desse", "dessa", "hoje", "ontem", "por", "no", "na", "em", "de", "do", "da", "com", "o", "a", "os", "as", "que", "mais", "gasto",
    "atual", "corrente", "inteiro", "todo", "toda", "periodo", "compra", "compras", "vez", NULL,
};
static const struct { const char *w; int m; } MONTHS[] = {
    {"janeiro", 1}, {"jan", 1}, {"fevereiro", 2}, {"fev", 2}, {"marco", 3}, {"abril", 4}, {"abr", 4}, {"maio", 5},
    {"junho", 6}, {"jun", 6}, {"julho", 7}, {"jul", 7}, {"agosto", 8}, {"ago", 8}, {"setembro", 9}, {"set", 9},
    {"outubro", 10}, {"out", 10}, {"novembro", 11}, {"nov", 11}, {"dezembro", 12}, {"dez", 12},
};

static gboolean in_set(const char *const *set, const char *w) {
    for (; *set; set++) if (strcmp(*set, w) == 0) return TRUE;
    return FALSE;
}

static gboolean any_in(char **words, const char *const *set) {
    for (char **w = words; *w; w++) if (in_set(set, *w)) return TRUE;
    return FALSE;
}

static int month_of(const char *w) {
    for (guint i = 0; i < G_N_ELEMENTS(MONTHS); i++) if (strcmp(MONTHS[i].w, w) == 0) return MONTHS[i].m;
    return 0;
}

static gboolean all_digits(const char *w) {
    if (!*w) return FALSE;
    for (; *w; w++) if (!g_ascii_isdigit(*w)) return FALSE;
    return TRUE;
}

static void used_add(GHashTable *used, const char *w) { g_hash_table_add(used, g_strdup(w)); }

static void used_add_set(GHashTable *used, const char *const *set) { for (; *set; set++) used_add(used, *set); }

void ask_parsed_free(AskParsed *p) {
    if (!p) return;
    g_free(p->period_label);
    g_free(p->category);
    g_strfreev(p->words);
    g_strfreev(p->ignored);
    g_free(p);
}

static gboolean has(const char *f, const char *phrase) {
    g_autofree char *n = g_strdup_printf(" %s ", phrase);
    return strstr(f, n) != NULL;
}

static void period(char **w, Day today, GHashTable *used, AskParsed *p) {
    g_autofree char *joined = g_strjoinv(" ", w);
    g_autofree char *f = g_strdup_printf(" %s ", joined);
    Ym ym = day_ym(today);
    char a[11], b[11];

    /* "últimos N dias" */
    const char *u = strstr(f, " ultimos ");
    while (u) {
        const char *q = u + 9;
        int nd = 0;
        while (g_ascii_isdigit(q[nd])) nd++;
        if (nd >= 1 && nd <= 3 && g_str_has_prefix(q + nd, " dias ")) {
            g_autofree char *num = g_strndup(q, (gsize)nd);
            int n = CLAMP(atoi(num), 1, 366);
            used_add(used, num);
            p->from = today - (n - 1);
            p->to = today;
            p->period_label = g_strdup_printf("últimos %d dias", n);
            return;
        }
        u = strstr(u + 1, " ultimos ");
    }
    if (has(f, "hoje")) { p->from = p->to = today; p->period_label = g_strdup("hoje"); return; }
    if (has(f, "ontem")) {
        p->from = p->to = today - 1;
        p->period_label = g_strdup_printf("ontem (%s)", day_br(today - 1, a));
        return;
    }
    if (has(f, "semana passada") || has(f, "ultima semana")) {
        Day mon = today - (day_weekday(today) - 1) - 7;
        p->from = mon; p->to = mon + 6;
        p->period_label = g_strdup_printf("semana passada (%s a %s)", day_br_short(mon, a), day_br_short(mon + 6, b));
        return;
    }
    if (has(f, "semana")) {
        Day mon = today - (day_weekday(today) - 1);
        p->from = mon; p->to = today;
        p->period_label = g_strdup_printf("esta semana (desde %s)", day_br_short(mon, a));
        return;
    }
    if (has(f, "mes passado") || has(f, "ultimo mes")) {
        p->from = ym_first(ym - 1); p->to = ym_last(ym - 1);
        p->period_label = br_month_year(ym - 1);
        return;
    }
    if (has(f, "ano passado")) {
        int y = day_year(today) - 1;
        p->from = day_from_ymd(y, 1, 1); p->to = day_from_ymd(y, 12, 31);
        p->period_label = g_strdup_printf("ano de %d", y);
        return;
    }
    if (has(f, "este ano") || has(f, "esse ano") || has(f, "neste ano") || has(f, "nesse ano") || has(f, "ano atual") ||
        has(f, "deste ano") || has(f, "desse ano")) {
        int y = day_year(today);
        p->from = day_from_ymd(y, 1, 1); p->to = today;
        p->period_label = g_strdup_printf("este ano (%d)", y);
        return;
    }
    /* nome de mês, com ou sem ano ("agosto", "ago de 2025", "em março 2024") */
    for (int i = 0; w[i]; i++) {
        int m = month_of(w[i]);
        if (!m) continue;
        used_add(used, w[i]);
        int year = 0;
        for (int j = i + 1; j <= i + 2 && w[j - 1] && w[j]; j++) {
            if (strlen(w[j]) == 4 && all_digits(w[j])) { year = atoi(w[j]); used_add(used, w[j]); break; }
        }
        if (!year) year = m > day_month(today) ? day_year(today) - 1 : day_year(today);
        Ym t = ym_make(year, m);
        p->from = ym_first(t); p->to = ym_last(t);
        p->period_label = br_month_year(t);
        return;
    }
    /* ano solto ("em 2025") */
    for (int i = 0; w[i]; i++) {
        if (strlen(w[i]) == 4 && all_digits(w[i])) {
            int y = atoi(w[i]);
            if (y < 1990 || y > 2100) continue;
            used_add(used, w[i]);
            Day jan1 = day_from_ymd(y, 1, 1), dec31 = day_from_ymd(y, 12, 31);
            p->from = jan1;
            p->to = MIN(dec31, MAX(today, jan1));
            p->period_label = g_strdup_printf("ano de %d", y);
            return;
        }
    }
    p->from = ym_first(ym); p->to = ym_last(ym);
    g_autofree char *my = br_month_year(ym);
    p->period_label = g_strdup_printf("%s (este mês)", my);
}

AskParsed *ask_parse(const char *question, const AppState *s, Day today) {
    AskParsed *p = g_new0(AskParsed, 1);
    g_autofree char *f = text_fold(question);
    g_auto(GStrv) words = g_strsplit(f, " ", -1);
    /* remove vazios (texto vazio vira [""]) */
    {
        int j = 0;
        for (int i = 0; words[i]; i++) { if (*words[i]) words[j++] = words[i]; else g_free(words[i]); }
        words[j] = NULL;
    }
    g_autoptr(GHashTable) used = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    /* categoria: nome de categoria do usuário que aparece na pergunta (o mais longo vence) */
    g_autofree char *padded = g_strdup_printf(" %s ", f);
    const char *category = NULL;
    size_t cat_len = 0;
    gboolean has_cat_kind = FALSE;
    Kind cat_kind = KIND_EXPENSE;
    for (int k = 0; k < 2; k++)
        for (guint i = 0; i < s->cats[k]->len; i++) {
            const char *c = s->cats[k]->pdata[i];
            g_autofree char *fc = text_fold(c);
            if (!*fc || !has(padded, fc)) continue;
            if (!category || strlen(fc) > cat_len) { category = c; cat_len = strlen(fc); cat_kind = (Kind)k; has_cat_kind = TRUE; }
        }
    if (category) {
        g_autofree char *fc = text_fold(category);
        g_auto(GStrv) parts = g_strsplit(fc, " ", -1);
        for (char **x = parts; *x; x++) used_add(used, *x);
    }

    gboolean has_kind = TRUE;
    Kind kind = KIND_EXPENSE;
    if (any_in(words, INCOME_W)) kind = KIND_INCOME;
    else if (any_in(words, EXPENSE_W)) kind = KIND_EXPENSE;
    else if (has_cat_kind) kind = cat_kind;
    else has_kind = FALSE;

    if (any_in(words, BALANCE_W)) p->intent = INTENT_BALANCE;
    else if (any_in(words, AVG_W)) p->intent = INTENT_AVERAGE;
    else if (any_in(words, MAX_W)) p->intent = INTENT_MAX;
    else if (any_in(words, COUNT_W)) p->intent = INTENT_COUNT;
    else p->intent = INTENT_TOTAL;
    used_add_set(used, EXPENSE_W); used_add_set(used, INCOME_W); used_add_set(used, BALANCE_W);
    used_add_set(used, MAX_W); used_add_set(used, COUNT_W); used_add_set(used, AVG_W);

    period(words, today, used, p);

    /* palavras que sobraram viram filtro de descrição ("uber", "netflix"...), mas só as que existem
     * em algum lançamento: "pedi", "comprei" etc. não filtram nada e aparecem como ignoradas. */
    g_autoptr(GPtrArray) rest = g_ptr_array_new();
    for (char **w = words; *w; w++) {
        if (g_hash_table_contains(used, *w) || in_set(FILLER, *w) || text_is_stop(*w) || strlen(*w) < 2 || all_digits(*w)) continue;
        gboolean dup = FALSE;
        for (guint i = 0; i < rest->len; i++) if (strcmp(rest->pdata[i], *w) == 0) dup = TRUE;
        if (!dup) g_ptr_array_add(rest, *w);
    }
    GPtrArray *known = g_ptr_array_new(), *unknown = g_ptr_array_new();
    if (rest->len) {
        g_autoptr(GHashTable) vocab = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        for (guint i = 0; i < s->txs->len; i++) {
            Tx *t = s->txs->pdata[i];
            g_autofree char *dc = g_strdup_printf("%s %s", t->desc, t->category);
            g_autofree char *fd = text_fold(dc);
            g_auto(GStrv) parts = g_strsplit(fd, " ", -1);
            for (char **x = parts; *x; x++) if (**x) g_hash_table_add(vocab, g_strdup(*x));
        }
        for (guint i = 0; i < rest->len; i++) {
            const char *w = rest->pdata[i];
            gboolean found = g_hash_table_contains(vocab, w);
            if (!found) {
                GHashTableIter it;
                gpointer key;
                g_hash_table_iter_init(&it, vocab);
                while (!found && g_hash_table_iter_next(&it, &key, NULL)) found = g_str_has_prefix(key, w);
            }
            g_ptr_array_add(found ? known : unknown, g_strdup(w));
        }
    }
    g_ptr_array_add(known, NULL);
    g_ptr_array_add(unknown, NULL);
    p->words = (char **)g_ptr_array_free(known, FALSE);
    p->ignored = (char **)g_ptr_array_free(unknown, FALSE);
    p->category = g_strdup(category);
    if (p->intent == INTENT_BALANCE) p->has_kind = FALSE;
    else { p->has_kind = TRUE; p->kind = has_kind ? kind : KIND_EXPENSE; }
    return p;
}

void ask_answer_free(AskAnswer *a) {
    if (!a) return;
    g_free(a->text);
    g_free(a->understood);
    ask_parsed_free(a->parsed);
    g_ptr_array_unref(a->matches);
    g_free(a);
}

static gboolean word_match(const AskParsed *p, const Tx *t) {
    if (!p->words[0]) return TRUE;
    g_autofree char *dc = g_strdup_printf("%s %s", t->desc, t->category);
    g_autofree char *fd = text_fold(dc);
    g_autofree char *pd = g_strdup_printf(" %s ", fd);
    for (char **w = p->words; *w; w++) {
        g_autofree char *n = g_strdup_printf(" %s", *w);
        if (!strstr(pd, n)) return FALSE;
    }
    return TRUE;
}

AskAnswer *ask_answer(const char *question, const AppState *s, Day today, MoneyFmt money) {
    AskParsed *p = ask_parse(question, s, today);
    g_autoptr(GPtrArray) pool = g_ptr_array_new_with_free_func(g_free);
#define M(v) ((const char *)({ char *_m = money(v); g_ptr_array_add(pool, _m); _m; }))
    GPtrArray *done = g_ptr_array_new();
    g_autoptr(GPtrArray) pending = g_ptr_array_new();
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (!tx_is_flow(t) || (p->has_kind && t->kind != p->kind) || t->date < p->from || t->date > p->to) continue;
        if (p->category && strcmp(t->category, p->category) != 0) continue;
        if (!word_match(p, t)) continue;
        g_ptr_array_add(t->paid ? done : pending, t);
    }
    const char *what = !p->has_kind ? "receitas e despesas" : p->kind == KIND_INCOME ? "receitas" : "despesas";
    g_autoptr(GPtrArray) filter = g_ptr_array_new_with_free_func(g_free);
    if (p->category) g_ptr_array_add(filter, g_strdup_printf("categoria %s", p->category));
    if (p->words[0]) {
        g_autofree char *j = g_strjoinv(" ", p->words);
        g_ptr_array_add(filter, g_strdup_printf("descrição com “%s”", j));
    }
    GString *u = g_string_new(NULL);
    g_string_printf(u, "Como entendi: %s de %s · %s", INTENT_LABEL[p->intent], what, p->period_label);
    for (guint i = 0; i < filter->len; i++) g_string_append_printf(u, " · %s", (char *)filter->pdata[i]);
    g_string_append(u, " · só valores realizados.");
    if (p->ignored[0]) {
        g_string_append(u, " Ignorei ");
        for (int i = 0; p->ignored[i]; i++) g_string_append_printf(u, "%s“%s”", i ? ", " : "", p->ignored[i]);
        g_string_append(u, ": não aparece em nenhum lançamento.");
    }
    GString *scope = g_string_new(NULL);
    if (filter->len) {
        g_string_append(scope, " (");
        for (guint i = 0; i < filter->len; i++) g_string_append_printf(scope, "%s%s", i ? ", " : "", (char *)filter->pdata[i]);
        g_string_append(scope, ")");
    }
    g_string_append_printf(scope, " em %s", p->period_label);
    Cents sum = 0, psum = 0;
    for (guint i = 0; i < done->len; i++) sum += ((Tx *)done->pdata[i])->value;
    for (guint i = 0; i < pending->len; i++) psum += ((Tx *)pending->pdata[i])->value;
    const char *verb = p->has_kind && p->kind == KIND_INCOME ? "recebeu" : "gastou";
    g_autofree char *pend_note = NULL;
    if (pending->len && p->intent != INTENT_BALANCE) {
        g_autofree char *pl = br_plural((int)pending->len, "lançamento", "lançamentos");
        pend_note = g_strdup_printf(" Há ainda %s pendente(s) em %s.", M(psum), pl);
    } else pend_note = g_strdup("");
    g_autofree char *dpl = br_plural((int)done->len, "lançamento", "lançamentos");
    char *text = NULL;
    char d[11];
    switch (p->intent) {
    case INTENT_TOTAL:
        text = done->len == 0 ? g_strdup_printf("Não encontrei %s realizadas%s.%s", what, scope->str, pend_note)
                              : g_strdup_printf("Você %s %s%s, em %s.%s", verb, M(sum), scope->str, dpl, pend_note);
        break;
    case INTENT_COUNT:
        text = done->len == 0 ? g_strdup_printf("Nenhum lançamento de %s%s.%s", what, scope->str, pend_note)
                              : g_strdup_printf("%s de %s%s, somando %s.%s", dpl, what, scope->str, M(sum), pend_note);
        break;
    case INTENT_MAX: {
        if (done->len == 0) { text = g_strdup_printf("Não encontrei %s realizadas%s.", what, scope->str); break; }
        /* três maiores (estável: em empate, o que veio antes) */
        Tx *top[3] = {NULL, NULL, NULL};
        for (guint i = 0; i < done->len; i++) {
            Tx *t = done->pdata[i];
            for (int k = 0; k < 3; k++) {
                if (!top[k] || t->value > top[k]->value) {
                    for (int m = 2; m > k; m--) top[m] = top[m - 1];
                    top[k] = t;
                    break;
                }
            }
        }
        GString *x = g_string_new(NULL);
        g_string_printf(x, "O maior foi “%s”: %s em %s (%s).", top[0]->desc, M(top[0]->value), day_br(top[0]->date, d), top[0]->category);
        if (top[1]) {
            g_string_append(x, " Depois: ");
            for (int k = 1; k < 3 && top[k]; k++) g_string_append_printf(x, "%s“%s” %s", k > 1 ? "; " : "", top[k]->desc, M(top[k]->value));
            g_string_append(x, ".");
        }
        text = g_string_free(x, FALSE);
        break;
    }
    case INTENT_AVERAGE: {
        Day end = MIN(p->to, today);
        gint64 days = MAX(1, (gint64)end - p->from + 1);
        text = done->len == 0 ? g_strdup_printf("Não encontrei %s realizadas%s.", what, scope->str)
                              : g_strdup_printf("Média de %s por dia%s (%s em %" G_GINT64_FORMAT " dia(s) até %s).",
                                                M(sum / days), scope->str, M(sum), days, day_br(end, d));
        break;
    }
    case INTENT_BALANCE: {
        Cents inc = 0, exp = 0;
        for (guint i = 0; i < done->len; i++) {
            Tx *t = done->pdata[i];
            if (t->kind == KIND_INCOME) inc += t->value; else exp += t->value;
        }
        text = g_strdup_printf("Em %s: receitas %s, despesas %s, saldo %s.", p->period_label, M(inc), M(exp), M(inc - exp));
        break;
    }
    }
#undef M
    g_string_free(scope, TRUE);
    AskAnswer *a = g_new0(AskAnswer, 1);
    a->text = text;
    a->understood = g_string_free(u, FALSE);
    a->parsed = p;
    a->matches = done;
    return a;
}
