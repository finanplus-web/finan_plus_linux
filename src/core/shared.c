/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tradução de core/Shared.kt (app Android 1.8). Mesmas regras e mensagens.
 */
#include "shared.h"
#include "assist.h"
#include "finance.h"
#include <string.h>

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

static gboolean blank(const char *s) {
    if (!s) return TRUE;
    for (; *s; s++) if (!g_ascii_isspace(*s)) return FALSE;
    return TRUE;
}

PersonState person_state(const PersonStatus *p) {
    if (p->is_payer || p->owed == 0) return PERSON_NONE;
    if (person_remaining(p) == 0) return PERSON_PAID;
    if (p->paid > 0) return PERSON_PARTIAL;
    return PERSON_PENDING;
}

const char *person_state_label(PersonState s) {
    static const char *const L[] = {"Pagou a despesa", "Quitado", "Pagou parte", "Pendente"};
    return L[s];
}

SharedState summary_state(const SharedSummary *s) {
    if (s->to_receive == 0 || summary_pending(s) == 0) return SHARED_SETTLED;
    if (s->received > 0) return SHARED_PARTIAL;
    return SHARED_OPEN;
}

const char *shared_state_label(SharedState s) {
    static const char *const L[] = {"Pendente", "Parcialmente paga", "Quitada"};
    return L[s];
}

void shared_summary(const SharedExpense *e, SharedSummary *out) {
    out->people = g_array_sized_new(FALSE, TRUE, sizeof(PersonStatus), e->people->len);
    out->to_receive = 0;
    out->received = 0;
    for (guint i = 0; i < e->people->len; i++) {
        const SharedPerson *p = e->people->pdata[i];
        PersonStatus st = {p, strcmp(p->id, e->payer_id) == 0, 0, 0};
        st.owed = st.is_payer ? 0 : p->share;
        for (guint j = 0; j < e->payments->len; j++) {
            const SharedPayment *pay = e->payments->pdata[j];
            if (strcmp(pay->person_id, p->id) == 0) st.paid += pay->value;
        }
        out->to_receive += st.owed;
        /* um pagamento acima da parte (não deveria acontecer) não conta como "recebido" a mais */
        out->received += MIN(st.paid, st.owed);
        g_array_append_val(out->people, st);
    }
}

void shared_summary_clear(SharedSummary *s) {
    if (s->people) g_array_unref(s->people);
    s->people = NULL;
}

void shared_totals(GPtrArray *list, Cents *pending, Cents *received) {
    Cents p = 0, r = 0;
    for (guint i = 0; i < list->len; i++) {
        SharedSummary s;
        shared_summary(list->pdata[i], &s);
        p += summary_pending(&s);
        r += s.received;
        shared_summary_clear(&s);
    }
    if (pending) *pending = p;
    if (received) *received = r;
}

void shared_equal_shares(Cents total, int n, Cents *out) {
    if (n > 0) split_installments(total, n, out);
}

static int cmp_fold(gconstpointer a, gconstpointer b) {
    g_autofree char *x = text_fold(*(char *const *)a);
    g_autofree char *y = text_fold(*(char *const *)b);
    return strcmp(x, y);
}

char **shared_people_names(GPtrArray *list) {
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GPtrArray *out = g_ptr_array_new();
    for (guint i = 0; i < list->len; i++) {
        SharedExpense *e = list->pdata[i];
        for (guint j = 0; j < e->people->len; j++) {
            SharedPerson *p = e->people->pdata[j];
            char *k = text_fold(p->name);
            if (g_hash_table_contains(seen, k)) { g_free(k); continue; }
            g_hash_table_add(seen, k);
            g_ptr_array_add(out, g_strdup(p->name));
        }
    }
    g_hash_table_unref(seen);
    g_ptr_array_sort(out, cmp_fold);
    g_ptr_array_add(out, NULL);
    return (char **)g_ptr_array_free(out, FALSE);
}

static int cmp_recent(gconstpointer a, gconstpointer b) {
    const SharedExpense *x = *(SharedExpense *const *)a, *y = *(SharedExpense *const *)b;
    if (x->date != y->date) return x->date > y->date ? -1 : 1;
    return -strcmp(x->id, y->id);
}

