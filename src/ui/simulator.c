/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Diálogo do simulador "E se…?" (aberto em Relatórios). Só lê os dados: nada aqui grava no estado.
 * As contas ficam em core/simulator.c (testadas); mesma tela do app Android 1.4.0 (SimulatorSheet.kt)
 * e do Finan+ web 1.3.0 (js/simsheet.js). Detalhes em SIMULADOR.md.
 */
#include "pages.h"
#include "widgets.h"
#include "core/finance.h"
#include "core/simulator.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef enum { SC_HOME, SC_SAVE, SC_BUY, SC_INCOME, SC_DEBT } Scen;

static const struct { const char *icon, *title, *sub, *head; } SCEN[] = {
    [SC_SAVE] = {"savings", "E se eu economizar…", "Ex.: R$ 200 por mês, por 12 meses", "E se eu economizar…?"},
    [SC_BUY] = {"shopping-bag", "Quanto tempo para comprar…", "Ex.: um computador de R$ 4.500", "Quanto tempo para comprar?"},
    [SC_INCOME] = {"work", "E se minha renda mudar…", "Ex.: diminuir 15% a partir do mês que vem", "E se minha renda mudar…?"},
    [SC_DEBT] = {"account-balance", "E se eu antecipar uma dívida…", "Parcelas que faltam, valor para quitar e quanto sobra", "E se eu antecipar uma dívida…?"},
};

/* o que foi digitado vale enquanto o diálogo está aberto (trocar de pergunta não apaga) */
static struct {
    AdwDialog *dlg;
    GtkWidget *title, *body, *out, *goal_btn, *share, *base_box;
    Scen scen;
    int adjust; /* -1 = automático (aberto só sem histórico) */
    char *inc, *exp;
    char *save_per, *save_months;
    char *buy_what, *buy_price, *buy_have, *buy_per;
    char *income_pct;
    char *debt_sel;
    GHashTable *debt_pay; /* group_id → texto */
} S;

static void set_str(char **f, const char *v) { g_free(*f); *f = g_strdup(v ? v : ""); }

static void state_reset(void) {
    set_str(&S.inc, ""); set_str(&S.exp, "");
    set_str(&S.save_per, "200,00"); set_str(&S.save_months, "12");
    set_str(&S.buy_what, ""); set_str(&S.buy_price, ""); set_str(&S.buy_have, ""); set_str(&S.buy_per, "");
    set_str(&S.income_pct, "-15");
    g_clear_pointer(&S.debt_sel, g_free);
    if (S.debt_pay) g_hash_table_remove_all(S.debt_pay);
    else S.debt_pay = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    S.adjust = -1;
}

/* ---------------------------------------------------------------- utilidades */

static char *M(Cents c) { return app_money(c); }

static char *plural(int n, const char *one, const char *many) {
    return n == 1 ? g_strdup_printf("1 %s", one) : g_strdup_printf("%d %s", n, many);
}

/* valor positivo; FALSE se vazio ou inválido */
static gboolean pos_money(const char *t, Cents *out) {
    Cents v;
    if (!t || !money_parse(t, &v) || v <= 0) return FALSE;
    *out = v;
    return TRUE;
}

/* inteiro entre lo e hi */
static gboolean int_in(const char *t, int lo, int hi, int *out) {
    if (!t) return FALSE;
    g_autofree char *s = g_strstrip(g_strdup(t));
    if (!*s) return FALSE;
    for (const char *p = s; *p; p++) if (*p < '0' || *p > '9') return FALSE;
    long n = strtol(s, NULL, 10);
    if (n < lo || n > hi) return FALSE;
    *out = (int)n;
    return TRUE;
}

static SimBase cur_base(SimBase *auto_out) {
    SimBase a = sim_base(APP->state, APP->today);
    if (auto_out) *auto_out = a;
    Cents v;
    SimBase b = a;
    if (S.inc && *S.inc && money_parse(S.inc, &v)) b.income = v;
    if (S.exp && *S.exp && money_parse(S.exp, &v)) b.expense = v;
    return b;
}

static gboolean adjust_open(void) {
    if (S.adjust >= 0) return S.adjust;
    return sim_base(APP->state, APP->today).months == 0;
}

