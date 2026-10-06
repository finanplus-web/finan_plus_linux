/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Início: saldo atual e previsto, receitas e despesas do mês, assistente, contas e cartões,
 * limites do mês, metas e próximos vencimentos. Em telas largas fica em três colunas.
 */
#include "pages.h"
#include "widgets.h"
#include "core/finance.h"
#include <math.h>
#include <string.h>

static GtkWidget *body;

/* "37,5" — número com vírgula */
static char *dec(double v, int places) {
    char *s = g_strdup_printf("%.*f", places, v);
    for (char *p = s; *p; p++) if (*p == '.') *p = ',';
    return s;
}

static void open_tx_income(gpointer u) { (void)u; editor_tx(KIND_INCOME, NULL); }
static void open_tx_expense(gpointer u) { (void)u; editor_tx(KIND_EXPENSE, NULL); }
static void open_goal_new(gpointer u) { (void)u; editor_goal(NULL); }
static void open_goal(gpointer id) { editor_goal(id); }
static void open_settings(gpointer u) { (void)u; app_show_page(PAGE_SETTINGS); }
static void open_pay(gpointer id) { editor_pay_invoice(id); }
static void open_limit_new(gpointer u) { (void)u; editor_limit(NULL); }
static void open_tx(gpointer id) { Tx *t = app_tx(APP->state, id); if (t) editor_tx(t->kind, id); }

static GtkWidget *stat_box_icon(const char *icon, const char *label, Cents v, const char *money_classes) {
    GtkWidget *b = w_vbox(4);
    gtk_widget_add_css_class(b, "fin-stat");
    gtk_widget_set_hexpand(b, TRUE);
    w_add(b, icon ? w_icon_label(icon, label, "fin-muted caption") : w_label(label, "fin-muted caption"));
    w_add(b, w_money(v, money_classes));
    return b;
}
#define stat_box(l, v, c) stat_box_icon(NULL, l, v, c)

static GtkWidget *quick(const char *icon, const char *label, FinFn fn, const char *tip) {
    GtkWidget *b = w_button(label, icon, "fin-quick", fn, NULL, NULL);
    gtk_widget_set_hexpand(b, TRUE);
    gtk_widget_set_tooltip_text(b, tip);
    return b;
}

static GtkWidget *hero(void) {
    const AppState *s = APP->state;
    Ym ym = day_ym(APP->today);
    Flow f = month_flow(s, ym);
    Cents bal = current_balance(s), fut = future_balance(s, ym_last(ym), APP->today);
    GtkWidget *c = w_card("fin-hero");
    gtk_widget_set_valign(c, GTK_ALIGN_START);

    GtkWidget *top = w_hbox(8);
    g_autofree char *date = br_full_date(APP->today); /* ex.: "03 de Outubro de 2026" — muda sozinha com o dia */
    GtkWidget *dl = w_label(date, "heading");
    gtk_widget_set_hexpand(dl, TRUE);
    w_add(top, dl);
    GtkWidget *priv = w_hbox(4); /* selo "Privado" com o ícone de escudo (Material Symbols) */
    gtk_widget_add_css_class(priv, "fin-badge");
    gtk_widget_add_css_class(priv, "green");
    gtk_widget_set_valign(priv, GTK_ALIGN_CENTER);
    w_add(priv, w_icon("shield", 13));
    w_add(priv, gtk_label_new("Privado"));
    gtk_widget_set_tooltip_text(priv, "Seus dados ficam só neste computador, criptografados.");
    w_add(top, priv);
    w_add(c, top);

    GtkWidget *r1 = w_hbox(10);
    gtk_widget_set_margin_top(r1, 10);
    w_add(r1, stat_box("Saldo atual", bal, bal < 0 ? "fin-money-big fin-red" : "fin-money-big"));
    w_add(r1, stat_box("Saldo previsto", fut, fut < 0 ? "fin-money-big fin-red" : "fin-money-big fin-accent"));
    gtk_box_set_homogeneous(GTK_BOX(r1), TRUE);
    w_add(c, r1);
    GtkWidget *r2 = w_hbox(10);
    gtk_box_set_homogeneous(GTK_BOX(r2), TRUE);
    w_add(r2, stat_box_icon("arrow-upward", "Receitas do mês", f.income, "fin-money-mid fin-green"));
    w_add(r2, stat_box_icon("arrow-downward", "Despesas do mês", f.expense, "fin-money-mid fin-red"));
    w_add(c, r2);

    GtkWidget *bar = w_level(f.income > 0 ? (double)f.expense / f.income : 0, NULL);
    gtk_widget_set_margin_top(bar, 8);
    w_add(c, bar);
    double used = f.income > 0 ? f.expense * 100.0 / f.income : 0;
    g_autofree char *u = dec(used, 1);
    g_autofree char *msg = f.income > 0 ? g_strdup_printf("Neste mês você usou %s%% das receitas.", u) : g_strdup("Adicione seus primeiros lançamentos.");
    w_add(c, w_label_wrap(msg, "fin-muted caption heading"));
    if (f.income > 0) {
        g_autofree char *sv = dec(MAX(0.0, 100 - used), 0);
        g_autofree char *badge = g_strdup_printf("%s%% economizado", sv);
        GtkWidget *b = w_badge(badge, "accent");
        gtk_widget_set_halign(b, GTK_ALIGN_START);
        w_add(c, b);
    }
    GtkWidget *q = w_hbox(9);
    gtk_box_set_homogeneous(GTK_BOX(q), TRUE);
    gtk_widget_set_margin_top(q, 8);
    w_add(q, quick("add", "Receita", open_tx_income, "Nova receita (Ctrl+Shift+N)"));
    w_add(q, quick("remove", "Despesa", open_tx_expense, "Nova despesa (Ctrl+N)"));
    w_add(q, quick("flag", "Meta", open_goal_new, "Nova meta (Ctrl+M)"));
    w_add(c, q);
    return c;
}