GPtrArray *shared_filter(GPtrArray *list, const SharedFilter *f) {
    /* text_fold já apara e troca pontuação por espaço */
    g_autofree char *q = text_fold(f->query);
    g_autofree char *who = text_fold(f->person);
    GPtrArray *out = g_ptr_array_new();
    for (guint i = 0; i < list->len; i++) {
        SharedExpense *e = list->pdata[i];
        if (f->from != DAY_NONE && e->date < f->from) continue;
        if (f->to != DAY_NONE && e->date > f->to) continue;
        if (*who) {
            gboolean any = FALSE;
            for (guint j = 0; j < e->people->len && !any; j++) {
                g_autofree char *n = text_fold(((SharedPerson *)e->people->pdata[j])->name);
                any = strcmp(n, who) == 0;
            }
            if (!any) continue;
        }
        if (f->state >= 0) {
            SharedSummary s;
            shared_summary(e, &s);
            int st = summary_state(&s);
            shared_summary_clear(&s);
            if (st != f->state) continue;
        }
        if (*q) {
            GString *all = g_string_new(e->name);
            g_string_append_printf(all, " %s", e->note);
            for (guint j = 0; j < e->people->len; j++) g_string_append_printf(all, " %s", ((SharedPerson *)e->people->pdata[j])->name);
            g_autofree char *hay = text_fold(all->str);
            g_string_free(all, TRUE);
            if (!strstr(hay, q)) continue;
        }
        g_ptr_array_add(out, e);
    }
    g_ptr_array_sort(out, cmp_recent);
    return out;
}

/* ------------------------------------------------------------------ alterações */

static SharedPerson *person_of(const SharedExpense *e, const char *id) {
    for (guint i = 0; i < e->people->len; i++) {
        SharedPerson *p = e->people->pdata[i];
        if (strcmp(p->id, id) == 0) return p;
    }
    return NULL;
}

