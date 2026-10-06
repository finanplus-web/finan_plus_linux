/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Ajustes: todos os cartões abrem e fecham com o botão + / −. Em telas largas ficam em duas colunas.
 */
#include "pages.h"
#include "theme.h"
#include "widgets.h"
#include "core/finance.h"
#include <string.h>

static GtkWidget *body;

/* ---------------------------------------------------------------- utilidades */

static GtkWidget *boxed(void) {
    GtkWidget *l = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(l), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(l, "boxed-list");
    return l;
}

static GtkWidget *manage_row(GtkWidget *list, const char *title, const char *subtitle) {
    GtkWidget *r = adw_action_row_new();
    g_autofree char *t = g_markup_escape_text(title, -1);
    g_autofree char *s = g_markup_escape_text(subtitle, -1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), t);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(r), s);
    gtk_list_box_append(GTK_LIST_BOX(list), r);
    return r;
}

static GtkWidget *suffix(GtkWidget *row, GtkWidget *btn) {
    gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), btn);
    return btn;
}

static GtkWidget *note(const char *text) { return w_label_wrap(text, "fin-muted caption"); }

typedef void (*BoolSetter)(gboolean v);
static void on_switch(GObject *o, GParamSpec *p, gpointer fn) { (void)p; ((BoolSetter)fn)(adw_switch_row_get_active(ADW_SWITCH_ROW(o))); }

static GtkWidget *switch_row(GtkWidget *list, const char *title, const char *subtitle, gboolean active, BoolSetter fn) {
    GtkWidget *r = row_switch(title, subtitle, active);
    g_signal_connect(r, "notify::active", G_CALLBACK(on_switch), (gpointer)fn);
    gtk_list_box_append(GTK_LIST_BOX(list), r);
    return r;
}

static void save_prefs(void) {
    g_autoptr(GError) e = NULL;
    if (!prefs_save(APP->prefs, &e)) dlg_notice("Não foi possível salvar", e->message);
}

static gboolean idle_refresh(gpointer u) { (void)u; app_refresh(); return G_SOURCE_REMOVE; }
/* redesenha depois que o sinal atual terminar (evita destruir o widget que disparou o sinal) */
static void later_refresh(void) { g_idle_add(idle_refresh, NULL); }

/* ---------------------------------------------------------------- aparência */

static void set_theme(gpointer p) {
    APP->state->theme = (ThemeId)GPOINTER_TO_INT(p);
    theme_apply(APP->state->theme);
    g_autoptr(GError) e = NULL;
    if (!app_save(&e)) dlg_notice("Tema aplicado, mas não salvo", e ? e->message : "Erro desconhecido ao gravar os dados.");
    else app_toast("Tema: %s", theme_label(APP->state->theme));
    later_refresh();
}

/* bolinha de cor do tema, com contorno para não sumir quando a cor é parecida com o fundo do cartão */
static void draw_swatch(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer p) {
    (void)a;
    guint32 rgb = GPOINTER_TO_UINT(p);
    double r = MIN(w, h) / 2.0 - 1;
    cairo_arc(cr, w / 2.0, h / 2.0, r, 0, 2 * G_PI);
    cairo_set_source_rgb(cr, ((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0, (rgb & 0xFF) / 255.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.5, 0.5, 0.55, 0.6);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
}

static GtkWidget *appearance(void) {
    GtkWidget *content;
    g_autofree char *sum = g_strdup_printf("Tema: %s", theme_label(APP->state->theme));
    GtkWidget *card = w_collapsible("aparencia", "Aparência", sum, &content);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 9);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 9);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    int cols = APP->layout == LAYOUT_NARROW ? 2 : 3;
    for (int t = 0; t < THEME_COUNT; t++) {
        gboolean on = APP->state->theme == (ThemeId)t;
        GtkWidget *b = w_vbox(7);
        GtkWidget *tl = w_hbox(6);
        w_add(tl, w_label(theme_label((ThemeId)t), "heading"));
        if (on) w_add(tl, w_icon("check", 16)); /* tema escolhido */
        w_add(b, tl);
        GtkWidget *sw = w_hbox(4);
        guint32 a, c;
        theme_swatch((ThemeId)t, &a, &c);
        for (int k = 0; k < 2; k++) {
            GtkWidget *dot = gtk_drawing_area_new();
            gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(dot), 16);
            gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(dot), 16);
            gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(dot), draw_swatch, GUINT_TO_POINTER((k ? c : a) & 0xFFFFFF), NULL);
            w_add(sw, dot);
        }
        w_add(b, sw);
        GtkWidget *btn = gtk_button_new();
        gtk_button_set_child(GTK_BUTTON(btn), b);
        gtk_widget_add_css_class(btn, "fin-theme-btn");
        if (on) gtk_widget_add_css_class(btn, "selected");
        gtk_accessible_update_state(GTK_ACCESSIBLE(btn), GTK_ACCESSIBLE_STATE_CHECKED, on ? GTK_ACCESSIBLE_TRISTATE_TRUE : GTK_ACCESSIBLE_TRISTATE_FALSE, -1);
        g_signal_connect_swapped(btn, "clicked", G_CALLBACK(set_theme), GINT_TO_POINTER(t));
        gtk_grid_attach(GTK_GRID(grid), btn, t % cols, t / cols, 1, 1);
    }
    w_add(content, grid);
    w_add(content, note("“Sistema” acompanha o modo claro/escuro da área de trabalho. “Material You” usa o azul do Material Design "
                        "(no Linux não há cores do papel de parede)."));
    return card;
}

