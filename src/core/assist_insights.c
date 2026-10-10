/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Resumo do mês e dicas de economia. Tudo é cálculo sobre os lançamentos (nada é "inventado"):
 * cada regra está descrita no próprio texto de Insight.why e em ASSISTENTE.md.
 *
 * Convenções (iguais às dos Relatórios do app):
 * - despesa = lançamento de despesa que não é pagamento de fatura;
 * - compras no cartão contam na data da compra;
 * - "realizado" = pago/recebido.
 */
#include "assist.h"
#include <math.h>
#include <string.h>

static const char *const INSIGHT_LABELS[] = {
    "Possível duplicado", "Aumento de preço", "Ritmo do limite", "Ritmo do mês",
    "Acima da média", "Pequenos gastos", "Gastos fixos",
};
const char *insight_type_label(InsightType t) { return INSIGHT_LABELS[t]; }

void insight_free(Insight *i) {
    if (!i) return;
    g_free(i->id); g_free(i->title); g_free(i->text); g_free(i->why); g_free(i->query);
    g_free(i);
}

void month_report_free(MonthReport *r) {
    if (!r) return;
    g_free(r->title);
    g_ptr_array_unref(r->lines);
    if (r->highlights) g_ptr_array_unref(r->highlights);
    g_free(r->why);
    g_free(r);
}

/* ------------------------------------------------------------------ utilidades */

/* strings temporárias liberadas no fim de cada função */
#define POOL g_autoptr(GPtrArray) pool = g_ptr_array_new_with_free_func(g_free)
static const char *pooled(GPtrArray *pool, char *s) { g_ptr_array_add(pool, s); return s; }
#define M(v) pooled(pool, money(v))
#define P(...) pooled(pool, g_strdup_printf(__VA_ARGS__))
#define PCT(v) pooled(pool, br_pct((v), g_malloc(24)))

static gboolean is_expense(const Tx *t) { return t->kind == KIND_EXPENSE && tx_is_flow(t); }
static gboolean is_fixed(const Tx *t) { return t->recurring_id[0] || t->group_id[0]; }
static gboolean in_month_until(const Tx *t, Ym ym, int last_day) { return day_ym(t->date) == ym && day_dom(t->date) <= last_day; }

static Cents sum(GPtrArray *a) {
    Cents n = 0;
    for (guint i = 0; i < a->len; i++) n += ((Tx *)a->pdata[i])->value;
    return n;
}

/* agrupamento que preserva a ordem de primeira aparição (como groupBy do Kotlin) */
typedef struct { char *key; GPtrArray *items; } Group;
typedef struct { GPtrArray *groups; GHashTable *idx; } Groups;

static void group_free(Group *g) { g_free(g->key); g_ptr_array_unref(g->items); g_free(g); }
static Groups groups_new(void) {
    Groups g = {g_ptr_array_new_with_free_func((GDestroyNotify)group_free), g_hash_table_new(g_str_hash, g_str_equal)};
    return g;
}
static void groups_add(Groups *g, const char *key, gpointer item) {
    Group *x = g_hash_table_lookup(g->idx, key);
    if (!x) {
        x = g_new0(Group, 1);
        x->key = g_strdup(key);
        x->items = g_ptr_array_new();
        g_ptr_array_add(g->groups, x);
        g_hash_table_insert(g->idx, x->key, x);
    }
    g_ptr_array_add(x->items, item);
}
static void groups_clear(Groups *g) { g_hash_table_unref(g->idx); g_ptr_array_unref(g->groups); }

static Insight *insight(char *id, InsightType type, char *title, char *text, char *why, int priority,
                        const char *query, Day from, Day to) {
    Insight *i = g_new0(Insight, 1);
    i->id = id; i->type = type; i->title = title; i->text = text; i->why = why; i->priority = priority;
    i->query = g_strdup(query); i->from = from; i->to = to;
    return i;
}

/* ------------------------------------------------------------------ resumo do mês */