gboolean shared_save(AppState *s, const char *edit_id, const SharedDraft *d, OpErr *err) {
    char b1[32], b2[32], b3[32];
    g_autofree char *name = str_clean(d->name, SHARED_NAME_MAX);
    if (!*name) return fail(err, "Nome da despesa", "Dê um nome para a despesa. Ex.: Mercado do mês");
    Cents total;
    if (!money_parse(d->total, &total) || total <= 0) return fail(err, "Valor inválido", "Informe o valor total, maior que zero. Ex.: 450,00");
    if (d->date == DAY_NONE) return fail(err, "Data inválida", "Informe a data da despesa.");
    int n = d->n_people;
    g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func(g_free);
    for (int i = 0; i < n; i++) g_ptr_array_add(names, str_clean(d->people[i].name, SHARED_PERSON_MAX));
    for (int i = 0; i < n; i++)
        if (!*(char *)names->pdata[i]) return fail(err, "Participantes", "Preencha o nome de todos os participantes (ou remova a linha vazia).");
    if (n < 2) return fail(err, "Participantes", "Adicione pelo menos duas pessoas: quem pagou e quem vai dividir.");
    if (n > SHARED_PEOPLE_MAX) return fail(err, "Participantes", "Use no máximo %d participantes.", SHARED_PEOPLE_MAX);
    {
        g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        for (int i = 0; i < n; i++) {
            char *k = text_fold(names->pdata[i]);
            if (g_hash_table_contains(seen, k)) {
                g_free(k);
                return fail(err, "Participantes", "\"%s\" aparece duas vezes. Use nomes diferentes (ex.: João S. e João P.).", (char *)names->pdata[i]);
            }
            g_hash_table_add(seen, k);
        }
    }
    if (d->payer_index < 0 || d->payer_index >= n) return fail(err, "Quem pagou", "Escolha quem pagou a despesa.");

    g_autofree Cents *shares = g_new0(Cents, n);
    if (d->split == SPLIT_EQUAL) shared_equal_shares(total, n, shares);
    else {
        Cents sum = 0;
        for (int i = 0; i < n; i++) {
            const char *t = d->people[i].share;
            if (blank(t)) shares[i] = 0;
            else if (!money_parse(t, &shares[i])) return fail(err, "Valor inválido", "Valor inválido para %s. Ex.: 150,00", (char *)names->pdata[i]);
        }
        for (int i = 0; i < n; i++) if (shares[i] < 0) return fail(err, "Valor inválido", "Os valores não podem ser negativos.");
        for (int i = 0; i < n; i++) sum += shares[i];
        if (sum != total) {
            Cents diff = total - sum;
            return fail(err, "Divisão não fecha", "A soma das partes (%s) precisa ser igual ao total (%s). %s %s.",
                        money_format(sum, b1), money_format(total, b2), diff > 0 ? "Faltam" : "Passou",
                        money_format(diff > 0 ? diff : -diff, b3));
        }
    }

    SharedExpense *old = NULL;
    if (edit_id) {
        old = app_shared(s, edit_id);
        if (!old) return fail(err, REVISE, "Despesa não encontrada.");
    }
    g_autofree char *new_id = old ? g_strdup(old->id) : ids_new();
    SharedExpense *e = shared_expense_new(new_id, name, total, d->date, "", d->split, "");
    g_autofree char *note = str_clean(d->note, SHARED_NOTE_MAX);
    tx_set_str(&e->note, note);
    if (old) { tx_set_str(&e->receipt, old->receipt); tx_set_str(&e->receipt_type, old->receipt_type); }
    for (int i = 0; i < n; i++) {
        const char *pid = d->people[i].id;
        g_autofree char *id = (old && pid && *pid && person_of(old, pid)) ? g_strdup(pid) : ids_new();
        g_ptr_array_add(e->people, shared_person_new(id, names->pdata[i], shares[i]));
    }
    tx_set_str(&e->payer_id, ((SharedPerson *)e->people->pdata[d->payer_index])->id);
    if (old)
        for (guint i = 0; i < old->payments->len; i++) {
            SharedPayment *p = old->payments->pdata[i];
            g_ptr_array_add(e->payments, shared_payment_new(p->id, p->person_id, p->value, p->date));
        }

    /* editar não pode deixar pagamentos "soltos": de quem saiu, de quem virou o pagador ou acima da nova parte */
    if (old) {
        SharedSummary st;
        shared_summary(e, &st);
        g_autoptr(GHashTable) done = g_hash_table_new(g_str_hash, g_str_equal);
        gboolean bad = FALSE;
        for (guint i = 0; i < e->payments->len && !bad; i++) {
            const char *pid = ((SharedPayment *)e->payments->pdata[i])->person_id;
            if (g_hash_table_contains(done, pid)) continue;
            g_hash_table_add(done, (gpointer)pid);
            const PersonStatus *who = NULL;
            for (guint j = 0; j < st.people->len; j++) {
                PersonStatus *x = &g_array_index(st.people, PersonStatus, j);
                if (strcmp(x->person->id, pid) == 0) who = x;
            }
            SharedPerson *op = person_of(old, pid);
            const char *old_name = op ? op->name : "um participante";
            if (!who) bad = !fail(err, "Pagamentos registrados", "%s tem pagamentos registrados. Exclua os pagamentos antes de remover essa pessoa.", old_name);
            else if (who->is_payer) bad = !fail(err, "Pagamentos registrados", "%s tem pagamentos registrados e não pode ser quem pagou. Exclua os pagamentos antes.", who->person->name);
            else if (who->paid > who->owed)
                bad = !fail(err, "Pagamentos registrados", "%s já devolveu %s, mais do que a nova parte (%s). Exclua algum pagamento antes.",
                            who->person->name, money_format(who->paid, b1), money_format(who->owed, b2));
        }
        shared_summary_clear(&st);
        if (bad) { shared_expense_free(e); return FALSE; }
    }
    if (!old) {
        AppCounts after = app_counts(s);
        after.shared++;
        const char *g = app_growth_error(app_counts(s), after);
        if (g) { shared_expense_free(e); return fail(err, "Limite atingido", "%s Exclua despesas antigas antes de criar novas.", g); }
        g_ptr_array_add(s->shared, e);
    } else {
        for (guint i = 0; i < s->shared->len; i++)
            if (s->shared->pdata[i] == old) {
                shared_expense_free(old);
                s->shared->pdata[i] = e;
            }
    }
    return TRUE;
}

