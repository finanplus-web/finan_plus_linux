/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Ajustes. Janelas a partir de 720 px (1.4.0): lista das seções à esquerda, com ícones coloridos, e a
 * seção escolhida aberta à direita, como nas configurações do GNOME. Janelas estreitas: cartões que
 * abrem e fecham com o botão + / −.
 */
#include "pages.h"
#include "charts.h"
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

/* ---------------------------------------------------------------- seções */

/* cada seção: ícone, cor e grupo na lista da esquerda */
typedef struct { const char *key, *icon, *color, *group; } SectionInfo;
static const SectionInfo SECTIONS[] = {
    {"aparencia", "palette", "blue", "Personalização"},
    {"avisos", "notifications", "orange", "Personalização"},
    {"assistente", "auto-awesome", "purple", "Planejamento e automação"},
    {"recorrencias", "repeat", "teal", "Planejamento e automação"},
    {"privacidade", "shield", "green", "Segurança e dados"},
    {"dados", "database", "yellow", "Segurança e dados"},
    {"sobre", "info", "gray", "Aplicativo"},
};
#define N_SECTIONS G_N_ELEMENTS(SECTIONS)

/* modo lista + detalhe: título e resumo de cada seção, guardados enquanto as seções são montadas */
static gboolean split_mode;
static char *sec_title[N_SECTIONS], *sec_summary[N_SECTIONS];
static char *selected = NULL; /* chave da seção aberta (NULL = Aparência) */

static int section_index(const char *key) {
    for (guint i = 0; i < N_SECTIONS; i++) if (strcmp(SECTIONS[i].key, key) == 0) return (int)i;
    return 0;
}

/* Começa uma seção: em janelas estreitas, um cartão que abre e fecha; na lista + detalhe, o painel da
 * direita com o título e o resumo. [content] recebe a caixa onde a seção põe o que tem. */
static GtkWidget *section_begin(const char *key, const char *title, const char *summary, GtkWidget **content) {
    if (!split_mode) return w_collapsible(key, title, summary, content);
    int i = section_index(key);
    g_free(sec_title[i]);
    g_free(sec_summary[i]);
    sec_title[i] = g_strdup(title);
    sec_summary[i] = g_strdup(summary);
    GtkWidget *card = w_card("fin-flat");
    GtkWidget *head = w_hbox(14);
    w_add(head, w_icon_tile(SECTIONS[i].icon, SECTIONS[i].color, TRUE));
    GtkWidget *t = w_vbox(2);
    gtk_widget_set_valign(t, GTK_ALIGN_CENTER);
    GtkWidget *tl = w_label(title, "title-2");
    gtk_accessible_update_property(GTK_ACCESSIBLE(tl), GTK_ACCESSIBLE_PROPERTY_LEVEL, 2, -1);
    w_add(t, tl);
    w_add(t, w_label_wrap(summary, "fin-muted"));
    w_add(head, t);
    w_add(card, head);
    GtkWidget *body = w_vbox(12);
    gtk_widget_set_margin_top(body, 14);
    w_add(card, body);
    *content = body;
    return card;
}

/* ---------------------------------------------------------------- aparência */

static void set_theme(gpointer p) {
    APP->state->theme = (ThemeId)GPOINTER_TO_INT(p);
    theme_apply(APP->state->theme);
    g_autoptr(GError) e = NULL;
    if (!app_save(&e)) dlg_notice("Tema aplicado, mas não salvo", e ? e->message : "Erro desconhecido ao gravar os dados.");
    else app_toast("Tema: %s", theme_label(APP->state->theme));
    later_refresh();
}

static void set_density(GtkToggleButton *b, gpointer v) {
    if (!gtk_toggle_button_get_active(b) || APP->prefs->density == GPOINTER_TO_INT(v)) return;
    APP->prefs->density = GPOINTER_TO_INT(v);
    save_prefs();
    app_apply_density();
}