MonthReport *insights_report(const AppState *s, Day today, MoneyFmt money) {
    POOL;
    Ym ym = day_ym(today), prev = ym - 1;
    int day = day_dom(today);
    int prev_day = MIN(day, ym_len(prev));
    g_autoptr(GPtrArray) cur_exp = g_ptr_array_new(), prev_exp = g_ptr_array_new(), cur_inc = g_ptr_array_new();
    g_autoptr(GPtrArray) pending = g_ptr_array_new(), to_receive = g_ptr_array_new(), late = g_ptr_array_new();
    Cents pe = 0, pi = 0;
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (is_expense(t)) {
            if (t->paid && in_month_until(t, ym, day)) g_ptr_array_add(cur_exp, t);
            if (t->paid && in_month_until(t, prev, prev_day)) g_ptr_array_add(prev_exp, t);
            if (!t->paid && !tx_is_card(t) && day_ym(t->date) == ym && t->date >= today) g_ptr_array_add(pending, t);
            if (!t->paid && !tx_is_card(t) && t->date < today) g_ptr_array_add(late, t);
            if (t->paid && day_ym(t->date) == prev) pe += t->value;
        }
        if (t->kind == KIND_INCOME) {
            if (t->paid && in_month_until(t, ym, day)) g_ptr_array_add(cur_inc, t);
            if (!t->paid && day_ym(t->date) == ym) g_ptr_array_add(to_receive, t);
            if (t->paid && day_ym(t->date) == prev) pi += t->value;
        }
    }
    Cents spent = sum(cur_exp), before = sum(prev_exp), income = sum(cur_inc);
    GPtrArray *lines = g_ptr_array_new_with_free_func(g_free);
    /* posição em [lines] das frases que podem ir para o Início (-1 = não existe) */
    int spent_i = -1, income_i = -1, pending_i = -1, receive_i = -1, late_i = -1;

    if (spent == 0) g_ptr_array_add(lines, g_strdup_printf("Ainda não há despesas realizadas em %s.", br_month(ym)));
    else {
        GString *l = g_string_new(NULL);
        g_string_printf(l, "Até hoje (dia %d) você gastou %s em %s.", day, M(spent), br_month(ym));
        if (before > 0) {
            double c = (spent - before) * 100.0 / before;
            if (fabs(c) < 3) g_string_append_printf(l, " Praticamente o mesmo que no mesmo período de %s (%s).", br_month(prev), M(before));
            else if (c > 0) g_string_append_printf(l, " São %s a mais que no mesmo período de %s (%s).", PCT(c), br_month(prev), M(before));
            else g_string_append_printf(l, " São %s a menos que no mesmo período de %s (%s).", PCT(c), br_month(prev), M(before));
        }
        spent_i = (int)lines->len;
        g_ptr_array_add(lines, g_string_free(l, FALSE));
    }
    if (income > 0) {
        income_i = (int)lines->len;
        if (income >= spent) g_ptr_array_add(lines, g_strdup_printf("Entraram %s; sobram %s até agora.", M(income), M(income - spent)));
        else g_ptr_array_add(lines, g_strdup_printf("Entraram %s; as despesas já passam as receitas em %s.", M(income), M(spent - income)));
    }
    if (spent > 0) {
        Groups g = groups_new();
        for (guint i = 0; i < cur_exp->len; i++) groups_add(&g, ((Tx *)cur_exp->pdata[i])->category, cur_exp->pdata[i]);
        const char *bc = NULL;
        Cents bv = 0;
        for (guint i = 0; i < g.groups->len; i++) {
            Group *x = g.groups->pdata[i];
            Cents v = sum(x->items);
            if (!bc || v > bv) { bc = x->key; bv = v; }
        }
        g_ptr_array_add(lines, g_strdup_printf("A maior categoria é %s: %s (%s das despesas).", bc, M(bv), PCT(bv * 100.0 / spent)));
        groups_clear(&g);
    }
    if (pending->len) {
        g_autofree char *pl = br_plural((int)pending->len, "conta", "contas");
        pending_i = (int)lines->len;
        g_ptr_array_add(lines, g_strdup_printf("Ainda faltam %s em %s a pagar até o fim do mês.", M(sum(pending)), pl));
    }
    if (to_receive->len) {
        g_autofree char *pl = br_plural((int)to_receive->len, "lançamento", "lançamentos");
        receive_i = (int)lines->len;
        g_ptr_array_add(lines, g_strdup_printf("A receber neste mês: %s em %s.", M(sum(to_receive)), pl));
    }
    if (late->len) {
        g_autofree char *pl = br_plural((int)late->len, "conta está", "contas estão");
        late_i = (int)lines->len;
        g_ptr_array_add(lines, g_strdup_printf("%s em atraso (%s).", pl, M(sum(late))));
    }
    if (day <= 7 && (pe > 0 || pi > 0))
        g_ptr_array_add(lines, g_strdup_printf("Fechamento de %s: receitas %s, despesas %s, saldo %s.", br_month(prev), M(pi), M(pe), M(pi - pe)));

    MonthReport *r = g_new0(MonthReport, 1);
    g_autofree char *my = br_month_year(ym);
    r->title = g_strdup_printf("Resumo de %s", my);
    r->lines = lines;
    /* as 2 frases do Início, por prioridade: atraso, a pagar, quanto gastou, a receber, quanto entrou */
    r->highlights = g_ptr_array_new_with_free_func(g_free);
    const int order[5] = {late_i, pending_i, spent_i, receive_i, income_i};
    for (int k = 0; k < 5 && r->highlights->len < 2; k++)
        if (order[k] >= 0) g_ptr_array_add(r->highlights, g_strdup(lines->pdata[order[k]]));
    r->why = g_strdup_printf(
        "Considera só lançamentos realizados (pagos ou recebidos) até hoje. Compras no cartão contam na data da compra; "
        "pagamentos de fatura não contam como despesa nova. A comparação usa os mesmos dias (1 a %d) do mês anterior, para ser justa.", day);
    return r;
}