/* ---------------------------------------------------------------- assistente no Início */

static void dismiss_tip(gpointer id) {
    prefs_dismiss_tip(APP->prefs, id);
    prefs_save(APP->prefs, NULL);
    app_refresh_page(PAGE_HOME);
    app_refresh_page(PAGE_ASSIST);
    app_toast("Dica dispensada. Você pode trazê-la de volta no Assistente.");
}

typedef struct { char *query; Day from, to; } OpenMoves;
static void open_moves_free(gpointer p) { OpenMoves *o = p; g_free(o->query); g_free(o); }
static void open_moves(gpointer p) { OpenMoves *o = p; app_open_moves(o->query, o->from, o->to, -1); }

GtkWidget *tip_item(const Insight *t, gboolean dismissible);
GtkWidget *tip_item(const Insight *t, gboolean dismissible) {
    GtkWidget *b = w_vbox(4);
    gtk_widget_add_css_class(b, "fin-tip");
    w_add(b, w_eyebrow(insight_type_label(t->type)));
    w_add(b, w_label_wrap(t->title, "heading"));
    w_add(b, w_label_wrap(t->text, NULL));
    w_add(b, w_why(t->why));
    GtkWidget *row = w_hbox(8);
    if (t->query || t->from != DAY_NONE) {
        OpenMoves *o = g_new0(OpenMoves, 1);
        o->query = g_strdup(t->query);
        o->from = t->from;
        o->to = t->to;
        w_add(row, w_pill("Ver lançamentos", open_moves, o, open_moves_free));
    }
    if (dismissible) w_add(row, w_pill("Dispensar", dismiss_tip, g_strdup(t->id), g_free));
    if (gtk_widget_get_first_child(row)) w_add(b, row);
    return b;
}

static void go_assist(gpointer u) { (void)u; app_show_page(PAGE_ASSIST); }
static void go_ask(gpointer u) { (void)u; app_show_page(PAGE_ASSIST); page_assist_focus_question(); }

