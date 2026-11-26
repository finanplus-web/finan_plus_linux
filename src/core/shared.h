/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Despesas compartilhadas (tradução de core/Shared.kt do app Android 1.8): uma despesa paga por uma pessoa e
 * dividida com outras. Quem pagou recebe de cada participante a parte dele; os pagamentos podem ser parciais.
 * Não mexe no saldo das contas nem nos lançamentos: é um controle de quem deve a quem.
 * Regras e formato em DESPESAS-COMPARTILHADAS.md. Testes em tests/test_shared.c (os casos do SharedTest).
 */
#pragma once
#include "model.h"
#include "ops.h"

G_BEGIN_DECLS

#define SHARED_NAME_MAX 60
#define SHARED_PERSON_MAX 40
#define SHARED_NOTE_MAX 500
#define SHARED_PEOPLE_MAX 50
#define SHARED_PAYMENTS_MAX 500

typedef enum { PERSON_NONE, PERSON_PAID, PERSON_PARTIAL, PERSON_PENDING } PersonState;
typedef enum { SHARED_OPEN, SHARED_PARTIAL, SHARED_SETTLED } SharedState;

/* situação de um participante: quanto deve a quem pagou, quanto já devolveu e quanto falta */
typedef struct {
    const SharedPerson *person;
    gboolean is_payer;
    Cents owed;
    Cents paid;
} PersonStatus;
#define person_remaining(p) (MAX(0, (p)->owed - (p)->paid))
PersonState person_state(const PersonStatus *p);
/* "Pagou a despesa", "Quitado", "Pagou parte", "Pendente" */
const char *person_state_label(PersonState s);

/* resumo da despesa: to_receive = soma do que os outros devem a quem pagou */
typedef struct {
    GArray *people; /* PersonStatus, na ordem dos participantes */
    Cents to_receive;
    Cents received;
} SharedSummary;
#define summary_pending(s) (MAX(0, (s)->to_receive - (s)->received))
SharedState summary_state(const SharedSummary *s);
/* "Pendente", "Parcialmente paga", "Quitada" */
const char *shared_state_label(SharedState s);

void shared_summary(const SharedExpense *e, SharedSummary *out);
void shared_summary_clear(SharedSummary *s);
/* soma de todas as despesas: o que falta receber e o que já foi recebido */
void shared_totals(GPtrArray *list, Cents *pending, Cents *received);
/* partes iguais (o resto dos centavos fica com o primeiro), em out[n] */
void shared_equal_shares(Cents total, int n, Cents *out);
/* nomes de todos os participantes (para o filtro), sem repetir, em ordem alfabética; g_strfreev */
char **shared_people_names(GPtrArray *list);

/* filtros do histórico; [person] compara o nome sem diferenciar maiúsculas e acentos; state -1 = todas */
typedef struct {
    const char *query;
    Day from, to;       /* DAY_NONE = sem limite */
    const char *person;
    int state;
} SharedFilter;
/* mais recentes primeiro; GPtrArray sem dono (aponta para os itens de [list]) */
GPtrArray *shared_filter(GPtrArray *list, const SharedFilter *f);

/* linha do formulário: id vazio = participante novo; share em texto (só na divisão personalizada) */
typedef struct { const char *id; const char *name; const char *share; } SharedPersonDraft;
typedef struct {
    const char *name;
    const char *total;
    Day date;
    SplitMode split;
    const SharedPersonDraft *people;
    int n_people;
    /* posição de quem pagou em people */
    int payer_index;
    const char *note;
} SharedDraft;

/* validam tudo antes de mexer em [s]; FALSE = nada mudou e [err] explica */
gboolean shared_save(AppState *s, const char *edit_id, const SharedDraft *d, OpErr *err);
gboolean shared_add_payment(AppState *s, const char *expense_id, const char *person_id, const char *value, Day date, OpErr *err);
/* quita tudo o que person_id ainda deve, na data [date] */
gboolean shared_settle(AppState *s, const char *expense_id, const char *person_id, Day date, OpErr *err);
void shared_delete_payment(AppState *s, const char *expense_id, const char *payment_id);
void shared_delete(AppState *s, const char *id);

/* "comprovante-mercado-do-mes-2026-11-02.pdf" e "finanplus-mercado-do-mes-2026-11-02.png" (g_free) */
char *shared_pdf_name(const SharedExpense *e);
char *shared_image_name(const SharedExpense *e);

G_END_DECLS
