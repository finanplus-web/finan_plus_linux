/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Editores em diálogos: lançamento, meta, conta, cartão, pagamento de fatura, recorrência e limite.
 * As validações e mensagens vêm de core/ops.c (as mesmas do app Android).
 */
#include "pages.h"
#include "widgets.h"
#include "core/finance.h"
#include <string.h>

/* ---------------------------------------------------------------- moldura comum */

typedef struct {
    AdwDialog *dialog;
    GtkWidget *content;
    GtkWidget *save;
    gboolean closing; /* já salvou: ignora cliques/Enter repetidos durante a animação de fechar */
} Frame;

static void frame_close(Frame *f) { adw_dialog_close(f->dialog); }

static Frame *frame_new(const char *title, const char *subtitle, const char *save_label) {
    Frame *f = g_new0(Frame, 1);
    f->dialog = adw_dialog_new();
    adw_dialog_set_title(f->dialog, title);
    adw_dialog_set_content_width(f->dialog, 560);
    adw_dialog_set_follows_content_size(f->dialog, FALSE);
    adw_dialog_set_content_height(f->dialog, 640);
    GtkWidget *tv = adw_toolbar_view_new();
    GtkWidget *hb = adw_header_bar_new();
    adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(hb), FALSE);
    adw_header_bar_set_show_start_title_buttons(ADW_HEADER_BAR(hb), FALSE);
    GtkWidget *cancel = gtk_button_new_with_label("Cancelar");
    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(adw_dialog_close), f->dialog);
    adw_header_bar_pack_start(ADW_HEADER_BAR(hb), cancel);
    f->save = gtk_button_new_with_label(save_label);
    gtk_widget_add_css_class(f->save, "suggested-action");
    adw_header_bar_pack_end(ADW_HEADER_BAR(hb), f->save);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), hb);
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sw), TRUE);
    f->content = w_vbox(14);
    gtk_widget_set_margin_start(f->content, 18);
    gtk_widget_set_margin_end(f->content, 18);
    gtk_widget_set_margin_top(f->content, 12);
    gtk_widget_set_margin_bottom(f->content, 18);
    if (subtitle && *subtitle) w_add(f->content, w_label_wrap(subtitle, "fin-muted"));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), f->content);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), sw);
    adw_dialog_set_child(f->dialog, tv);
    adw_dialog_set_default_widget(f->dialog, f->save);
    g_object_set_data_full(G_OBJECT(f->dialog), "frame", f, g_free);
    return f;
}

static void frame_present(Frame *f, GtkWidget *focus) {
    adw_dialog_present(f->dialog, GTK_WIDGET(APP->window));
    if (focus) adw_dialog_set_focus(f->dialog, focus);
}

static GtkWidget *group(GtkWidget *content, const char *title) {
    GtkWidget *g = adw_preferences_group_new();
    if (title) adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(g), title);
    w_add(content, g);
    return g;
}

static void gadd(GtkWidget *g, GtkWidget *row) { adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), row); }

static GtkWidget *delete_button(Frame *f, const char *label) {
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(b, "destructive-action");
    gtk_widget_add_css_class(b, "pill");
    gtk_widget_set_halign(b, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(b, 6);
    w_add(f->content, b);
    return b;
}

static gboolean apply(Frame *f, gboolean ok, const OpErr *e, const char *toast) {
    if (!ok) { dlg_notice(e->title, e->msg); return FALSE; }
    f->closing = TRUE;
    gtk_widget_set_sensitive(f->save, FALSE);
    app_commit();
    frame_close(f);
    if (toast) app_toast("%s", toast);
    return TRUE;
}

/* lista de nomes (char*) → combo */
static GtkWidget *combo_of(const char *title, GPtrArray *names, const char *selected) {
    int sel = 0;
    for (guint i = 0; i < names->len; i++) if (selected && strcmp(names->pdata[i], selected) == 0) sel = (int)i;
    return row_combo(title, (const char *const *)names->pdata, (int)names->len, sel);
}

static void combo_set_items(GtkWidget *row, GPtrArray *names, const char *selected) {
    GtkStringList *sl = gtk_string_list_new(NULL);
    int sel = 0;
    for (guint i = 0; i < names->len; i++) {
        gtk_string_list_append(sl, names->pdata[i]);
        if (selected && strcmp(names->pdata[i], selected) == 0) sel = (int)i;
    }
    adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(sl));
    adw_combo_row_set_selected(ADW_COMBO_ROW(row), (guint)sel);
    g_object_unref(sl);
}

static const char *combo_name(GtkWidget *row) {
    GObject *it = adw_combo_row_get_selected_item(ADW_COMBO_ROW(row));
    return it ? gtk_string_object_get_string(GTK_STRING_OBJECT(it)) : "";
}

static GPtrArray *account_names(void) {
    GPtrArray *a = g_ptr_array_new();
    for (guint i = 0; i < APP->state->accounts->len; i++) g_ptr_array_add(a, ((Account *)APP->state->accounts->pdata[i])->name);
    return a;
}