/* ---------------------------------------------------------------- privacidade e segurança */

static void pin_confirm(const char *p2, gpointer p1) {
    if (strcmp(p1, p2) != 0) { dlg_notice("PIN não definido", "Os PINs não conferem."); return; }
    g_autofree char *h = pin_hash(p1);
    if (!h) { dlg_notice("PIN não definido", "Não foi possível calcular o hash do PIN."); return; }
    g_free(APP->prefs->pin_hash);
    APP->prefs->pin_hash = g_steal_pointer(&h);
    save_prefs();
    later_refresh();
    dlg_notice("PIN ativado", "O PIN será pedido ao abrir o Finan+ e quando você bloquear (Ctrl+L) ou o bloqueio automático agir.");
}

static void pin_first(const char *p1, gpointer u) {
    (void)u;
    if (!pin_valid_format(p1)) { dlg_notice("PIN inválido", "Use de 4 a 8 números."); return; }
    dlg_input("Confirmar PIN", "Digite o PIN de novo.", "Repita o PIN", NULL, TRUE, pin_confirm, g_strdup(p1), g_free);
}

static void pin_remove(const char *pin, gpointer u) {
    (void)u;
    if (!pin_verify(pin, APP->prefs->pin_hash)) { dlg_notice("Não foi possível remover", "PIN incorreto."); return; }
    g_free(APP->prefs->pin_hash);
    APP->prefs->pin_hash = g_strdup("");
    save_prefs();
    later_refresh();
    app_toast("PIN removido");
}

static void pin_action(gpointer u) {
    (void)u;
    if (prefs_has_pin(APP->prefs)) dlg_input("Remover PIN", "Digite o PIN atual para confirmar.", "PIN atual", NULL, TRUE, pin_remove, NULL, NULL);
    else dlg_input("Definir PIN", "Use de 4 a 8 números.", "Novo PIN", NULL, TRUE, pin_first, NULL, NULL);
}

static void set_privacy(gboolean v) { APP->state->privacy = v; app_save(NULL); later_refresh(); }

static void on_autolock(GObject *o, GParamSpec *p, gpointer u) {
    (void)p; (void)u;
    int i = row_combo_get(GTK_WIDGET(o));
    if (i < 0) return;
    APP->state->auto_lock = AUTOLOCK_OPTIONS[i];
    app_save(NULL);
    later_refresh();
}

