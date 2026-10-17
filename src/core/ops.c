/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ops.h"
#include "finance.h"
#include <string.h>
#include <stdlib.h>

static gboolean fail(OpErr *e, const char *title, const char *fmt, ...) G_GNUC_PRINTF(3, 4);
static gboolean fail(OpErr *e, const char *title, const char *fmt, ...) {
    if (e) {
        g_strlcpy(e->title, title, sizeof e->title);
        va_list ap;
        va_start(ap, fmt);
        g_vsnprintf(e->msg, sizeof e->msg, fmt, ap);
        va_end(ap);
    }
    return FALSE;
}
#define REVISE "Revise os dados"

/* o que vai mudar nas quantidades cabe nos limites? (só conta quando a coleção cresce) */
static gboolean room(const AppState *s, AppCounts after, OpErr *err) {
    const char *e = app_growth_error(app_counts(s), after);
    return e ? fail(err, "Limite atingido", "%s Exclua itens antigos antes de criar novos.", e) : TRUE;
}
/* trocar valores que somam [minus] por outros que somam [plus] mantém o total no limite seguro? */
static gboolean money_room(const AppState *s, Cents minus, Cents plus, OpErr *err) {
    return app_money_fits(s, minus, plus) ? TRUE
        : fail(err, "Limite financeiro excedido", "O total dos valores excede o limite seguro. Reduza algum valor antes de salvar.");
}

/* Data efetiva se realizado; data prevista se pendente (igual ao Android). */
static void tx_dates(const Tx *old, Day requested, gboolean paid, gboolean card, Day today, Day *date, Day *sched) {
    *sched = DAY_NONE;
    if (card) { *date = requested; return; }
    if (!paid) { *date = old && old->paid && old->scheduled_date != DAY_NONE ? old->scheduled_date : requested; return; }
    if (old) {
        if (!old->paid) { *date = today; *sched = old->date != today ? old->date : DAY_NONE; return; }
        *date = requested; *sched = old->scheduled_date; return;
    }
    if (requested > today) { *date = today; *sched = requested; return; }
    *date = requested;
}

static gboolean blank(const char *s) {
    if (!s) return TRUE;
    for (; *s; s++) if (!g_ascii_isspace(*s)) return FALSE;
    return TRUE;
}

/* inteiro simples ("12", com espaços em volta); FALSE se não for número */
static gboolean parse_int(const char *s, int *out) {
    if (!s) return FALSE;
    g_autofree char *t = g_strstrip(g_strdup(s));
    if (!*t || strlen(t) > 9) return FALSE;
    const char *p = t;
    if (*p == '-' || *p == '+') p++;
    if (!*p) return FALSE;
    for (const char *q = p; *q; q++) if (!g_ascii_isdigit(*q)) return FALSE;
    *out = atoi(t);
    return TRUE;
}

/* ------------------------------------------------------------------ lançamentos */