/* ------------------------------------------------------------------ dicas */

static void duplicates(const AppState *s, Day today, MoneyFmt money, GPtrArray *out) {
    POOL;
    Day since = today - INS_DUP_DAYS;
    Groups g = groups_new();
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (!is_expense(t) || t->date < since || is_fixed(t)) continue;
        g_autofree char *k = text_key(t->desc);
        if (!*k) continue;
        char d[11];
        g_autofree char *key = g_strdup_printf("%s:%" G_GINT64_FORMAT ":%s", day_iso(t->date, d), t->value, k);
        groups_add(&g, key, t);
    }
    /* grupos com 2+ itens, do mais recente para o mais antigo, no máximo 3 */
    g_autoptr(GPtrArray) dups = g_ptr_array_new();
    for (guint i = 0; i < g.groups->len; i++) if (((Group *)g.groups->pdata[i])->items->len >= 2) g_ptr_array_add(dups, g.groups->pdata[i]);
    for (guint a = 1; a < dups->len; a++) /* ordenação por inserção, estável, data decrescente */
        for (guint b = a; b > 0; b--) {
            Tx *x = ((Group *)dups->pdata[b - 1])->items->pdata[0], *y = ((Group *)dups->pdata[b])->items->pdata[0];
            if (y->date > x->date) { gpointer tmp = dups->pdata[b - 1]; dups->pdata[b - 1] = dups->pdata[b]; dups->pdata[b] = tmp; }
            else break;
        }
    for (guint i = 0; i < dups->len && i < 3; i++) {
        Group *x = dups->pdata[i];
        Tx *t = x->items->pdata[0];
        char dm[6];
        g_ptr_array_add(out, insight(
            g_strdup_printf("dup:%s", x->key), INSIGHT_DUPLICATE, g_strdup("Possível lançamento duplicado"),
            g_strdup_printf("“%s” aparece %u vezes em %s com o mesmo valor (%s). Se foi lançado em dobro, exclua a cópia.",
                            t->desc, x->items->len, day_br_short(t->date, dm), M(t->value)),
            g_strdup_printf("Regra: mesma descrição, mesmo valor e mesma data, nos últimos %d dias, sem ser parcela nem recorrência. "
                            "Se forem compras diferentes de verdade, dispense este aviso.", INS_DUP_DAYS),
            10, t->desc, t->date, t->date));
    }
    groups_clear(&g);
}