static GPtrArray *card_names(void) {
    GPtrArray *a = g_ptr_array_new();
    for (guint i = 0; i < APP->state->cards->len; i++) g_ptr_array_add(a, ((Card *)APP->state->cards->pdata[i])->name);
    return a;
}

static int index_of_account(const char *id) {
    for (guint i = 0; i < APP->state->accounts->len; i++) if (strcmp(((Account *)APP->state->accounts->pdata[i])->id, id) == 0) return (int)i;
    return 0;
}

static int index_of_card(const char *id) {
    for (guint i = 0; i < APP->state->cards->len; i++) if (strcmp(((Card *)APP->state->cards->pdata[i])->id, id) == 0) return (int)i;
    return 0;
}

static const char *account_at(int i) {
    GPtrArray *a = APP->state->accounts;
    return ((Account *)a->pdata[i >= 0 && (guint)i < a->len ? i : 0])->id;
}

static const char *card_at(int i) {
    GPtrArray *a = APP->state->cards;
    if (!a->len) return "";
    return ((Card *)a->pdata[i >= 0 && (guint)i < a->len ? i : 0])->id;
}

/* categorias do tipo + a atual, se não estiver na lista */
static GPtrArray *cats_with(Kind k, const char *current) {
    GPtrArray *a = g_ptr_array_new();
    for (guint i = 0; i < APP->state->cats[k]->len; i++) g_ptr_array_add(a, APP->state->cats[k]->pdata[i]);
    if (current && *current && !app_has_category(APP->state, k, current)) g_ptr_array_add(a, (gpointer)current);
    return a;
}

/* ================================================================ lançamento */

typedef struct {
    Frame *f;
    char *id;
    gboolean payment;
    Kind kind;
    char *category; /* escolha atual (para manter ao trocar de tipo) */
    GtkWidget *seg_exp, *seg_inc;
    GtkWidget *desc, *value, *cat, *method, *card, *account, *date, *paid, *reps, *mode, *recurring;
    GtkWidget *hint, *hint_title, *hint_src, *hint_why_box;
    char *hint_cat;
    Categorizer *cz[2];
} TxEd;

static void tx_ed_free(TxEd *e) {
    g_free(e->id);
    g_free(e->category);
    g_free(e->hint_cat);
    categorizer_free(e->cz[0]);
    categorizer_free(e->cz[1]);
    g_free(e);
}

static gboolean tx_card_mode(TxEd *e) {
    return e->kind == KIND_EXPENSE && APP->state->cards->len && !e->payment && row_combo_get(e->method) == 1;
}

static void tx_update_visibility(TxEd *e) {
    gboolean can_card = e->kind == KIND_EXPENSE && APP->state->cards->len && !e->payment;
    gboolean card = tx_card_mode(e);
    gtk_widget_set_visible(e->method, can_card);
    gtk_widget_set_visible(e->card, card);
    gtk_widget_set_visible(e->account, !card);
    if (e->paid) {
        gtk_widget_set_visible(e->paid, !card && !e->payment);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(e->paid), e->kind == KIND_INCOME ? "Receita já recebida" : "Despesa já paga");
    }
    if (e->mode) gtk_widget_set_visible(e->mode, adw_spin_row_get_value(ADW_SPIN_ROW(e->reps)) > 1);
}

static void tx_hint_update(TxEd *e);

static void tx_set_kind(TxEd *e, Kind k) {
    if (k == e->kind) return;
    e->kind = k;
    const char *cur = combo_name(e->cat);
    g_autofree char *keep = g_strdup(app_has_category(APP->state, k, cur) ? cur : app_first_category(APP->state, k));
    g_autoptr(GPtrArray) names = cats_with(k, NULL);
    combo_set_items(e->cat, names, keep);
    tx_update_visibility(e);
    tx_hint_update(e);
}

static void on_seg(GtkToggleButton *b, TxEd *e) {
    if (!gtk_toggle_button_get_active(b)) return;
    tx_set_kind(e, GTK_WIDGET(b) == e->seg_inc ? KIND_INCOME : KIND_EXPENSE);
}

static void on_tx_changed(GObject *o, GParamSpec *p, TxEd *e) { (void)o; (void)p; tx_update_visibility(e); }

/* assistente: sugere a categoria pela descrição (lançamento novo, ou quando a descrição foi alterada) */
static void tx_hint_update(TxEd *e) {
    if (!e->hint) return;
    gboolean show = FALSE;
    const char *desc = row_text(e->desc);
    Tx *orig = e->id ? app_tx(APP->state, e->id) : NULL;
    if (APP->prefs->assist_category && !e->payment && (!orig || strcmp(orig->desc, desc) != 0)) {
        g_autofree char *t = g_strstrip(g_strdup(desc));
        if (g_utf8_strlen(t, -1) >= 2) {
            if (!e->cz[e->kind]) e->cz[e->kind] = categorizer_build(APP->state, e->kind, APP->dict);
            Suggestion *s = categorizer_suggest(e->cz[e->kind], t);
            if (s && strcmp(s->category, combo_name(e->cat)) != 0) {
                show = TRUE;
                g_free(e->hint_cat);
                e->hint_cat = g_strdup(s->category);
                g_autofree char *tt = g_strdup_printf("Sugestão: %s", s->category);
                gtk_label_set_text(GTK_LABEL(e->hint_title), tt);
                gtk_label_set_text(GTK_LABEL(e->hint_src), s->source == SOURCE_SAME_DESCRIPTION ? "pelo que você já lançou"
                                                           : s->source == SOURCE_LEARNED ? "aprendido com seus lançamentos" : "pelo dicionário");
                w_clear(e->hint_why_box);
                w_add(e->hint_why_box, w_why(s->why));
            }
            suggestion_free(s);
        }
    }
    gtk_revealer_set_reveal_child(GTK_REVEALER(e->hint), show);
}

