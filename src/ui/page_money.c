/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Meu dinheiro (1.6.0, como no app Android): contas e cartões, limites mensais, metas, atalho para o
 * simulador e categorias (que começam minimizadas). Tudo isso saiu do Início e de Ajustes.
 * Cada parte vazia explica o que fazer e oferece um atalho.
 */
#include "pages.h"
#include "widgets.h"
#include "core/finance.h"
#include <string.h>

static GtkWidget *body;
static gboolean cats_open = FALSE;

static char *dec0(double v) { return g_strdup_printf("%.0f", v); }

static void open_account(gpointer id) { editor_account(id); }
static void open_card(gpointer id) { editor_card(id); }
static void new_account(gpointer u) { (void)u; editor_account(NULL); }
static void new_card(gpointer u) { (void)u; editor_card(NULL); }
static void open_pay(gpointer id) { editor_pay_invoice(id); }
static void open_limit(gpointer cat) { editor_limit(cat); }
static void new_limit(gpointer u) { (void)u; editor_limit(NULL); }
static void open_goal(gpointer id) { editor_goal(id); }
static void new_goal(gpointer u) { (void)u; editor_goal(NULL); }
static void open_sim(gpointer u) { (void)u; simulator_open(); }

static GtkWidget *boxed(void) {
    GtkWidget *l = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(l), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(l, "boxed-list");
    return l;
}

static GtkWidget *manage_row(GtkWidget *list, const char *title, const char *subtitle, GtkWidget *btn) {
    GtkWidget *r = adw_action_row_new();
    g_autofree char *t = g_markup_escape_text(title, -1);
    g_autofree char *s = g_markup_escape_text(subtitle, -1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), t);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(r), s);
    if (btn) { gtk_widget_set_valign(btn, GTK_ALIGN_CENTER); adw_action_row_add_suffix(ADW_ACTION_ROW(r), btn); }
    gtk_list_box_append(GTK_LIST_BOX(list), r);
    return r;
}

/* cabeçalho do cartão: ícone, título, subtítulo e uma ação à direita (ou nada) */
static GtkWidget *money_head(const char *icon, const char *title, const char *sub, GtkWidget *action) {
    GtkWidget *row = w_hbox(12);
    GtkWidget *tile = w_icon(icon, 22);
    gtk_widget_add_css_class(tile, "fin-tile");
    gtk_widget_set_valign(tile, GTK_ALIGN_CENTER);
    w_add(row, tile);
    GtkWidget *t = w_vbox(1);
    gtk_widget_set_hexpand(t, TRUE);
    gtk_widget_set_valign(t, GTK_ALIGN_CENTER);
    w_add(t, w_label(title, "heading"));
    w_add(t, w_label_wrap(sub, "fin-muted caption"));
    w_add(row, t);
    if (action) { gtk_widget_set_valign(action, GTK_ALIGN_CENTER); w_add(row, action); }
    return row;
}

static GtkWidget *card_box(void) {
    GtkWidget *c = w_card(NULL);
    gtk_widget_set_valign(c, GTK_ALIGN_START);
    return c;
}

/* ---------------------------------------------------------------- contas e cartões */