static void monthly_free(MonthlyExpense *m) {
    g_free(m->name); g_free(m->key);
    g_ptr_array_unref(m->months);
    g_array_unref(m->yms);
    g_free(m);
}

static Tx *monthly_last(const MonthlyExpense *m) { return m->months->pdata[m->months->len - 1]; }

static gint cmp_cents(gconstpointer a, gconstpointer b) {
    Cents x = *(const Cents *)a, y = *(const Cents *)b;
    return (x > y) - (x < y);
}

GPtrArray *insights_recurring_expenses(const AppState *s, Day today) {
    Ym cur = day_ym(today), window = cur - 5;
    GPtrArray *out = g_ptr_array_new_with_free_func((GDestroyNotify)monthly_free);
    Groups g = groups_new();
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        Ym y = day_ym(t->date);
        if (!is_expense(t) || t->group_id[0] || y < window || y > cur) continue;
        g_autofree char *clean = text_clean_parcel(t->desc);
        g_autofree char *k = text_key(clean);
        groups_add(&g, k, t);
    }
    for (guint gi = 0; gi < g.groups->len; gi++) {
        Group *grp = g.groups->pdata[gi];
        if (!*grp->key) continue;
        /* um lançamento por mês; várias vezes no mesmo mês: não é mensalidade */
        Tx *by[6] = {0};
        gboolean multi = FALSE;
        for (guint i = 0; i < grp->items->len; i++) {
            Tx *t = grp->items->pdata[i];
            int idx = day_ym(t->date) - window;
            if (by[idx]) { multi = TRUE; break; }
            by[idx] = t;
        }
        if (multi) continue;
        int end = -1;
        for (int i = 5; i >= 0; i--) if (by[i]) { end = i; break; }
        if (end < 0 || (window + end != cur && window + end != cur - 1)) continue;
        int run = 0;
        while (end - run >= 0 && by[end - run]) run++;
        if (run < INS_SUB_MIN_MONTHS) continue;
        Cents vals[6];
        for (int i = 0; i < run; i++) vals[i] = by[end - run + 1 + i]->value;
        qsort(vals, (size_t)run, sizeof(Cents), cmp_cents);
        double median = (double)vals[run / 2];
        gboolean out_tol = FALSE;
        for (int i = 0; i < run; i++)
            if (vals[i] < median * (1 - INS_SUB_TOLERANCE) || vals[i] > median * (1 + INS_SUB_TOLERANCE)) out_tol = TRUE;
        if (out_tol) continue;
        MonthlyExpense *m = g_new0(MonthlyExpense, 1);
        m->key = g_strdup(grp->key);
        m->months = g_ptr_array_new();
        m->yms = g_array_new(FALSE, FALSE, sizeof(Ym));
        for (int i = end - run + 1; i <= end; i++) {
            Ym y = window + i;
            g_ptr_array_add(m->months, by[i]);
            g_array_append_val(m->yms, y);
        }
        m->name = text_clean_parcel(monthly_last(m)->desc);
        g_ptr_array_add(out, m);
    }
    groups_clear(&g);
    /* maior valor mensal primeiro (estável) */
    for (guint a = 1; a < out->len; a++)
        for (guint b = a; b > 0; b--) {
            if (monthly_last(out->pdata[b])->value > monthly_last(out->pdata[b - 1])->value) {
                gpointer tmp = out->pdata[b - 1]; out->pdata[b - 1] = out->pdata[b]; out->pdata[b] = tmp;
            } else break;
        }
    return out;
}