static GtkWidget *privacy(void) {
    GtkWidget *content;
    gboolean pin = prefs_has_pin(APP->prefs);
    g_autofree char *sum = g_strdup_printf("%s%s", pin ? "Bloqueio por PIN ativo" : "Bloqueio desativado", APP->state->privacy ? " · valores ocultos" : "");
    GtkWidget *card = w_collapsible("privacidade", "Privacidade e segurança", sum, &content);
    GtkWidget *list = boxed();
    GtkWidget *r = manage_row(list, "Bloqueio do app", pin ? "Ativo: PIN de 4 a 8 números" : "Desativado. Defina um PIN para proteger o Finan+ neste computador.");
    suffix(r, w_pill(pin ? "Remover PIN" : "Definir PIN", pin_action, NULL, NULL));
    switch_row(list, "Ocultar valores", "Esconde os valores em reais na tela e nos avisos (Ctrl+H)", APP->state->privacy, set_privacy);
    const char *labels[5] = {"Desativado", "1 minuto sem usar", "5 minutos sem usar", "15 minutos sem usar", "30 minutos sem usar"};
    int sel = 0;
    for (int i = 0; i < 5; i++) if (AUTOLOCK_OPTIONS[i] == APP->state->auto_lock) sel = i;
    GtkWidget *al = row_combo("Bloqueio automático", labels, 5, sel);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(al), pin ? "Conta o tempo com a janela minimizada ou sem foco" : "Precisa de um PIN");
    gtk_widget_set_sensitive(al, pin);
    g_signal_connect(al, "notify::selected", G_CALLBACK(on_autolock), NULL);
    gtk_list_box_append(GTK_LIST_BOX(list), al);
    w_add(content, list);
    g_autofree char *where = g_strdup_printf(
        "Os dados ficam criptografados (XChaCha20-Poly1305, libsodium) em %s. %s O PIN é guardado só como hash Argon2id e nunca vai para o backup.",
        store_data_path(APP->store),
        store_key_source(APP->store) == KEY_SOURCE_KEYRING
            ? "A chave fica no chaveiro do sistema (GNOME Keyring/KWallet)."
            : "Não há chaveiro do sistema disponível: a chave fica em um arquivo legível só pelo seu usuário, na mesma pasta.");
    GtkWidget *n = note(where);
    gtk_label_set_selectable(GTK_LABEL(n), TRUE);
    w_add(content, n);
    w_add(content, note("No Linux não há como impedir capturas de tela por aplicativo; use “Ocultar valores” ao compartilhar a tela."));
    return card;
}

/* ---------------------------------------------------------------- avisos */

static void set_notify(gboolean v) { APP->prefs->notifications = v; save_prefs(); later_refresh(); }

static void set_login(gboolean v) {
    g_autoptr(GError) e = NULL;
    if (!notify_autostart_set(v, &e)) { dlg_notice("Não foi possível alterar", e->message); later_refresh(); return; }
    APP->prefs->notify_on_login = v;
    save_prefs();
    later_refresh();
}

static void test_notify(gpointer u) { (void)u; notify_check(TRUE); }

static GtkWidget *notifications(void) {
    GtkWidget *content;
    gboolean on = APP->prefs->notifications;
    GtkWidget *card = w_collapsible("avisos", "Avisos de vencimento", on ? "Avisos de vencimento ligados" : "Avisos de vencimento desligados", &content);
    GtkWidget *list = boxed();
    switch_row(list, "Avisar vencimentos", "Contas a pagar, valores a receber e faturas, uma vez por dia a partir das 9h", on, set_notify);
    GtkWidget *lr = switch_row(list, "Avisar também ao entrar na sessão", "Verifica os vencimentos ao ligar o computador, mesmo com o Finan+ fechado",
                               notify_autostart_enabled(), set_login);
    gtk_widget_set_sensitive(lr, on);
    GtkWidget *t = manage_row(list, "Testar", "Mostra agora o aviso com os vencimentos dos próximos 2 dias");
    suffix(t, w_pill("Avisar agora", test_notify, NULL, NULL));
    w_add(content, list);
    w_add(content, note("Os avisos usam as notificações da área de trabalho. Com “Ocultar valores” ligado, mostram só os nomes."));
    return card;
}

/* ---------------------------------------------------------------- assistente */

static void set_a_cat(gboolean v) { APP->prefs->assist_category = v; save_prefs(); later_refresh(); }
static void set_a_tips(gboolean v) { APP->prefs->assist_tips = v; save_prefs(); later_refresh(); }
static void set_a_ask(gboolean v) { APP->prefs->assist_ask = v; save_prefs(); later_refresh(); }
static void restore_tips(gpointer u) {
    (void)u;
    g_ptr_array_set_size(APP->prefs->dismissed_tips, 0);
    save_prefs();
    later_refresh();
}