gboolean ops_save_tx(AppState *s, const char *edit_id, const TxDraft *d, Day today, OpErr *err) {
    Cents value = 0;
    gboolean value_ok = money_parse(d->value, &value);
    gboolean card = d->kind == KIND_EXPENSE && d->card_id && d->card_id[0];
    if (!value_ok || value <= 0) return fail(err, "Valor inválido", "Informe um valor maior que zero. Ex.: 59,90");
    if (d->date == DAY_NONE) return fail(err, "Data inválida", "Informe uma data válida.");
    if (card && !app_card(s, d->card_id)) return fail(err, "Sem cartão", "Cadastre um cartão em Meu dinheiro antes de lançar no cartão.");
    const char *account_id = app_account(s, d->account_id) ? d->account_id : ((Account *)s->accounts->pdata[0])->id;
    const char *category = blank(d->category) ? app_first_category(s, d->kind) : d->category;
    /* descrição é opcional: em branco, a categoria serve de rótulo na lista */
    g_autofree char *desc0 = str_clean(d->desc, 200);
    g_autofree char *desc = *desc0 ? g_strdup(desc0) : str_clean(category, 200);
    const char *card_id = card ? d->card_id : "";
    gboolean paid = card || d->paid;

    if (edit_id) {
        Tx *t = app_tx(s, edit_id);
        if (!t) return fail(err, REVISE, "Lançamento não encontrado.");
        if (!money_room(s, t->value, value, err)) return FALSE;
        g_autofree char *acc = g_strdup(account_id), *cat = g_strdup(category), *cid = g_strdup(card_id);
        tx_set_str(&t->desc, desc);
        t->value = value;
        tx_set_str(&t->category, cat);
        tx_set_str(&t->account_id, acc);
        if (t->card_payment[0]) { t->paid = TRUE; t->date = d->date; }
        else {
            Day date, sched;
            tx_dates(t, d->date, paid, card, today, &date, &sched);
            t->kind = d->kind;
            t->paid = paid;
            t->date = date;
            t->scheduled_date = sched;
            tx_set_str(&t->card_id, cid);
        }
        return TRUE;
    }

    int n = CLAMP(d->reps, 1, 60);
    if (n > 1 && d->recurring) return fail(err, "Revise o lançamento", "Escolha parcelas ou repetição mensal, não os dois.");
    Cents values[60];
    if (n > 1) {
        if (d->reps_mode == REPS_EACH) for (int i = 0; i < n; i++) values[i] = value;
        else split_installments(value, n, values);
        for (int i = 0; i < n; i++)
            if (values[i] <= 0) return fail(err, "Valor inválido", "O valor é pequeno demais para tantas parcelas.");
    }
    AppCounts after = app_counts(s);
    after.txs += n;
    if (d->recurring) after.recurring++;
    if (!room(s, after, err)) return FALSE;
    Cents added = 0;
    for (int i = 0; i < n; i++) added += n > 1 ? values[i] : value;
    if (!money_room(s, 0, added + (d->recurring ? value : 0), err)) return FALSE;
    Day entry_date, sched;
    tx_dates(NULL, d->date, paid, card, today, &entry_date, &sched);
    if (n > 1) {
        g_autofree char *group = ids_new();
        g_autofree char *acc = g_strdup(account_id), *cat = g_strdup(category);
        for (int i = 0; i < n; i++) {
            g_autofree char *id = ids_new();
            g_autofree char *suffix = g_strdup_printf(" (%d/%d)", i + 1, n);
            g_autofree char *base = str_clean(desc, 200 - (int)strlen(suffix));
            g_autofree char *pdesc = g_strconcat(g_strchomp(base), suffix, NULL);
            gboolean ipaid = card || (i == 0 && d->paid);
            Tx *t = tx_new(id, d->kind, values[i], i == 0 ? entry_date : day_plus_months_clamped(d->date, i), pdesc, cat,
                           ipaid, acc, card_id);
            if (i == 0 && ipaid) t->scheduled_date = sched;
            tx_set_str(&t->group_id, group);
            t->parcel_n = i + 1;
            t->parcel_total = n;
            g_ptr_array_add(s->txs, t);
        }
    } else {
        g_autofree char *id = ids_new();
        Tx *t = tx_new(id, d->kind, value, entry_date, desc, category, paid, account_id, card_id);
        t->scheduled_date = sched;
        g_ptr_array_add(s->txs, t);
    }
    if (d->recurring) {
        g_autofree char *rid = ids_new();
        g_ptr_array_add(s->recurring, recurring_new(rid, d->kind, desc, value, category, account_id, card_id,
                                                    day_dom(d->date), TRUE, d->date, day_ym(d->date)));
    }
    return TRUE;
}

int ops_later_parcels(const AppState *s, const char *id) {
    Tx *t = app_tx(s, id);
    if (!t || !t->group_id[0]) return 0;
    int n = 0;
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *o = s->txs->pdata[i];
        if (o != t && strcmp(o->group_id, t->group_id) == 0 && o->date >= t->date) n++;
    }
    return n;
}

void ops_delete_tx(AppState *s, const char *id, gboolean with_later) {
    Tx *t = app_tx(s, id);
    if (!t) return;
    g_autofree char *group = g_strdup(t->group_id);
    Day date = t->date;
    for (guint i = s->txs->len; i-- > 0;) {
        Tx *o = s->txs->pdata[i];
        gboolean kill = o == t || (with_later && group[0] && strcmp(o->group_id, group) == 0 && o->date >= date);
        if (kill) g_ptr_array_remove_index(s->txs, i);
    }
}

