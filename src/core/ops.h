/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Todas as alterações de dados passam por aqui, com as mesmas validações e mensagens do app
 * Android e do Finan+ web. Cada função valida tudo ANTES de alterar o estado: se devolver
 * FALSE, o estado não foi tocado e [err] traz título e mensagem para o usuário.
 */
#pragma once
#include "model.h"

G_BEGIN_DECLS

typedef struct { char title[96]; char msg[384]; } OpErr;

typedef enum { REPS_TOTAL, REPS_EACH } RepsMode;

typedef struct {
    Kind kind;
    const char *desc;
    const char *value;
    const char *category;
    Day date; /* DAY_NONE = inválida */
    gboolean paid;
    const char *account_id;
    /* vazio = conta/dinheiro */
    const char *card_id;
    int reps;
    RepsMode reps_mode;
    gboolean recurring;
} TxDraft;

gboolean ops_save_tx(AppState *s, const char *edit_id, const TxDraft *d, OpErr *err);
/* parcelas seguintes do mesmo grupo (para perguntar se exclui junto) */
int ops_later_parcels(const AppState *s, const char *id);
void ops_delete_tx(AppState *s, const char *id, gboolean with_later);
void ops_toggle_paid(AppState *s, const char *id);

gboolean ops_save_goal(AppState *s, const char *id, const char *name, const char *target, const char *move,
                       Day deadline, const char *monthly, OpErr *err);
void ops_delete_goal(AppState *s, const char *id);

gboolean ops_save_account(AppState *s, const char *id, const char *name, const char *initial, OpErr *err);
/* com check_only, só verifica se pode excluir */
gboolean ops_delete_account(AppState *s, const char *id, gboolean check_only, OpErr *err);

gboolean ops_save_card(AppState *s, const char *id, const char *name, const char *limit, const char *close,
                       const char *due, OpErr *err);
gboolean ops_delete_card(AppState *s, const char *id, gboolean check_only, OpErr *err);
gboolean ops_pay_invoice(AppState *s, const char *card_id, const char *value, const char *account_id, Day date, OpErr *err);

gboolean ops_save_recurring(AppState *s, const char *id, Kind kind, const char *desc, const char *value, const char *day,
                            const char *category, const char *account_id, const char *card_id, gboolean active,
                            Day start, Day today, OpErr *err);
void ops_delete_recurring(AppState *s, const char *id);

gboolean ops_save_limit(AppState *s, const char *old, const char *category, const char *value, OpErr *err);
void ops_delete_limit(AppState *s, const char *category);

gboolean ops_add_category(AppState *s, Kind kind, const char *name, OpErr *err);
gboolean ops_rename_category(AppState *s, Kind kind, const char *old, const char *name, OpErr *err);
gboolean ops_check_delete_category(const AppState *s, Kind kind, const char *name, OpErr *err);
void ops_delete_category(AppState *s, Kind kind, const char *name);
int ops_category_use_count(const AppState *s, Kind kind, const char *name);

G_END_DECLS