static void learned_toggle(GtkButton *b, GtkRevealer *r) {
    gboolean open = !gtk_revealer_get_reveal_child(r);
    gtk_revealer_set_reveal_child(r, open);
    gtk_button_set_label(b, open ? "Ocultar o que o assistente aprendeu" : "Ver o que o assistente aprendeu");
}

static GtkWidget *assistant(void) {
    Prefs *pr = APP->prefs;
    GtkWidget *content;
    g_autofree char *sum = g_strdup_printf("%d de 3 funções ligadas", pr->assist_category + pr->assist_tips + pr->assist_ask);
    GtkWidget *card = w_collapsible("assistente", "Assistente", sum, &content);
    w_add(content, note("Funciona só neste computador, sem internet e sem enviar dados. Cada função pode ser desligada."));
    GtkWidget *list = boxed();
    switch_row(list, "Sugerir categoria", "Ao digitar a descrição de um lançamento novo", pr->assist_category, set_a_cat);
    switch_row(list, "Resumo e dicas", "No Início: resumo do mês, gastos fora do padrão, fixos, duplicados", pr->assist_tips, set_a_tips);
    switch_row(list, "Perguntas rápidas", "Ex.: “quanto gastei com mercado em agosto?”", pr->assist_ask, set_a_ask);
    w_add(content, list);
    if (pr->dismissed_tips->len) {
        g_autofree char *l = g_strdup_printf("Restaurar %u dica(s) dispensada(s)", pr->dismissed_tips->len);
        GtkWidget *p = w_pill(l, restore_tips, NULL, NULL);
        gtk_widget_set_halign(p, GTK_ALIGN_START);
        w_add(content, p);
    }
    GtkWidget *rev = gtk_revealer_new();
    GtkWidget *tb = gtk_button_new_with_label("Ver o que o assistente aprendeu");
    gtk_widget_add_css_class(tb, "flat");
    gtk_widget_add_css_class(tb, "fin-accent");
    gtk_widget_set_halign(tb, GTK_ALIGN_START);
    g_signal_connect(tb, "clicked", G_CALLBACK(learned_toggle), rev);
    w_add(content, tb);
    GtkWidget *lb = w_vbox(8);
    w_add(lb, note("O aprendizado vem dos seus próprios lançamentos (que ficam criptografados no computador). Não existe uma cópia separada: "
                   "corrigir a categoria de um lançamento corrige o aprendizado, e apagar o lançamento apaga o que ele ensinou."));
    for (int k = 1; k >= 0; k--) {
        Categorizer *c = categorizer_build(APP->state, (Kind)k, APP->dict);
        g_autofree char *eb = g_strdup_printf("%s · %d lançamento(s) analisado(s)", k == KIND_EXPENSE ? "Despesas" : "Receitas", categorizer_training_size(c));
        w_add(lb, w_eyebrow(eb));
        g_autoptr(GPtrArray) words = categorizer_learned_words(c, 6);
        if (!words->len) w_add(lb, note("Ainda não há palavras repetidas o suficiente."));
        GtkWidget *wl = boxed();
        for (guint i = 0; i < words->len; i++) {
            LearnedCategory *l = words->pdata[i];
            GString *s = g_string_new(NULL);
            for (guint j = 0; j < l->words->len; j++) {
                LearnedWord *w = &g_array_index(l->words, LearnedWord, j);
                g_string_append_printf(s, "%s%s (%d)", j ? ", " : "", w->word, w->count);
            }
            manage_row(wl, l->category, s->str);
            g_string_free(s, TRUE);
        }
        if (words->len) w_add(lb, wl);
        categorizer_free(c);
    }
    g_autofree char *dn = g_strdup_printf("Dicionário inicial: %u seções, arquivo aberto /usr/share/finan-plus/dicionario.txt. "
                                          "As regras de cada função estão descritas em ASSISTENTE.md no código-fonte.", APP->dict->sections->len);
    w_add(lb, note(dn));
    gtk_revealer_set_child(GTK_REVEALER(rev), lb);
    w_add(content, rev);
    return card;
}

/* ---------------------------------------------------------------- contas e cartões */

