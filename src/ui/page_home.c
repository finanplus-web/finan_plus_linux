/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Início: saldo atual e previsto (com a data e o que considera), o que falta receber e pagar, os
 * próximos vencimentos, o assistente, a evolução do saldo no mês, receitas × despesas dos últimos meses,
 * onde foi o dinheiro, os últimos lançamentos, metas e limites. Em janelas bem largas são três colunas
 * (1.4.0); cada cartão vazio explica o que aparece ali, em vez de sumir.
 */
#include "pages.h"
#include "charts.h"
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
    GtkWidget *sb = stat_box_full(NULL, "Saldo atual", bal, bal < 0 ? "fin-money-big fin-red" : "fin-money-big", NULL, 0, NULL);
    g_autofree char *accs = s->accounts->len == 1 ? g_strdup("Soma da sua conta, só com o que já foi pago ou recebido.")
                                                  : g_strdup_printf("Soma de %u contas, só com o que já foi pago ou recebido.", s->accounts->len);
    w_add(sb, w_label_wrap(accs, "fin-muted caption"));
    gtk_flow_box_append(GTK_FLOW_BOX(r1), sb);
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

/* ---------------------------------------------------------------- próximos vencimentos */

static void open_pending(gpointer u) { (void)u; app_open_moves_pending(); }
static void due_pay_clicked(GtkButton *b, gpointer u) { (void)u; open_pay(g_object_get_data(G_OBJECT(b), "id")); }
static void due_tx_clicked(GtkButton *b, gpointer u) { (void)u; open_tx(g_object_get_data(G_OBJECT(b), "id")); }

/* "Hoje", "Amanhã", "Atrasado" ou "20 out" */
static char *when_short(Day d) {
    if (d < APP->today) return g_strdup("Atrasado");
    if (d == APP->today) return g_strdup("Hoje");
    if (d == APP->today + 1) return g_strdup("Amanhã");
    int y, m, dd;
    day_to_ymd(d, &y, &m, &dd);
    return g_strdup_printf("%d %s", dd, BR_MONTHS_SHORT[m - 1]);
}

/* valor com sinal ("+ R$ 10,00" / "− R$ 10,00") */
static GtkWidget *signed_money(Cents v, gboolean income) {
    g_autofree char *m = app_money(v);
    g_autofree char *t = g_strdup_printf("%s %s", income ? "+" : "−", m);
    GtkWidget *l = w_label(t, income ? "fin-money fin-green" : "fin-money fin-red");
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_NONE);
    gtk_label_set_xalign(GTK_LABEL(l), 1);
    return l;
}

static GtkWidget *due_card(int limit) {
    const AppState *s = APP->state;
    GtkWidget *c = w_card(NULL);
    g_autoptr(GPtrArray) r = upcoming_dues(s, APP->today, limit);
    w_add(c, w_card_head("Próximos vencimentos", r->len ? w_more_link("Ver todos", open_pending, NULL, NULL) : NULL));
    if (!r->len) {
        GtkWidget *row = w_hbox(12);
        gtk_widget_set_margin_top(row, 4);
        w_add(row, w_icon_tile("check", "green", FALSE));
        GtkWidget *t = w_vbox(1);
        w_add(t, w_label("Nenhum pagamento pendente", "heading"));
        w_add(t, w_label_wrap("Quando houver uma conta ou fatura, ela aparecerá aqui.", "fin-muted caption"));
        w_add(row, t);
        w_add(c, row);
        return c;
    }
    GtkWidget *list = w_vbox(0);
    for (guint i = 0; i < r->len; i++) {
        Reminder *m = r->pdata[i];
        gboolean late = m->date < APP->today, inv = m->type == REMINDER_INVOICE_DUE, income = m->type == REMINDER_INCOME_DUE;
        if (i) w_add(list, w_sep());
        GtkWidget *btn = gtk_button_new();
        gtk_widget_add_css_class(btn, "fin-line");
        char d[11];
        g_autofree char *tip = g_strdup_printf("%s · %s %s", inv ? "Pagar fatura" : "Abrir lançamento", late ? "venceu em" : "vence em", day_br(m->date, d));
        gtk_widget_set_tooltip_text(btn, tip);
        GtkWidget *row = w_hbox(10);
        g_autofree char *when = when_short(m->date);
        GtkWidget *wl = w_label(when, late ? "fin-late caption" : "fin-muted caption");
        gtk_widget_set_size_request(wl, 64, -1);
        w_add(row, wl);
        GtkWidget *tl = w_label(m->title, NULL);
        gtk_widget_set_hexpand(tl, TRUE);
        w_add(row, tl);
        w_add(row, signed_money(m->amount, income));
        gtk_button_set_child(GTK_BUTTON(btn), row);
        g_object_set_data_full(G_OBJECT(btn), "id", g_strdup(m->ref_id), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(inv ? due_pay_clicked : due_tx_clicked), NULL);
        w_add(list, btn);
    }
    w_add(c, list);
    return c;
}