static GtkWidget *line(const char *label, const char *value, const char *cls) {
    GtkWidget *r = w_hbox(8);
    GtkWidget *l = w_label_wrap(label, "fin-muted");
    gtk_widget_set_hexpand(l, TRUE);
    w_add(r, l);
    GtkWidget *v = w_label(value, cls && *cls ? cls : "heading");
    if (cls && *cls) gtk_widget_add_css_class(v, "heading");
    gtk_label_set_ellipsize(GTK_LABEL(v), PANGO_ELLIPSIZE_NONE);
    w_add(r, v);
    return r;
}

static void result(GtkWidget *box, const char *big, const char *sentence, const char *cls) {
    w_add(box, w_eyebrow("Resultado"));
    GtkWidget *b = w_label(big, "fin-money-big");
    if (cls) w_classes(b, cls);
    w_add(box, b);
    if (sentence) w_add(box, w_label_wrap(sentence, NULL));
}

static void why_text(GtkWidget *box, const char *t) {
    GtkWidget *l = w_label_wrap(t, "fin-muted caption");
    gtk_widget_set_margin_top(l, 6);
    w_add(box, l);
}

static void msg(GtkWidget *box, const char *t) { w_add(box, w_label_wrap(t, "fin-muted")); }

static GtkWidget *safe_badge(void) {
    GtkWidget *b = w_hbox(6);
    gtk_widget_add_css_class(b, "fin-badge");
    gtk_widget_add_css_class(b, "green");
    gtk_widget_set_halign(b, GTK_ALIGN_START);
    w_add(b, w_icon("shield", 14));
    w_add(b, gtk_label_new("Só simulação: seus dados não mudam"));
    return b;
}

/* campo de texto com rótulo; [field] guarda o texto; [on_change] recalcula */
typedef struct { char **field; void (*on_change)(void); } FieldCb;

static void on_field(GtkEditable *e, gpointer p) {
    FieldCb *cb = p;
    set_str(cb->field, gtk_editable_get_text(e));
    cb->on_change();
}

static GtkWidget *field(const char *label, char **value, const char *placeholder, int max, void (*on_change)(void)) {
    GtkWidget *b = w_vbox(4);
    gtk_widget_set_hexpand(b, TRUE);
    w_add(b, w_label(label, "fin-muted caption"));
    GtkWidget *e = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(e), *value ? *value : "");
    if (placeholder) gtk_entry_set_placeholder_text(GTK_ENTRY(e), placeholder);
    if (max > 0) gtk_entry_set_max_length(GTK_ENTRY(e), max);
    gtk_accessible_update_property(GTK_ACCESSIBLE(e), GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
    FieldCb *cb = g_new0(FieldCb, 1);
    cb->field = value;
    cb->on_change = on_change;
    g_signal_connect_data(e, "changed", G_CALLBACK(on_field), cb, (GClosureNotify)(void (*)(void))g_free, 0);
    w_add(b, e);
    return b;
}

static void render(Scen sc);

/* ---------------------------------------------------------------- base */

static void fill_base(GtkWidget *box) {
    w_clear(box);
    SimBase a, b = cur_base(&a);
    g_autofree char *eb = a.months > 0 ? g_strdup_printf("Sua base · média dos últimos %d %s", a.months, a.months == 1 ? "mês" : "meses")
                                       : g_strdup("Sua base");
    w_add(box, w_eyebrow(eb));
    if (a.months == 0 && b.income == 0 && b.expense == 0)
        msg(box, "Ainda não há meses completos com valores realizados. Informe abaixo quanto entra e sai num mês típico.");
    GtkWidget *row = w_hbox(10);
    gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
    const char *labels[3] = {"Entra", "Sai", "Sobra"};
    Cents vals[3] = {b.income, b.expense, sim_base_left(b)};
    const char *cls[3] = {"fin-money-mid fin-green", "fin-money-mid fin-red", sim_base_left(b) < 0 ? "fin-money-mid fin-red" : "fin-money-mid fin-accent"};
    for (int i = 0; i < 3; i++) {
        GtkWidget *c = w_vbox(2);
        w_add(c, w_label(labels[i], "fin-muted caption"));
        w_add(c, w_money(vals[i], cls[i]));
        w_add(row, c);
    }
    w_add(box, row);
    if (a.months >= 1 && a.months <= 2) {
        g_autofree char *t = g_strdup_printf("Pouco histórico: a média usa só %d %s. Os resultados são aproximados.", a.months, a.months == 1 ? "mês" : "meses");
        w_add(box, w_label_wrap(t, "fin-muted caption"));
    }
}