static void edit_account(gpointer id) { editor_account(id); }
static void edit_card(gpointer id) { editor_card(id); }
static void new_account(gpointer u) { (void)u; editor_account(NULL); }
static void new_card(gpointer u) { (void)u; editor_card(NULL); }

static GtkWidget *accounts(void) {
    const AppState *s = APP->state;
    GtkWidget *content;
    g_autofree char *sum = g_strdup_printf("%u conta(s) · %u cartão(ões)", s->accounts->len, s->cards->len);
    GtkWidget *card = w_collapsible("contas", "Contas e cartões", sum, &content);
    GtkWidget *list = boxed();
    for (guint i = 0; i < s->accounts->len; i++) {
        Account *a = s->accounts->pdata[i];
        g_autofree char *m = app_money(account_balance(s, a));
        g_autofree char *sub = g_strdup_printf("Saldo %s", m);
        GtkWidget *r = manage_row(list, a->name, sub);
        adw_action_row_add_prefix(ADW_ACTION_ROW(r), w_icon("account-balance-wallet", 20));
        suffix(r, w_pill("Editar", edit_account, g_strdup(a->id), g_free));
    }
    for (guint i = 0; i < s->cards->len; i++) {
        Card *c = s->cards->pdata[i];
        CardStatus st;
        card_status(s, c, APP->today, &st);
        g_autofree char *l = app_money(c->limit), *u = app_money(st.used);
        g_autofree char *sub = g_strdup_printf("Limite %s · usado %s · fecha dia %d · vence dia %d", l, u, c->close, c->due);
        card_status_clear(&st);
        g_autofree char *t = g_strdup_printf("Cartão %s", c->name);
        GtkWidget *r = manage_row(list, t, sub);
        adw_action_row_add_prefix(ADW_ACTION_ROW(r), w_icon("credit-card", 20));
        suffix(r, w_pill("Editar", edit_card, g_strdup(c->id), g_free));
    }
    w_add(content, list);
    GtkWidget *row = w_hbox(8);
    gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
    w_add(row, w_pill("＋ Conta", new_account, NULL, NULL));
    w_add(row, w_pill("＋ Cartão", new_card, NULL, NULL));
    w_add(content, row);
    return card;
}

/* ---------------------------------------------------------------- recorrências */

static void edit_rec(gpointer id) { editor_recurring(id); }
static void new_rec(gpointer u) { (void)u; editor_recurring(NULL); }

static GtkWidget *recurring(void) {
    const AppState *s = APP->state;
    GtkWidget *content;
    g_autofree char *sum = s->recurring->len ? g_strdup_printf("%u recorrência(s) cadastrada(s)", s->recurring->len)
                                             : g_strdup("Nenhuma recorrência cadastrada");
    GtkWidget *card = w_collapsible("recorrencias", "Recorrências", sum, &content);
    GtkWidget *nb = w_pill("＋ Nova", new_rec, NULL, NULL);
    gtk_widget_set_halign(nb, GTK_ALIGN_END);
    w_add(content, nb);
    if (!s->recurring->len) w_add(content, note("Você também pode marcar “Repetir mensalmente” ao criar um lançamento."));
    GtkWidget *list = boxed();
    for (guint i = 0; i < s->recurring->len; i++) {
        Recurring *r = s->recurring->pdata[i];
        g_autofree char *where = NULL;
        if (r->card_id[0]) { Card *c = app_card(s, r->card_id); where = g_strdup_printf("Cartão %s", c ? c->name : ""); }
        else { Account *a = app_account(s, r->account_id); where = g_strdup(a ? a->name : ""); }
        g_autofree char *v = app_money(r->value);
        g_autofree char *sub = g_strdup_printf("%s · %s · dia %d · %s · %s%s", r->kind == KIND_INCOME ? "Receita" : "Despesa", v, r->day,
                                               r->category, where, r->active ? "" : " · pausada");
        GtkWidget *row = manage_row(list, r->desc, sub);
        adw_action_row_add_prefix(ADW_ACTION_ROW(row), w_icon("repeat", 20));
        suffix(row, w_pill("Editar", edit_rec, g_strdup(r->id), g_free));
    }
    if (s->recurring->len) w_add(content, list);
    return card;
}

/* ---------------------------------------------------------------- limites */