static GtkWidget *assistant_card(void) {
    Prefs *pr = APP->prefs;
    if (!pr->assist_tips && !pr->assist_ask) return NULL;
    GtkWidget *c = w_card(NULL);
    gtk_widget_set_valign(c, GTK_ALIGN_START);
    GtkWidget *eb = w_hbox(6);
    w_add(eb, w_icon("auto-awesome", 14));
    GtkWidget *ebl = w_eyebrow("Assistente · neste computador");
    gtk_widget_set_hexpand(ebl, TRUE);
    w_add(eb, ebl);
    w_add(c, eb);
    MonthReport *r = pr->assist_tips ? insights_report(APP->state, APP->today, app_money_fmt()) : NULL;
    w_add(c, w_title(r ? r->title : "Pergunte sobre seus gastos"));
    int visible = 0;
    if (r) {
        for (guint i = 0; i < r->lines->len; i++) {
            g_autofree char *l = g_strdup_printf("• %s", (char *)r->lines->pdata[i]);
            w_add(c, w_label_wrap(l, NULL));
        }
        w_add(c, w_why(r->why));
        g_autoptr(GPtrArray) tips = insights_tips(APP->state, APP->today, app_money_fmt());
        for (guint i = 0; i < tips->len; i++) {
            Insight *t = tips->pdata[i];
            if (prefs_tip_dismissed(pr, t->id)) continue;
            if (visible < 2) w_add(c, tip_item(t, TRUE));
            visible++;
        }
        if (visible == 0 && APP->state->txs->len)
            w_add(c, w_label_wrap("Nenhuma dica no momento: nada fora do padrão.", "fin-muted caption"));
        month_report_free(r);
    }
    GtkWidget *row = w_hbox(8);
    gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
    gtk_widget_set_margin_top(row, 4);
    if (pr->assist_tips) {
        g_autofree char *l = visible > 2 ? g_strdup_printf("Ver as %d dicas", visible) : g_strdup("Abrir assistente");
        w_add(row, w_pill(l, go_assist, NULL, NULL));
    }
    if (pr->assist_ask) {
        GtkWidget *p = w_pill("Perguntar", go_ask, NULL, NULL);
        gtk_widget_set_tooltip_text(p, "Perguntar ao assistente (Ctrl+K)");
        w_add(row, p);
    }
    w_add(c, row);
    return c;
}

/* ---------------------------------------------------------------- contas e cartões */

static GtkWidget *patrimony(void) {
    const AppState *s = APP->state;
    GtkWidget *wrap = w_vbox(10);
    GtkWidget *head = w_hbox(8);
    GtkWidget *h = w_section_head("Patrimônio", "Contas e cartões");
    gtk_widget_set_hexpand(h, TRUE);
    w_add(head, h);
    GtkWidget *g = w_pill("Gerenciar", open_settings, NULL, NULL);
    gtk_widget_set_valign(g, GTK_ALIGN_END);
    w_add(head, g);
    w_add(wrap, head);
    GtkWidget *flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 10);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 10);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 6);
    for (guint i = 0; i < s->accounts->len; i++) {
        Account *a = s->accounts->pdata[i];
        GtkWidget *c = w_card("fin-tight fin-flat");
        gtk_widget_set_size_request(c, 200, -1);
        w_add(c, w_label(a->name, "fin-muted caption"));
        Cents b = account_balance(s, a);
        w_add(c, w_money(b, b < 0 ? "fin-money-mid fin-red" : "fin-money-mid"));
        w_add(c, w_label("Conta", "fin-muted caption"));
        gtk_flow_box_append(GTK_FLOW_BOX(flow), c);
    }
    for (guint i = 0; i < s->cards->len; i++) {
        Card *k = s->cards->pdata[i];
        CardStatus st;
        card_status(s, k, APP->today, &st);
        const Invoice *cur = card_status_current(&st);
        GtkWidget *c = w_card("fin-tight fin-flat");
        gtk_widget_set_size_request(c, 200, -1);
        g_autofree char *info = NULL;
        if (cur) {
            g_autofree char *ml = br_month_label(cur->ym);
            char d[11];
            info = g_strdup_printf("%s · Fatura %s · vence %s%s", k->name, ml, day_br(cur->due, d), cur->closed ? " · fechada" : "");
        } else info = g_strdup_printf("%s · Sem fatura em aberto", k->name);
        GtkWidget *il = w_label_wrap(info, "fin-muted caption");
        gtk_label_set_lines(GTK_LABEL(il), 2);
        gtk_label_set_ellipsize(GTK_LABEL(il), PANGO_ELLIPSIZE_END);
        w_add(c, il);
        w_add(c, w_money(cur ? invoice_open(cur) : 0, "fin-money-mid"));
        GtkWidget *av = w_hbox(4);
        w_add(av, w_label("Disponível", "fin-muted caption"));
        w_add(av, w_money(st.available, "fin-muted caption"));
        w_add(c, av);
        if (cur) {
            GtkWidget *pay = w_button("Pagar fatura", "credit-card", "suggested-action pill", open_pay, g_strdup(k->id), g_free);
            gtk_widget_set_halign(pay, GTK_ALIGN_START);
            w_add(c, pay);
        }
        card_status_clear(&st);
        gtk_flow_box_append(GTK_FLOW_BOX(flow), c);
    }
    w_add(wrap, flow);
    return wrap;
}