static void base_changed(void) { if (S.base_box) fill_base(S.base_box); }

static void toggle_adjust(gpointer u) {
    (void)u;
    S.adjust = !adjust_open();
    render(SC_HOME);
}

static void go_scen(gpointer sc) { render((Scen)GPOINTER_TO_INT(sc)); }

static GtkWidget *scen_row(Scen sc) {
    GtkWidget *btn = w_button(SCEN[sc].title, NULL, "fin-row", go_scen, GINT_TO_POINTER(sc), NULL);
    GtkWidget *row = w_hbox(12);
    w_add(row, w_icon(SCEN[sc].icon, 22));
    GtkWidget *t = w_vbox(1);
    gtk_widget_set_hexpand(t, TRUE);
    w_add(t, w_label(SCEN[sc].title, "heading"));
    w_add(t, w_label_wrap(SCEN[sc].sub, "fin-muted caption"));
    w_add(row, t);
    w_add(row, w_icon("chevron-right", 20));
    gtk_button_set_child(GTK_BUTTON(btn), row);
    return btn;
}

static void home_body(GtkWidget *c) {
    w_add(c, w_label_wrap("Teste decisões antes de tomá-las.", "fin-muted"));
    w_add(c, safe_badge());
    S.base_box = w_card("fin-flat");
    fill_base(S.base_box);
    w_add(c, S.base_box);
    gboolean open = adjust_open();
    GtkWidget *adj = w_button(open ? "Ocultar ajuste da base" : "Ajustar a base", NULL, "flat fin-link", toggle_adjust, NULL, NULL);
    gtk_widget_set_halign(adj, GTK_ALIGN_START);
    gtk_accessible_update_state(GTK_ACCESSIBLE(adj), GTK_ACCESSIBLE_STATE_EXPANDED, open, -1);
    w_add(c, adj);
    if (open) {
        SimBase a;
        cur_base(&a);
        char p1[32], p2[32];
        GtkWidget *two = w_hbox(10);
        gtk_box_set_homogeneous(GTK_BOX(two), TRUE);
        w_add(two, field("Entra por mês", &S.inc, money_input(a.income, p1), 20, base_changed));
        w_add(two, field("Sai por mês", &S.exp, money_input(a.expense, p2), 20, base_changed));
        w_add(c, two);
        w_add(c, w_label_wrap("Vazio = usa a média. Vale só para esta simulação.", "fin-muted caption"));
    }
    w_add(c, w_label("Escolha uma pergunta", "heading"));
    for (Scen sc = SC_SAVE; sc <= SC_DEBT; sc++) w_add(c, scen_row(sc));
}

/* ---------------------------------------------------------------- resultados */

static void update(void);

static const SimDebt *cur_debt(GPtrArray *debts) {
    if (!debts->len) return NULL;
    for (guint i = 0; i < debts->len; i++) {
        const SimDebt *d = debts->pdata[i];
        if (S.debt_sel && !strcmp(d->group_id, S.debt_sel)) return d;
    }
    return debts->pdata[0];
}

static void transform_goal(gpointer u) {
    (void)u;
    Cents v, pr, pm = 0;
    if (S.scen == SC_SAVE && pos_money(S.save_per, &v)) {
        int n;
        if (!int_in(S.save_months, 1, 600, &n)) n = 12;
        adw_dialog_close(S.dlg);
        editor_goal_pre(NULL, "Reserva", v * n, v);
    } else if (S.scen == SC_BUY && pos_money(S.buy_price, &pr) && money_parse(S.buy_per, &pm) && pm > 0) {
        g_autofree char *name = g_strstrip(g_strdup(S.buy_what));
        adw_dialog_close(S.dlg);
        editor_goal_pre(NULL, *name ? name : "Compra", pr, pm);
    }
}