static void edit_limit(gpointer cat) { editor_limit(cat); }
static void new_limit(gpointer u) { (void)u; editor_limit(NULL); }

static GtkWidget *limits(void) {
    const AppState *s = APP->state;
    GtkWidget *content;
    g_autofree char *sum = s->limits->len ? g_strdup_printf("%u limite(s) definido(s)", s->limits->len) : g_strdup("Nenhum limite definido");
    GtkWidget *card = w_collapsible("limites", "Limites mensais", sum, &content);
    GtkWidget *nb = w_pill("＋ Adicionar", new_limit, NULL, NULL);
    gtk_widget_set_halign(nb, GTK_ALIGN_END);
    w_add(content, nb);
    GtkWidget *list = boxed();
    for (guint i = 0; i < s->limits->len; i++) {
        Limit *l = s->limits->pdata[i];
        g_autofree char *v = app_money(l->value);
        g_autofree char *sub = g_strdup_printf("%s por mês", v);
        GtkWidget *row = manage_row(list, l->category, sub);
        suffix(row, w_pill("Editar", edit_limit, g_strdup(l->category), g_free));
    }
    if (s->limits->len) w_add(content, list);
    return card;
}

/* ---------------------------------------------------------------- categorias */

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
    g_autofree char *msg = used > 0 ? g_strdup_printf("Excluir “%s” da lista de categorias? %d lançamento(s) antigo(s) continuará(ão) com essa categoria no histórico.", c->name, used)
                                    : g_strdup_printf("Excluir a categoria “%s”?", c->name);
    dlg_confirm("Excluir categoria", msg, "Excluir", TRUE, do_delete_cat, cat_ref(c->kind, c->name), cat_ref_free);
}

static GtkWidget *categories(void) {
    const AppState *s = APP->state;
    GtkWidget *content;
    g_autofree char *sum = g_strdup_printf("%u categorias cadastradas", s->cats[0]->len + s->cats[1]->len);
    GtkWidget *card = w_collapsible("categorias", "Categorias", sum, &content);
    GtkWidget *row = w_hbox(8);
    new_cat_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(new_cat_entry), "Nova categoria");
    gtk_entry_set_max_length(GTK_ENTRY(new_cat_entry), 40);
    gtk_widget_set_hexpand(new_cat_entry, TRUE);
    static const char *const kinds[] = {"Despesa", "Receita", NULL};
    new_cat_kind = gtk_drop_down_new_from_strings(kinds);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(new_cat_kind), new_cat_kind_sel);
    GtkWidget *add = w_button("Adicionar", "add", "suggested-action", add_category, NULL, NULL);
    g_signal_connect_swapped(new_cat_entry, "activate", G_CALLBACK(add_category), NULL);
    w_add(row, new_cat_entry);
    w_add(row, new_cat_kind);
    w_add(row, add);
    w_add(content, row);
    for (int k = 1; k >= 0; k--) {
        GtkWidget *eb = w_eyebrow(k == KIND_EXPENSE ? "Despesas" : "Receitas");
        gtk_widget_set_margin_top(eb, 8);
        w_add(content, eb);
        GtkWidget *list = boxed();
        for (guint i = 0; i < s->cats[k]->len; i++) {
            const char *c = s->cats[k]->pdata[i];
            GtkWidget *r = manage_row(list, c, k == KIND_EXPENSE ? "Despesa" : "Receita");
            adw_action_row_add_prefix(ADW_ACTION_ROW(r), w_category_glyph(c));
            suffix(r, w_pill("Renomear", rename_cat, cat_ref((Kind)k, c), cat_ref_free));
            GtkWidget *del = w_button("Excluir", NULL, "fin-pill danger", delete_cat, cat_ref((Kind)k, c), cat_ref_free);
            suffix(r, del);
        }
        w_add(content, list);
    }
    return card;
}

/* ---------------------------------------------------------------- dados */

static void d_csv(gpointer u) { (void)u; data_export_csv(); }
static void d_backup(gpointer u) { (void)u; data_export_backup(); }
static void d_pdf(gpointer u) { (void)u; report_pdf_dialog(APP->filters.from, APP->filters.to); }
static void d_restore(gpointer u) { (void)u; data_restore_backup(); }
static void d_wipe(gpointer u) { (void)u; data_wipe(); }