static GtkWidget *appearance(void) {
    GtkWidget *content;
    static const char *const DENS[] = {"Padrão", "Compacta", "Confortável"};
    g_autofree char *sum = g_strdup_printf("Tema: %s · densidade %s", theme_label(APP->state->theme), DENS[APP->prefs->density]);
    GtkWidget *card = section_begin("aparencia", "Aparência", sum, &content);
    w_add(content, w_label("Tema", "heading"));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    int cols = APP->layout == LAYOUT_NARROW ? 2 : APP->layout == LAYOUT_XWIDE ? 4 : 3;
    for (int t = 0; t < THEME_COUNT; t++) {
        gboolean on = APP->state->theme == (ThemeId)t;
        GtkWidget *b = w_vbox(8);
        w_add(b, theme_preview_new((ThemeId)t));
        GtkWidget *tl = w_hbox(6);
        w_add(tl, w_label(theme_label((ThemeId)t), "heading"));
        if (on) { GtkWidget *ck = w_icon("check", 16); gtk_widget_add_css_class(ck, "fin-accent"); w_add(tl, ck); } /* tema escolhido */
        w_add(b, tl);
        GtkWidget *btn = gtk_button_new();
        gtk_button_set_child(GTK_BUTTON(btn), b);
        gtk_widget_add_css_class(btn, "fin-theme-card");
        if (on) gtk_widget_add_css_class(btn, "selected");
        g_autofree char *tip = g_strdup_printf("Usar o tema %s", theme_label((ThemeId)t));
        gtk_widget_set_tooltip_text(btn, tip);
        gtk_accessible_update_state(GTK_ACCESSIBLE(btn), GTK_ACCESSIBLE_STATE_CHECKED, on ? GTK_ACCESSIBLE_TRISTATE_TRUE : GTK_ACCESSIBLE_TRISTATE_FALSE, -1);
        g_signal_connect_swapped(btn, "clicked", G_CALLBACK(set_theme), GINT_TO_POINTER(t));
        gtk_grid_attach(GTK_GRID(grid), btn, t % cols, t / cols, 1, 1);
    }
    w_add(content, grid);
    w_add(content, note("“Sistema” acompanha o modo claro/escuro da área de trabalho (claro ou OLED Cinza). Os temas são os mesmos "
                        "do app Android e da versão web; o tema vai junto no backup."));

    /* densidade: só neste computador */
    GtkWidget *row = w_hbox(16);
    gtk_widget_set_margin_top(row, 8);
    GtkWidget *dt = w_vbox(2);
    gtk_widget_set_hexpand(dt, TRUE);
    w_add(dt, w_label("Densidade", "heading"));
    w_add(dt, w_label_wrap("Compacta mostra mais lançamentos por tela; Confortável dá mais respiro. Vale só neste computador.", "fin-muted caption"));
    w_add(row, dt);
    GtkWidget *seg = w_hbox(0);
    gtk_widget_add_css_class(seg, "fin-segbar");
    gtk_widget_set_valign(seg, GTK_ALIGN_CENTER);
    gtk_accessible_update_property(GTK_ACCESSIBLE(seg), GTK_ACCESSIBLE_PROPERTY_LABEL, "Densidade", -1);
    static const int ORDER[] = {DENSITY_COMFY, DENSITY_DEFAULT, DENSITY_COMPACT};
    GtkWidget *first = NULL;
    for (int i = 0; i < 3; i++) {
        GtkWidget *tb = gtk_toggle_button_new_with_label(DENS[ORDER[i]]);
        if (first) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(tb), GTK_TOGGLE_BUTTON(first));
        else first = tb;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tb), APP->prefs->density == ORDER[i]);
        g_signal_connect(tb, "toggled", G_CALLBACK(set_density), GINT_TO_POINTER(ORDER[i]));
        w_add(seg, tb);
    }
    w_add(row, seg);
    w_add(content, row);
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
    /* "Remover PIN" segue o mesmo limite de tentativas da tela de bloqueio */
    int wait = pin_wait_seconds(APP->prefs);
    if (wait > 0) {
        g_autofree char *m = wait >= 120 ? g_strdup_printf("Muitas tentativas. Aguarde %d min.", (wait + 59) / 60)
                                         : g_strdup_printf("Muitas tentativas. Aguarde %d s.", wait);
        dlg_notice("Não foi possível remover", m);
        return;
    }
    pin_attempt(APP->prefs);
    if (!pin_verify(pin, APP->prefs->pin_hash)) { dlg_notice("Não foi possível remover", "PIN incorreto."); return; }
    pin_reset_fails(APP->prefs);
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
    APP->prefs->auto_lock = DEVICE_AUTOLOCK_OPTIONS[i];
    APP->prefs->auto_lock_set = TRUE;
    save_prefs();
    later_refresh();
}