static void update_save(GtkWidget *o, SimBase b) {
    Cents v;
    int n;
    gboolean okv = pos_money(S.save_per, &v), okn = int_in(S.save_months, 1, 600, &n);
    gtk_widget_set_visible(S.goal_btn, okv);
    if (!okv || !okn) { msg(o, "Informe o valor por mês e por quantos meses."); return; }
    SimSave r = sim_save(b, v, n);
    g_autofree char *tot = M(r.total), *pl = plural(n, "mês", "meses");
    g_autofree char *sent = g_strdup_printf("juntados em %s.", pl);
    result(o, tot, sent, "fin-accent");
    g_autofree char *left = M(sim_base_left(b)), *vv = M(v), *nl = M(r.new_left);
    w_add(o, line("Sobra por mês hoje", left, NULL));
    g_autofree char *lab = g_strdup_printf("Sobra por mês guardando %s", vv);
    w_add(o, line(lab, nl, r.new_left < 0 ? "fin-red" : NULL));
    if (r.over_left) w_add(o, w_label_wrap("Esse valor é maior do que sobra por mês: faltaria dinheiro para as despesas de sempre.", "fin-red caption"));
    const int ks[3] = {6, 12, 24};
    for (int i = 0; i < 3; i++) {
        if (ks[i] == n) continue;
        g_autofree char *l = g_strdup_printf("Em %d meses", ks[i]);
        g_autofree char *x = M(v * ks[i]);
        w_add(o, line(l, x, NULL));
    }
    g_autofree char *why = g_strdup_printf("Conta: %s × %d meses. Não considera rendimento: o Finan+ não sabe quanto o dinheiro guardado renderia.", vv, n);
    why_text(o, why);
}

static void draw_buy(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer p) {
    (void)a;
    int bars = GPOINTER_TO_INT(p);
    if (bars <= 0) return;
    double gap = 2, bw = MAX(2.0, (w - gap * (bars - 1)) / bars);
    GdkRGBA acc, done;
    gdk_rgba_parse(&acc, "rgba(66,105,216,0.45)");
    gdk_rgba_parse(&done, "rgb(46,125,50)");
    for (int i = 0; i < bars; i++) {
        double hh = h * (0.12 + 0.88 * (i + 1) / bars);
        gdk_cairo_set_source_rgba(cr, i == bars - 1 ? &done : &acc);
        cairo_rectangle(cr, i * (bw + gap), h - hh, bw, hh);
        cairo_fill(cr);
    }
}

static void update_buy(GtkWidget *o, SimBase b) {
    Cents pr, hv = 0, pm = 0;
    gboolean okp = pos_money(S.buy_price, &pr);
    if (S.buy_have && *S.buy_have) money_parse(S.buy_have, &hv);
    hv = MAX(0, hv);
    if (S.buy_per && *S.buy_per) money_parse(S.buy_per, &pm);
    if (S.share) {
        g_autofree char *t = NULL;
        if (pm > 0 && sim_base_left(b) > 0 && !app_hidden()) {
            g_autofree char *l = money_fmt(sim_base_left(b));
            t = g_strdup_printf("%ld%% do que sobra por mês (%s)", (long)floor(pm * 100.0 / sim_base_left(b) + 0.5), l);
        }
        gtk_label_set_text(GTK_LABEL(S.share), t ? t : "");
    }
    gtk_widget_set_visible(S.goal_btn, okp && pm > 0);
    if (!okp) { msg(o, "Informe o preço."); return; }
    SimBuy r;
    if (!sim_buy(pr, hv, pm, APP->today, &r)) { msg(o, "Informe quanto dá para guardar por mês."); return; }
    if (r.months == 0) { result(o, "Já dá", "Você já tem o valor.", "fin-accent"); return; }
    g_autofree char *big = plural(r.months, "mês", "meses"), *when = br_month_year(r.done);
    g_autofree char *sent = g_strdup_printf("Você teria o valor em %s.", when);
    result(o, big, sent, "fin-accent");
    int bars = MIN(r.months, 36);
    GtkWidget *da = gtk_drawing_area_new();
    gtk_widget_set_size_request(da, -1, 70);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), draw_buy, GINT_TO_POINTER(app_hidden() ? 0 : bars), NULL);
    gtk_accessible_update_property(GTK_ACCESSIBLE(da), GTK_ACCESSIBLE_PROPERTY_LABEL, "Gráfico do valor juntado mês a mês", -1);
    w_add(o, da);
    Cents alts[2] = {pm * 2, pm / 2};
    for (int i = 0; i < 2; i++) {
        SimBuy a;
        if (alts[i] <= 0 || !sim_buy(pr, hv, alts[i], APP->today, &a)) continue;
        g_autofree char *am = M(alts[i]), *ap = plural(a.months, "mês", "meses"), *aw = br_month_year(a.done);
        g_autofree char *l = g_strdup_printf("Guardando %s/mês", am), *v = g_strdup_printf("%s · %s", ap, aw);
        w_add(o, line(l, v, NULL));
    }
    g_autofree char *mm = M(r.missing), *pmm = M(pm);
    g_autofree char *why = g_strdup_printf("Conta: falta %s ÷ %s por mês = %s (arredondado para cima), começando no mês que vem. Não considera rendimento nem mudança de preço.",
                                           mm, pmm, big);
    why_text(o, why);
}

