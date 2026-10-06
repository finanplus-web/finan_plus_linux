/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Modelo de dados do Finan+ (o mesmo do app Android e do backup JSON do Finan+ web).
 * Todo valor em dinheiro é int64 em centavos: nada de ponto flutuante.
 * Datas são dias desde 1970-01-01 (Day) e meses são ano*12 + (mês-1) (Ym).
 * Strings nunca são NULL: campo vazio = "".
 */
#pragma once
#include <glib.h>
#include <stdint.h>

G_BEGIN_DECLS

typedef int64_t Cents;
typedef int32_t Day;
typedef int32_t Ym;
#define DAY_NONE INT32_MIN
#define YM_NONE INT32_MIN

/* ---------------------------------------------------------------- datas (date.c) */
Day day_from_ymd(int y, int m, int d);
void day_to_ymd(Day day, int *y, int *m, int *d);
int day_year(Day d);
int day_month(Day d);
int day_dom(Day d);
/* 1 = segunda … 7 = domingo */
int day_weekday(Day d);
Ym day_ym(Day d);
Ym ym_make(int y, int m);
int ym_year(Ym ym);
int ym_month(Ym ym);
int ym_len(Ym ym);
Day ym_first(Ym ym);
Day ym_last(Ym ym);
/* dia [day] do mês, limitado ao último dia (31 → 28/29/30) */
Day ym_day_clamped(Ym ym, int day);
/* soma meses mantendo o dia quando possível (31/jan + 1 → 28/fev) */
Day day_plus_months_clamped(Day d, int n);
/* data de hoje no fuso local do sistema */
Day day_today(void);
gboolean day_is_valid_ymd(int y, int m, int d);
/* "AAAA-MM-DD" estrito */
gboolean day_parse_iso(const char *s, Day *out);
/* "DD/MM/AAAA" (também aceita D/M/AAAA) */
gboolean day_parse_br(const char *s, Day *out);
/* buffers de pelo menos 11 bytes */
char *day_iso(Day d, char *buf);
char *day_br(Day d, char *buf);
/* "DD/MM" (6 bytes) */
char *day_br_short(Day d, char *buf);
/* "AAAA-MM" (8 bytes) */
char *ym_iso(Ym ym, char *buf);
gboolean ym_parse_iso(const char *s, Ym *out);

/* nomes em português, sem depender do idioma do sistema */
extern const char *const BR_MONTHS[12];
extern const char *const BR_MONTHS_SHORT[12];
/* "março" */
const char *br_month(Ym ym);
/* "março de 2026" (g_free) */
char *br_month_year(Ym ym);
/* "03 de Outubro de 2026" (g_free) */
char *br_full_date(Day d);
/* "mar de 2026" (g_free) */
char *br_month_label(Ym ym);

/* ---------------------------------------------------------------- modelo */
typedef enum { KIND_INCOME = 0, KIND_EXPENSE = 1 } Kind;
const char *kind_json(Kind k);
gboolean kind_parse(const char *s, Kind *out);

typedef enum {
    THEME_AUTO, THEME_LIGHT, THEME_MATERIAL, THEME_OLED, THEME_TOKYO, THEME_NORD, THEME_COUNT
} ThemeId;
const char *theme_json(ThemeId t);
const char *theme_label(ThemeId t);
ThemeId theme_parse(const char *s);

typedef struct {
    char *id;
    Kind kind;
    Cents value;
    Day date;
    char *desc;
    char *category;
    gboolean paid;
    char *account_id;
    /* compra feita no cartão (entra na fatura) */
    char *card_id;
    /* pagamento da fatura deste cartão (debita a conta, não é despesa nova) */
    char *card_payment;
    char *recurring_id;
    char *group_id;
    int parcel_n;
    int parcel_total;
} Tx;

#define tx_is_flow(t) ((t)->card_payment[0] == '\0')
#define tx_is_card(t) ((t)->card_id[0] != '\0')

typedef struct { char *id; char *name; Cents initial; } Account;
typedef struct { char *id; char *name; Cents limit; int close; int due; } Card;
typedef struct { char *id; char *name; Cents target; Cents saved; Day deadline; Cents monthly; } Goal;
typedef struct {
    char *id;
    Kind kind;
    char *desc;
    Cents value;
    char *category;
    char *account_id;
    char *card_id;
    /* 1..31, limitado ao último dia de cada mês */
    int day;
    gboolean active;
    Day start;
    Ym last;
} Recurring;
typedef struct { char *category; Cents value; } Limit;