static void price_ups(GPtrArray *subs, MoneyFmt money, GPtrArray *out) {
    POOL;
    for (guint i = 0; i < subs->len; i++) {
        MonthlyExpense *m = subs->pdata[i];
        if (m->months->len < 2) continue;
        Tx *last = monthly_last(m), *before = m->months->pdata[m->months->len - 2];
        Ym ym_last = g_array_index(m->yms, Ym, m->yms->len - 1);
        if (last->value < before->value * INS_PRICE_UP_RATIO || last->value - before->value < 100) continue;
        double c = (last->value - before->value) * 100.0 / before->value;
        char ymb[8];
        g_ptr_array_add(out, insight(
            g_strdup_printf("up:%s:%s", m->key, ym_iso(ym_last, ymb)), INSIGHT_PRICE_UP,
            g_strdup_printf("%s ficou mais caro", m->name),
            g_strdup_printf("“%s” passou de %s para %s em %s (+%s). Vale conferir se houve reajuste ou mudança de plano.",
                            m->name, M(before->value), M(last->value), br_month(ym_last), PCT(c)),
            g_strdup_printf("Regra: gasto mensal (1 vez por mês, %u meses seguidos) cujo último valor ficou pelo menos %s e R$ 1,00 acima do mês anterior.",
                            m->months->len, PCT((INS_PRICE_UP_RATIO - 1) * 100)),
            9, m->name, DAY_NONE, DAY_NONE));
    }
}

static void subscriptions_summary(GPtrArray *subs, Day today, MoneyFmt money, GPtrArray *out) {
    POOL;
    if (!subs->len) return;
    Cents total = 0;
    for (guint i = 0; i < subs->len; i++) total += monthly_last(subs->pdata[i])->value;
    GString *list = g_string_new(NULL);
    for (guint i = 0; i < subs->len && i < 5; i++) {
        MonthlyExpense *m = subs->pdata[i];
        if (i) g_string_append(list, ", ");
        g_string_append_printf(list, "%s (%s)", m->name, M(monthly_last(m)->value));
    }
    if (subs->len > 5) g_string_append_printf(list, " e mais %u", subs->len - 5);
    char ymb[8];
    g_autofree char *pl = br_plural((int)subs->len, "gasto que se repete", "gastos que se repetem");
    g_ptr_array_add(out, insight(
        g_strdup_printf("subs:%s:%u:%" G_GINT64_FORMAT, ym_iso(day_ym(today), ymb), subs->len, total), INSIGHT_SUBSCRIPTIONS,
        g_strdup("Gastos fixos do mês"),
        g_strdup_printf("Encontrei %s todo mês, somando %s por mês (%s por ano): %s. "
                        "Gastos fixos pesam o ano inteiro: vale revisar planos, assinaturas e tarifas que dá para reduzir.",
                        pl, M(total), M(total * 12), list->str),
        g_strdup_printf("Regra: mesma descrição, uma vez por mês, em pelo menos %d meses seguidos (até este mês ou o anterior), "
                        "com valores a até %s da mediana. Parcelas não entram. O valor mensal é o do último lançamento.",
                        INS_SUB_MIN_MONTHS, PCT(INS_SUB_TOLERANCE * 100)),
        4, NULL, DAY_NONE, DAY_NONE));
    g_string_free(list, TRUE);
}

