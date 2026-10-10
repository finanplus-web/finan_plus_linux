/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Simulador "E se…?" (aberto em Relatórios). Só lê os dados: nada aqui grava no estado.
 * As contas ficam em core/simulator.c; mesma tela do app Android (SimulatorSheet.kt) e da versão web.
 * Detalhes em SIMULADOR.md.
 */
#include "pages.h"
#include "theme.h"
#include "widgets.h"
#include "core/finance.h"
#include "core/simulator.h"
#include <math.h>
#include <string.h>

typedef enum { SC_HOME, SC_SAVE, SC_BUY, SC_INCOME, SC_DEBT } Scen;

static const struct { const char *icon, *title, *sub, *head; } SCEN[] = {
    [SC_SAVE] = {"savings", "E se eu economizar…", "Ex.: R$ 200 por mês, por 12 meses", "E se eu economizar…?"},
    [SC_BUY] = {"shopping-bag", "Quanto tempo para comprar…", "Ex.: um computador de R$ 4.500", "Quanto tempo para comprar?"},
    [SC_INCOME] = {"work", "E se minha renda mudar…", "Ex.: diminuir 15% a partir do mês que vem", "E se minha renda mudar…?"},
    [SC_DEBT] = {"account-balance", "E se eu antecipar uma dívida…", "Parcelas que faltam, valor para quitar e quanto sobra", "E se eu antecipar uma dívida…?"},
};

/* o que foi digitado vale enquanto a janela está aberta (trocar de pergunta não apaga) */
typedef struct {
    AdwDialog *dialog;
    GtkWidget *body, *out, *goal, *share, *base_box;
    Scen scen;
    gboolean adjust;
    char *inc, *exp;                       /* ajuste da base (vazio = média) */
    char *save_per, *save_months;
    char *buy_what, *buy_price, *buy_have, *buy_per;
    char *pct;
    char *debt_sel, *debt_pay;
    int chart_bars;
} Sim;

static Sim *S;

static void sim_free(gpointer p) {
    Sim *s = p;
    g_free(s->inc); g_free(s->exp); g_free(s->save_per); g_free(s->save_months);
    g_free(s->buy_what); g_free(s->buy_price); g_free(s->buy_have); g_free(s->buy_per);
    g_free(s->pct); g_free(s->debt_sel); g_free(s->debt_pay);
    g_free(s);
    if (S == s) S = NULL;
}

static char *m(Cents c) { return app_hidden() ? g_strdup("R$ ••••") : money_fmt(c); }
static char *plural(int n, const char *one, const char *many) { return n == 1 ? g_strdup_printf("1 %s", one) : g_strdup_printf("%d %s", n, many); }

static gboolean pos_money(const char *t, Cents *out) { Cents v; if (!t || !money_parse(t, &v) || v <= 0) return FALSE; *out = v; return TRUE; }
static Cents money_or(const char *t, Cents def) { Cents v; return t && *t && money_parse(t, &v) ? v : def; }
static gboolean int_in(const char *t, int lo, int hi, int *out) {
    if (!t || !*t) return FALSE;
    for (const char *p = t; *p; p++) if (!g_ascii_isdigit(*p)) return FALSE;
    long v = strtol(t, NULL, 10);
    if (v < lo || v > hi) return FALSE;
    *out = (int)v;
    return TRUE;
}

static SimBase cur_base(SimBase *auto_out) {
    SimBase a = sim_base(APP->state, APP->today);
    if (auto_out) *auto_out = a;
    SimBase b = {money_or(S->inc, a.income), money_or(S->exp, a.expense), a.months};
    return b;
}

/* ---------------------------------------------------------------- peças */

static GtkWidget *safe_badge(void) {
    GtkWidget *b = w_hbox(6);
    gtk_widget_add_css_class(b, "fin-safe");
    gtk_widget_set_halign(b, GTK_ALIGN_START);
    w_add(b, w_icon("shield", 15));
    w_add(b, gtk_label_new("Só simulação: seus dados não mudam"));
    return b;
}