/* Estado completo do app. É o que vai para o backup JSON (compatível com o Finan+ web). */
typedef struct {
    GPtrArray *txs;       /* Tx* */
    GPtrArray *goals;     /* Goal* */
    GPtrArray *accounts;  /* Account* (sempre pelo menos 1) */
    GPtrArray *cards;     /* Card* */
    GPtrArray *recurring; /* Recurring* */
    GPtrArray *cats[2];   /* char*, indexado por Kind */
    GPtrArray *limits;    /* Limit*, na ordem em que foram criados */
    gboolean privacy;
    /* minutos sem usar até bloquear; 0 = desativado */
    int auto_lock;
    ThemeId theme;
} AppState;

#define MAIN_ACCOUNT "main"
#define CARD_PAYMENT_CAT "Pagamento de fatura"
extern const int AUTOLOCK_OPTIONS[5];
extern const char *const DEFAULT_EXPENSE[7];
extern const char *const DEFAULT_INCOME[4];

Tx *tx_new(const char *id, Kind kind, Cents value, Day date, const char *desc, const char *category,
           gboolean paid, const char *account_id, const char *card_id);
Tx *tx_copy(const Tx *t);
void tx_free(Tx *t);
void tx_set_str(char **field, const char *v);

Account *account_new(const char *id, const char *name, Cents initial);
void account_free(Account *a);
Card *card_new(const char *id, const char *name, Cents limit, int close, int due);
void card_free(Card *c);
Goal *goal_new(const char *id, const char *name, Cents target, Cents saved, Day deadline, Cents monthly);
void goal_free(Goal *g);
Recurring *recurring_new(const char *id, Kind kind, const char *desc, Cents value, const char *category,
                         const char *account_id, const char *card_id, int day, gboolean active, Day start, Ym last);
void recurring_free(Recurring *r);
Limit *limit_new(const char *category, Cents value);
void limit_free(Limit *l);

/* estado vazio: só "Conta principal" e as categorias padrão */
AppState *app_state_new(void);
/* estado sem nada (sem conta, sem categorias), usado pelo leitor de backup */
AppState *app_state_new_empty(void);
AppState *app_state_copy(const AppState *s);
void app_state_free(AppState *s);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AppState, app_state_free)

Account *app_account(const AppState *s, const char *id);
Card *app_card(const AppState *s, const char *id);
Tx *app_tx(const AppState *s, const char *id);
Goal *app_goal(const AppState *s, const char *id);
Recurring *app_recurring(const AppState *s, const char *id);
/* limite da categoria; FALSE se não houver */
gboolean app_limit(const AppState *s, const char *cat, Cents *out);
gboolean app_has_category(const AppState *s, Kind k, const char *name);
const char *app_first_category(const AppState *s, Kind k);

/* ids únicos (tempo + sequência + aleatório, base 36), g_free */
char *ids_new(void);

/* ---------------------------------------------------------------- dinheiro (money.c) */
/* "R$ 1.234,56" / "-R$ 0,50" — formatação própria, não depende do idioma. buf ≥ 32 */
char *money_format(Cents c, char *buf);
char *money_fmt(Cents c); /* g_free */
/* valor para campo de edição: "1500,50". buf ≥ 32 */
char *money_input(Cents c, char *buf);
/* centavos → "12.50" (para o JSON). buf ≥ 32 */
char *money_reais_json(Cents c, char *buf);
/* reais (double do JSON) → centavos, arredondando como Math.round do Kotlin */
Cents money_from_reais(double d);
/* Aceita "1.500,50", "1500,50", "1500.50", "1,500.25", "R$ 2.000", "-50". */
gboolean money_parse(const char *input, Cents *out);

/* Formatação que respeita "Ocultar valores": a interface escolhe uma das duas. */
typedef char *(*MoneyFmt)(Cents c);
char *money_fmt_hidden(Cents c);

/* ---------------------------------------------------------------- utilidades de texto */
/* aparar espaços e limitar a [max] caracteres (UTF-8), g_free */
char *str_clean(const char *s, int max);
/* comparação de nomes sem diferenciar maiúscula (Unicode) */
gboolean str_same_ci(const char *a, const char *b);

G_END_DECLS