static GtkWidget *data(void) {
    GtkWidget *content;
    GtkWidget *card = w_collapsible("dados", "Dados", "Backup, restauração, CSV e relatório em PDF", &content);
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g), 8);
    gtk_grid_set_row_spacing(GTK_GRID(g), 8);
    gtk_grid_set_column_homogeneous(GTK_GRID(g), TRUE);
    gtk_grid_attach(GTK_GRID(g), w_button("Exportar CSV", "table-view", "fin-pill", d_csv, NULL, NULL), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), w_button("Backup JSON", "download", "fin-pill", d_backup, NULL, NULL), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), w_button("Relatório em PDF", "picture-as-pdf", "fin-pill", d_pdf, NULL, NULL), 0, 1, 2, 1);
    gtk_grid_attach(GTK_GRID(g), w_button("Restaurar", "upload", "fin-pill", d_restore, NULL, NULL), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(g), w_button("Apagar tudo", "delete", "fin-pill danger", d_wipe, NULL, NULL), 1, 2, 1, 1);
    w_add(content, g);
    w_add(content, note("O backup JSON é compatível com o Finan+ web e com o app Android: dá para levar os dados de um para o outro. "
                        "O backup não é criptografado: guarde em local seguro."));
    return card;
}

/* ---------------------------------------------------------------- sobre */

static void license_toggle(GtkButton *b, GtkRevealer *r) {
    gboolean open = !gtk_revealer_get_reveal_child(r);
    gtk_revealer_set_reveal_child(r, open);
    const char *kind = g_object_get_data(G_OBJECT(b), "kind");
    g_autofree char *l = g_strdup_printf(open ? "Ocultar %s" : "Ver %s", kind);
    gtk_button_set_label(b, l);
}

static GtkWidget *license_block(const char *resource, const char *what) {
    GtkWidget *box = w_vbox(6);
    GtkWidget *r = gtk_revealer_new();
    g_autofree char *l = g_strdup_printf("Ver %s", what);
    GtkWidget *b = gtk_button_new_with_label(l);
    gtk_widget_add_css_class(b, "fin-pill");
    gtk_widget_set_halign(b, GTK_ALIGN_START);
    g_object_set_data_full(G_OBJECT(b), "kind", g_strdup(what), g_free);
    g_signal_connect(b, "clicked", G_CALLBACK(license_toggle), r);
    g_autoptr(GBytes) bytes = g_resources_lookup_data(resource, 0, NULL);
    gsize n = 0;
    const char *d = bytes ? g_bytes_get_data(bytes, &n) : NULL;
    g_autofree char *text = d ? g_strndup(d, n) : g_strdup("Texto da licença: https://www.gnu.org/licenses/gpl-3.0.html");
    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)), text, -1);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(tv), 10);
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), tv);
    gtk_widget_set_size_request(sw, -1, 320);
    gtk_widget_add_css_class(sw, "card");
    gtk_revealer_set_child(GTK_REVEALER(r), sw);
    w_add(box, b);
    w_add(box, r);
    return box;
}

static void about_dialog(gpointer u) { (void)u; show_about(); }

