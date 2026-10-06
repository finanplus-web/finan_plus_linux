/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Assistente do Finan+: sugestão de categoria, resumo do mês, dicas de economia e perguntas
 * rápidas. Tudo é regra e cálculo sobre os lançamentos do usuário, no próprio computador,
 * sem internet e sem modelo de linguagem. Cada regra está descrita em ASSISTENTE.md.
 */
#pragma once
#include "model.h"

G_BEGIN_DECLS

/* ================================================================ texto (assist_text.c) */
/* minúsculas, sem acentos, só letras e números separados por um espaço (g_free) */
char *text_fold(const char *s);
/* palavras relevantes: 2+ letras, não só números, fora da lista de palavras vazias (g_strfreev) */
char **text_tokens(const char *s);
/* chave para reconhecer a "mesma descrição": palavras relevantes sem repetição (g_free) */
char *text_key(const char *s);
gboolean text_same(const char *a, const char *b);
gboolean text_is_stop(const char *w);
/* descrição sem o sufixo de parcela " (2/10)" que o próprio app acrescenta (g_free) */
char *text_clean_parcel(const char *desc);
/* percentual inteiro, sempre positivo: 37.6 → "38%" (buf ≥ 24) */
char *br_pct(double v, char *buf);
/* "1 conta" / "3 contas" (g_free) */
char *br_plural(int n, const char *one, const char *many);

/* ================================================================ dicionário (assist_dict.c) */
typedef struct {
    Kind kind;
    char **names; /* alternativas de nome de categoria */
    char **terms; /* termos já normalizados */
    int line;
} DictSection;

typedef struct { GPtrArray *sections; /* DictSection* */ } Dictionary;

Dictionary *dictionary_parse(const char *text);
void dictionary_free(Dictionary *d);

typedef struct {
    char *category;
    char **terms;
    const DictSection *section;
} DictMatch;

/* Devolve a categoria com maior peso de termos encontrados (NULL se nada ou empate). */
DictMatch *dictionary_match(const Dictionary *d, const char *desc, Kind kind, GPtrArray *categories);
void dict_match_free(DictMatch *m);

/* ================================================================ categorias (assist_categorizer.c) */
typedef enum { SOURCE_SAME_DESCRIPTION, SOURCE_LEARNED, SOURCE_DICTIONARY } SuggestSource;

typedef struct {
    char *category;
    SuggestSource source;
    char *why;
    double confidence;
} Suggestion;

#define CAT_MIN_CONFIDENCE 0.70
#define CAT_MIN_DOCS 5
#define CAT_MIN_WORD_DOCS 2
#define CAT_ALPHA 0.1

typedef struct Categorizer Categorizer;

Categorizer *categorizer_build(const AppState *s, Kind kind, const Dictionary *dict);
void categorizer_free(Categorizer *c);
Suggestion *categorizer_suggest(const Categorizer *c, const char *desc);
void suggestion_free(Suggestion *s);
int categorizer_training_size(const Categorizer *c);

typedef struct { char *word; int count; } LearnedWord;
typedef struct { char *category; GArray *words; /* LearnedWord */ } LearnedCategory;
/* "O que o assistente aprendeu": palavras mais frequentes por categoria (≥ 2 lançamentos) */
GPtrArray *categorizer_learned_words(const Categorizer *c, int per_category);

/* ================================================================ resumo e dicas (assist_insights.c) */
typedef enum {
    INSIGHT_DUPLICATE, INSIGHT_PRICE_UP, INSIGHT_LIMIT_PACE, INSIGHT_OVER_INCOME,
    INSIGHT_CATEGORY_SPIKE, INSIGHT_SMALL_SPENDS, INSIGHT_SUBSCRIPTIONS,
} InsightType;
const char *insight_type_label(InsightType t);

#define INS_SMALL_VALUE 2000
#define INS_SMALL_MIN_COUNT 10
#define INS_SPIKE_RATIO 1.30
#define INS_SPIKE_MIN_DIFF 5000
#define INS_PACE_MIN_DAY 7
#define INS_SUB_MIN_MONTHS 3
#define INS_SUB_TOLERANCE 0.30
#define INS_PRICE_UP_RATIO 1.05
#define INS_DUP_DAYS 60

typedef struct {
    char *id;
    InsightType type;
    char *title;
    char *text;
    char *why;
    int priority;
    /* para abrir a lista de lançamentos já filtrada (query pode ser NULL; datas DAY_NONE) */
    char *query;
    Day from;
    Day to;
} Insight;
void insight_free(Insight *i);

typedef struct {
    char *title;
    GPtrArray *lines; /* char* */
    char *why;
} MonthReport;
void month_report_free(MonthReport *r);

MonthReport *insights_report(const AppState *s, Day today, MoneyFmt money);
/* dicas em ordem de prioridade (Insight* com free func) */
GPtrArray *insights_tips(const AppState *s, Day today, MoneyFmt money);

typedef struct {
    char *name;
    char *key;
    GPtrArray *months; /* Tx* (do estado), um por mês, em ordem */
    GArray *yms;       /* Ym */
} MonthlyExpense;
GPtrArray *insights_recurring_expenses(const AppState *s, Day today);

typedef struct { Cents committed, variable, projected; } Projection;
Projection insights_project(GPtrArray *txs, Day today);

/* ================================================================ perguntas (assist_ask.c) */
typedef enum { INTENT_TOTAL, INTENT_MAX, INTENT_COUNT, INTENT_AVERAGE, INTENT_BALANCE } AskIntent;

typedef struct {
    AskIntent intent;
    gboolean has_kind;
    Kind kind;
    Day from, to;
    char *period_label;
    char *category;  /* NULL se nenhuma */
    char **words;    /* filtro da descrição */
    char **ignored;  /* palavras que não aparecem em nenhum lançamento */
} AskParsed;

typedef struct {
    char *text;
    char *understood;
    AskParsed *parsed;
    GPtrArray *matches; /* Tx* do estado (sem free func) */
} AskAnswer;

extern const char *const ASK_EXAMPLES[6];
AskParsed *ask_parse(const char *question, const AppState *s, Day today);
void ask_parsed_free(AskParsed *p);
AskAnswer *ask_answer(const char *question, const AppState *s, Day today, MoneyFmt money);
void ask_answer_free(AskAnswer *a);

G_END_DECLS