static GtkWidget *privacy(void) {
    GtkWidget *content;
    gboolean pin = prefs_has_pin(APP->prefs);
    g_autofree char *sum = g_strdup_printf("%s%s", pin ? "Bloqueio por PIN ativo" : "Bloqueio desativado", APP->state->privacy ? " · valores ocultos" : "");
    GtkWidget *card = section_begin("privacidade", "Privacidade e segurança", sum, &content);
    GtkWidget *list = boxed();
    GtkWidget *r = manage_row(list, "Bloqueio do app", pin ? "Ativo: PIN de 4 a 8 números" : "Desativado. Defina um PIN para proteger o Finan+ neste computador.");
    suffix(r, w_pill(pin ? "Remover PIN" : "Definir PIN", pin_action, NULL, NULL));
    switch_row(list, "Ocultar valores", "Esconde os valores em reais na tela e nos avisos (Ctrl+H)", APP->state->privacy, set_privacy);
    int sel = 0;
    for (int i = 0; i < 6; i++) if (DEVICE_AUTOLOCK_OPTIONS[i] == APP->prefs->auto_lock) sel = i;
    GtkWidget *al = row_combo("Bloqueio automático", DEVICE_AUTOLOCK_LABELS, 6, sel);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(al), pin ? "Ao sair da janela (minimizar ou trocar de programa). Fica só neste computador: restaurar um backup não muda."
                                                        : "Precisa de um PIN");
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
    GtkWidget *card = section_begin("avisos", "Avisos de vencimento", on ? "Avisos de vencimento ligados" : "Avisos de vencimento desligados", &content);
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
    GtkWidget *card = section_begin("assistente", "Assistente", sum, &content);
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

/* ---------------------------------------------------------------- recorrências */

static void edit_rec(gpointer id) { editor_recurring(id); }
static void new_rec(gpointer u) { (void)u; editor_recurring(NULL); }