static GtkWidget *line(const char *label, const char *value, const char *cls) {
    GtkWidget *r = w_hbox(10);
    GtkWidget *l = w_label_wrap(label, "fin-muted caption");
    gtk_widget_set_hexpand(l, TRUE);
    w_add(r, l);
    GtkWidget *v = w_label(value, cls ? cls : "heading");
    gtk_label_set_ellipsize(GTK_LABEL(v), PANGO_ELLIPSIZE_NONE);
    w_add(r, v);
    return r;
}

static void result(const char *big, const char *sentence, const char *cls) {
    w_add(S->out, w_eyebrow("Resultado"));
    g_autofree char *c = g_strdup_printf("fin-sim-big %s", cls ? cls : "fin-accent");
    w_add(S->out, w_label_wrap(big, c));
    if (sentence && *sentence) w_add(S->out, w_label_wrap(sentence, NULL));
}

static void why(const char *t) {
    GtkWidget *l = w_label_wrap(t, "fin-muted caption");
    gtk_widget_set_margin_top(l, 6);
    w_add(S->out, l);
}

static void msg(const char *t) { w_add(S->out, w_label_wrap(t, "fin-muted")); }

typedef struct { char **field; gboolean base; } EntryCtx;

static void update(void);
static void refresh_base(void);

static void on_entry(GtkEditable *e, gpointer p) {
    EntryCtx *c = p;
    g_free(*c->field);
    *c->field = g_strdup(gtk_editable_get_text(e));
    if (c->base) refresh_base(); else update();
}

static GtkWidget *entry(const char *label, char **field, const char *placeholder, int max, gboolean base) {
    GtkWidget *b = w_vbox(4);
    gtk_widget_set_hexpand(b, TRUE);
    w_add(b, w_label(label, "fin-muted caption"));
    GtkWidget *e = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(e), *field ? *field : "");
    if (placeholder) gtk_entry_set_placeholder_text(GTK_ENTRY(e), placeholder);
    gtk_entry_set_max_length(GTK_ENTRY(e), max);
    gtk_accessible_update_property(GTK_ACCESSIBLE(e), GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
    EntryCtx *c = g_new0(EntryCtx, 1);
    c->field = field;
    c->base = base;
    g_signal_connect_data(e, "changed", G_CALLBACK(on_entry), c, (GClosureNotify)(void (*)(void))g_free, 0);
    w_add(b, e);
    return b;
}

/* ---------------------------------------------------------------- base */

static void base_card_into(GtkWidget *box) {
    w_clear(box);
    SimBase a, b = cur_base(&a);
    const char *mo = a.months == 1 ? "mês" : "meses";
    g_autofree char *eb = a.months > 0 ? g_strdup_printf("Sua base · média dos últimos %d %s", a.months, mo) : g_strdup("Sua base");
    w_add(box, w_eyebrow(eb));
    if (a.months == 0 && b.income == 0 && b.expense == 0)
        w_add(box, w_label_wrap("Ainda não há meses completos com valores realizados. Informe abaixo quanto entra e sai num mês típico.", "fin-muted caption"));
    GtkWidget *row = w_hbox(8);
    gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
    const char *ls[3] = {"Entra", "Sai", "Sobra"};
    Cents vs[3] = {b.income, b.expense, sim_left(b)};
    const char *cs[3] = {"fin-money-mid fin-green", "fin-money-mid fin-red", sim_left(b) < 0 ? "fin-money-mid fin-red" : "fin-money-mid fin-accent"};
    for (int i = 0; i < 3; i++) {
        GtkWidget *c = w_vbox(2);
        w_add(c, w_label(ls[i], "fin-muted caption"));
        w_add(c, w_money(vs[i], cs[i]));
        w_add(row, c);
    }
    w_add(box, row);
    if (a.months >= 1 && a.months <= 2) {
        g_autofree char *t = g_strdup_printf("Pouco histórico: a média usa só %d %s. Os resultados são aproximados.", a.months, mo);
        w_add(box, w_label_wrap(t, "fin-muted caption"));
    }
}