static void update_income(GtkWidget *o, SimBase b) {
    gtk_widget_set_visible(S.goal_btn, FALSE);
    g_autofree char *raw = g_strstrip(g_strdup(S.income_pct ? S.income_pct : ""));
    for (char *p = raw; *p; p++) if (*p == ',') *p = '.';
    g_autoptr(GRegex) re = g_regex_new("^[-+]?\\d+(\\.\\d+)?$", 0, 0, NULL);
    double v = g_regex_match(re, raw, 0, NULL) ? g_ascii_strtod(raw, NULL) : NAN;
    if (!(v > -100 && v <= 1000)) { msg(o, "Informe a mudança em porcentagem (ex.: -15 para diminuir 15%)."); return; }
    if (b.income == 0) { msg(o, "Sem renda na base: ajuste a base em \"E se…?\"."); return; }
    SimIncome r = sim_income(b, v, sim_goals_monthly(APP->state));
    g_autofree char *nl = M(r.new_left);
    result(o, nl, r.new_left >= 0 ? "passaria a sobrar por mês." : "faltariam por mês.", r.new_left < 0 ? "fin-red" : "fin-accent");
    g_autofree char *bi = M(b.income), *ni = M(r.new_income);
    g_autofree char *ch = g_strdup_printf("%s → %s", bi, ni);
    w_add(o, line("Renda por mês", ch, NULL));
    g_autofree char *d = M(r.diff < 0 ? -r.diff : r.diff), *y = M(r.year_diff < 0 ? -r.year_diff : r.year_diff);
    g_autofree char *ds = g_strdup_printf("%s %s", r.diff >= 0 ? "+" : "−", d), *ys = g_strdup_printf("%s %s", r.year_diff >= 0 ? "+" : "−", y);
    w_add(o, line("Diferença por mês", ds, r.diff < 0 ? "fin-red" : "fin-green"));
    w_add(o, line("Em 12 meses", ys, r.year_diff < 0 ? "fin-red" : "fin-green"));
    if (r.goals_monthly > 0) {
        g_autofree char *gm = M(r.goals_monthly);
        g_autofree char *t = r.new_left >= r.goals_monthly ? g_strdup_printf("Suas metas pedem %s por mês: ainda cabe no que sobra.", gm)
                                                           : g_strdup_printf("Suas metas pedem %s por mês: não cabe no que sobraria. Os prazos das metas atrasariam.", gm);
        w_add(o, w_label_wrap(t, r.new_left >= r.goals_monthly ? "fin-muted caption" : "fin-red caption"));
    }
    g_autofree char *pct = g_strdup_printf("%g", fabs(v));
    for (char *p = pct; *p; p++) if (*p == '.') *p = ',';
    g_autofree char *why = g_strdup_printf("Conta: renda da base × (1 %s %s%%), com as despesas da base iguais. Começa a valer no mês que vem.", v >= 0 ? "+" : "−", pct);
    why_text(o, why);
}