static GtkWidget *wallet(void) {
    const AppState *s = APP->state;
    GtkWidget *c = card_box();
    w_add(c, money_head("account-balance", "Contas e cartões", "Saldo das contas e situação dos cartões", NULL));
    if (!s->cards->len) w_add(c, w_label_wrap("Sem cartão cadastrado. Você pode adicionar um para acompanhar faturas e vencimentos.", "fin-muted caption"));
    GtkWidget *list = boxed();
    for (guint i = 0; i < s->accounts->len; i++) {
        Account *a = s->accounts->pdata[i];
        g_autofree char *m = app_money(account_balance(s, a));
        g_autofree char *sub = g_strdup_printf("Conta · Saldo %s", m);
        GtkWidget *r = manage_row(list, a->name, sub, w_pill("Editar", open_account, g_strdup(a->id), g_free));
        adw_action_row_add_prefix(ADW_ACTION_ROW(r), w_icon("account-balance-wallet", 20));
    }
    for (guint i = 0; i < s->cards->len; i++) {
        Card *k = s->cards->pdata[i];
        CardStatus st;
        card_status(s, k, APP->today, &st);
        g_autofree char *l = app_money(k->limit), *u = app_money(st.used);
        g_autofree char *sub = g_strdup_printf("Cartão · limite %s · usado %s · fecha dia %d · vence dia %d", l, u, k->close, k->due);
        card_status_clear(&st);
        GtkWidget *r = manage_row(list, k->name, sub, w_pill("Editar", open_card, g_strdup(k->id), g_free));
        adw_action_row_add_prefix(ADW_ACTION_ROW(r), w_icon("credit-card", 20));
    }
    w_add(c, list);
    GtkWidget *row = w_hbox(8);
    gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
    w_add(row, w_button("Adicionar conta", "add", "fin-pill", new_account, NULL, NULL));
    w_add(row, w_button("Adicionar cartão", "add", "fin-pill", new_card, NULL, NULL));
    w_add(c, row);
    /* fatura atual de cada cartão, com "Pagar fatura" */
    for (guint i = 0; i < s->cards->len; i++) {
        Card *k = s->cards->pdata[i];
        CardStatus st;
        card_status(s, k, APP->today, &st);
        const Invoice *cur = card_status_current(&st);
        if (cur) {
            GtkWidget *inv = w_hbox(10);
            GtkWidget *t = w_vbox(1);
            gtk_widget_set_hexpand(t, TRUE);
            g_autofree char *ml = br_month_label(cur->ym);
            char d[11];
            g_autofree char *info = g_strdup_printf("Fatura %s · %s · vence %s", k->name, ml, day_br(cur->due, d));
            w_add(t, w_label_wrap(info, "fin-muted caption"));
            w_add(t, w_money(invoice_open(cur), "fin-money-mid"));
            w_add(inv, t);
            GtkWidget *pay = w_button("Pagar fatura", "credit-card", "suggested-action pill", open_pay, g_strdup(k->id), g_free);
            gtk_widget_set_valign(pay, GTK_ALIGN_CENTER);
            w_add(inv, pay);
            w_add(c, inv);
        }
        card_status_clear(&st);
    }
    return c;
}

/* ---------------------------------------------------------------- limites */

static GtkWidget *limits(void) {
    const AppState *s = APP->state;
    Ym ym = day_ym(APP->today);
    GtkWidget *c = card_box();
    w_add(c, money_head("track-changes", "Limites mensais", "Acompanhe quanto gasta por categoria",
                        s->limits->len ? w_button("Adicionar limite", "add", "fin-pill", new_limit, NULL, NULL) : NULL));
    if (!s->limits->len) {
        w_add(c, w_label_wrap("Defina quanto deseja gastar por categoria para acompanhar seus gastos do mês.", "fin-muted caption"));
        w_add(c, w_button("Criar meu primeiro limite", "add", "fin-pill", new_limit, NULL, NULL));
        return c;
    }
    w_add(c, w_label_wrap("Inclui lançamentos pendentes deste mês.", "fin-muted caption"));
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
        GtkWidget *ed = w_pill("Editar", open_limit, g_strdup(l->category), g_free);
        gtk_widget_set_margin_start(ed, 8);
        w_add(top, ed);
        w_add(item, top);
        w_add(item, w_level(pct / 100, u > l->value ? "over" : pct >= 80 ? "warn" : NULL));
        g_autofree char *p0 = dec0(pct);
        /* com "Ocultar valores" a porcentagem também some (revelaria a proporção) */
        g_autofree char *status = u > l->value ? g_strdup("Limite ultrapassado")
                                 : pct >= 100 ? g_strdup("Limite atingido")
                                 : app_hidden() ? g_strdup(pct >= 80 ? "Atenção: perto do limite" : "Dentro do limite")
                                 : pct >= 80 ? g_strdup_printf("Atenção: %s%% usado", p0)
                                             : g_strdup_printf("%s%% usado", p0);
        w_add(item, w_label(status, u > l->value ? "fin-red caption" : "fin-muted caption"));
        w_add(c, item);
    }
    return c;
}

/* ---------------------------------------------------------------- metas */

static void goal_clicked(GtkButton *b, gpointer u) { (void)u; open_goal(g_object_get_data(G_OBJECT(b), "id")); }