/* ---------------------------------------------------------------- evolução do saldo no mês */

static GtkWidget *balance_card(void) {
    const AppState *s = APP->state;
    Ym ym = day_ym(APP->today);
    Day first = ym_first(ym), last = ym_last(ym);
    int n = (int)(last - first) + 1, solid = (int)(APP->today - first);
    Cents *v = g_new0(Cents, n);
    balance_series(s, first, APP->today, v);
    /* depois de hoje: o saldo previsto de cada dia (pendências, faturas e recorrências até ele) */
    for (int i = solid + 1; i < n; i++) v[i] = future_balance(s, first + i, APP->today);
    const char *labels[31] = {0};
    char lb[4][4];
    int marks[4] = {0, 9, 19, n - 1};
    for (int k = 0; k < 4; k++) { g_snprintf(lb[k], sizeof lb[k], "%02d", marks[k] + 1); labels[marks[k]] = lb[k]; }
    const char *mname = br_month(ym);
    GtkWidget *c = w_card(NULL);
    g_autofree char *sub = g_strdup_printf("Dia a dia · %s", mname);
    GtkWidget *subl = w_label(sub, "fin-muted caption");
    w_add(c, w_card_head("Evolução do saldo", subl));
    g_autofree char *b0 = money_fmt(v[0]), *b1 = money_fmt(v[solid]), *b2 = money_fmt(v[n - 1]);
    g_autofree char *desc = app_hidden() ? g_strdup("Gráfico do saldo dia a dia (valores ocultos).")
                                         : g_strdup_printf("Saldo dia a dia em %s: começou em %s, hoje está em %s e a previsão para o fim do mês é %s.",
                                                           mname, b0, b1, b2);
    gboolean flat = TRUE;
    for (int i = 0; i < n; i++) if (v[i] != 0) flat = FALSE;
    if (flat) {
        /* sem saldo nem movimento: um texto no lugar de uma linha reta no zero */
        w_add(c, w_label_wrap("O gráfico aparece quando houver saldo nas contas ou lançamentos pagos neste mês.", "fin-muted"));
        g_free(v);
        return c;
    }
    w_add(c, chart_line_new(v, n, solid, labels, 190, desc));
    w_add(c, w_label(solid < n - 1 ? "Linha cheia: realizado · tracejada: previsto até o fim do mês" : "Saldo realizado em cada dia do mês",
                     "fin-muted caption"));
    g_free(v);
    return c;
}

/* ---------------------------------------------------------------- receitas × despesas */

static GtkWidget *flows_card(int months) {
    GtkWidget *c = w_card(NULL);
    w_add(c, w_card_head("Receitas × Despesas", w_label("Realizado por mês", "fin-muted caption")));
    Ym yms[12];
    Flow fl[12];
    Ym cur = day_ym(APP->today);
    gboolean any = FALSE;
    GString *desc = g_string_new("Receitas e despesas realizadas por mês. ");
    for (int i = 0; i < months; i++) {
        yms[i] = cur - (months - 1) + i;
        fl[i] = month_flow(APP->state, yms[i]);
        if (fl[i].income || fl[i].expense) any = TRUE;
        g_autofree char *my = br_month_year(yms[i]);
        if (!app_hidden()) {
            g_autofree char *a = money_fmt(fl[i].income), *b = money_fmt(fl[i].expense);
            g_string_append_printf(desc, "%s: receitas %s, despesas %s; ", my, a, b);
        }
    }
    if (!any) {
        w_add(c, w_label_wrap("Aparece quando houver receitas ou despesas pagas nos últimos meses.", "fin-muted"));
        g_string_free(desc, TRUE);
        return c;
    }
    w_add(c, chart_month_bars_new(yms, fl, months, 190, desc->str));
    g_string_free(desc, TRUE);
    GtkWidget *leg = w_hbox(14);
    GtkWidget *a = w_icon_label("arrow-upward", "Receitas", "fin-green caption"), *b = w_icon_label("arrow-downward", "Despesas", "fin-red caption");
    w_add(leg, a);
    w_add(leg, b);
    w_add(c, leg);
    return c;
}