static void on_desc(GtkEditable *ed, TxEd *e) { (void)ed; tx_hint_update(e); }

static void use_hint(GtkButton *b, TxEd *e) {
    (void)b;
    if (!e->hint_cat) return;
    g_autoptr(GPtrArray) names = cats_with(e->kind, NULL);
    combo_set_items(e->cat, names, e->hint_cat);
    tx_hint_update(e);
}

static void on_cat_changed(GObject *o, GParamSpec *p, TxEd *e) { (void)o; (void)p; tx_hint_update(e); }

static void tx_save(GtkButton *b, TxEd *e) {
    (void)b;
    if (e->f->closing) return;
    Day date;
    if (!row_date_get(e->date, &date)) date = DAY_NONE;
    gboolean card = tx_card_mode(e);
    TxDraft d = {
        .kind = e->kind,
        .desc = row_text(e->desc),
        .value = row_text(e->value),
        .category = combo_name(e->cat),
        .date = date,
        .paid = e->paid ? row_switch_get(e->paid) : TRUE,
        .account_id = account_at(row_combo_get(e->account)),
        .card_id = card ? card_at(row_combo_get(e->card)) : "",
        .reps = e->reps ? (int)adw_spin_row_get_value(ADW_SPIN_ROW(e->reps)) : 1,
        .reps_mode = e->mode && row_combo_get(e->mode) == 1 ? REPS_EACH : REPS_TOTAL,
        .recurring = e->recurring ? row_switch_get(e->recurring) : FALSE,
    };
    OpErr err;
    gboolean ok = ops_save_tx(APP->state, e->id, &d, &err);
    apply(e->f, ok, &err, e->id ? "Lançamento atualizado" : d.reps > 1 ? "Parcelas lançadas" : "Lançamento salvo");
}

typedef struct { char *id; Frame *f; } DelCtx;
static void del_ctx_free(gpointer p) { DelCtx *d = p; g_free(d->id); g_free(d); }
static void tx_delete_only(gpointer p) { DelCtx *d = p; ops_delete_tx(APP->state, d->id, FALSE); app_commit(); frame_close(d->f); app_toast("Lançamento excluído"); }
static void tx_delete_all(gpointer p) { DelCtx *d = p; ops_delete_tx(APP->state, d->id, TRUE); app_commit(); frame_close(d->f); app_toast("Parcelas excluídas"); }

static void tx_delete_confirmed(gpointer p) {
    DelCtx *d = p;
    int later = ops_later_parcels(APP->state, d->id);
    if (!later) { tx_delete_only(d); return; }
    g_autofree char *msg = g_strdup_printf("Excluir também as %d parcela(s) seguinte(s)?", later);
    DelCtx *n = g_new0(DelCtx, 1);
    n->id = g_strdup(d->id);
    n->f = d->f;
    dlg_choice("Parcelas", msg, "Só esta", "Excluir também", TRUE, tx_delete_only, tx_delete_all, n, del_ctx_free);
}

static void tx_delete(GtkButton *b, TxEd *e) {
    (void)b;
    DelCtx *d = g_new0(DelCtx, 1);
    d->id = g_strdup(e->id);
    d->f = e->f;
    dlg_confirm("Excluir lançamento", "Excluir este lançamento?", "Excluir", TRUE, tx_delete_confirmed, d, del_ctx_free);
}