/* ---------------------------------------------------------------- limites */

static GtkWidget *limits_card(void) {
    const AppState *s = APP->state;
    Ym ym = day_ym(APP->today);
    GtkWidget *c = w_card(NULL);
    gtk_widget_set_valign(c, GTK_ALIGN_START);
    w_add(c, w_section_head("Orçamento · inclui pendentes", "Limites do mês"));
    if (!s->limits->len) {
        w_add(c, w_label_wrap("Defina limites para acompanhar seu orçamento.", "fin-muted"));
        GtkWidget *b = w_pill("＋ Definir limite", open_limit_new, NULL, NULL);
        gtk_widget_set_halign(b, GTK_ALIGN_START);
        w_add(c, b);
    }
    for (guint i = 0; i < s->limits->len; i++) {
        Limit *l = s->limits->pdata[i];
        Cents u = budget_usage(s, ym, l->category);
        double pct = l->value > 0 ? u * 100.0 / l->value : 0;
        GtkWidget *item = w_vbox(4);
        gtk_widget_set_margin_top(item, 6);
        GtkWidget *top = w_hbox(4);
        GtkWidget *name = w_label(l->category, NULL);
        gtk_widget_set_hexpand(name, TRUE);
        w_add(top, name);
        w_add(top, w_money(u, NULL));
        w_add(top, w_label(" / ", "fin-muted"));
        w_add(top, w_money(l->value, NULL));
        w_add(item, top);
        w_add(item, w_level(pct / 100, u > l->value ? "over" : pct >= 80 ? "warn" : NULL));
        g_autofree char *p0 = dec(pct, 0);
        g_autofree char *status = u > l->value ? g_strdup("Limite ultrapassado")
                                 : pct >= 100 ? g_strdup("Limite atingido")
                                 : pct >= 80 ? g_strdup_printf("Atenção: %s%% usado", p0)
                                             : g_strdup_printf("%s%% usado", p0);
        w_add(item, w_label(status, u > l->value ? "fin-red caption" : "fin-muted caption"));
        w_add(c, item);
    }
    return c;
}

/* ---------------------------------------------------------------- metas */

static void goal_clicked(GtkButton *b, gpointer u) {
    (void)u;
    open_goal(g_object_get_data(G_OBJECT(b), "id"));
}