static void update_debt(GtkWidget *o) {
    gtk_widget_set_visible(S.goal_btn, FALSE);
    g_autoptr(GPtrArray) debts = sim_debts(APP->state, APP->today);
    const SimDebt *d = cur_debt(debts);
    if (!d) {
        msg(o, "Nenhuma compra parcelada com parcelas a pagar. As parcelas aparecem aqui quando um lançamento é feito com \"Parcelas\" maior que 1.");
        return;
    }
    const char *typed = g_hash_table_lookup(S.debt_pay, d->group_id);
    char buf[32];
    Cents pay;
    if (!pos_money(typed ? typed : money_input(d->left, buf), &pay)) { msg(o, "Informe o valor para quitar."); return; }
    SimPayoff r = sim_payoff(d, pay, current_balance(APP->state));
    g_autofree char *saved = M(r.saved), *pl = plural(r.months, "parcela", "parcelas");
    g_autofree char *sent = g_strdup_printf("a menos do que pagar as %s.", pl);
    if (r.saved > 0) result(o, saved, sent, "fin-green");
    else result(o, "Sem desconto", "Pagar hoje o mesmo valor só adianta a saída do dinheiro.", "fin-muted");
    g_autofree char *pn = M(r.pay_now), *pp = M(d->parcel), *fr = M(r.freed_per_month), *ba = M(r.balance_after);
    w_add(o, line("Pagaria hoje", pn, NULL));
    g_autofree char *stop = g_strdup_printf("%s de %s", pl, pp);
    w_add(o, line("Deixaria de pagar", stop, NULL));
    g_autofree char *more = g_strdup_printf("+ %s por mês", fr);
    w_add(o, line("A partir do mês que vem, sobra a mais", more, "fin-green"));
    w_add(o, line("Saldo das contas depois de pagar", ba, r.balance_after < 0 ? "fin-red" : NULL));
    if (r.balance_after < 0) w_add(o, w_label_wrap("O saldo atual não cobre esse pagamento.", "fin-red caption"));
    g_autofree char *left = M(d->left);
    g_autofree char *why = g_strdup_printf("O Finan+ não conhece os juros do parcelamento: a economia é só a diferença entre o que falta (%s) e o valor para quitar. %s",
                                           left, d->card ? "No cartão, a antecipação é feita com o banco do cartão." : "Confirme o valor com o credor.");
    why_text(o, why);
}

static void update(void) {
    if (!S.out) return;
    w_clear(S.out);
    SimBase b = cur_base(NULL);
    switch (S.scen) {
    case SC_SAVE: update_save(S.out, b); break;
    case SC_BUY: update_buy(S.out, b); break;
    case SC_INCOME: update_income(S.out, b); break;
    case SC_DEBT: update_debt(S.out); break;
    default: break;
    }
}

/* ---------------------------------------------------------------- formulários */

static void pick_debt(gpointer id) {
    set_str(&S.debt_sel, id);
    render(SC_DEBT);
}

static void on_debt_pay(GtkEditable *e, gpointer u) {
    (void)u;
    const char *gid = g_object_get_data(G_OBJECT(e), "gid");
    g_hash_table_replace(S.debt_pay, g_strdup(gid), g_strdup(gtk_editable_get_text(e)));
    update();
}

static void form_for(GtkWidget *c) {
    switch (S.scen) {
    case SC_SAVE:
        w_add(c, field("Guardar por mês (R$)", &S.save_per, "Ex.: 200,00", 20, update));
        w_add(c, field("Por quantos meses", &S.save_months, NULL, 3, update));
        break;
    case SC_BUY: {
        w_add(c, field("O que", &S.buy_what, "Ex.: Computador", 40, update));
        GtkWidget *two = w_hbox(10);
        gtk_box_set_homogeneous(GTK_BOX(two), TRUE);
        w_add(two, field("Preço (R$)", &S.buy_price, NULL, 20, update));
        w_add(two, field("Já tenho (R$)", &S.buy_have, NULL, 20, update));
        w_add(c, two);
        w_add(c, field("Guardar por mês (R$)", &S.buy_per, NULL, 20, update));
        S.share = w_label("", "fin-muted caption");
        w_add(c, S.share);
        break;
    }
    case SC_INCOME:
        w_add(c, field("Mudança na renda (%)", &S.income_pct, "Ex.: -15 ou 10", 6, update));
        break;
    case SC_DEBT: {
        g_autoptr(GPtrArray) debts = sim_debts(APP->state, APP->today);
        const SimDebt *d = cur_debt(debts);
        if (!d) break;
        w_add(c, w_label("Qual parcelamento", "heading"));
        for (guint i = 0; i < debts->len; i++) {
            const SimDebt *x = debts->pdata[i];
            gboolean on = !strcmp(x->group_id, d->group_id);
            GtkWidget *btn = w_button(x->name, NULL, on ? "fin-row fin-sim-on" : "fin-row", pick_debt, g_strdup(x->group_id), g_free);
            GtkWidget *row = w_hbox(10);
            GtkWidget *t = w_vbox(1);
            gtk_widget_set_hexpand(t, TRUE);
            w_add(t, w_label(x->name, "heading"));
            g_autofree char *pl = plural(x->remaining, "parcela restante", "parcelas restantes"), *pv = M(x->parcel);
            g_autofree char *sub = g_strdup_printf("%s de %s%s", pl, pv, x->card ? " · cartão" : "");
            w_add(t, w_label(sub, "fin-muted caption"));
            w_add(row, t);
            w_add(row, w_money(x->left, "heading"));
            gtk_button_set_child(GTK_BUTTON(btn), row);
            gtk_accessible_update_state(GTK_ACCESSIBLE(btn), GTK_ACCESSIBLE_STATE_CHECKED, on ? GTK_ACCESSIBLE_TRISTATE_TRUE : GTK_ACCESSIBLE_TRISTATE_FALSE, -1);
            w_add(c, btn);
        }
        const char *typed = g_hash_table_lookup(S.debt_pay, d->group_id);
        char buf[32];
        GtkWidget *b = w_vbox(4);
        w_add(b, w_label("Valor para quitar hoje (R$)", "fin-muted caption"));
        GtkWidget *e = gtk_entry_new();
        gtk_editable_set_text(GTK_EDITABLE(e), typed ? typed : money_input(d->left, buf));
        gtk_entry_set_max_length(GTK_ENTRY(e), 20);
        gtk_accessible_update_property(GTK_ACCESSIBLE(e), GTK_ACCESSIBLE_PROPERTY_LABEL, "Valor para quitar hoje", -1);
        g_object_set_data_full(G_OBJECT(e), "gid", g_strdup(d->group_id), g_free);
        g_signal_connect(e, "changed", G_CALLBACK(on_debt_pay), NULL);
        w_add(b, e);
        w_add(b, w_label_wrap("Use o valor que o credor ou o banco oferecer para quitar.", "fin-muted caption"));
        w_add(c, b);
        break;
    }
    default: break;
    }
}