void editor_tx(Kind kind, const char *id) {
    const AppState *s = APP->state;
    Tx *tx = id ? app_tx(s, id) : NULL;
    TxEd *e = g_new0(TxEd, 1);
    e->id = tx ? g_strdup(tx->id) : NULL;
    e->payment = tx && !tx_is_flow(tx);
    e->kind = tx ? tx->kind : kind;
    Frame *f = frame_new(tx ? "Editar lançamento" : "Novo lançamento", e->payment ? "" : "Registre uma receita ou despesa", "Salvar");
    e->f = f;
    g_object_set_data_full(G_OBJECT(f->dialog), "ed", e, (GDestroyNotify)tx_ed_free);

    if (e->payment) {
        w_add(f->content, w_label_wrap("Pagamento de fatura: debita a conta e abate da fatura do cartão. Não conta como despesa nova.", "fin-muted"));
    } else {
        GtkWidget *seg = w_hbox(0);
        gtk_widget_add_css_class(seg, "linked");
        gtk_widget_set_halign(seg, GTK_ALIGN_FILL);
        e->seg_exp = gtk_toggle_button_new_with_label("Despesa");
        e->seg_inc = gtk_toggle_button_new_with_label("Receita");
        gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(e->seg_inc), GTK_TOGGLE_BUTTON(e->seg_exp));
        gtk_widget_set_hexpand(e->seg_exp, TRUE);
        gtk_widget_set_hexpand(e->seg_inc, TRUE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(e->kind == KIND_INCOME ? e->seg_inc : e->seg_exp), TRUE);
        g_signal_connect(e->seg_exp, "toggled", G_CALLBACK(on_seg), e);
        g_signal_connect(e->seg_inc, "toggled", G_CALLBACK(on_seg), e);
        w_add(seg, e->seg_exp);
        w_add(seg, e->seg_inc);
        w_add(f->content, seg);
    }

    GtkWidget *g1 = group(f->content, NULL);
    e->desc = row_entry("Descrição", tx ? tx->desc : "", 200);
    gtk_widget_set_tooltip_text(e->desc, "Ex.: Mercado");
    gadd(g1, e->desc);

    /* sugestão de categoria do assistente */
    if (!e->payment) {
        e->hint = gtk_revealer_new();
        GtkWidget *hb = w_vbox(2);
        gtk_widget_add_css_class(hb, "fin-hint");
        GtkWidget *top = w_hbox(8);
        w_add(top, w_icon("auto-awesome", 16));
        GtkWidget *tt = w_vbox(0);
        gtk_widget_set_hexpand(tt, TRUE);
        e->hint_title = w_label("", "heading");
        e->hint_src = w_label("", "fin-muted caption");
        w_add(tt, e->hint_title);
        w_add(tt, e->hint_src);
        w_add(top, tt);
        GtkWidget *use = gtk_button_new_with_label("Usar");
        gtk_widget_add_css_class(use, "fin-pill");
        gtk_widget_add_css_class(use, "selected");
        gtk_widget_set_valign(use, GTK_ALIGN_CENTER);
        g_signal_connect(use, "clicked", G_CALLBACK(use_hint), e);
        w_add(top, use);
        w_add(hb, top);
        e->hint_why_box = w_vbox(0);
        w_add(hb, e->hint_why_box);
        gtk_revealer_set_child(GTK_REVEALER(e->hint), hb);
        w_add(f->content, e->hint);
    }

    GtkWidget *g2 = group(f->content, NULL);
    char vb[32];
    e->value = row_entry("Valor (R$)", tx ? money_input(tx->value, vb) : "", 20);
    gtk_widget_set_tooltip_text(e->value, "Ex.: 59,90 ou 1.500,00");
    gadd(g2, e->value);
    g_autoptr(GPtrArray) cats = cats_with(e->kind, tx ? tx->category : NULL);
    e->cat = combo_of("Categoria", cats, tx ? tx->category : app_first_category(s, e->kind));
    adw_combo_row_set_expression(ADW_COMBO_ROW(e->cat), gtk_property_expression_new(GTK_TYPE_STRING_OBJECT, NULL, "string"));
    adw_combo_row_set_enable_search(ADW_COMBO_ROW(e->cat), TRUE);
    gadd(g2, e->cat);
    const char *methods[2] = {"Conta / dinheiro", "Cartão de crédito"};
    e->method = row_combo("Forma de pagamento", methods, 2, tx && tx_is_card(tx) ? 1 : 0);
    gadd(g2, e->method);
    g_autoptr(GPtrArray) cn = card_names();
    e->card = row_combo("Cartão", (const char *const *)cn->pdata, (int)cn->len, tx && tx_is_card(tx) ? index_of_card(tx->card_id) : 0);
    gadd(g2, e->card);
    g_autoptr(GPtrArray) an = account_names();
    e->account = row_combo("Conta", (const char *const *)an->pdata, (int)an->len, tx ? index_of_account(tx->account_id) : 0);
    gadd(g2, e->account);
    e->date = row_date("Data", tx ? tx->date : APP->today, FALSE);
    gadd(g2, e->date);
    if (!e->payment) {
        e->paid = row_switch("Despesa já paga", NULL, tx ? tx->paid : TRUE);
        gadd(g2, e->paid);
    }
    if (!tx) {
        GtkWidget *g3 = group(f->content, "Repetição");
        e->reps = adw_spin_row_new_with_range(1, 60, 1);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(e->reps), "Parcelas");
        adw_action_row_set_subtitle(ADW_ACTION_ROW(e->reps), "1 = à vista");
        gadd(g3, e->reps);
        const char *modes[2] = {"O total da compra (divide entre as parcelas)", "O valor de cada parcela"};
        e->mode = row_combo("O valor informado é", modes, 2, 0);
        gadd(g3, e->mode);
        e->recurring = row_switch("Repetir mensalmente", "Cria uma recorrência a partir desta data", FALSE);
        gadd(g3, e->recurring);
        g_signal_connect(e->reps, "notify::value", G_CALLBACK(on_tx_changed), e);
    }
    g_signal_connect(e->method, "notify::selected", G_CALLBACK(on_tx_changed), e);
    g_signal_connect(e->desc, "changed", G_CALLBACK(on_desc), e);
    g_signal_connect(e->cat, "notify::selected", G_CALLBACK(on_cat_changed), e);
    g_signal_connect(f->save, "clicked", G_CALLBACK(tx_save), e);
    g_signal_connect(e->desc, "entry-activated", G_CALLBACK(tx_save), e);
    g_signal_connect(e->value, "entry-activated", G_CALLBACK(tx_save), e);
    if (tx) g_signal_connect(delete_button(f, "Excluir lançamento"), "clicked", G_CALLBACK(tx_delete), e);
    tx_update_visibility(e);
    tx_hint_update(e);
    frame_present(f, tx ? NULL : e->desc);
}