/* ---------------------------------------------------------------- onde foi o dinheiro */

static void go_reports(gpointer u) { (void)u; app_show_page(PAGE_REPORTS); }

static GtkWidget *spend_card(void) {
    Ym ym = day_ym(APP->today);
    GtkWidget *c = w_card(NULL);
    w_add(c, w_card_head("Onde foi o dinheiro", w_more_link("Resumo", go_reports, NULL, NULL)));
    g_autoptr(GPtrArray) cats = category_totals(APP->state, ym_first(ym), ym_last(ym));
    Cents max = 0;
    for (guint i = 0; i < cats->len; i++) max = MAX(max, ((CatTotal *)cats->pdata[i])->value);
    if (!cats->len) {
        w_add(c, w_label_wrap("Ainda não há despesas pagas neste mês. As categorias em que você mais gasta aparecem aqui.", "fin-muted"));
        return c;
    }
    for (guint i = 0; i < MIN(cats->len, 5u); i++) {
        CatTotal *ct = cats->pdata[i];
        GtkWidget *item = w_vbox(5);
        gtk_widget_set_margin_top(item, 4);
        GtkWidget *top = w_hbox(8);
        GtkWidget *nl = w_label(ct->name, NULL);
        gtk_widget_set_hexpand(nl, TRUE);
        w_add(top, nl);
        w_add(top, w_money(ct->value, NULL));
        w_add(item, top);
        w_add(item, w_level(max > 0 ? (double)ct->value / max : 0, NULL));
        w_add(c, item);
    }
    return c;
}

/* ---------------------------------------------------------------- últimos lançamentos */

static void go_moves(gpointer u) { (void)u; app_show_page(PAGE_MOVES); }
static void recent_clicked(GtkButton *b, gpointer u) { (void)u; open_tx(g_object_get_data(G_OBJECT(b), "id")); }

static gint cmp_recent(gconstpointer a, gconstpointer b) {
    const Tx *x = *(Tx *const *)a, *y = *(Tx *const *)b;
    if (x->date != y->date) return x->date < y->date ? 1 : -1;
    return strcmp(y->id, x->id);
}

static GtkWidget *recent_card(int cols, int count) {
    const AppState *s = APP->state;
    GtkWidget *c = w_card(NULL);
    w_add(c, w_card_head("Últimos lançamentos", s->txs->len ? w_more_link("Ver todos", go_moves, NULL, NULL) : NULL));
    g_autoptr(GPtrArray) all = g_ptr_array_new();
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (t->date <= APP->today && tx_is_flow(t)) g_ptr_array_add(all, t);
    }
    g_ptr_array_sort(all, cmp_recent);
    if (!all->len) {
        w_add(c, w_label_wrap("Nada lançado ainda. Use “Despesa” ou “Receita” no topo da janela (Ctrl+N).", "fin-muted"));
        return c;
    }
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g), 24);
    gtk_grid_set_row_spacing(GTK_GRID(g), 2);
    gtk_grid_set_column_homogeneous(GTK_GRID(g), TRUE);
    guint n = MIN(all->len, (guint)count);
    for (guint i = 0; i < n; i++) {
        Tx *t = all->pdata[i];
        GtkWidget *btn = gtk_button_new();
        gtk_widget_add_css_class(btn, "fin-line");
        gtk_widget_set_tooltip_text(btn, "Abrir lançamento");
        GtkWidget *row = w_hbox(12);
        w_add(row, w_icon_tile(category_icon(t->category) ? category_icon(t->category) : "more-horiz", category_color(t->category), FALSE));
        GtkWidget *tx = w_vbox(1);
        gtk_widget_set_hexpand(tx, TRUE);
        w_add(tx, w_label(t->desc, "heading"));
        char d[11];
        g_autofree char *sub = g_strdup_printf("%s · %s%s", t->category, day_br(t->date, d), t->paid || tx_is_card(t) ? "" : " · pendente");
        w_add(tx, w_label(sub, "fin-muted caption"));
        w_add(row, tx);
        GtkWidget *mv = signed_money(t->value, t->kind == KIND_INCOME);
        gtk_widget_set_valign(mv, GTK_ALIGN_CENTER);
        w_add(row, mv);
        gtk_button_set_child(GTK_BUTTON(btn), row);
        g_object_set_data_full(G_OBJECT(btn), "id", g_strdup(t->id), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(recent_clicked), NULL);
        gtk_grid_attach(GTK_GRID(g), btn, (int)(i % cols), (int)(i / cols), 1, 1);
    }
    w_add(c, g);
    return c;
}