static GtkWidget *goals_card(void) {
    const AppState *s = APP->state;
    GtkWidget *c = w_card(NULL);
    gtk_widget_set_valign(c, GTK_ALIGN_START);
    GtkWidget *head = w_hbox(8);
    GtkWidget *h = w_section_head("Objetivos", "Metas");
    gtk_widget_set_hexpand(h, TRUE);
    w_add(head, h);
    GtkWidget *nb = w_pill("＋ Meta", open_goal_new, NULL, NULL);
    gtk_widget_set_valign(nb, GTK_ALIGN_END);
    w_add(head, nb);
    w_add(c, head);
    if (!s->goals->len) w_add(c, w_label_wrap("Crie uma meta com “＋ Meta”: o Finan+ calcula quanto guardar por mês.", "fin-muted"));
    for (guint i = 0; i < s->goals->len; i++) {
        Goal *g = s->goals->pdata[i];
        GoalPlan p = goal_plan(g, APP->today);
        GtkWidget *btn = gtk_button_new();
        gtk_widget_add_css_class(btn, "fin-row");
        gtk_widget_set_tooltip_text(btn, "Editar meta, guardar ou retirar valor");
        GtkWidget *b = w_vbox(6);
        GtkWidget *top = w_hbox(4);
        GtkWidget *n = w_label(g->name, "heading");
        gtk_widget_set_hexpand(n, TRUE);
        w_add(top, n);
        w_add(top, w_money(g->saved, NULL));
        w_add(top, w_label(" / ", "fin-muted"));
        w_add(top, w_money(g->target, NULL));
        w_add(b, top);
        w_add(b, w_level((double)g->saved / g->target, NULL));
        char d[11];
        g_autofree char *info = g_strdup_printf("%s%s", g->deadline != DAY_NONE ? "" : "Sem prazo", p.past_due ? " · prazo vencido" : "");
        if (g->deadline != DAY_NONE) { g_free(info); info = g_strdup_printf("Até %s%s", day_br(g->deadline, d), p.past_due ? " · prazo vencido" : ""); }
        GString *plan = g_string_new(NULL);
        if (p.done) g_string_append(plan, "Meta atingida");
        else {
            if (p.has_needed) { g_autofree char *m = app_money(p.needed); g_string_append_printf(plan, "Precisa de %s/mês", m); }
            if (p.eta != YM_NONE) {
                g_autofree char *ml = br_month_label(p.eta);
                g_string_append_printf(plan, "%sPlano: conclui em %s%s", plan->len ? " · " : "", ml, p.late ? " (após o prazo)" : "");
            }
        }
        GtkWidget *bottom = w_hbox(8);
        GtkWidget *il = w_label(info, "fin-muted caption");
        gtk_widget_set_hexpand(il, TRUE);
        w_add(bottom, il);
        GtkWidget *pl = w_label_wrap(plan->str, "fin-muted caption");
        gtk_label_set_xalign(GTK_LABEL(pl), 1);
        gtk_label_set_justify(GTK_LABEL(pl), GTK_JUSTIFY_RIGHT);
        w_add(bottom, pl);
        g_string_free(plan, TRUE);
        w_add(b, bottom);
        gtk_button_set_child(GTK_BUTTON(btn), b);
        g_signal_connect(btn, "clicked", G_CALLBACK(goal_clicked), NULL);
        w_add(c, btn);
        g_object_set_data_full(G_OBJECT(btn), "id", g_strdup(g->id), g_free);
    }
    return c;
}

/* ---------------------------------------------------------------- próximos vencimentos */

static GtkWidget *due_card(void) {
    const AppState *s = APP->state;
    GtkWidget *c = w_card(NULL);
    gtk_widget_set_valign(c, GTK_ALIGN_START);
    w_add(c, w_section_head("Próximos 30 dias", "Vencimentos"));
    g_autoptr(GPtrArray) r = reminders(s, APP->today, 30);
    if (!r->len) w_add(c, w_label_wrap("Nada pendente para os próximos 30 dias.", "fin-muted"));
    for (guint i = 0; i < r->len && i < 6; i++) {
        Reminder *m = r->pdata[i];
        GtkWidget *row = w_hbox(10);
        gtk_widget_set_margin_top(row, 4);
        const char *icon = m->type == REMINDER_INVOICE_DUE ? "credit-card" : m->type == REMINDER_INCOME_DUE ? "arrow-upward" : "receipt-long";
        w_add(row, w_icon(icon, 18));
        GtkWidget *texts = w_vbox(1);
        gtk_widget_set_hexpand(texts, TRUE);
        w_add(texts, w_label(m->title, "heading"));
        char d[11];
        g_autofree char *when = m->date < APP->today ? g_strdup_printf("Em atraso · %s", day_br(m->date, d))
                               : m->date == APP->today ? g_strdup(m->type == REMINDER_INCOME_DUE ? "A receber hoje" : "Vence hoje")
                                                       : g_strdup_printf("%s %s", m->type == REMINDER_INCOME_DUE ? "Recebe em" : "Vence em", day_br(m->date, d));
        w_add(texts, w_label(when, m->date < APP->today && m->type != REMINDER_INCOME_DUE ? "fin-late caption" : "fin-muted caption"));
        w_add(row, texts);
        w_add(row, w_money(m->amount, m->type == REMINDER_INCOME_DUE ? "fin-green" : NULL));
        GtkWidget *btn = m->type == REMINDER_INVOICE_DUE ? w_button(NULL, "chevron-right", "flat circular", open_pay, g_strdup(m->ref_id), g_free)
                                                         : w_button(NULL, "chevron-right", "flat circular", open_tx, g_strdup(m->ref_id), g_free);
        gtk_widget_set_tooltip_text(btn, m->type == REMINDER_INVOICE_DUE ? "Pagar fatura" : "Abrir lançamento");
        gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
        w_add(row, btn);
        w_add(c, row);
    }
    if (r->len > 6) {
        g_autofree char *more = g_strdup_printf("e mais %u", r->len - 6);
        w_add(c, w_label(more, "fin-muted caption"));
    }
    return c;
}