/* Compra no cartão e pagamento de fatura não alternam pago/pendente: desmarcar um pagamento de fatura reabria
 * a fatura e deixava o pagamento pendente, descontando o mesmo valor duas vezes (igual ao Android 1.1.1). */
gboolean ops_can_toggle_paid(const Tx *t) { return t && !tx_is_card(t) && tx_is_flow(t) && !tx_is_projected(t); }

void ops_toggle_paid(AppState *s, const char *id, Day today) {
    Tx *t = app_tx(s, id);
    if (!ops_can_toggle_paid(t)) return;
    if (t->paid) {
        t->paid = FALSE;
        if (t->scheduled_date != DAY_NONE) t->date = t->scheduled_date;
        t->scheduled_date = DAY_NONE;
    } else {
        t->paid = TRUE;
        t->scheduled_date = t->date != today ? t->date : DAY_NONE;
        t->date = today;
    }
}

/* ------------------------------------------------------------------ metas */

gboolean ops_save_goal(AppState *s, const char *id, const char *name, const char *target, const char *move,
                       Day deadline, const char *monthly, OpErr *err) {
    g_autofree char *n = str_clean(name, 60);
    Cents t = 0, m = 0, mv = 0;
    gboolean t_ok = money_parse(target, &t);
    gboolean m_ok = blank(monthly) ? TRUE : money_parse(monthly, &m);
    if (!*n) return fail(err, REVISE, "Informe o nome da meta.");
    if (!t_ok || t <= 0) return fail(err, REVISE, "Informe um valor de meta maior que zero. Ex.: 1500,50");
    if (!m_ok || m < 0) return fail(err, REVISE, "Contribuição mensal inválida.");
    if (!blank(move) && !money_parse(move, &mv)) return fail(err, REVISE, "Valor a guardar inválido.");
    if (!id) {
        AppCounts after = app_counts(s); after.goals++;
        if (!room(s, after, err) || !money_room(s, 0, t + m, err)) return FALSE;
        g_autofree char *nid = ids_new();
        g_ptr_array_add(s->goals, goal_new(nid, n, t, 0, deadline, m));
        return TRUE;
    }
    Goal *g = app_goal(s, id);
    if (!g) return TRUE;
    Cents saved = MAX(0, g->saved + mv);
    if (saved > MONEY_MAX_ABS) return fail(err, "Limite financeiro excedido", "O valor guardado excede o limite seguro.");
    if (!money_room(s, g->target + g->saved + g->monthly, t + saved + m, err)) return FALSE;
    tx_set_str(&g->name, n);
    g->target = t;
    g->deadline = deadline;
    g->monthly = m;
    g->saved = saved;
    return TRUE;
}

void ops_delete_goal(AppState *s, const char *id) {
    Goal *g = app_goal(s, id);
    if (g) g_ptr_array_remove(s->goals, g);
}

/* ------------------------------------------------------------------ contas */

gboolean ops_save_account(AppState *s, const char *id, const char *name, const char *initial, OpErr *err) {
    g_autofree char *n = str_clean(name, 40);
    Cents ini = 0;
    gboolean ok = blank(initial) ? TRUE : money_parse(initial, &ini);
    if (!*n) return fail(err, REVISE, "Informe o nome da conta.");
    if (!ok) return fail(err, REVISE, "Saldo inicial inválido. Ex.: 1.250,00 ou -300");
    if (!id) {
        AppCounts after = app_counts(s); after.accounts++;
        if (!room(s, after, err) || !money_room(s, 0, ABS(ini), err)) return FALSE;
        g_autofree char *nid = ids_new();
        g_ptr_array_add(s->accounts, account_new(nid, n, ini));
        return TRUE;
    }
    Account *a = app_account(s, id);
    if (a && !money_room(s, ABS(a->initial), ABS(ini), err)) return FALSE;
    if (a) { tx_set_str(&a->name, n); a->initial = ini; }
    return TRUE;
}

