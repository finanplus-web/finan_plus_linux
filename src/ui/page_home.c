/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Início: saldo atual e previsto (com a data e o que considera), o que falta receber e pagar, os
 * próximos 3 vencimentos e o assistente. Contas, cartões, limites e metas ficam em Meu dinheiro (1.6.0).
 */
#include "pages.h"
#include "widgets.h"
#include "core/finance.h"
#include "core/period.h"
#include <math.h>
#include <string.h>

static GtkWidget *body;

/* "37,5" — número com vírgula */
static char *dec(double v, int places) {
    char *s = g_strdup_printf("%.*f", places, v);
    for (char *p = s; *p; p++) if (*p == '.') *p = ',';
    return s;
}

static void open_pay(gpointer id) { editor_pay_invoice(id); }
static void open_tx(gpointer id) { Tx *t = app_tx(APP->state, id); if (t) editor_tx(t->kind, id); }

/* quadro de valor; [sub_label] + [sub] aparecem embaixo quando sub > 0 ("a receber R$ …") */
static GtkWidget *stat_box_full(const char *icon, const char *label, Cents v, const char *money_classes,
                                const char *sub_label, Cents sub, const char *sub_classes) {
    GtkWidget *b = w_vbox(4);
    gtk_widget_add_css_class(b, "fin-stat");
    gtk_widget_set_hexpand(b, TRUE);
    w_add(b, icon ? w_icon_label(icon, label, "fin-muted caption") : w_label(label, "fin-muted caption"));
    w_add(b, w_money(v, money_classes));
    if (sub_label && sub > 0) {
        GtkWidget *r = w_hbox(4);
        w_add(r, w_label(sub_label, "fin-muted caption"));
        w_add(r, w_money(sub, sub_classes));
        w_add(b, r);
    } else if (sub_label && !sub_classes) {
        w_add(b, w_label(sub_label, "fin-muted caption"));
    }
    return b;
}

/* linha de dois quadros que vira coluna quando falta largura */
static GtkWidget *stat_row(void) {
    GtkWidget *f = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(f), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(f), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(f), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(f), 2);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(f), 10);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(f), 10);
    gtk_widget_add_css_class(f, "fin-plain-flow");
    return f;
}

static GtkWidget *pending_box(const char *icon, const char *label, Cents pending, Cents realized, const char *word,
                              const char *color, gboolean progress) {
    g_autofree char *cls = g_strdup_printf("fin-money-mid %s", color);
    GtkWidget *b = stat_box_full(icon, label, pending, cls, NULL, 0, NULL);
    Cents total = pending + realized;
    if (progress && total > 0) {
        double frac = (double)realized / total;
        w_add(b, w_level(frac, NULL));
        g_autofree char *pct = dec(frac * 100, 0);
        g_autofree char *t = app_hidden() ? g_strdup("Progresso oculto") : g_strdup_printf("%s%% %s", pct, word);
        w_add(b, w_label(t, "fin-muted caption"));
    } else {
        w_add(b, w_label(!progress && pending > 0 ? "Contas e faturas em aberto neste mês" : !progress ? "Tudo pago neste mês" : "Nada registrado neste mês",
                         "fin-muted caption"));
    }
    return b;
}