Projection insights_project(GPtrArray *txs, Day today, int min_count) {
    Ym ym = day_ym(today);
    int day = day_dom(today), len = ym_len(ym);
    Projection p = {0, 0, 0, 0, 0, FALSE};
    Cents biggest = 0;
    for (guint i = 0; i < txs->len; i++) {
        Tx *t = txs->pdata[i];
        if (day_ym(t->date) != ym) continue;
        if (is_fixed(t) || !t->paid) p.committed += t->value;
        else if (day_dom(t->date) <= day) { p.variable += t->value; p.count++; if (t->value > biggest) biggest = t->value; }
    }
    /* uma despesa que sozinha passa de metade do gasto variável é pontual: conta uma vez */
    if (p.variable > 0 && biggest > p.variable * INS_ONE_OFF_SHARE) p.one_off = biggest;
    p.projected = p.committed + p.one_off + (Cents)floor((double)(p.variable - p.one_off) / day * len + 0.5);
    p.enough = p.count >= min_count;
    return p;
}

/* texto do "Por quê?" com a conta da projeção (string nova; quem chama libera) */
static char *projection_why(const Projection *p, int day, int len, MoneyFmt money, int min_count) {
    POOL;
    if (p->one_off > 0)
        return g_strdup_printf("Conta: compromissos do mês (recorrências, parcelas e contas agendadas) %s + gasto pontual %s (conta uma vez) "
                               "+ resto do gasto variável até hoje %s ÷ %d dias × %d dias. "
                               "Só é calculada a partir do dia %d e com pelo menos %d despesas variáveis pagas no mês.",
                               M(p->committed), M(p->one_off), M(p->variable - p->one_off), day, len, INS_PACE_MIN_DAY, min_count);
    return g_strdup_printf("Conta: compromissos do mês (recorrências, parcelas e contas agendadas) %s + gasto variável até hoje %s ÷ %d dias × %d dias. "
                           "Só é calculada a partir do dia %d e com pelo menos %d despesas variáveis pagas no mês.",
                           M(p->committed), M(p->variable), day, len, INS_PACE_MIN_DAY, min_count);
}

static void limit_pace(const AppState *s, Day today, MoneyFmt money, GPtrArray *out) {
    POOL;
    Ym ym = day_ym(today);
    int day = day_dom(today), len = ym_len(ym);
    if (day < INS_PACE_MIN_DAY || day >= len) return;
    for (guint li = 0; li < s->limits->len; li++) {
        Limit *lim = s->limits->pdata[li];
        g_autoptr(GPtrArray) l = g_ptr_array_new();
        Cents used = 0;
        for (guint i = 0; i < s->txs->len; i++) {
            Tx *t = s->txs->pdata[i];
            if (!is_expense(t) || strcmp(t->category, lim->category) != 0) continue;
            g_ptr_array_add(l, t);
            if (day_ym(t->date) == ym) used += t->value;
        }
        if (used >= lim->value) continue; /* já ultrapassado: o Início já mostra */
        Projection p = insights_project(l, today, INS_PACE_MIN_COUNT_CAT);
        if (!p.enough || p.projected <= lim->value || p.projected - lim->value < 1000) continue;
        int left = len - day;
        Cents per_day = MAX(0, lim->value - used) / left;
        char ymb[8];
        g_ptr_array_add(out, insight(
            g_strdup_printf("pace:%s:%s", ym_iso(ym, ymb), lim->category), INSIGHT_LIMIT_PACE,
            g_strdup_printf("%s pode passar do limite", lim->category),
            g_strdup_printf("No ritmo atual, %s deve fechar %s em cerca de %s, acima do limite de %s. "
                            "Para ficar dentro, gaste até %s por dia nos %d dias restantes.",
                            lim->category, br_month(ym), M(p.projected), M(lim->value), M(per_day), left),
            g_strdup_printf("%s Já usado: %s de %s.", pooled(pool, projection_why(&p, day, len, money, INS_PACE_MIN_COUNT_CAT)), M(used), M(lim->value)),
            8, lim->category, ym_first(ym), today));
    }
}