/* ---------------------------------------------------------------- montagem */

static GtkWidget *grid(int cols) {
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g), 18);
    gtk_grid_set_row_spacing(GTK_GRID(g), 18);
    gtk_grid_set_column_homogeneous(GTK_GRID(g), TRUE);
    (void)cols;
    return g;
}

void page_home_refresh(void) {
    if (!body || !APP->state) return;
    w_clear(body);
    GtkWidget *h = hero();
    GtkWidget *a = assistant_card();
    GtkWidget *pat = patrimony();
    GtkWidget *lim = limits_card();
    GtkWidget *goals = goals_card();
    GtkWidget *due = due_card();
    GtkWidget *g = grid(3);
    /* coluna principal (saldo + vencimentos) ao lado do assistente: sem buracos entre os cartões */
    GtkWidget *main = w_vbox(18);
    w_add(main, h);
    if (APP->layout != LAYOUT_NARROW) w_add(main, due);
    switch (APP->layout) {
    case LAYOUT_WIDE:
        gtk_grid_attach(GTK_GRID(g), main, 0, 0, a ? 2 : 3, 1);
        if (a) gtk_grid_attach(GTK_GRID(g), a, 2, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(g), pat, 0, 1, 3, 1);
        gtk_grid_attach(GTK_GRID(g), lim, 0, 2, 1, 1);
        gtk_grid_attach(GTK_GRID(g), goals, 1, 2, 2, 1);
        break;
    case LAYOUT_MEDIUM:
        gtk_grid_attach(GTK_GRID(g), main, 0, 0, a ? 1 : 2, 1);
        if (a) gtk_grid_attach(GTK_GRID(g), a, 1, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(g), pat, 0, 1, 2, 1);
        gtk_grid_attach(GTK_GRID(g), lim, 0, 2, 1, 1);
        gtk_grid_attach(GTK_GRID(g), goals, 1, 2, 1, 1);
        break;
    default: {
        int r = 0;
        gtk_grid_attach(GTK_GRID(g), main, 0, r++, 1, 1);
        if (a) gtk_grid_attach(GTK_GRID(g), a, 0, r++, 1, 1);
        gtk_grid_attach(GTK_GRID(g), due, 0, r++, 1, 1);
        gtk_grid_attach(GTK_GRID(g), pat, 0, r++, 1, 1);
        gtk_grid_attach(GTK_GRID(g), lim, 0, r++, 1, 1);
        gtk_grid_attach(GTK_GRID(g), goals, 0, r++, 1, 1);
    }
    }
    w_add(body, g);
}

GtkWidget *page_home_new(void) {
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 1500);
    adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp), 1200);
    body = w_vbox(18);
    gtk_widget_set_margin_start(body, 24);
    gtk_widget_set_margin_end(body, 24);
    gtk_widget_set_margin_top(body, 20);
    gtk_widget_set_margin_bottom(body, 32);
    adw_clamp_set_child(ADW_CLAMP(clamp), body);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), clamp);
    return sw;
}