gboolean ops_delete_account(AppState *s, const char *id, gboolean check_only, OpErr *err) {
    if (s->accounts->len <= 1) return fail(err, "Conta necessária", "Mantenha pelo menos uma conta.");
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (strcmp(t->account_id, id) == 0 && !tx_is_card(t))
            return fail(err, "Conta em uso", "Mova ou exclua os lançamentos desta conta antes.");
    }
    for (guint i = 0; i < s->recurring->len; i++) {
        Recurring *r = s->recurring->pdata[i];
        if (strcmp(r->account_id, id) == 0 && !r->card_id[0])
            return fail(err, "Conta em uso", "Há recorrências usando esta conta. Edite ou exclua essas recorrências antes.");
    }
    Account *a = app_account(s, id);
    if (!a || check_only) return TRUE;
    g_ptr_array_remove(s->accounts, a);
    const char *first = ((Account *)s->accounts->pdata[0])->id;
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (strcmp(t->account_id, id) == 0) tx_set_str(&t->account_id, first);
    }
    for (guint i = 0; i < s->recurring->len; i++) {
        Recurring *r = s->recurring->pdata[i];
        if (strcmp(r->account_id, id) == 0) tx_set_str(&r->account_id, first);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ cartões */

gboolean ops_save_card(AppState *s, const char *id, const char *name, const char *limit, const char *close,
                       const char *due, OpErr *err) {
    g_autofree char *n = str_clean(name, 40);
    Cents lim = 0;
    gboolean lim_ok = blank(limit) ? TRUE : money_parse(limit, &lim);
    int c = 0, d = 0;
    gboolean c_ok = parse_int(close, &c), d_ok = parse_int(due, &d);
    if (!*n) return fail(err, REVISE, "Informe o nome do cartão.");
    if (!lim_ok || lim < 0) return fail(err, REVISE, "Limite inválido.");
    if (!c_ok || !d_ok || c < 1 || c > 31 || d < 1 || d > 31)
        return fail(err, REVISE, "Os dias de fechamento e vencimento devem estar entre 1 e 31.");
    if (!id) {
        AppCounts after = app_counts(s); after.cards++;
        if (!room(s, after, err) || !money_room(s, 0, lim, err)) return FALSE;
        g_autofree char *nid = ids_new();
        g_ptr_array_add(s->cards, card_new(nid, n, lim, c, d));
        return TRUE;
    }
    Card *k = app_card(s, id);
    if (k && !money_room(s, k->limit, lim, err)) return FALSE;
    if (k) { tx_set_str(&k->name, n); k->limit = lim; k->close = c; k->due = d; }
    return TRUE;
}

gboolean ops_delete_card(AppState *s, const char *id, gboolean check_only, OpErr *err) {
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (strcmp(t->card_id, id) == 0 || strcmp(t->card_payment, id) == 0)
            return fail(err, "Cartão em uso",
                        "Este cartão tem compras ou pagamentos registrados. Exclua esses lançamentos antes de excluir o cartão.");
    }
    for (guint i = 0; i < s->recurring->len; i++)
        if (strcmp(((Recurring *)s->recurring->pdata[i])->card_id, id) == 0)
            return fail(err, "Cartão em uso", "Há recorrências usando este cartão. Edite ou exclua essas recorrências antes.");
    Card *c = app_card(s, id);
    if (c && !check_only) g_ptr_array_remove(s->cards, c);
    return TRUE;
}

gboolean ops_pay_invoice(AppState *s, const char *card_id, const char *value, const char *account_id, Day date, OpErr *err) {
    Card *c = app_card(s, card_id);
    if (!c) return fail(err, REVISE, "Cartão não encontrado.");
    Cents v = 0;
    if (!money_parse(value, &v) || v <= 0) return fail(err, REVISE, "Informe um valor maior que zero.");
    if (date == DAY_NONE) return fail(err, REVISE, "Informe uma data válida.");
    const char *acc = app_account(s, account_id) ? account_id : ((Account *)s->accounts->pdata[0])->id;
    AppCounts after = app_counts(s); after.txs++;
    if (!room(s, after, err) || !money_room(s, 0, v, err)) return FALSE;
    g_autofree char *id = ids_new();
    g_autofree char *desc = g_strdup_printf("Pagamento fatura %s", c->name);
    Tx *t = tx_new(id, KIND_EXPENSE, v, date, desc, CARD_PAYMENT_CAT, TRUE, acc, "");
    tx_set_str(&t->card_payment, c->id);
    g_ptr_array_add(s->txs, t);
    return TRUE;
}