static GtkWidget *about(void) {
    GtkWidget *content;
    g_autofree char *sum = g_strdup_printf("Conheça o Finan+ · versão %s", APP_VERSION);
    GtkWidget *card = w_collapsible("sobre", "Sobre", sum, &content);
    static const char *const paras[] = {
        "Finan+ é um aplicativo para gerenciamento financeiro pessoal, desenvolvido com foco em simplicidade, privacidade, leveza e funcionamento offline.",
        "O aplicativo permite organizar receitas, despesas, contas, cartões, categorias, limites mensais, metas e lançamentos recorrentes, além de acompanhar saldos e relatórios financeiros.",
        "Esta versão é nativa para Linux, escrita em C com GTK 4 e libadwaita, com janela pensada para computador e notebook. Os dados ficam no computador, criptografados com XChaCha20-Poly1305 e chave no chaveiro do sistema, sem conta, cadastro ou servidor.",
        "Inclui avisos de vencimento, bloqueio por PIN, relatório em PDF e backup em JSON compatível com o Finan+ web e com o app Android.",
        "O assistente (sugestão de categoria, resumo do mês, dicas de economia e perguntas rápidas) funciona inteiro no computador, sem internet e sem modelo de IA externo: são regras e um classificador simples, com código aberto e explicação em cada resposta.",
        "A interface traz os temas do Finan+: Claro, Material You, OLED, Tokyo Night e Nord.",
    };
    for (guint i = 0; i < G_N_ELEMENTS(paras); i++) w_add(content, w_label_wrap(paras[i], "fin-muted"));
    w_add(content, w_label_wrap("Privacidade em primeiro lugar: seus dados financeiros permanecem no seu dispositivo.", "heading"));
    w_add(content, w_title("Desenvolvimento"));
    w_add(content, w_label_wrap("Finan+ é um projeto independente desenvolvido de forma colaborativa com auxílio de inteligência artificial. "
                                "A concepção, as decisões de produto, os testes e o direcionamento da experiência são realizados por Juscelino Be, "
                                "autor e idealizador do projeto, enquanto a inteligência artificial auxilia na implementação, revisão e evolução do código.",
                                "fin-muted"));
    GtkWidget *who = w_vbox(2);
    gtk_widget_add_css_class(who, "fin-soft");
    w_add(who, w_eyebrow("Idealizado e desenvolvido por"));
    w_add(who, w_label("Juscelino Be", "title-2"));
    w_add(content, who);
    w_add(content, w_title("Licença"));
    w_add(content, w_label_wrap("Finan+ — Copyright (C) 2026 Juscelino Be.\n\n"
                                "Este programa é software livre: você pode redistribuí-lo e/ou modificá-lo sob os termos da Licença Pública Geral GNU (GNU GPL), "
                                "publicada pela Free Software Foundation, na versão 3 da licença ou (a seu critério) qualquer versão posterior.\n\n"
                                "Este programa é distribuído na esperança de que seja útil, mas SEM NENHUMA GARANTIA, nem mesmo a garantia implícita de "
                                "COMERCIABILIDADE ou de ADEQUAÇÃO A UMA FINALIDADE ESPECÍFICA. Veja a licença completa para mais detalhes.", "fin-muted"));
    w_add(content, license_block("/com/finanplus/FinanPlus/LICENSE", "licença completa (GNU GPL v3)"));
    w_add(content, w_label_wrap("Ícones: Material Symbols, © Google, sob a Licença Apache 2.0 (compatível com a GPL v3).", "fin-muted"));
    w_add(content, license_block("/com/finanplus/FinanPlus/APACHE-2.0.txt", "licença dos ícones (Apache 2.0)"));
    GtkWidget *ab = w_pill("Sobre o Finan+ e bibliotecas", about_dialog, NULL, NULL);
    gtk_widget_set_halign(ab, GTK_ALIGN_START);
    w_add(content, ab);
    return card;
}

/* ---------------------------------------------------------------- montagem */

void page_settings_refresh(void) {
    if (!body || !APP->state) return;
    w_clear(body);
    GtkWidget *head = w_vbox(2);
    w_add(head, w_eyebrow("Configurações"));
    w_add(head, w_label("Ajustes", "fin-page-title"));
    w_add(head, w_label_wrap("Tudo fica salvo e criptografado neste computador.", "fin-muted"));
    w_add(body, head);
    GtkWidget *cards[] = {appearance(), privacy(), notifications(), assistant(), accounts(), recurring(), limits(), categories(), data(), about()};
    if (APP->layout == LAYOUT_NARROW) {
        for (guint i = 0; i < G_N_ELEMENTS(cards); i++) w_add(body, cards[i]);
        return;
    }
    /* duas colunas: preferências à esquerda, cadastros e dados à direita */
    GtkWidget *cols = w_hbox(18);
    gtk_box_set_homogeneous(GTK_BOX(cols), TRUE);
    GtkWidget *a = w_vbox(14), *b = w_vbox(14);
    int left[] = {0, 1, 2, 3, 9}, right[] = {4, 5, 6, 7, 8};
    for (int i = 0; i < 5; i++) { w_add(a, cards[left[i]]); w_add(b, cards[right[i]]); }
    w_add(cols, a);
    w_add(cols, b);
    w_add(body, cols);
}

GtkWidget *page_settings_new(void) {
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