static void refresh_base(void) { if (S->base_box) base_card_into(S->base_box); }

/* ---------------------------------------------------------------- navegação */

static void build(void);
static void go(gpointer sc) { S->scen = GPOINTER_TO_INT(sc); build(); }
static void toggle_adjust(gpointer u) {
    (void)u;
    SimBase a;
    cur_base(&a);
    gboolean open = S->adjust || (a.months == 0 && !S->adjust);
    S->adjust = !open;
    build();
}

static void make_goal(gpointer u) {
    (void)u;
    g_autofree char *name = NULL;
    Cents target = 0, monthly = 0;
    if (S->scen == SC_SAVE) {
        int n = 12;
        if (!pos_money(S->save_per, &monthly)) return;
        int_in(S->save_months, 1, 600, &n);
        name = g_strdup("Reserva");
        target = monthly * n;
    } else if (S->scen == SC_BUY) {
        if (!pos_money(S->buy_price, &target)) return;
        monthly = money_or(S->buy_per, 0);
        if (monthly <= 0) return;
        g_autofree char *w = g_strstrip(g_strdup(S->buy_what ? S->buy_what : ""));
        name = g_strdup(*w ? w : "Compra");
    } else return;
    adw_dialog_close(S->dialog);
    editor_goal_prefill(name, target, monthly);
}

static void pick_debt(gpointer id) {
    if (S->debt_sel && strcmp(S->debt_sel, id) == 0) return;
    g_free(S->debt_sel);
    S->debt_sel = g_strdup(id);
    g_clear_pointer(&S->debt_pay, g_free);
    build();
}

/* ---------------------------------------------------------------- gráfico de "comprar" */

static void draw_buy(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer u) {
    (void)a; (void)u;
    if (!S || S->chart_bars <= 0) return;
    const FinPalette *pal = theme_palette();
    int n = S->chart_bars;
    double slot = (double)w / n, bw = slot * 0.7;
    for (int i = 0; i < n; i++) {
        double frac = (i + 1.0) / n, hh = h * (0.12 + 0.88 * frac);
        theme_cairo(cr, i == n - 1 ? pal->green : ((pal->accent & 0x00FFFFFF) | 0x73000000));
        cairo_rectangle(cr, i * slot, h - hh, bw, hh);
        cairo_fill(cr);
    }
}

/* ---------------------------------------------------------------- resultados */