/* ------------------------------------------------------------------ recorrências */

/* Ao reativar uma recorrência pausada, os meses parados não geram lançamento: retoma a partir do mês atual.
 * (Antes, reativar em outubro uma recorrência pausada em março criava 7 lançamentos pendentes de uma vez.) */
Ym ops_resumed_last(Ym last, Day today) {
    Ym prev = day_ym(today) - 1;
    return last != YM_NONE && last > prev ? last : prev;
}

gboolean ops_save_recurring(AppState *s, const char *id, Kind kind, const char *desc, const char *value, const char *day,
                            const char *category, const char *account_id, const char *card_id, gboolean active,
                            Day start, Day today, OpErr *err) {
    g_autofree char *ds = str_clean(desc, 120);
    Cents v = 0;
    gboolean v_ok = money_parse(value, &v);
    int dd = 0;
    gboolean d_ok = parse_int(day, &dd);
    if (!*ds) return fail(err, REVISE, "Informe uma descrição.");
    if (!v_ok || v <= 0) return fail(err, REVISE, "Informe um valor maior que zero.");
    if (!d_ok || dd < 1 || dd > 31) return fail(err, REVISE, "O dia deve estar entre 1 e 31.");
    g_autofree char *acc = g_strdup(app_account(s, account_id) ? account_id : ((Account *)s->accounts->pdata[0])->id);
    g_autofree char *card = g_strdup(kind == KIND_EXPENSE && card_id && app_card(s, card_id) ? card_id : "");
    g_autofree char *cat = g_strdup(blank(category) ? app_first_category(s, kind) : category);
    if (!id) {
        if (start == DAY_NONE) return fail(err, REVISE, "Informe a data de início.");
        AppCounts after = app_counts(s); after.recurring++;
        if (!room(s, after, err) || !money_room(s, 0, v, err)) return FALSE;
        g_autofree char *nid = ids_new();
        g_ptr_array_add(s->recurring, recurring_new(nid, kind, ds, v, cat, acc, card, dd, TRUE, start, YM_NONE));
    } else {
        Recurring *r = app_recurring(s, id);
        if (r && !money_room(s, r->value, v, err)) return FALSE;
        if (r) {
            r->kind = kind;
            tx_set_str(&r->desc, ds);
            r->value = v;
            r->day = dd;
            tx_set_str(&r->category, cat);
            tx_set_str(&r->account_id, acc);
            tx_set_str(&r->card_id, card);
            if (!r->active && active) r->last = ops_resumed_last(r->last, today);
            r->active = active;
        }
    }
    generate_recurring(s, today);
    return TRUE;
}

void ops_delete_recurring(AppState *s, const char *id) {
    Recurring *r = app_recurring(s, id);
    if (r) g_ptr_array_remove(s->recurring, r);
}

/* ------------------------------------------------------------------ limites */

static Limit *find_limit(const AppState *s, const char *cat) {
    for (guint i = 0; i < s->limits->len; i++) {
        Limit *l = s->limits->pdata[i];
        if (strcmp(l->category, cat) == 0) return l;
    }
    return NULL;
}

gboolean ops_save_limit(AppState *s, const char *old, const char *category, const char *value, OpErr *err) {
    Cents v = 0;
    gboolean ok = money_parse(value, &v);
    if (blank(category)) return fail(err, REVISE, "Escolha uma categoria.");
    if (!ok || v <= 0) return fail(err, REVISE, "Informe um valor maior que zero.");
    Limit *prev = find_limit(s, old ? old : category);
    if (!money_room(s, prev ? prev->value : 0, v, err)) return FALSE;
    if (old && strcmp(old, category) != 0) {
        Limit *o = find_limit(s, old);
        if (o) g_ptr_array_remove(s->limits, o);
    }
    Limit *l = find_limit(s, category);
    if (l) l->value = v;
    else g_ptr_array_add(s->limits, limit_new(category, v));
    return TRUE;
}