static GtkWidget *goals(void) {
    const AppState *s = APP->state;
    GtkWidget *c = card_box();
    w_add(c, money_head("flag", "Metas", "Acompanhe seus objetivos financeiros",
                        s->goals->len ? w_button("Criar meta", "add", "fin-pill", new_goal, NULL, NULL) : NULL));
    if (!s->goals->len) {
        w_add(c, w_label_wrap("Uma meta ajuda a acompanhar um objetivo, como uma viagem ou uma reserva.", "fin-muted caption"));
        w_add(c, w_button("Criar minha primeira meta", "add", "fin-pill", new_goal, NULL, NULL));
        return c;
    }
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
        g_autofree char *info = g->deadline != DAY_NONE ? g_strdup_printf("Até %s%s", day_br(g->deadline, d), p.past_due ? " · prazo vencido" : "")
                                                        : g_strdup("Sem prazo");
        GString *plan = g_string_new(NULL);
        if (p.done) g_string_append(plan, "Meta atingida");
        else {
            if (p.has_needed) { g_autofree char *m = app_money(p.needed); g_string_append_printf(plan, "Precisa de %s/mês", m); }
            if (p.eta != YM_NONE) {
                g_autofree char *ml = br_month_label(p.eta);
                g_string_append_printf(plan, "%sConclui em %s%s", plan->len ? " · " : "", ml, p.late ? " (após o prazo)" : "");
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
        g_object_set_data_full(G_OBJECT(btn), "id", g_strdup(g->id), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(goal_clicked), NULL);
        w_add(c, btn);
    }
    return c;
}

/* ---------------------------------------------------------------- simulador */

static GtkWidget *simulate(void) {
    GtkWidget *c = card_box();
    w_add(c, money_head("auto-awesome", "Simular meu dinheiro", "Teste cenários sem alterar seus dados", NULL));
    w_add(c, w_label_wrap("Explore compras, mudanças na renda, economias e quitação de dívidas.", "fin-muted caption"));
    GtkWidget *b = w_button("Abrir simulação", "auto-awesome", "fin-pill", open_sim, NULL, NULL);
    gtk_widget_set_halign(b, GTK_ALIGN_START);
    w_add(c, b);
    return c;
}

/* ---------------------------------------------------------------- categorias (minimizadas por padrão) */

static GtkWidget *new_cat_entry, *new_cat_kind;
static guint new_cat_kind_sel = 0; /* mantém Despesa/Receita depois de adicionar */

static void add_category(gpointer u) {
    (void)u;
    OpErr e;
    new_cat_kind_sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(new_cat_kind));
    Kind k = new_cat_kind_sel == 1 ? KIND_INCOME : KIND_EXPENSE;
    if (!ops_add_category(APP->state, k, gtk_editable_get_text(GTK_EDITABLE(new_cat_entry)), &e)) { dlg_notice(e.title, e.msg); return; }
    app_commit();
    app_toast("Categoria adicionada");
}

typedef struct { Kind kind; char *name; } CatRef;
static void cat_ref_free(gpointer p) { CatRef *c = p; g_free(c->name); g_free(c); }
static CatRef *cat_ref(Kind k, const char *n) { CatRef *c = g_new0(CatRef, 1); c->kind = k; c->name = g_strdup(n); return c; }

static void do_rename(const char *name, gpointer p) {
    CatRef *c = p;
    OpErr e;
    if (!ops_rename_category(APP->state, c->kind, c->name, name, &e)) { dlg_notice(e.title, e.msg); return; }
    app_commit();
}

static void rename_cat(gpointer p) {
    CatRef *c = p;
    g_autofree char *msg = g_strdup_printf("Lançamentos, recorrências e limites de “%s” passam a usar o novo nome.", c->name);
    dlg_input("Renomear categoria", msg, "Novo nome", c->name, FALSE, do_rename, cat_ref(c->kind, c->name), cat_ref_free);
}

static void do_delete_cat(gpointer p) {
    CatRef *c = p;
    ops_delete_category(APP->state, c->kind, c->name);
    app_commit();
}

static void delete_cat(gpointer p) {
    CatRef *c = p;
    OpErr e;
    if (!ops_check_delete_category(APP->state, c->kind, c->name, &e)) { dlg_notice(e.title, e.msg); return; }
    int used = ops_category_use_count(APP->state, c->kind, c->name);
    g_autofree char *msg = used > 0 ? g_strdup_printf("Excluir “%s” da lista? %d lançamento(s) antigo(s) continuará(ão) com essa categoria no histórico.", c->name, used)
                                    : g_strdup_printf("Excluir a categoria “%s”?", c->name);
    dlg_confirm("Excluir categoria", msg, "Excluir", TRUE, do_delete_cat, cat_ref(c->kind, c->name), cat_ref_free);
}