static void go_home(gpointer u) { (void)u; render(SC_HOME); }

static void render(Scen sc) {
    S.scen = sc;
    S.out = S.goal_btn = S.share = S.base_box = NULL;
    w_clear(S.body);
    adw_dialog_set_title(S.dlg, sc == SC_HOME ? "E se…?" : SCEN[sc].head);
    if (sc == SC_HOME) { home_body(S.body); return; }
    GtkWidget *back = w_button(NULL, NULL, "flat fin-link", go_home, NULL, NULL);
    GtkWidget *br = w_hbox(2);
    w_add(br, w_icon("chevron-left", 20));
    w_add(br, gtk_label_new("E se…?"));
    gtk_button_set_child(GTK_BUTTON(back), br);
    gtk_widget_set_halign(back, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(back, "Voltar para as perguntas");
    w_add(S.body, back);
    w_add(S.body, safe_badge());
    form_for(S.body);
    S.out = w_card("fin-flat");
    gtk_accessible_update_property(GTK_ACCESSIBLE(S.out), GTK_ACCESSIBLE_PROPERTY_LABEL, "Resultado da simulação", -1);
    w_add(S.body, S.out);
    S.goal_btn = w_button("Transformar em meta", "flag", "suggested-action pill", transform_goal, NULL, NULL);
    gtk_widget_set_halign(S.goal_btn, GTK_ALIGN_CENTER);
    w_add(S.body, S.goal_btn);
    update();
}

static void on_closed(AdwDialog *d, gpointer u) {
    (void)d; (void)u;
    S.dlg = NULL;
    S.body = S.out = S.goal_btn = S.share = S.base_box = NULL;
}

void simulator_open(void) {
    if (S.dlg) return;
    state_reset(); /* aberto de novo em Relatórios: começa do zero */
    S.dlg = adw_dialog_new();
    adw_dialog_set_content_width(S.dlg, 560);
    adw_dialog_set_content_height(S.dlg, 680);
    GtkWidget *tv = adw_toolbar_view_new();
    GtkWidget *hb = adw_header_bar_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), hb);
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    S.body = w_vbox(12);
    gtk_widget_set_margin_start(S.body, 18);
    gtk_widget_set_margin_end(S.body, 18);
    gtk_widget_set_margin_top(S.body, 8);
    gtk_widget_set_margin_bottom(S.body, 20);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), S.body);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), sw);
    adw_dialog_set_child(S.dlg, tv);
    g_signal_connect(S.dlg, "closed", G_CALLBACK(on_closed), NULL);
    render(SC_HOME);
    adw_dialog_present(S.dlg, GTK_WIDGET(APP->window));
}