/* ================================================================ meta */

typedef struct { Frame *f; char *id; GtkWidget *name, *target, *move, *deadline, *monthly; } GoalEd;
static void goal_ed_free(GoalEd *e) { g_free(e->id); g_free(e); }

static void goal_save(GtkButton *b, GoalEd *e) {
    (void)b;
    if (e->f->closing) return;
    Day dl;
    if (!row_date_get(e->deadline, &dl)) { dlg_notice("Revise os dados", "Prazo inválido. Use DD/MM/AAAA ou deixe em branco."); return; }
    OpErr err;
    gboolean ok = ops_save_goal(APP->state, e->id, row_text(e->name), row_text(e->target), e->move ? row_text(e->move) : "", dl,
                                row_text(e->monthly), &err);
    apply(e->f, ok, &err, e->id ? "Meta atualizada" : "Meta criada");
}

static void goal_del(gpointer p) { DelCtx *d = p; ops_delete_goal(APP->state, d->id); app_commit(); frame_close(d->f); app_toast("Meta excluída"); }

static void goal_delete(GtkButton *b, GoalEd *e) {
    (void)b;
    Goal *g = app_goal(APP->state, e->id);
    if (!g) return;
    g_autofree char *msg = g_strdup_printf("Excluir a meta “%s”?", g->name);
    DelCtx *d = g_new0(DelCtx, 1);
    d->id = g_strdup(e->id);
    d->f = e->f;
    dlg_confirm("Excluir meta", msg, "Excluir", TRUE, goal_del, d, del_ctx_free);
}

void editor_goal(const char *id) {
    Goal *g = id ? app_goal(APP->state, id) : NULL;
    GoalEd *e = g_new0(GoalEd, 1);
    e->id = g ? g_strdup(g->id) : NULL;
    g_autofree char *saved = g ? money_fmt(g->saved) : NULL;
    g_autofree char *sub = g ? g_strdup_printf("Guardado até agora: %s", saved) : g_strdup("Dê um nome e um valor ao seu objetivo.");
    Frame *f = frame_new(g ? "Editar meta" : "Nova meta", sub, "Salvar");
    e->f = f;
    g_object_set_data_full(G_OBJECT(f->dialog), "ed", e, (GDestroyNotify)goal_ed_free);
    GtkWidget *gr = group(f->content, NULL);
    char b1[32], b2[32];
    e->name = row_entry("Nome", g ? g->name : "", 60);
    e->target = row_entry("Valor da meta (R$)", g ? money_input(g->target, b1) : "", 20);
    gadd(gr, e->name);
    gadd(gr, e->target);
    if (g) {
        e->move = row_entry("Guardar ou retirar agora (R$)", "", 20);
        gtk_widget_set_tooltip_text(e->move, "Ex.: 100 ou -50");
        gadd(gr, e->move);
    }
    e->deadline = row_date("Prazo (opcional)", g ? g->deadline : DAY_NONE, TRUE);
    e->monthly = row_entry("Contribuição mensal planejada (opcional)", g && g->monthly > 0 ? money_input(g->monthly, b2) : "", 20);
    gadd(gr, e->deadline);
    gadd(gr, e->monthly);
    g_signal_connect(f->save, "clicked", G_CALLBACK(goal_save), e);
    if (g) g_signal_connect(delete_button(f, "Excluir meta"), "clicked", G_CALLBACK(goal_delete), e);
    frame_present(f, g ? e->move : e->name);
}

/* ================================================================ conta */

typedef struct { Frame *f; char *id; GtkWidget *name, *initial; } AccEd;
static void acc_ed_free(AccEd *e) { g_free(e->id); g_free(e); }

static void acc_save(GtkButton *b, AccEd *e) {
    (void)b;
    if (e->f->closing) return;
    OpErr err;
    gboolean ok = ops_save_account(APP->state, e->id, row_text(e->name), row_text(e->initial), &err);
    apply(e->f, ok, &err, e->id ? "Conta atualizada" : "Conta criada");
}

static void acc_del(gpointer p) {
    DelCtx *d = p;
    OpErr err;
    if (!ops_delete_account(APP->state, d->id, FALSE, &err)) { dlg_notice(err.title, err.msg); return; }
    app_commit();
    frame_close(d->f);
    app_toast("Conta excluída");
}