static void over_income(const AppState *s, Day today, MoneyFmt money, GPtrArray *out) {
    POOL;
    Ym ym = day_ym(today);
    int day = day_dom(today), len = ym_len(ym);
    if (day < INS_PACE_MIN_DAY || day >= len) return;
    Cents income = 0;
    g_autoptr(GPtrArray) exp = g_ptr_array_new();
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (t->kind == KIND_INCOME && day_ym(t->date) == ym) income += t->value; /* inclui receitas previstas */
        if (is_expense(t)) g_ptr_array_add(exp, t);
    }
    if (income <= 0) return;
    Projection p = insights_project(exp, today, INS_PACE_MIN_COUNT);
    if (!p.enough || p.projected <= income) return;
    char ymb[8];
    g_ptr_array_add(out, insight(
        g_strdup_printf("over:%s", ym_iso(ym, ymb)), INSIGHT_OVER_INCOME, g_strdup("Despesas podem passar das receitas"),
        g_strdup_printf("No ritmo atual, as despesas de %s chegam a cerca de %s, acima das receitas previstas para o mês (%s). "
                        "Diferença estimada: %s.", br_month(ym), M(p.projected), M(income), M(p.projected - income)),
        g_strdup_printf("%s Receitas previstas = recebidas + a receber neste mês.", pooled(pool, projection_why(&p, day, len, money, INS_PACE_MIN_COUNT))),
        8, NULL, ym_first(ym), today));
}

static void spikes(const AppState *s, Day today, MoneyFmt money, GPtrArray *out) {
    POOL;
    Ym ym = day_ym(today);
    Groups g = groups_new();
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (is_expense(t) && t->paid && day_ym(t->date) == ym && t->date <= today) groups_add(&g, t->category, t);
    }
    for (guint gi = 0; gi < g.groups->len; gi++) {
        Group *grp = g.groups->pdata[gi];
        Cents cur = sum(grp->items);
        Cents hist[3] = {0, 0, 0}; /* mês -1, -2, -3 */
        for (guint i = 0; i < s->txs->len; i++) {
            Tx *t = s->txs->pdata[i];
            if (!is_expense(t) || !t->paid || strcmp(t->category, grp->key) != 0) continue;
            int back = ym - day_ym(t->date);
            if (back >= 1 && back <= 3) hist[back - 1] += t->value;
        }
        int with_data = (hist[0] > 0) + (hist[1] > 0) + (hist[2] > 0);
        if (with_data < 2) continue;
        Cents avg = (hist[0] + hist[1] + hist[2]) / 3;
        if (avg <= 0 || cur < avg * INS_SPIKE_RATIO || cur - avg < INS_SPIKE_MIN_DIFF) continue;
        /* duas maiores do mês */
        Tx *t1 = NULL, *t2 = NULL;
        for (guint i = 0; i < grp->items->len; i++) {
            Tx *t = grp->items->pdata[i];
            if (!t1 || t->value > t1->value) { t2 = t1; t1 = t; }
            else if (!t2 || t->value > t2->value) t2 = t;
        }
        GString *top = g_string_new(NULL);
        g_string_append_printf(top, "“%s” (%s)", t1->desc, M(t1->value));
        if (t2) g_string_append_printf(top, ", “%s” (%s)", t2->desc, M(t2->value));
        char ymb[8];
        g_ptr_array_add(out, insight(
            g_strdup_printf("spike:%s:%s", ym_iso(ym, ymb), grp->key), INSIGHT_CATEGORY_SPIKE,
            g_strdup_printf("%s acima do normal", grp->key),
            g_strdup_printf("%s já soma %s em %s, %s acima da sua média dos últimos 3 meses (%s). Maiores: %s.",
                            grp->key, M(cur), br_month(ym), PCT((cur - avg) * 100.0 / avg), M(avg), top->str),
            g_strdup_printf("Regra: gasto realizado da categoria neste mês ≥ %s acima da média de %s, %s, %s (%s + %s + %s ÷ 3) "
                            "e pelo menos %s a mais. Precisa de dados em pelo menos 2 desses meses.",
                            PCT((INS_SPIKE_RATIO - 1) * 100), br_month(ym - 3), br_month(ym - 2), br_month(ym - 1),
                            M(hist[2]), M(hist[1]), M(hist[0]), M(INS_SPIKE_MIN_DIFF)),
            7, grp->key, ym_first(ym), today));
        g_string_free(top, TRUE);
    }
    groups_clear(&g);
}