void ops_delete_limit(AppState *s, const char *category) {
    Limit *l = find_limit(s, category);
    if (l) g_ptr_array_remove(s->limits, l);
}

/* ------------------------------------------------------------------ categorias */

gboolean ops_add_category(AppState *s, Kind kind, const char *name, OpErr *err) {
    g_autofree char *n = str_clean(name, 40);
    if (!*n) return fail(err, "Nome vazio", "Digite o nome da categoria.");
    for (guint i = 0; i < s->cats[kind]->len; i++)
        if (str_same_ci(s->cats[kind]->pdata[i], n)) return fail(err, "Categoria duplicada", "Essa categoria já existe.");
    AppCounts after = app_counts(s); after.cats[kind]++;
    if (!room(s, after, err)) return FALSE;
    g_ptr_array_add(s->cats[kind], g_steal_pointer(&n));
    return TRUE;
}

gboolean ops_rename_category(AppState *s, Kind kind, const char *old, const char *name, OpErr *err) {
    g_autofree char *n = str_clean(name, 40);
    if (!*n) return fail(err, "Nome vazio", "Informe um nome.");
    if (strcmp(n, old) == 0) return TRUE;
    for (guint i = 0; i < s->cats[kind]->len; i++) {
        const char *c = s->cats[kind]->pdata[i];
        if (strcmp(c, old) != 0 && str_same_ci(c, n)) return fail(err, "Categoria duplicada", "Essa categoria já existe.");
    }
    g_autofree char *o = g_strdup(old); /* [old] pode apontar para dentro do estado */
    for (guint i = 0; i < s->cats[kind]->len; i++) {
        if (strcmp(s->cats[kind]->pdata[i], o) == 0) {
            g_free(s->cats[kind]->pdata[i]);
            s->cats[kind]->pdata[i] = g_strdup(n);
        }
    }
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (t->kind == kind && strcmp(t->category, o) == 0) tx_set_str(&t->category, n);
    }
    for (guint i = 0; i < s->recurring->len; i++) {
        Recurring *r = s->recurring->pdata[i];
        if (r->kind == kind && strcmp(r->category, o) == 0) tx_set_str(&r->category, n);
    }
    if (kind == KIND_EXPENSE) {
        Limit *l = find_limit(s, o);
        if (l) {
            /* o limite renomeado vai para o fim, como no app Android (LinkedHashMap.put) */
            Cents v = l->value;
            g_ptr_array_remove(s->limits, l);
            Limit *dup = find_limit(s, n);
            if (dup) dup->value = v; else g_ptr_array_add(s->limits, limit_new(n, v));
        }
    }
    return TRUE;
}

gboolean ops_check_delete_category(const AppState *s, Kind kind, const char *name, OpErr *err) {
    if (s->cats[kind]->len <= 1)
        return fail(err, "Categoria necessária", "Mantenha pelo menos uma categoria de %s.", kind == KIND_EXPENSE ? "despesa" : "receita");
    for (guint i = 0; i < s->recurring->len; i++) {
        Recurring *r = s->recurring->pdata[i];
        if (r->kind == kind && strcmp(r->category, name) == 0)
            return fail(err, "Categoria em uso", "Esta categoria está sendo usada por uma recorrência. Altere ou exclua a recorrência primeiro.");
    }
    return TRUE;
}

void ops_delete_category(AppState *s, Kind kind, const char *name) {
    g_autofree char *n = g_strdup(name);
    for (guint i = s->cats[kind]->len; i-- > 0;)
        if (strcmp(s->cats[kind]->pdata[i], n) == 0) g_ptr_array_remove_index(s->cats[kind], i);
    if (kind == KIND_EXPENSE) ops_delete_limit(s, n);
}

int ops_category_use_count(const AppState *s, Kind kind, const char *name) {
    int n = 0;
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (t->kind == kind && strcmp(t->category, name) == 0) n++;
    }
    return n;
}