static void update(void) {
    if (!S || !S->out) return;
    w_clear(S->out);
    SimBase b = cur_base(NULL);
    gboolean can_goal = FALSE;
    switch (S->scen) {
    case SC_SAVE: {
        Cents v;
        int n;
        if (!pos_money(S->save_per, &v) || !int_in(S->save_months, 1, 600, &n)) { msg("Informe o valor por mês e por quantos meses."); can_goal = pos_money(S->save_per, &v); break; }
        SimSave r = sim_save(b, v, n);
        g_autofree char *tot = m(r.total), *pl = plural(n, "mês", "meses"), *sent = g_strdup_printf("juntados em %s.", pl);
        result(tot, sent, NULL);
        g_autofree char *left = m(sim_left(b)), *nl = m(r.new_left), *vv = m(v), *lab = g_strdup_printf("Sobra por mês guardando %s", vv);
        w_add(S->out, line("Sobra por mês hoje", left, NULL));
        w_add(S->out, line(lab, nl, r.new_left < 0 ? "heading fin-red" : NULL));
        if (r.over_left) w_add(S->out, w_label_wrap("Esse valor é maior do que sobra por mês: faltaria dinheiro para as despesas de sempre.", "fin-red caption"));
        int ks[3] = {6, 12, 24};
        for (int i = 0; i < 3; i++) {
            if (ks[i] == n) continue;
            g_autofree char *l = g_strdup_printf("Em %d meses", ks[i]), *x = m(v * ks[i]);
            w_add(S->out, line(l, x, NULL));
        }
        g_autofree char *w = g_strdup_printf("Conta: %s × %d meses. Não considera rendimento: o Finan+ não sabe quanto o dinheiro guardado renderia.", vv, n);
        why(w);
        can_goal = TRUE;
        break;
    }
    case SC_BUY: {
        Cents pr, pm = money_or(S->buy_per, 0), hv = MAX(0, money_or(S->buy_have, 0));
        if (S->share) {
            g_autofree char *sh = NULL;
            if (pm > 0 && sim_left(b) > 0 && !app_hidden()) {
                g_autofree char *l = money_fmt(sim_left(b));
                sh = g_strdup_printf("%.0f%% do que sobra por mês (%s)", floor(pm * 100.0 / sim_left(b) + 0.5), l);
            }
            gtk_label_set_text(GTK_LABEL(S->share), sh ? sh : "");
            gtk_widget_set_visible(S->share, sh != NULL);
        }
        if (!pos_money(S->buy_price, &pr)) { msg("Informe o preço."); break; }
        SimBuy r;
        if (!sim_buy(pr, hv, pm, APP->today, &r)) { msg("Informe quanto dá para guardar por mês."); break; }
        can_goal = pm > 0;
        if (r.months == 0) { result("Já dá", "Você já tem o valor.", NULL); break; }
        g_autofree char *pl = plural(r.months, "mês", "meses"), *my = br_month_year(r.done_ym), *sent = g_strdup_printf("Você teria o valor em %s.", my);
        result(pl, sent, NULL);
        S->chart_bars = MIN(r.months, 36);
        GtkWidget *da = gtk_drawing_area_new();
        gtk_widget_set_size_request(da, -1, 80);
        gtk_widget_set_margin_top(da, 6);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), draw_buy, NULL, NULL);
        gtk_accessible_update_property(GTK_ACCESSIBLE(da), GTK_ACCESSIBLE_PROPERTY_LABEL, "Gráfico do valor juntado mês a mês", -1);
        gtk_widget_set_visible(da, !app_hidden());
        w_add(S->out, da);
        Cents alts[2] = {pm * 2, pm / 2};
        for (int i = 0; i < 2; i++) {
            SimBuy a;
            if (alts[i] <= 0 || !sim_buy(pr, hv, alts[i], APP->today, &a)) continue;
            g_autofree char *am = m(alts[i]), *l = g_strdup_printf("Guardando %s/mês", am), *ap = plural(a.months, "mês", "meses"),
                            *amy = br_month_year(a.done_ym), *v = g_strdup_printf("%s · %s", ap, amy);
            w_add(S->out, line(l, v, NULL));
        }
        g_autofree char *mi = m(r.missing), *pmm = m(pm);
        g_autofree char *w = g_strdup_printf("Conta: falta %s ÷ %s por mês = %s (arredondado para cima), começando no mês que vem. Não considera rendimento nem mudança de preço.", mi, pmm, pl);
        why(w);
        break;
    }
    case SC_INCOME: {
        g_autofree char *raw = g_strstrip(g_strdup(S->pct ? S->pct : ""));
        for (char *p = raw; *p; p++) if (*p == ',') *p = '.';
        char *end = NULL;
        double v = raw[0] ? g_ascii_strtod(raw, &end) : NAN;
        if (!raw[0] || (end && *end) || !(v > -100 && v <= 1000)) { msg("Informe a mudança em porcentagem (ex.: -15 para diminuir 15%)."); break; }
        if (b.income == 0) { msg("Sem renda na base: ajuste a base em \"E se…?\"."); break; }
        SimIncome r = sim_income(b, v, sim_goals_monthly(APP->state));
        g_autofree char *nl = m(r.new_left);
        result(nl, r.new_left >= 0 ? "passaria a sobrar por mês." : "faltariam por mês.", r.new_left < 0 ? "fin-red" : NULL);
        g_autofree char *bi = m(b.income), *ni = m(r.new_income), *ren = g_strdup_printf("%s → %s", bi, ni);
        w_add(S->out, line("Renda por mês", ren, NULL));
        g_autofree char *dd = m(r.diff < 0 ? -r.diff : r.diff), *ds = g_strdup_printf("%s %s", r.diff >= 0 ? "+" : "−", dd);
        w_add(S->out, line("Diferença por mês", ds, r.diff < 0 ? "heading fin-red" : "heading fin-green"));
        g_autofree char *yd = m(r.year_diff < 0 ? -r.year_diff : r.year_diff), *ys = g_strdup_printf("%s %s", r.year_diff >= 0 ? "+" : "−", yd);
        w_add(S->out, line("Em 12 meses", ys, r.year_diff < 0 ? "heading fin-red" : "heading fin-green"));
        if (r.goals_monthly > 0) {
            g_autofree char *gm = m(r.goals_monthly);
            gboolean fits = r.new_left >= r.goals_monthly;
            g_autofree char *t = fits ? g_strdup_printf("Suas metas pedem %s por mês: ainda cabe no que sobra.", gm)
                                      : g_strdup_printf("Suas metas pedem %s por mês: não cabe no que sobraria. Os prazos das metas atrasariam.", gm);
            w_add(S->out, w_label_wrap(t, fits ? "fin-muted caption" : "fin-red caption"));
        }
        char pctb[32];
        g_ascii_formatd(pctb, sizeof pctb, "%g", fabs(v));
        for (char *p = pctb; *p; p++) if (*p == '.') *p = ',';
        g_autofree char *w = g_strdup_printf("Conta: renda da base × (1 %s %s%%), com as despesas da base iguais. Começa a valer no mês que vem.", v >= 0 ? "+" : "−", pctb);
        why(w);
        break;
    }
    case SC_DEBT: {
        g_autoptr(GPtrArray) debts = sim_debts(APP->state, APP->today);
        if (!debts->len) { msg("Nenhuma compra parcelada com parcelas a pagar. As parcelas aparecem aqui quando um lançamento é feito com \"Parcelas\" maior que 1."); break; }
        SimDebt *d = debts->pdata[0];
        for (guint i = 0; i < debts->len; i++) if (S->debt_sel && strcmp(((SimDebt *)debts->pdata[i])->group_id, S->debt_sel) == 0) d = debts->pdata[i];
        Cents pay;
        if (!pos_money(S->debt_pay, &pay)) { msg("Informe o valor para quitar."); break; }
        SimPayoff r = sim_payoff(d, pay, current_balance(APP->state));
        g_autofree char *pl = plural(r.months, "parcela", "parcelas");
        g_autofree char *sv = m(r.saved), *sent = g_strdup_printf("a menos do que pagar as %s.", pl);
        if (r.saved > 0) result(sv, sent, "fin-green");
        else result("Sem desconto", "Pagar hoje o mesmo valor só adianta a saída do dinheiro.", "fin-muted");
        g_autofree char *pn = m(r.pay_now), *par = m(d->parcel), *skip = g_strdup_printf("%s de %s", pl, par);
        g_autofree char *fr = m(r.freed_per_month), *frs = g_strdup_printf("+ %s por mês", fr), *ba = m(r.balance_after), *dl = m(d->left);
        w_add(S->out, line("Pagaria hoje", pn, NULL));
        w_add(S->out, line("Deixaria de pagar", skip, NULL));
        w_add(S->out, line("A partir do mês que vem, sobra a mais", frs, "heading fin-green"));
        w_add(S->out, line("Saldo das contas depois de pagar", ba, r.balance_after < 0 ? "heading fin-red" : NULL));
        if (r.balance_after < 0) w_add(S->out, w_label_wrap("O saldo atual não cobre esse pagamento.", "fin-red caption"));
        g_autofree char *w = g_strdup_printf("O Finan+ não conhece os juros do parcelamento: a economia é só a diferença entre o que falta (%s) e o valor para quitar. %s",
                                             dl, d->card ? "No cartão, a antecipação é feita com o banco do cartão." : "Confirme o valor com o credor.");
        why(w);
        break;
    }
    default: break;
    }
    gtk_widget_set_visible(S->goal, can_goal && (S->scen == SC_SAVE || S->scen == SC_BUY));
}