static GtkWidget *hero(void) {
    const AppState *s = APP->state;
    Ym ym = day_ym(APP->today);
    Flow f = month_flow(s, ym);
    Pending pend = period_month_pending(s, ym, APP->today);
    Cents bal = current_balance(s), fut = future_balance(s, ym_last(ym), APP->today);
    GtkWidget *c = w_card("fin-hero");
    gtk_widget_set_valign(c, GTK_ALIGN_START);

    GtkWidget *top = w_hbox(8);
    g_autofree char *date = cal_day_title(APP->today, APP->today); /* "Quinta, 8 de outubro" — muda sozinha com o dia */
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

    GtkWidget *r1 = stat_row();
    gtk_widget_set_margin_top(r1, 10);
    gtk_flow_box_append(GTK_FLOW_BOX(r1), stat_box_full(NULL, "Saldo atual", bal, bal < 0 ? "fin-money-big fin-red" : "fin-money-big", NULL, 0, NULL));
    char until[11];
    g_autofree char *fl = g_strdup_printf("Saldo previsto até %s", day_br(ym_last(ym), until));
    GtkWidget *fb = stat_box_full(NULL, fl, fut, fut < 0 ? "fin-money-big fin-red" : "fin-money-big fin-accent", NULL, 0, NULL);
    w_add(fb, w_label_wrap("Considera valores pendentes, faturas e recorrências até esta data.", "fin-muted caption"));
    gtk_flow_box_append(GTK_FLOW_BOX(r1), fb);
    w_add(c, r1);
    /* como o previsto foi calculado (só quando a conta fecha com os números da tela) */
    if (!app_hidden() && (pend.to_receive > 0 || pend.to_pay > 0) && bal + pend.to_receive - pend.to_pay == fut) {
        g_autofree char *b0 = money_fmt(bal), *b1 = money_fmt(pend.to_receive), *b2 = money_fmt(pend.to_pay);
        g_autofree char *ex = g_strdup_printf("Hoje %s + %s a receber − %s a pagar", b0, b1, b2);
        w_add(c, w_label_wrap(ex, "fin-muted caption"));
    }
    /* o que ainda falta entrar e sair no mês; realizado e pendente ficam separados (em despesas, sem
     * porcentagem: faturas abertas podem corresponder a compras já lançadas e contariam duas vezes) */
    GtkWidget *r2 = stat_row();
    gtk_flow_box_append(GTK_FLOW_BOX(r2), pending_box("arrow-upward", "Falta receber", pend.to_receive, f.income, "recebido", "fin-green", TRUE));
    gtk_flow_box_append(GTK_FLOW_BOX(r2), pending_box("arrow-downward", "Falta pagar", pend.to_pay, f.expense, "pago", "fin-red", FALSE));
    w_add(c, r2);

    /* a barra de uso das receitas só aparece quando já entrou alguma receita */
    if (f.income > 0) {
        GtkWidget *bar = w_level((double)f.expense / f.income, f.expense > f.income ? "over" : NULL);
        gtk_widget_set_margin_top(bar, 8);
        w_add(c, bar);
        GtkWidget *row = w_hbox(8);
        if (app_hidden()) {
            /* "Ocultar valores" também oculta as porcentagens */
            w_add(row, w_label_wrap("Uso das receitas oculto.", "fin-muted caption heading"));
        } else {
            double used = f.expense * 100.0 / f.income;
            g_autofree char *u = dec(used, 1);
            g_autofree char *msg = g_strdup_printf("Despesas equivalem a %s%% das receitas recebidas.", u);
            GtkWidget *ml = w_label_wrap(msg, "fin-muted caption heading");
            gtk_widget_set_hexpand(ml, TRUE);
            w_add(row, ml);
            g_autofree char *sv = dec(MAX(0.0, 100 - used), 0);
            g_autofree char *badge = g_strdup_printf("%s%% economizado", sv);
            GtkWidget *b = w_badge(badge, "accent");
            gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
            w_add(row, b);
        }
        w_add(c, row);
    }
    /* os botões Receita/Despesa/Meta saíram: os botões do topo da janela e os atalhos fazem o mesmo */
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

/* Cartão compacto: as 2 frases mais úteis do mês (MonthReport.highlights), a dica principal e um link só.
 * O resumo completo, o "Por quê?" e as perguntas ficam na tela Assistente. */
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
    int visible = 0;
    if (pr->assist_tips) {
        MonthReport *r = insights_report(APP->state, APP->today, app_money_fmt());
        GPtrArray *lines = r->highlights->len ? r->highlights : r->lines;
        guint n = r->highlights->len ? lines->len : MIN(1u, lines->len);
        for (guint i = 0; i < n; i++) {
            g_autofree char *l = g_strdup_printf("• %s", (char *)lines->pdata[i]);
            w_add(c, w_label_wrap(l, NULL));
        }
        month_report_free(r);
        g_autoptr(GPtrArray) tips = insights_tips(APP->state, APP->today, app_money_fmt());
        for (guint i = 0; i < tips->len; i++) {
            Insight *t = tips->pdata[i];
            if (prefs_tip_dismissed(pr, t->id)) continue;
            if (visible == 0) w_add(c, tip_item(t, TRUE)); /* só a dica principal */
            visible++;
        }
    } else {
        w_add(c, w_label_wrap("Pergunte sobre seus gastos.", NULL));
    }
    g_autofree char *l = !pr->assist_tips ? g_strdup("Perguntar")
                         : visible > 1   ? g_strdup_printf("Ver as %d dicas", visible)
                                         : g_strdup("Abrir assistente");
    GtkWidget *link = w_more_link(l, pr->assist_tips ? go_assist : go_ask, NULL, NULL);
    gtk_widget_set_tooltip_text(link, pr->assist_tips ? "Resumo completo, dicas e perguntas" : "Perguntar ao assistente (Ctrl+K)");
    w_add(c, link);
    return c;
}

/* título de seção com um botão à direita (ou nada) */
static GtkWidget *section_head(const char *title, GtkWidget *action) {
    GtkWidget *head = w_hbox(8);
    GtkWidget *h = w_title(title);
    gtk_widget_set_hexpand(h, TRUE);
    gtk_widget_set_valign(h, GTK_ALIGN_CENTER);
    w_add(head, h);
    if (action) {
        gtk_widget_set_valign(action, GTK_ALIGN_CENTER);
        w_add(head, action);
    }
    return head;
}