gboolean shared_add_payment(AppState *s, const char *expense_id, const char *person_id, const char *value, Day date, OpErr *err) {
    char b[32];
    SharedExpense *e = app_shared(s, expense_id);
    if (!e) return fail(err, REVISE, "Despesa não encontrada.");
    SharedSummary st;
    shared_summary(e, &st);
    PersonStatus who = {0};
    gboolean found = FALSE;
    for (guint j = 0; j < st.people->len; j++) {
        PersonStatus *x = &g_array_index(st.people, PersonStatus, j);
        if (strcmp(x->person->id, person_id) == 0) { who = *x; found = TRUE; }
    }
    shared_summary_clear(&st);
    if (!found) return fail(err, REVISE, "Participante não encontrado.");
    if (who.is_payer) return fail(err, "Quem pagou", "%s pagou a despesa: não há o que devolver.", who.person->name);
    Cents v;
    if (!money_parse(value, &v) || v <= 0) return fail(err, "Valor inválido", "Informe um valor maior que zero. Ex.: 50,00");
    Cents rem = person_remaining(&who);
    if (rem == 0) return fail(err, "Já quitado", "%s já quitou a parte dele.", who.person->name);
    if (v > rem) return fail(err, "Valor acima do devido", "%s deve %s. Informe até esse valor.", who.person->name, money_format(rem, b));
    if (date == DAY_NONE) return fail(err, "Data inválida", "Informe a data do pagamento.");
    if (e->payments->len >= SHARED_PAYMENTS_MAX) return fail(err, "Limite atingido", "Limite de %d pagamentos nesta despesa.", SHARED_PAYMENTS_MAX);
    g_autofree char *id = ids_new();
    g_ptr_array_add(e->payments, shared_payment_new(id, person_id, v, date));
    return TRUE;
}

gboolean shared_settle(AppState *s, const char *expense_id, const char *person_id, Day date, OpErr *err) {
    SharedExpense *e = app_shared(s, expense_id);
    if (!e) return fail(err, REVISE, "Despesa não encontrada.");
    SharedSummary st;
    shared_summary(e, &st);
    Cents rem = -1;
    for (guint j = 0; j < st.people->len; j++) {
        PersonStatus *x = &g_array_index(st.people, PersonStatus, j);
        if (strcmp(x->person->id, person_id) == 0) rem = person_remaining(x);
    }
    shared_summary_clear(&st);
    if (rem < 0) return fail(err, REVISE, "Participante não encontrado.");
    char b[32];
    return shared_add_payment(s, expense_id, person_id, money_input(rem, b), date, err);
}

void shared_delete_payment(AppState *s, const char *expense_id, const char *payment_id) {
    SharedExpense *e = app_shared(s, expense_id);
    if (!e) return;
    for (guint i = 0; i < e->payments->len; i++)
        if (strcmp(((SharedPayment *)e->payments->pdata[i])->id, payment_id) == 0) { g_ptr_array_remove_index(e->payments, i); return; }
}

void shared_delete(AppState *s, const char *id) {
    SharedExpense *e = app_shared(s, id);
    if (e) g_ptr_array_remove(s->shared, e);
}

char *shared_pdf_name(const SharedExpense *e) {
    g_autofree char *f = text_fold(e->name);
    g_autofree char *low = g_ascii_strdown(f, -1);
    GString *slug = g_string_new(NULL);
    gboolean dash = FALSE;
    for (const char *p = low; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')) {
            if (dash && slug->len) g_string_append_c(slug, '-');
            dash = FALSE;
            g_string_append_c(slug, *p);
        } else dash = TRUE;
    }
    /* como no Kotlin: as pontas sem "-" primeiro, depois o corte em 40 (que pode terminar em "-") */
    if (slug->len > 40) g_string_truncate(slug, 40);
    if (!slug->len) g_string_append(slug, "despesa");
    char b[11];
    char *out = g_strdup_printf("comprovante-%s-%s.pdf", slug->str, day_iso(e->date, b));
    g_string_free(slug, TRUE);
    return out;
}

char *shared_image_name(const SharedExpense *e) {
    g_autofree char *pdf = shared_pdf_name(e);
    /* "comprovante-" + miolo + ".pdf" → "finanplus-" + miolo + ".png" */
    size_t n = strlen(pdf);
    g_autofree char *mid = g_strndup(pdf + strlen("comprovante-"), n - strlen("comprovante-") - 4);
    return g_strdup_printf("finanplus-%s.png", mid);
}