static void toggle_cats(gpointer u) { (void)u; cats_open = !cats_open; page_money_refresh(); }

static GtkWidget *categories(void) {
    const AppState *s = APP->state;
    GtkWidget *c = card_box();
    GtkWidget *tg = w_button(cats_open ? "Minimizar" : "Maximizar", "expand-more", "fin-pill", toggle_cats, NULL, NULL);
    gtk_accessible_update_state(GTK_ACCESSIBLE(tg), GTK_ACCESSIBLE_STATE_EXPANDED, cats_open, -1);
    w_add(c, money_head("tune", "Categorias", "Organize relatórios e lançamentos", tg));
    if (!cats_open) return c;
    GtkWidget *row = w_hbox(8);
    new_cat_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(new_cat_entry), "Nova categoria");
    gtk_entry_set_max_length(GTK_ENTRY(new_cat_entry), 40);
    gtk_widget_set_hexpand(new_cat_entry, TRUE);
    static const char *const kinds[] = {"Despesa", "Receita", NULL};
    new_cat_kind = gtk_drop_down_new_from_strings(kinds);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(new_cat_kind), new_cat_kind_sel);
    GtkWidget *add = w_button("Adicionar categoria", "add", "suggested-action", add_category, NULL, NULL);
    g_signal_connect_swapped(new_cat_entry, "activate", G_CALLBACK(add_category), NULL);
    w_add(row, new_cat_entry);
    w_add(row, new_cat_kind);
    w_add(row, add);
    w_add(c, row);
    for (int k = 1; k >= 0; k--) {
        GtkWidget *eb = w_eyebrow(k == KIND_EXPENSE ? "Despesas" : "Receitas");
        gtk_widget_set_margin_top(eb, 8);
        w_add(c, eb);
        GtkWidget *list = boxed();
        for (guint i = 0; i < s->cats[k]->len; i++) {
            const char *cat = s->cats[k]->pdata[i];
            GtkWidget *r = manage_row(list, cat, k == KIND_EXPENSE ? "Despesa" : "Receita", w_pill("Renomear", rename_cat, cat_ref((Kind)k, cat), cat_ref_free));
            adw_action_row_add_prefix(ADW_ACTION_ROW(r), w_category_glyph(cat));
            GtkWidget *del = w_button("Excluir", NULL, "fin-pill danger", delete_cat, cat_ref((Kind)k, cat), cat_ref_free);
            gtk_widget_set_valign(del, GTK_ALIGN_CENTER);
            adw_action_row_add_suffix(ADW_ACTION_ROW(r), del);
        }
        w_add(c, list);
    }
    return c;
}

/* ---------------------------------------------------------------- montagem */

void page_money_refresh(void) {
    if (!body || !APP->state) return;
    w_clear(body);
    GtkWidget *head = w_vbox(2);
    w_add(head, w_label("Meu dinheiro", "fin-page-title"));
    w_add(head, w_label_wrap("Contas, cartões e seus planos financeiros em um só lugar.", "fin-muted"));
    w_add(body, head);
    GtkWidget *wal = wallet(), *lim = limits(), *gl = goals(), *sim = simulate(), *cat = categories();
    if (APP->layout == LAYOUT_NARROW) {
        GtkWidget *all[] = {wal, lim, gl, sim, cat};
        for (guint i = 0; i < G_N_ELEMENTS(all); i++) w_add(body, all[i]);
        return;
    }
    GtkWidget *cols = w_hbox(18);
    gtk_box_set_homogeneous(GTK_BOX(cols), TRUE);
    GtkWidget *a = w_vbox(14), *b = w_vbox(14);
    w_add(a, wal); w_add(a, gl); w_add(a, sim);
    w_add(b, lim); w_add(b, cat);
    w_add(cols, a);
    w_add(cols, b);
    w_add(body, cols);
}

GtkWidget *page_money_new(void) {
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 1400);
    adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp), 1100);
    body = w_vbox(14);
    gtk_widget_set_margin_start(body, 24);
    gtk_widget_set_margin_end(body, 24);
    gtk_widget_set_margin_top(body, 20);
    gtk_widget_set_margin_bottom(body, 32);
    adw_clamp_set_child(ADW_CLAMP(clamp), body);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), clamp);
    return sw;
}