static void acc_delete(GtkButton *b, AccEd *e) {
    (void)b;
    OpErr err;
    if (!ops_delete_account(APP->state, e->id, TRUE, &err)) { dlg_notice(err.title, err.msg); return; }
    Account *a = app_account(APP->state, e->id);
    g_autofree char *msg = g_strdup_printf("Excluir a conta “%s”?", a ? a->name : "");
    DelCtx *d = g_new0(DelCtx, 1);
    d->id = g_strdup(e->id);
    d->f = e->f;
    dlg_confirm("Excluir conta", msg, "Excluir", TRUE, acc_del, d, del_ctx_free);
}

void editor_account(const char *id) {
    Account *a = id ? app_account(APP->state, id) : NULL;
    AccEd *e = g_new0(AccEd, 1);
    e->id = a ? g_strdup(a->id) : NULL;
    Frame *f = frame_new(a ? "Editar conta" : "Nova conta", "O saldo inicial entra no saldo atual.", "Salvar");
    e->f = f;
    g_object_set_data_full(G_OBJECT(f->dialog), "ed", e, (GDestroyNotify)acc_ed_free);
    GtkWidget *g = group(f->content, NULL);
    char b1[32];
    e->name = row_entry("Nome", a ? a->name : "", 40);
    e->initial = row_entry("Saldo inicial (R$)", a ? money_input(a->initial, b1) : "0,00", 20);
    gadd(g, e->name);
    gadd(g, e->initial);
    g_signal_connect(f->save, "clicked", G_CALLBACK(acc_save), e);
    if (a && APP->state->accounts->len > 1) g_signal_connect(delete_button(f, "Excluir conta"), "clicked", G_CALLBACK(acc_delete), e);
    frame_present(f, e->name);
}

/* ================================================================ cartão */

typedef struct { Frame *f; char *id; GtkWidget *name, *limit, *close, *due; } CardEd;
static void card_ed_free(CardEd *e) { g_free(e->id); g_free(e); }

static void card_save(GtkButton *b, CardEd *e) {
    (void)b;
    if (e->f->closing) return;
    char c[8], d[8];
    g_snprintf(c, sizeof c, "%d", (int)adw_spin_row_get_value(ADW_SPIN_ROW(e->close)));
    g_snprintf(d, sizeof d, "%d", (int)adw_spin_row_get_value(ADW_SPIN_ROW(e->due)));
    OpErr err;
    gboolean ok = ops_save_card(APP->state, e->id, row_text(e->name), row_text(e->limit), c, d, &err);
    apply(e->f, ok, &err, e->id ? "Cartão atualizado" : "Cartão criado");
}

static void card_del(gpointer p) {
    DelCtx *d = p;
    OpErr err;
    if (!ops_delete_card(APP->state, d->id, FALSE, &err)) { dlg_notice(err.title, err.msg); return; }
    app_commit();
    frame_close(d->f);
    app_toast("Cartão excluído");
}

static void card_delete(GtkButton *b, CardEd *e) {
    (void)b;
    OpErr err;
    if (!ops_delete_card(APP->state, e->id, TRUE, &err)) { dlg_notice(err.title, err.msg); return; }
    Card *c = app_card(APP->state, e->id);
    g_autofree char *msg = g_strdup_printf("Excluir o cartão “%s”?", c ? c->name : "");
    DelCtx *d = g_new0(DelCtx, 1);
    d->id = g_strdup(e->id);
    d->f = e->f;
    dlg_confirm("Excluir cartão", msg, "Excluir", TRUE, card_del, d, del_ctx_free);
}