/* ---------------------------------------------------------------- metas e limites */

static void go_money(gpointer u) { (void)u; app_show_page(PAGE_MONEY); }
static void new_goal(gpointer u) { (void)u; editor_goal(NULL); }
static void new_limit(gpointer u) { (void)u; editor_limit(NULL); }
static void goal_clicked(GtkButton *b, gpointer u) { (void)u; editor_goal(g_object_get_data(G_OBJECT(b), "id")); }

static GtkWidget *goals_card(void) {
    const AppState *s = APP->state;
    GtkWidget *c = w_card(NULL);
    w_add(c, w_card_head("Metas", s->goals->len ? w_more_link("Ver todas", go_money, NULL, NULL) : NULL));
    if (!s->goals->len) {
        w_add(c, w_label_wrap("Uma meta ajuda a juntar dinheiro para uma viagem, uma reserva ou uma compra.", "fin-muted"));
        GtkWidget *b = w_button("Criar uma meta", "add", "fin-pill", new_goal, NULL, NULL);
        gtk_widget_set_halign(b, GTK_ALIGN_START);
        w_add(c, b);
        return c;
    }
    for (guint i = 0; i < MIN(s->goals->len, 3u); i++) {
        Goal *g = s->goals->pdata[i];
        double frac = g->target > 0 ? (double)g->saved / g->target : 0;
        GtkWidget *btn = gtk_button_new();
        gtk_widget_add_css_class(btn, "fin-line");
        gtk_widget_set_tooltip_text(btn, "Editar meta, guardar ou retirar valor");
        GtkWidget *b = w_vbox(5);
        GtkWidget *top = w_hbox(8);
        GtkWidget *nl = w_label(g->name, "heading");
        gtk_widget_set_hexpand(nl, TRUE);
        w_add(top, nl);
        g_autofree char *pct = dec(MIN(frac, 1.0) * 100, 0);
        g_autofree char *pt = app_hidden() ? g_strdup("••%") : g_strdup_printf("%s%%", pct);
        w_add(top, w_label(pt, "fin-green heading"));
        w_add(b, top);
        w_add(b, w_level(frac, "green"));
        g_autofree char *sv = app_money(g->saved), *tg = app_money(g->target);
        g_autofree char *info = g_strdup_printf("%s de %s", sv, tg);
        w_add(b, w_label(info, "fin-muted caption"));
        gtk_button_set_child(GTK_BUTTON(btn), b);
        g_object_set_data_full(G_OBJECT(btn), "id", g_strdup(g->id), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(goal_clicked), NULL);
        w_add(c, btn);
    }
    return c;
}

static void limit_clicked(GtkButton *b, gpointer u) { (void)u; editor_limit(g_object_get_data(G_OBJECT(b), "cat")); }