/* ---------------------------------------------------------------- próximos vencimentos (3) */

static void open_pending(gpointer u) { (void)u; app_open_moves_pending(); }
static void due_pay_clicked(GtkButton *b, gpointer u) { (void)u; open_pay(g_object_get_data(G_OBJECT(b), "id")); }
static void due_tx_clicked(GtkButton *b, gpointer u) { (void)u; open_tx(g_object_get_data(G_OBJECT(b), "id")); }

static GtkWidget *due_card(void) {
    const AppState *s = APP->state;
    GtkWidget *c = w_card(NULL);
    gtk_widget_set_valign(c, GTK_ALIGN_START);
    g_autoptr(GPtrArray) r = upcoming_dues(s, APP->today, 3);
    w_add(c, section_head("Próximos vencimentos", r->len ? w_pill("Ver todos", open_pending, NULL, NULL) : NULL));
    if (!r->len) {
        GtkWidget *row = w_hbox(10);
        GtkWidget *ok = w_icon("check", 20);
        gtk_widget_add_css_class(ok, "fin-green");
        w_add(row, ok);
        GtkWidget *t = w_vbox(1);
        w_add(t, w_label("Nenhum pagamento pendente", "heading"));
        w_add(t, w_label_wrap("Quando houver uma conta ou fatura, ela aparecerá aqui.", "fin-muted caption"));
        w_add(row, t);
        w_add(c, row);
        return c;
    }
    for (guint i = 0; i < r->len; i++) {
        Reminder *m = r->pdata[i];
        gboolean late = m->date < APP->today, inv = m->type == REMINDER_INVOICE_DUE;
        GtkWidget *btn = gtk_button_new();
        gtk_widget_add_css_class(btn, "fin-row");
        gtk_widget_set_tooltip_text(btn, inv ? "Pagar fatura" : "Abrir lançamento");
        GtkWidget *row = w_hbox(10);
        GtkWidget *ic = w_icon(late ? "warning" : "calendar-month", 20);
        if (late) gtk_widget_add_css_class(ic, "fin-red");
        w_add(row, ic);
        GtkWidget *texts = w_vbox(1);
        gtk_widget_set_hexpand(texts, TRUE);
        w_add(texts, w_label(m->title, "heading"));
        char d[11];
        g_autofree char *when = g_strdup_printf("%s %s", late ? "Vencido em" : "Vence em", day_br(m->date, d));
        w_add(texts, w_label(when, late ? "fin-late caption" : "fin-muted caption"));
        w_add(texts, w_label("Ver pagamento ›", "fin-accent caption heading"));
        w_add(row, texts);
        GtkWidget *mv = w_money(m->amount, "fin-red");
        gtk_widget_set_valign(mv, GTK_ALIGN_CENTER);
        w_add(row, mv);
        gtk_button_set_child(GTK_BUTTON(btn), row);
        g_object_set_data_full(G_OBJECT(btn), "id", g_strdup(m->ref_id), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(inv ? due_pay_clicked : due_tx_clicked), NULL);
        w_add(c, btn);
    }
    return c;
}

/* ---------------------------------------------------------------- montagem */

/* coluna com os blocos dados (NULL = bloco que não aparece); termina com um ponteiro sentinela */
static GtkWidget *column(GtkWidget **blocks, int n) {
    GtkWidget *col = w_vbox(18);
    gtk_widget_set_valign(col, GTK_ALIGN_START);
    for (int i = 0; i < n; i++) if (blocks[i]) w_add(col, blocks[i]);
    return col;
}

void page_home_refresh(void) {
    if (!body || !APP->state) return;
    w_clear(body);
    GtkWidget *hero_w = hero(), *due = due_card(), *assist = assistant_card();
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g), 18);
    gtk_grid_set_row_spacing(GTK_GRID(g), 18);
    gtk_grid_set_column_homogeneous(GTK_GRID(g), TRUE);
        switch (APP->layout) {
    case LAYOUT_WIDE:
    case LAYOUT_MEDIUM: {
        /* duas colunas (3/5 e 2/5 da largura): saldo e vencimentos na larga; assistente na estreita */
        GtkWidget *c0[] = {hero_w, due}, *c1[] = {assist};
        gtk_grid_attach(GTK_GRID(g), column(c0, 2), 0, 0, 3, 1);
        gtk_grid_attach(GTK_GRID(g), column(c1, 1), 3, 0, 2, 1);
        break;
    }
    default: {
        GtkWidget *c0[] = {hero_w, due, assist};
        gtk_grid_attach(GTK_GRID(g), column(c0, 3), 0, 0, 1, 1);
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