static GtkWidget *day_spin(const char *title, int v) {
    GtkWidget *r = adw_spin_row_new_with_range(1, 31, 1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
    adw_spin_row_set_value(ADW_SPIN_ROW(r), v);
    return r;
}

void editor_card(const char *id) {
    Card *c = id ? app_card(APP->state, id) : NULL;
    CardEd *e = g_new0(CardEd, 1);
    e->id = c ? g_strdup(c->id) : NULL;
    Frame *f = frame_new(c ? "Editar cartão" : "Novo cartão", "Compras feitas após o dia de fechamento entram na fatura seguinte.", "Salvar");
    e->f = f;
    g_object_set_data_full(G_OBJECT(f->dialog), "ed", e, (GDestroyNotify)card_ed_free);
    GtkWidget *g = group(f->content, NULL);
    char b1[32];
    e->name = row_entry("Nome", c ? c->name : "", 40);
    e->limit = row_entry("Limite (R$)", c ? money_input(c->limit, b1) : "", 20);
    e->close = day_spin("Fecha dia", c ? c->close : 5);
    e->due = day_spin("Vence dia", c ? c->due : 12);
    gadd(g, e->name);
    gadd(g, e->limit);
    gadd(g, e->close);
    gadd(g, e->due);
    g_signal_connect(f->save, "clicked", G_CALLBACK(card_save), e);
    if (c) g_signal_connect(delete_button(f, "Excluir cartão"), "clicked", G_CALLBACK(card_delete), e);
    frame_present(f, e->name);
}

/* ================================================================ pagar fatura */

typedef struct { Frame *f; char *card_id; GtkWidget *value, *account, *date; } PayEd;
static void pay_ed_free(PayEd *e) { g_free(e->card_id); g_free(e); }

static void pay_save(GtkButton *b, PayEd *e) {
    (void)b;
    if (e->f->closing) return;
    Day d;
    if (!row_date_get(e->date, &d)) d = DAY_NONE;
    OpErr err;
    gboolean ok = ops_pay_invoice(APP->state, e->card_id, row_text(e->value), account_at(row_combo_get(e->account)), d, &err);
    apply(e->f, ok, &err, "Pagamento registrado");
}

void editor_pay_invoice(const char *card_id) {
    Card *c = app_card(APP->state, card_id);
    CardStatus st = {0};
    const Invoice *cur = NULL;
    if (c) { card_status(APP->state, c, APP->today, &st); cur = card_status_current(&st); }
    if (!c || !cur) {
        if (c) card_status_clear(&st);
        dlg_notice("Fatura", "Não há fatura em aberto neste cartão.");
        return;
    }
    PayEd *e = g_new0(PayEd, 1);
    e->card_id = g_strdup(c->id);
    g_autofree char *ml = br_month_label(cur->ym);
    g_autofree char *open = money_fmt(invoice_open(cur));
    char d[11], vb[32];
    g_autofree char *title = g_strdup_printf("Pagar fatura · %s", c->name);
    g_autofree char *sub = g_strdup_printf("Fatura de %s · vence %s · em aberto %s", ml, day_br(cur->due, d), open);
    Frame *f = frame_new(title, sub, "Registrar pagamento");
    e->f = f;
    g_object_set_data_full(G_OBJECT(f->dialog), "ed", e, (GDestroyNotify)pay_ed_free);
    GtkWidget *g = group(f->content, NULL);
    e->value = row_entry("Valor pago (R$)", money_input(invoice_open(cur), vb), 20);
    g_autoptr(GPtrArray) an = account_names();
    e->account = row_combo("Pago com a conta", (const char *const *)an->pdata, (int)an->len, 0);
    e->date = row_date("Data do pagamento", APP->today, FALSE);
    gadd(g, e->value);
    gadd(g, e->account);
    gadd(g, e->date);
    card_status_clear(&st);
    g_signal_connect(f->save, "clicked", G_CALLBACK(pay_save), e);
    frame_present(f, e->value);
}

/* ================================================================ recorrência */

typedef struct { Frame *f; char *id; GtkWidget *desc, *value, *kind, *day, *cat, *account, *card, *active, *start; Kind k; } RecEd;
static void rec_ed_free(RecEd *e) { g_free(e->id); g_free(e); }

static void rec_kind_changed(GObject *o, GParamSpec *p, RecEd *e) {
    (void)o; (void)p;
    Kind k = row_combo_get(e->kind) == 1 ? KIND_INCOME : KIND_EXPENSE;
    if (k != e->k) {
        e->k = k;
        const char *cur = combo_name(e->cat);
        g_autofree char *keep = g_strdup(app_has_category(APP->state, k, cur) ? cur : app_first_category(APP->state, k));
        g_autoptr(GPtrArray) names = cats_with(k, NULL);
        combo_set_items(e->cat, names, keep);
    }
    gtk_widget_set_visible(e->card, k == KIND_EXPENSE && APP->state->cards->len);
}

static void rec_save(GtkButton *b, RecEd *e) {
    (void)b;
    if (e->f->closing) return;
    Day start = DAY_NONE;
    if (e->start && !row_date_get(e->start, &start)) start = DAY_NONE;
    char day[8];
    g_snprintf(day, sizeof day, "%d", (int)adw_spin_row_get_value(ADW_SPIN_ROW(e->day)));
    int ci = row_combo_get(e->card);
    const char *card = e->k == KIND_EXPENSE && ci > 0 ? card_at(ci - 1) : "";
    OpErr err;
    gboolean ok = ops_save_recurring(APP->state, e->id, e->k, row_text(e->desc), row_text(e->value), day, combo_name(e->cat),
                                     account_at(row_combo_get(e->account)), card, e->active ? row_switch_get(e->active) : TRUE, start,
                                     APP->today, &err);
    apply(e->f, ok, &err, e->id ? "Recorrência atualizada" : "Recorrência criada");
}

static void rec_del(gpointer p) { DelCtx *d = p; ops_delete_recurring(APP->state, d->id); app_commit(); frame_close(d->f); app_toast("Recorrência excluída"); }

static void rec_delete(GtkButton *b, RecEd *e) {
    (void)b;
    DelCtx *d = g_new0(DelCtx, 1);
    d->id = g_strdup(e->id);
    d->f = e->f;
    dlg_confirm("Excluir recorrência", "Excluir esta recorrência? Os lançamentos já criados serão mantidos.", "Excluir", TRUE, rec_del, d, del_ctx_free);
}

void editor_recurring(const char *id) {
    const AppState *s = APP->state;
    Recurring *r = id ? app_recurring(s, id) : NULL;
    RecEd *e = g_new0(RecEd, 1);
    e->id = r ? g_strdup(r->id) : NULL;
    e->k = r ? r->kind : KIND_EXPENSE;
    Frame *f = frame_new(r ? "Editar recorrência" : "Nova recorrência", "Cria um lançamento pendente por mês, a partir da data de início.", "Salvar");
    e->f = f;
    g_object_set_data_full(G_OBJECT(f->dialog), "ed", e, (GDestroyNotify)rec_ed_free);
    GtkWidget *g = group(f->content, NULL);
    char vb[32];
    e->desc = row_entry("Descrição", r ? r->desc : "", 120);
    e->value = row_entry("Valor (R$)", r ? money_input(r->value, vb) : "", 20);
    const char *kinds[2] = {"Despesa", "Receita"};
    e->kind = row_combo("Tipo", kinds, 2, e->k == KIND_INCOME ? 1 : 0);
    e->day = adw_spin_row_new_with_range(1, 31, 1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(e->day), "Dia do mês");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(e->day), "Em meses mais curtos, usa o último dia");
    adw_spin_row_set_value(ADW_SPIN_ROW(e->day), r ? r->day : 1);
    g_autoptr(GPtrArray) cats = cats_with(e->k, r ? r->category : NULL);
    e->cat = combo_of("Categoria", cats, r ? r->category : app_first_category(s, e->k));
    g_autoptr(GPtrArray) an = account_names();
    e->account = row_combo("Conta", (const char *const *)an->pdata, (int)an->len, r ? index_of_account(r->account_id) : 0);
    GPtrArray *cn = g_ptr_array_new();
    g_ptr_array_add(cn, "Nenhum (debita da conta)");
    for (guint i = 0; i < s->cards->len; i++) g_ptr_array_add(cn, ((Card *)s->cards->pdata[i])->name);
    e->card = row_combo("Cartão (opcional)", (const char *const *)cn->pdata, (int)cn->len, r && r->card_id[0] ? index_of_card(r->card_id) + 1 : 0);
    g_ptr_array_unref(cn);
    gadd(g, e->desc);
    gadd(g, e->value);
    gadd(g, e->kind);
    gadd(g, e->day);
    gadd(g, e->cat);
    gadd(g, e->account);
    gadd(g, e->card);
    if (r) {
        e->active = row_switch("Ativa", "Pausada não gera novos lançamentos", r->active);
        gadd(g, e->active);
    } else {
        e->start = row_date("Começa em", APP->today, FALSE);
        gadd(g, e->start);
    }
    g_signal_connect(e->kind, "notify::selected", G_CALLBACK(rec_kind_changed), e);
    rec_kind_changed(NULL, NULL, e);
    g_signal_connect(f->save, "clicked", G_CALLBACK(rec_save), e);
    if (r) g_signal_connect(delete_button(f, "Excluir recorrência"), "clicked", G_CALLBACK(rec_delete), e);
    frame_present(f, e->desc);
}