static GtkWidget *limits_card(void) {
    const AppState *s = APP->state;
    Ym ym = day_ym(APP->today);
    GtkWidget *c = w_card(NULL);
    w_add(c, w_card_head("Limites do mês", s->limits->len ? w_more_link("Ajustar", go_money, NULL, NULL) : NULL));
    if (!s->limits->len) {
        w_add(c, w_label_wrap("Defina quanto quer gastar por categoria; o Finan+ avisa quando estiver perto.", "fin-muted"));
        GtkWidget *b = w_button("Definir um limite", "add", "fin-pill", new_limit, NULL, NULL);
        gtk_widget_set_halign(b, GTK_ALIGN_START);
        w_add(c, b);
        return c;
    }
    for (guint i = 0; i < MIN(s->limits->len, 4u); i++) {
        Limit *l = s->limits->pdata[i];
        Cents u = budget_usage(s, ym, l->category);
        double pct = l->value > 0 ? (double)u / l->value : 0;
        GtkWidget *btn = gtk_button_new();
        gtk_widget_add_css_class(btn, "fin-line");
        gtk_widget_set_tooltip_text(btn, "Editar limite");
        GtkWidget *b = w_vbox(5);
        GtkWidget *top = w_hbox(4);
        GtkWidget *nl = w_label(l->category, NULL);
        gtk_widget_set_hexpand(nl, TRUE);
        w_add(top, nl);
        w_add(top, w_money(u, u > l->value ? "fin-red" : NULL));
        w_add(top, w_label(" / ", "fin-muted"));
        w_add(top, w_money(l->value, "fin-muted"));
        w_add(b, top);
        w_add(b, w_level(pct, u > l->value ? "over" : pct >= 0.8 ? "warn" : NULL));
        gtk_button_set_child(GTK_BUTTON(btn), b);
        g_object_set_data_full(G_OBJECT(btn), "cat", g_strdup(l->category), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(limit_clicked), NULL);
        w_add(c, btn);
    }
    return c;
}

/* ---------------------------------------------------------------- montagem */

static GtkWidget *grid_new(void) {
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g), 18);
    gtk_grid_set_row_spacing(GTK_GRID(g), 18);
    gtk_grid_set_column_homogeneous(GTK_GRID(g), TRUE);
    return g;
}

/* põe [w] na grade; os cartões de uma linha ficam com a mesma altura */
static void put(GtkWidget *g, GtkWidget *w, int col, int row, int span) {
    if (!w) return;
    gtk_widget_set_valign(w, GTK_ALIGN_FILL);
    gtk_grid_attach(GTK_GRID(g), w, col, row, span, 1);
}

void page_home_refresh(void) {
    if (!body || !APP->state) return;
    w_clear(body);
    Layout l = APP->layout;
    GtkWidget *hero_w = hero(), *assist = assistant_card();
    if (l == LAYOUT_XWIDE) {
        /* três colunas (a primeira com o dobro da largura), três linhas */
        GtkWidget *g = grid_new();
        put(g, hero_w, 0, 0, 2);
        put(g, due_card(5), 2, 0, assist ? 1 : 2);
        put(g, assist, 3, 0, 1);
        put(g, balance_card(), 0, 1, 2);
        put(g, flows_card(5), 2, 1, 1);
        put(g, spend_card(), 3, 1, 1);
        put(g, recent_card(2, 6), 0, 2, 2);
        put(g, goals_card(), 2, 2, 1);
        put(g, limits_card(), 3, 2, 1);
        w_add(body, g);
        return;
    }
    if (l == LAYOUT_NARROW) {
        GtkWidget *all[] = {hero_w, due_card(3), assist, balance_card(), spend_card(), flows_card(4), goals_card(), limits_card(), recent_card(1, 5)};
        for (guint i = 0; i < G_N_ELEMENTS(all); i++) if (all[i]) { gtk_widget_set_valign(all[i], GTK_ALIGN_START); w_add(body, all[i]); }
        return;
    }
    /* duas colunas iguais */
    GtkWidget *g = grid_new();
    put(g, hero_w, 0, 0, 1);
    put(g, due_card(5), 1, 0, 1);
    put(g, balance_card(), 0, 1, assist ? 1 : 2);
    put(g, assist, 1, 1, 1);
    put(g, flows_card(5), 0, 2, 1);
    put(g, spend_card(), 1, 2, 1);
    put(g, goals_card(), 0, 3, 1);
    put(g, limits_card(), 1, 3, 1);
    put(g, recent_card(2, 6), 0, 4, 2);
    w_add(body, g);
}

GtkWidget *page_home_new(void) {
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    /* o conteúdo usa a largura toda da janela (até 2400 px, para monitores ultralargos) */
    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 2400);
    adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp), 2000);
    body = w_vbox(18);
    gtk_widget_set_margin_start(body, 24);
    gtk_widget_set_margin_end(body, 24);
    gtk_widget_set_margin_top(body, 20);
    gtk_widget_set_margin_bottom(body, 32);
    adw_clamp_set_child(ADW_CLAMP(clamp), body);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), clamp);
    return sw;
}