/* ---------------------------------------------------------------- telas */

static GtkWidget *scen_button(Scen sc) {
    GtkWidget *b = gtk_button_new();
    gtk_widget_add_css_class(b, "fin-scen");
    GtkWidget *r = w_hbox(12);
    GtkWidget *ic = w_icon(SCEN[sc].icon, 22);
    gtk_widget_add_css_class(ic, "fin-scen-ico");
    w_add(r, ic);
    GtkWidget *t = w_vbox(1);
    gtk_widget_set_hexpand(t, TRUE);
    w_add(t, w_label(SCEN[sc].title, "heading"));
    w_add(t, w_label_wrap(SCEN[sc].sub, "fin-muted caption"));
    w_add(r, t);
    w_add(r, w_icon("chevron-right", 20));
    gtk_button_set_child(GTK_BUTTON(b), r);
    g_signal_connect_swapped(b, "clicked", G_CALLBACK(go), GINT_TO_POINTER(sc));
    return b;
}

static void build(void) {
    w_clear(S->body);
    S->out = S->goal = S->share = S->base_box = NULL;
    adw_dialog_set_title(S->dialog, S->scen == SC_HOME ? "E se…?" : SCEN[S->scen].head);
    if (S->scen != SC_HOME) {
        GtkWidget *back = w_button("E se…?", "chevron-left", "flat fin-link", go, GINT_TO_POINTER(SC_HOME), NULL);
        gtk_widget_set_halign(back, GTK_ALIGN_START);
        w_add(S->body, back);
    } else w_add(S->body, w_label_wrap("Teste decisões antes de tomá-las.", "fin-muted"));
    w_add(S->body, safe_badge());

    if (S->scen == SC_HOME) {
        S->base_box = w_vbox(4);
        gtk_widget_add_css_class(S->base_box, "fin-simbox");
        base_card_into(S->base_box);
        w_add(S->body, S->base_box);
        SimBase a;
        cur_base(&a);
        gboolean open = S->adjust || (a.months == 0 && !S->adjust && !S->inc && !S->exp);
        GtkWidget *adj = w_button(open ? "Ocultar ajuste da base" : "Ajustar a base", NULL, "flat fin-link", toggle_adjust, NULL, NULL);
        gtk_widget_set_halign(adj, GTK_ALIGN_START);
        w_add(S->body, adj);
        if (open) {
            char pi[32], pe[32];
            GtkWidget *row = w_hbox(8);
            gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
            w_add(row, entry("Entra por mês", &S->inc, money_input(a.income, pi), 20, TRUE));
            w_add(row, entry("Sai por mês", &S->exp, money_input(a.expense, pe), 20, TRUE));
            w_add(S->body, row);
            w_add(S->body, w_label("Vazio = usa a média. Vale só para esta simulação.", "fin-muted caption"));
        }
        GtkWidget *h = w_label("Escolha uma pergunta", "fin-title");
        gtk_widget_set_margin_top(h, 8);
        w_add(S->body, h);
        for (Scen sc = SC_SAVE; sc <= SC_DEBT; sc++) w_add(S->body, scen_button(sc));
        return;
    }

    GtkWidget *form = w_vbox(10);
    switch (S->scen) {
    case SC_SAVE:
        w_add(form, entry("Guardar por mês (R$)", &S->save_per, "0,00", 20, FALSE));
        w_add(form, entry("Por quantos meses", &S->save_months, "12", 3, FALSE));
        break;
    case SC_BUY: {
        w_add(form, entry("O que", &S->buy_what, "Ex.: Computador", 40, FALSE));
        GtkWidget *row = w_hbox(8);
        gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
        w_add(row, entry("Preço (R$)", &S->buy_price, "0,00", 20, FALSE));
        w_add(row, entry("Já tenho (R$)", &S->buy_have, "0,00", 20, FALSE));
        w_add(form, row);
        w_add(form, entry("Guardar por mês (R$)", &S->buy_per, "0,00", 20, FALSE));
        S->share = w_label("", "fin-muted caption");
        w_add(form, S->share);
        break;
    }
    case SC_INCOME:
        w_add(form, entry("Mudança na renda (%)", &S->pct, "Ex.: -15 ou 10", 6, FALSE));
        break;
    case SC_DEBT: {
        g_autoptr(GPtrArray) debts = sim_debts(APP->state, APP->today);
        if (debts->len) {
            SimDebt *sel = debts->pdata[0];
            for (guint i = 0; i < debts->len; i++) if (S->debt_sel && strcmp(((SimDebt *)debts->pdata[i])->group_id, S->debt_sel) == 0) sel = debts->pdata[i];
            if (!S->debt_sel || strcmp(S->debt_sel, sel->group_id)) { g_free(S->debt_sel); S->debt_sel = g_strdup(sel->group_id); }
            w_add(form, w_label("Qual parcelamento", "heading"));
            for (guint i = 0; i < debts->len; i++) {
                SimDebt *x = debts->pdata[i];
                GtkWidget *b = gtk_button_new();
                gtk_widget_add_css_class(b, "fin-scen");
                if (x == sel) gtk_widget_add_css_class(b, "on");
                GtkWidget *r = w_hbox(10);
                GtkWidget *t = w_vbox(1);
                gtk_widget_set_hexpand(t, TRUE);
                w_add(t, w_label(x->name, "heading"));
                g_autofree char *pl = plural(x->remaining, "parcela restante", "parcelas restantes"), *pm = m(x->parcel);
                g_autofree char *sub = g_strdup_printf("%s de %s%s", pl, pm, x->card ? " · cartão" : "");
                w_add(t, w_label(sub, "fin-muted caption"));
                w_add(r, t);
                w_add(r, w_money(x->left, "heading"));
                gtk_button_set_child(GTK_BUTTON(b), r);
                gtk_accessible_update_state(GTK_ACCESSIBLE(b), GTK_ACCESSIBLE_STATE_PRESSED, x == sel ? GTK_ACCESSIBLE_TRISTATE_TRUE : GTK_ACCESSIBLE_TRISTATE_FALSE, -1);
                g_signal_connect_data(b, "clicked", G_CALLBACK(pick_debt), g_strdup(x->group_id), (GClosureNotify)(void (*)(void))g_free, G_CONNECT_SWAPPED);
                w_add(form, b);
            }
            if (!S->debt_pay) { char buf[32]; S->debt_pay = g_strdup(money_input(sel->left, buf)); }
            w_add(form, entry("Valor para quitar hoje (R$)", &S->debt_pay, "0,00", 20, FALSE));
            w_add(form, w_label_wrap("Use o valor que o credor ou o banco oferecer para quitar.", "fin-muted caption"));
        }
        break;
    }
    default: break;
    }
    w_add(S->body, form);
    S->out = w_vbox(4);
    gtk_widget_add_css_class(S->out, "fin-simbox");
    w_add(S->body, S->out);
    S->goal = w_button("Transformar em meta", "flag", "suggested-action pill", make_goal, NULL, NULL);
    gtk_widget_set_halign(S->goal, GTK_ALIGN_FILL);
    w_add(S->body, S->goal);
    update();
}

void simulator_dialog(void) {
    if (S) return;
    S = g_new0(Sim, 1);
    S->save_per = g_strdup("200,00");
    S->save_months = g_strdup("12");
    S->pct = g_strdup("-15");
    S->dialog = adw_dialog_new();
    adw_dialog_set_content_width(S->dialog, 560);
    adw_dialog_set_content_height(S->dialog, 700);
    GtkWidget *tv = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), adw_header_bar_new());
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sw), TRUE);
    S->body = w_vbox(10);
    gtk_widget_set_margin_start(S->body, 18);
    gtk_widget_set_margin_end(S->body, 18);
    gtk_widget_set_margin_bottom(S->body, 18);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), S->body);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), sw);
    adw_dialog_set_child(S->dialog, tv);
    g_object_set_data_full(G_OBJECT(S->dialog), "sim", S, sim_free);
    build();
    adw_dialog_present(S->dialog, GTK_WIDGET(APP->window));
}