static void small_spends(const AppState *s, Day today, MoneyFmt money, GPtrArray *out) {
    POOL;
    Ym ym = day_ym(today);
    g_autoptr(GPtrArray) small = g_ptr_array_new();
    Cents all = 0;
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (!is_expense(t) || !t->paid || day_ym(t->date) != ym || t->date > today) continue;
        all += t->value;
        if (t->value <= INS_SMALL_VALUE) g_ptr_array_add(small, t);
    }
    if (small->len < INS_SMALL_MIN_COUNT) return;
    Cents total = sum(small);
    Groups g = groups_new();
    for (guint i = 0; i < small->len; i++) {
        g_autofree char *k = text_key(((Tx *)small->pdata[i])->desc);
        if (*k) groups_add(&g, k, small->pdata[i]);
    }
    g_autoptr(GPtrArray) freq = g_ptr_array_new();
    for (guint i = 0; i < g.groups->len; i++) if (((Group *)g.groups->pdata[i])->items->len >= 2) g_ptr_array_add(freq, g.groups->pdata[i]);
    for (guint a = 1; a < freq->len; a++)
        for (guint b = a; b > 0; b--) {
            if (((Group *)freq->pdata[b])->items->len > ((Group *)freq->pdata[b - 1])->items->len) {
                gpointer tmp = freq->pdata[b - 1]; freq->pdata[b - 1] = freq->pdata[b]; freq->pdata[b] = tmp;
            } else break;
        }
    GString *f = g_string_new(NULL);
    for (guint i = 0; i < freq->len && i < 3; i++) {
        Group *x = freq->pdata[i];
        if (i) g_string_append(f, ", ");
        g_string_append_printf(f, "“%s” (%u×)", ((Tx *)x->items->pdata[0])->desc, x->items->len);
    }
    char ymb[8];
    g_autofree char *more = f->len ? g_strdup_printf(" Os mais frequentes: %s.", f->str) : g_strdup("");
    g_ptr_array_add(out, insight(
        g_strdup_printf("small:%s:%u", ym_iso(ym, ymb), small->len), INSIGHT_SMALL_SPENDS, g_strdup("Pequenos gastos somando"),
        g_strdup_printf("%u compras de até %s somaram %s em %s (%s das despesas).%s",
                        small->len, M(INS_SMALL_VALUE), M(total), br_month(ym), PCT(total * 100.0 / all), more),
        g_strdup_printf("Regra: despesas realizadas de até %s neste mês, quando passam de %d. "
                        "Sozinhas parecem pouco; juntas mostram para onde vai o dinheiro.", M(INS_SMALL_VALUE), INS_SMALL_MIN_COUNT),
        5, NULL, ym_first(ym), today));
    g_string_free(f, TRUE);
    groups_clear(&g);
}

GPtrArray *insights_tips(const AppState *s, Day today, MoneyFmt money) {
    GPtrArray *out = g_ptr_array_new_with_free_func((GDestroyNotify)insight_free);
    duplicates(s, today, money, out);
    g_autoptr(GPtrArray) subs = insights_recurring_expenses(s, today);
    price_ups(subs, money, out);
    limit_pace(s, today, money, out);
    over_income(s, today, money, out);
    spikes(s, today, money, out);
    small_spends(s, today, money, out);
    subscriptions_summary(subs, today, money, out);
    /* prioridade decrescente, estável */
    for (guint a = 1; a < out->len; a++)
        for (guint b = a; b > 0; b--) {
            if (((Insight *)out->pdata[b])->priority > ((Insight *)out->pdata[b - 1])->priority) {
                gpointer tmp = out->pdata[b - 1]; out->pdata[b - 1] = out->pdata[b]; out->pdata[b] = tmp;
            } else break;
        }
    return out;
}