static GtkWidget *recurring(void) {
    const AppState *s = APP->state;
    GtkWidget *content;
    g_autofree char *sum = s->recurring->len ? g_strdup_printf("%u recorrência(s) cadastrada(s)", s->recurring->len)
                                             : g_strdup("Nenhuma recorrência cadastrada");
    GtkWidget *card = section_begin("recorrencias", "Recorrências", sum, &content);
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

/* ---------------------------------------------------------------- dados */

static void d_csv(gpointer u) { (void)u; data_export_csv(); }
static void d_backup(gpointer u) { (void)u; data_export_backup(); }
static void d_pdf(gpointer u) { (void)u; report_pdf_dialog(APP->filters.from, APP->filters.to); }
static void d_restore(gpointer u) { (void)u; data_restore_backup(); }
static void d_wipe(gpointer u) { (void)u; data_wipe(); }

static GtkWidget *data(void) {
    GtkWidget *content;
    GtkWidget *card = section_begin("dados", "Dados", "Backup, restauração, CSV e relatório em PDF", &content);
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
    GtkWidget *card = section_begin("sobre", "Sobre", sum, &content);
    static const char *const paras[] = {
        "Finan+ é um aplicativo para gerenciamento financeiro pessoal, desenvolvido com foco em simplicidade, privacidade, leveza e funcionamento offline.",
        "O aplicativo permite organizar receitas, despesas, contas, cartões, categorias, limites mensais, metas e lançamentos recorrentes, além de acompanhar saldos e relatórios financeiros.",
        "Esta versão é nativa para Linux, escrita em C com GTK 4 e libadwaita, com janela pensada para computador e notebook. Os dados ficam no computador, criptografados com XChaCha20-Poly1305 e chave no chaveiro do sistema, sem conta, cadastro ou servidor.",
        "Inclui avisos de vencimento, bloqueio por PIN, relatório em PDF e backup em JSON compatível com o Finan+ web e com o app Android.",
        "O assistente (sugestão de categoria, resumo do mês, dicas de economia e perguntas rápidas) funciona inteiro no computador, sem internet e sem modelo de IA externo: são regras e um classificador simples, com código aberto e explicação em cada resposta.",
        "A interface traz os temas do Finan+: Claro, Gruvbox, OLED Cinza, Gruvbox OLED, Tokyo Night e Nord, ou o do sistema.",
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

static gboolean idle_page(gpointer u) { (void)u; page_settings_refresh(); return G_SOURCE_REMOVE; }

static void pick_section(GtkButton *b, gpointer u) {
    (void)u;
    g_free(selected);
    selected = g_strdup(g_object_get_data(G_OBJECT(b), "key"));
    g_idle_add(idle_page, NULL); /* o botão clicado é recriado: redesenha depois do sinal */
}

/* lista das seções (esquerda): grupo, ícone colorido, título, resumo e a seta */
static GtkWidget *section_list(int sel) {
    GtkWidget *card = w_card("fin-flat");
    gtk_widget_add_css_class(card, "fin-tight");
    gtk_accessible_update_property(GTK_ACCESSIBLE(card), GTK_ACCESSIBLE_PROPERTY_LABEL, "Seções de Ajustes", -1);
    const char *last = NULL;
    for (guint i = 0; i < N_SECTIONS; i++) {
        if (!last || strcmp(last, SECTIONS[i].group) != 0) {
            GtkWidget *eb = w_eyebrow(SECTIONS[i].group);
            gtk_widget_add_css_class(eb, "fin-set-group");
            w_add(card, eb);
            last = SECTIONS[i].group;
        }
        GtkWidget *btn = gtk_button_new();
        gtk_widget_add_css_class(btn, "fin-set-item");
        if ((int)i == sel) gtk_widget_add_css_class(btn, "selected");
        GtkWidget *row = w_hbox(12);
        w_add(row, w_icon_tile(SECTIONS[i].icon, SECTIONS[i].color, FALSE));
        GtkWidget *t = w_vbox(1);
        gtk_widget_set_hexpand(t, TRUE);
        gtk_widget_set_valign(t, GTK_ALIGN_CENTER);
        w_add(t, w_label(sec_title[i] ? sec_title[i] : SECTIONS[i].key, "heading"));
        w_add(t, w_label(sec_summary[i] ? sec_summary[i] : "", "fin-muted caption"));
        w_add(row, t);
        GtkWidget *ch = w_icon("chevron-right", 18);
        gtk_widget_add_css_class(ch, "fin-chevron");
        gtk_widget_add_css_class(ch, "fin-muted");
        w_add(row, ch);
        gtk_button_set_child(GTK_BUTTON(btn), row);
        g_object_set_data(G_OBJECT(btn), "key", (gpointer)SECTIONS[i].key);
        gtk_accessible_update_state(GTK_ACCESSIBLE(btn), GTK_ACCESSIBLE_STATE_SELECTED, (int)i == sel, -1);
        g_signal_connect(btn, "clicked", G_CALLBACK(pick_section), NULL);
        w_add(card, btn);
    }
    return card;
}

void page_settings_refresh(void) {
    if (!body || !APP->state) return;
    w_clear(body);
    GtkWidget *head = w_vbox(2);
    w_add(head, w_eyebrow("Configurações"));
    w_add(head, w_label("Ajustes", "fin-page-title"));
    w_add(head, w_label_wrap("Tudo fica salvo e criptografado neste computador.", "fin-muted"));
    w_add(body, head);
    split_mode = APP->layout != LAYOUT_NARROW;
    /* na ordem de SECTIONS */
    GtkWidget *cards[N_SECTIONS] = {appearance(), notifications(), assistant(), recurring(), privacy(), data(), about()};
    if (split_mode) {
        int sel = section_index(selected ? selected : "aparencia");
        GtkWidget *split = w_hbox(18);
        GtkWidget *list = section_list(sel);
        gtk_widget_set_size_request(list, APP->layout == LAYOUT_MEDIUM ? 300 : 380, -1);
        gtk_widget_set_valign(list, GTK_ALIGN_START);
        gtk_widget_set_hexpand(list, FALSE); /* largura fixa: o resto fica com a seção aberta */
        w_add(split, list);
        GtkWidget *detail = cards[sel];
        gtk_widget_set_hexpand(detail, TRUE);
        gtk_widget_set_valign(detail, GTK_ALIGN_START);
        w_add(split, detail);
        for (guint i = 0; i < N_SECTIONS; i++) if ((int)i != sel) { g_object_ref_sink(cards[i]); g_object_unref(cards[i]); }
        w_add(body, split);
        return;
    }
    /* janelas estreitas: cartões que abrem e fecham, por grupo (como no Android 1.6.0) */
    const char *last = NULL;
    for (guint i = 0; i < N_SECTIONS; i++) {
        if (!last || strcmp(last, SECTIONS[i].group) != 0) {
            GtkWidget *h = w_label(SECTIONS[i].group, "fin-muted heading");
            gtk_widget_set_margin_top(h, 6);
            w_add(body, h);
            last = SECTIONS[i].group;
        }
        w_add(body, cards[i]);
    }
}

GtkWidget *page_settings_new(void) {
    /* FINAN_PLUS_START_SECTION=dados (etc.) abre direto numa seção (só para capturas de tela da documentação) */
    const char *ss = g_getenv("FINAN_PLUS_START_SECTION");
    if (ss && *ss) selected = g_strdup(ss);
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 2400);
    adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp), 2000);
    body = w_vbox(14);
    gtk_widget_set_margin_start(body, 24);
    gtk_widget_set_margin_end(body, 24);
    gtk_widget_set_margin_top(body, 20);
    gtk_widget_set_margin_bottom(body, 32);
    adw_clamp_set_child(ADW_CLAMP(clamp), body);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), clamp);
    return sw;
}