/* ================================================================ limite */

typedef struct { Frame *f; char *current; GtkWidget *cat, *value; } LimEd;
static void lim_ed_free(LimEd *e) { g_free(e->current); g_free(e); }

static void lim_save(GtkButton *b, LimEd *e) {
    (void)b;
    if (e->f->closing) return;
    OpErr err;
    gboolean ok = ops_save_limit(APP->state, e->current, combo_name(e->cat), row_text(e->value), &err);
    apply(e->f, ok, &err, "Limite salvo");
}

typedef struct { char *cat; Frame *f; } LimDel;
static void lim_del_free(gpointer p) { LimDel *d = p; g_free(d->cat); g_free(d); }
static void lim_del(gpointer p) { LimDel *d = p; ops_delete_limit(APP->state, d->cat); app_commit(); frame_close(d->f); app_toast("Limite excluído"); }

static void lim_delete(GtkButton *b, LimEd *e) {
    (void)b;
    g_autofree char *msg = g_strdup_printf("Excluir o limite de “%s”?", e->current);
    LimDel *d = g_new0(LimDel, 1);
    d->cat = g_strdup(e->current);
    d->f = e->f;
    dlg_confirm("Excluir limite", msg, "Excluir", TRUE, lim_del, d, lim_del_free);
}

void editor_limit(const char *category) {
    LimEd *e = g_new0(LimEd, 1);
    e->current = g_strdup(category);
    Frame *f = frame_new(category ? "Editar limite" : "Novo limite", "Valor máximo mensal da categoria. Despesas pendentes do mês também contam.", "Salvar");
    e->f = f;
    g_object_set_data_full(G_OBJECT(f->dialog), "ed", e, (GDestroyNotify)lim_ed_free);
    GtkWidget *g = group(f->content, NULL);
    g_autoptr(GPtrArray) cats = cats_with(KIND_EXPENSE, category);
    e->cat = combo_of("Categoria", cats, category ? category : app_first_category(APP->state, KIND_EXPENSE));
    Cents v = 0;
    char vb[32];
    gboolean has = category && app_limit(APP->state, category, &v);
    e->value = row_entry("Valor mensal (R$)", has ? money_input(v, vb) : "", 20);
    gadd(g, e->cat);
    gadd(g, e->value);
    g_signal_connect(f->save, "clicked", G_CALLBACK(lim_save), e);
    if (category) g_signal_connect(delete_button(f, "Excluir limite"), "clicked", G_CALLBACK(lim_delete), e);
    frame_present(f, e->value);
}
