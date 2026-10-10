/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "widgets.h"
#include "app.h"
#include "core/assist.h"
#include <string.h>

/* ------------------------------------------------------------------ layout */

GtkWidget *w_box(GtkOrientation o, int spacing) { return gtk_box_new(o, spacing); }

void w_clear(GtkWidget *box) {
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(box))) {
        if (GTK_IS_BOX(box)) gtk_box_remove(GTK_BOX(box), c);
        else gtk_widget_unparent(c);
    }
}

void w_add(GtkWidget *box, GtkWidget *child) {
    if (child) gtk_box_append(GTK_BOX(box), child);
}

void w_classes(GtkWidget *w, const char *classes) {
    if (!classes || !*classes) return;
    g_auto(GStrv) parts = g_strsplit(classes, " ", -1);
    for (char **p = parts; *p; p++) if (**p) gtk_widget_add_css_class(w, *p);
}

GtkWidget *w_card(const char *extra) {
    GtkWidget *b = w_vbox(8);
    gtk_widget_add_css_class(b, "fin-card");
    w_classes(b, extra);
    return b;
}

GtkWidget *w_spacer(void) {
    GtkWidget *s = w_hbox(0);
    gtk_widget_set_hexpand(s, TRUE);
    return s;
}

/* ------------------------------------------------------------------ texto */

GtkWidget *w_label(const char *text, const char *classes) {
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    w_classes(l, classes);
    return l;
}

GtkWidget *w_label_wrap(const char *text, const char *classes) {
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(l), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_natural_wrap_mode(GTK_LABEL(l), GTK_NATURAL_WRAP_WORD);
    gtk_label_set_selectable(GTK_LABEL(l), FALSE);
    w_classes(l, classes);
    return l;
}

GtkWidget *w_eyebrow(const char *text) {
    g_autofree char *up = g_utf8_strup(text, -1);
    return w_label_wrap(up, "fin-eyebrow");
}

GtkWidget *w_title(const char *text) {
    GtkWidget *l = w_label(text, "fin-title");
    gtk_accessible_update_property(GTK_ACCESSIBLE(l), GTK_ACCESSIBLE_PROPERTY_LEVEL, 2, -1);
    return l;
}

GtkWidget *w_section_head(const char *eyebrow, const char *title) {
    GtkWidget *b = w_vbox(2);
    if (eyebrow) w_add(b, w_eyebrow(eyebrow));
    w_add(b, w_title(title));
    return b;
}

GtkWidget *w_money(Cents c, const char *classes) {
    g_autofree char *s = app_money(c);
    GtkWidget *l = w_label(s, "fin-money");
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_NONE);
    w_classes(l, classes);
    return l;
}

GtkWidget *w_badge(const char *text, const char *variant) {
    GtkWidget *l = gtk_label_new(text);
    gtk_widget_add_css_class(l, "fin-badge");
    w_classes(l, variant);
    gtk_widget_set_valign(l, GTK_ALIGN_CENTER);
    return l;
}

GtkWidget *w_level(double fraction, const char *variant) {
    GtkWidget *b = gtk_level_bar_new_for_interval(0, 1);
    gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(b), GTK_LEVEL_BAR_OFFSET_LOW);
    gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(b), GTK_LEVEL_BAR_OFFSET_HIGH);
    gtk_level_bar_remove_offset_value(GTK_LEVEL_BAR(b), GTK_LEVEL_BAR_OFFSET_FULL);
    gtk_level_bar_set_value(GTK_LEVEL_BAR(b), app_hidden() ? 0 : CLAMP(fraction, 0, 1));
    gtk_widget_add_css_class(b, "fin-level");
    w_classes(b, variant);
    gtk_widget_set_hexpand(b, TRUE);
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    return b;
}

/* ------------------------------------------------------------------ ícones */

char *icon_name(const char *name) { return g_strdup_printf("fin-%s-symbolic", name); }

/* rótulo com ícone Material à esquerda (no lugar de setas de texto como ↑ ↓) */
GtkWidget *w_icon_label(const char *icon, const char *text, const char *classes) {
    GtkWidget *b = w_hbox(4);
    GtkWidget *i = w_icon(icon, 14);
    if (classes) for (char **c = g_strsplit(classes, " ", -1), **k = c; *k || (g_strfreev(c), FALSE); k++) if (**k) gtk_widget_add_css_class(i, *k);
    w_add(b, i);
    w_add(b, w_label(text, classes));
    return b;
}

GtkWidget *w_icon(const char *name, int px) {
    g_autofree char *n = icon_name(name);
    GtkWidget *i = gtk_image_new_from_icon_name(n);
    gtk_image_set_pixel_size(GTK_IMAGE(i), px);
    gtk_accessible_update_state(GTK_ACCESSIBLE(i), GTK_ACCESSIBLE_STATE_HIDDEN, TRUE, -1);
    return i;
}

typedef struct { const char *icon; const char *names; } CatIcon;
/* Reconhece os nomes padrão e variações comuns, sem diferenciar acento e maiúscula (igual ao app Android). */
static const CatIcon CAT_ICONS[] = {
    {"shopping-cart", "|alimentacao|mercado|supermercado|comida|feira|"},
    {"restaurant", "|restaurante|delivery|lanche|lanches|"},
    {"directions-car", "|transporte|carro|uber|mobilidade|"},
    {"local-gas-station", "|combustivel|gasolina|posto|"},
    {"home", "|moradia|casa|aluguel|contas|"},
    {"favorite", "|saude|farmacia|medico|"},
    {"fitness-center", "|academia|esporte|esportes|"},
    {"theaters", "|lazer|diversao|entretenimento|viagem|"},
    {"subscriptions", "|assinaturas|assinatura|streaming|"},
    {"school", "|educacao|estudos|escola|faculdade|cursos|"},
    {"more-horiz", "|outros|outro|diversos|"},
    {"payments", "|salario|pagamento|pro labore|"},
    {"attach-money", "|extra|extras|renda extra|freela|bonus|"},
    {"savings", "|investimentos|investimento|poupanca|rendimentos|reserva|"},
    {"pets", "|pets|pet|animais|"},
    {"checkroom", "|vestuario|roupas|roupa|"},
    {"shopping-bag", "|compras|shopping|"},
    {"spa", "|beleza|cuidados pessoais|estetica|"},
    {"receipt-long", "|impostos|taxas|tarifas|impostos e taxas|"},
    {"redeem", "|presentes|doacoes|presente|doacao|"},
    {"account-balance", "|emprestimo|emprestimos|financiamento|banco|dividas|"},
    {"work", "|trabalho|adiantamento quinzenal|adiantamento|comissao|"},
};

const char *category_icon(const char *category) {
    if (strcmp(category, CARD_PAYMENT_CAT) == 0) return "credit-card";
    g_autofree char *f = text_fold(category);
    g_autofree char *key = g_strdup_printf("|%s|", f);
    for (guint i = 0; i < G_N_ELEMENTS(CAT_ICONS); i++)
        if (strstr(CAT_ICONS[i].names, key)) return CAT_ICONS[i].icon;
    return NULL;
}

GtkWidget *w_category_glyph(const char *category) {
    GtkWidget *box = w_hbox(0);
    gtk_widget_add_css_class(box, "fin-glyph");
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(box, GTK_ALIGN_START);
    gtk_widget_set_size_request(box, 42, 42);
    const char *ic = category_icon(category);
    GtkWidget *inner;
    if (ic) inner = w_icon(ic, 20);
    else {
        /* sem ícone próprio: a inicial do nome (ex.: "E" de Empréstimo) */
        g_autofree char *t = g_strstrip(g_strdup(category));
        gunichar c = *t ? g_unichar_toupper(g_utf8_get_char(t)) : 0x2022;
        char buf[8] = {0};
        g_unichar_to_utf8(c, buf);
        inner = gtk_label_new(buf);
    }
    gtk_widget_set_hexpand(inner, TRUE);
    gtk_widget_set_halign(inner, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), inner);
    gtk_widget_set_hexpand(box, FALSE); /* não deixa o ícone esticar a caixa */
    return box;
    return box;
}

/* ------------------------------------------------------------------ botões */

typedef struct { FinFn fn; gpointer data; GDestroyNotify destroy; } Cb;

static void cb_free(gpointer p, GClosure *c) {
    (void)c;
    Cb *cb = p;
    if (cb->destroy && cb->data) cb->destroy(cb->data);
    g_free(cb);
}

static void on_clicked(GtkButton *b, Cb *cb) { (void)b; if (cb->fn) cb->fn(cb->data); }

GtkWidget *w_button(const char *label, const char *icon, const char *classes, FinFn fn, gpointer data, GDestroyNotify destroy) {
    GtkWidget *b = gtk_button_new();
    if (icon && label) {
        GtkWidget *c = adw_button_content_new();
        g_autofree char *n = icon_name(icon);
        adw_button_content_set_icon_name(ADW_BUTTON_CONTENT(c), n);
        adw_button_content_set_label(ADW_BUTTON_CONTENT(c), label);
        adw_button_content_set_use_underline(ADW_BUTTON_CONTENT(c), TRUE);
        gtk_button_set_child(GTK_BUTTON(b), c);
    } else if (icon) {
        g_autofree char *n = icon_name(icon);
        gtk_button_set_icon_name(GTK_BUTTON(b), n);
    } else gtk_button_set_label(GTK_BUTTON(b), label);
    w_classes(b, classes);
    Cb *cb = g_new0(Cb, 1);
    cb->fn = fn; cb->data = data; cb->destroy = destroy;
    g_signal_connect_data(b, "clicked", G_CALLBACK(on_clicked), cb, cb_free, 0);
    return b;
}

GtkWidget *w_more_link(const char *label, FinFn fn, gpointer data, GDestroyNotify destroy) {
    GtkWidget *b = w_button(label, NULL, "flat fin-link", fn, data, destroy);
    GtkWidget *row = w_hbox(2);
    w_add(row, gtk_label_new(label));
    w_add(row, w_icon("chevron-right", 18));
    gtk_button_set_child(GTK_BUTTON(b), row);
    gtk_widget_set_halign(b, GTK_ALIGN_START);
    return b;
}

GtkWidget *w_pill(const char *label, FinFn fn, gpointer data, GDestroyNotify destroy) {
    return w_button(label, NULL, "fin-pill", fn, data, destroy);
}

static void why_toggle(GtkButton *b, GtkRevealer *r) {
    gboolean open = !gtk_revealer_get_reveal_child(r);
    gtk_revealer_set_reveal_child(r, open);
    gtk_button_set_label(b, open ? "Ocultar explicação" : "Por quê?");
}

GtkWidget *w_why(const char *why) {
    GtkWidget *box = w_vbox(4);
    GtkWidget *b = gtk_button_new_with_label("Por quê?");
    gtk_widget_add_css_class(b, "flat");
    gtk_widget_add_css_class(b, "fin-accent");
    gtk_widget_add_css_class(b, "fin-link");
    gtk_widget_set_halign(b, GTK_ALIGN_START);
    GtkWidget *r = gtk_revealer_new();
    GtkWidget *t = w_label_wrap(why, "fin-why fin-muted caption");
    gtk_revealer_set_child(GTK_REVEALER(r), t);
    g_signal_connect(b, "clicked", G_CALLBACK(why_toggle), r);
    w_add(box, b);
    w_add(box, r);
    return box;
}

/* estado aberto/fechado dos cartões de Ajustes durante a sessão */
static GHashTable *open_cards(void) {
    static GHashTable *t = NULL;
    if (!t) t = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    return t;
}

static void collapsible_toggle(GtkButton *b, gpointer user) {
    (void)b;
    GtkWidget *card = user;
    GtkRevealer *r = g_object_get_data(G_OBJECT(card), "revealer");
    GtkButton *btn = g_object_get_data(G_OBJECT(card), "button");
    const char *key = g_object_get_data(G_OBJECT(card), "key");
    gboolean open = !gtk_revealer_get_reveal_child(r);
    gtk_revealer_set_reveal_child(r, open);
    gtk_button_set_label(btn, open ? "−" : "+");
    gtk_accessible_update_state(GTK_ACCESSIBLE(btn), GTK_ACCESSIBLE_STATE_EXPANDED, open, -1);
    if (open) g_hash_table_add(open_cards(), g_strdup(key)); else g_hash_table_remove(open_cards(), key);
}

static void collapsible_click(GtkGestureClick *g, int n, double x, double y, gpointer card) {
    (void)g; (void)n; (void)x; (void)y;
    collapsible_toggle(NULL, card);
}

GtkWidget *w_collapsible(const char *key, const char *title, const char *summary, GtkWidget **content) {
    GtkWidget *card = w_card("fin-flat");
    gtk_widget_set_margin_bottom(card, 4);
    GtkWidget *head = w_hbox(12);
    GtkWidget *texts = w_vbox(2);
    gtk_widget_set_hexpand(texts, TRUE);
    w_add(texts, w_title(title));
    w_add(texts, w_label_wrap(summary, "fin-muted caption"));
    gboolean open = g_hash_table_contains(open_cards(), key);
    GtkWidget *btn = gtk_button_new_with_label(open ? "−" : "+");
    gtk_widget_add_css_class(btn, "fin-round");
    gtk_widget_add_css_class(btn, "title-3");
    gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
    g_autofree char *tip = g_strdup_printf("Abrir ou fechar %s", title);
    gtk_widget_set_tooltip_text(btn, tip);
    gtk_accessible_update_state(GTK_ACCESSIBLE(btn), GTK_ACCESSIBLE_STATE_EXPANDED, open, -1);
    w_add(head, texts);
    w_add(head, btn);
    /* clicar em qualquer parte do cabeçalho também abre/fecha */
    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "released", G_CALLBACK(collapsible_click), card);
    gtk_widget_add_controller(texts, GTK_EVENT_CONTROLLER(click));
    gtk_widget_set_cursor_from_name(texts, "pointer");
    GtkWidget *r = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(r), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    GtkWidget *body = w_vbox(10);
    gtk_widget_set_margin_top(body, 12);
    gtk_revealer_set_child(GTK_REVEALER(r), body);
    gtk_revealer_set_reveal_child(GTK_REVEALER(r), open);
    w_add(card, head);
    w_add(card, r);
    g_object_set_data(G_OBJECT(card), "revealer", r);
    g_object_set_data(G_OBJECT(card), "button", btn);
    g_object_set_data_full(G_OBJECT(card), "key", g_strdup(key), g_free);
    g_signal_connect(btn, "clicked", G_CALLBACK(collapsible_toggle), card);
    *content = body;
    return card;
}

/* ------------------------------------------------------------------ diálogos */

typedef struct {
    FinFn on_ok, on_alt;
    FinTextFn on_text;
    gpointer data;
    GDestroyNotify destroy;
    GtkWidget *entry;
} DlgCtx;

static void dlg_ctx_free(DlgCtx *c) {
    if (c->destroy && c->data) c->destroy(c->data);
    g_free(c);
}

static void on_response(AdwAlertDialog *d, const char *response, DlgCtx *c) {
    (void)d;
    if (g_strcmp0(response, "ok") == 0) {
        if (c->on_text) c->on_text(gtk_editable_get_text(GTK_EDITABLE(c->entry)), c->data);
        else if (c->on_ok) c->on_ok(c->data);
    } else if (g_strcmp0(response, "alt") == 0 && c->on_alt) c->on_alt(c->data);
}

static void present(AdwDialog *d) { adw_dialog_present(d, APP && APP->window ? GTK_WIDGET(APP->window) : NULL); }

void dlg_notice(const char *title, const char *msg) {
    AdwDialog *d = adw_alert_dialog_new(title, msg);
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(d), "ok", "OK");
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(d), "ok");
    present(d);
}

static AdwDialog *alert(const char *title, const char *msg, DlgCtx *c) {
    AdwDialog *d = adw_alert_dialog_new(title, msg);
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(d), "cancel");
    g_signal_connect_data(d, "response", G_CALLBACK(on_response), c, (GClosureNotify)(void (*)(void))dlg_ctx_free, 0);
    return d;
}

void dlg_confirm(const char *title, const char *msg, const char *ok, gboolean danger, FinFn on_ok, gpointer data, GDestroyNotify destroy) {
    DlgCtx *c = g_new0(DlgCtx, 1);
    c->on_ok = on_ok; c->data = data; c->destroy = destroy;
    AdwDialog *d = alert(title, msg, c);
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(d), "cancel", "Cancelar", "ok", ok, NULL);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(d), "ok", danger ? ADW_RESPONSE_DESTRUCTIVE : ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(d), danger ? "cancel" : "ok");
    present(d);
}

void dlg_choice(const char *title, const char *msg, const char *alt, const char *ok, gboolean danger,
                FinFn on_alt, FinFn on_ok, gpointer data, GDestroyNotify destroy) {
    DlgCtx *c = g_new0(DlgCtx, 1);
    c->on_ok = on_ok; c->on_alt = on_alt; c->data = data; c->destroy = destroy;
    AdwDialog *d = alert(title, msg, c);
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(d), "cancel", "Cancelar", "alt", alt, "ok", ok, NULL);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(d), "ok", danger ? ADW_RESPONSE_DESTRUCTIVE : ADW_RESPONSE_SUGGESTED);
    present(d);
}

void dlg_input(const char *title, const char *msg, const char *placeholder, const char *initial, gboolean password,
               FinTextFn on_text, gpointer data, GDestroyNotify destroy) {
    DlgCtx *c = g_new0(DlgCtx, 1);
    c->on_text = on_text; c->data = data; c->destroy = destroy;
    AdwDialog *d = alert(title, msg, c);
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(d), "cancel", "Cancelar", "ok", "OK", NULL);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(d), "ok", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(d), "ok");
    GtkWidget *e = password ? gtk_password_entry_new() : gtk_entry_new();
    if (password) {
        gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(e), TRUE);
        g_object_set(e, "placeholder-text", placeholder, NULL);
        gtk_editable_set_max_width_chars(GTK_EDITABLE(e), 12);
    } else {
        gtk_entry_set_placeholder_text(GTK_ENTRY(e), placeholder);
        gtk_entry_set_max_length(GTK_ENTRY(e), 40);
        gtk_entry_set_activates_default(GTK_ENTRY(e), TRUE);
    }
    if (initial) gtk_editable_set_text(GTK_EDITABLE(e), initial);
    if (password) g_object_set(e, "activates-default", TRUE, NULL);
    c->entry = e;
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(d), e);
    present(d);
    gtk_widget_grab_focus(e);
}

/* ------------------------------------------------------------------ formulários */

GtkWidget *row_entry(const char *title, const char *text, int max_chars) {
    GtkWidget *r = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
    if (text) gtk_editable_set_text(GTK_EDITABLE(r), text);
    if (max_chars > 0) g_object_set_data(G_OBJECT(r), "max", GINT_TO_POINTER(max_chars));
    return r;
}

const char *row_text(GtkWidget *row) { return gtk_editable_get_text(GTK_EDITABLE(row)); }

GtkWidget *row_combo(const char *title, const char *const *labels, int n, int selected) {
    GtkStringList *sl = gtk_string_list_new(NULL);
    for (int i = 0; i < n; i++) gtk_string_list_append(sl, labels[i]);
    GtkWidget *r = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
    adw_combo_row_set_model(ADW_COMBO_ROW(r), G_LIST_MODEL(sl));
    g_object_unref(sl);
    if (selected >= 0 && selected < n) adw_combo_row_set_selected(ADW_COMBO_ROW(r), (guint)selected);
    return r;
}

int row_combo_get(GtkWidget *row) {
    guint s = adw_combo_row_get_selected(ADW_COMBO_ROW(row));
    return s == GTK_INVALID_LIST_POSITION ? -1 : (int)s;
}

GtkWidget *row_switch(const char *title, const char *subtitle, gboolean active) {
    GtkWidget *r = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
    if (subtitle) adw_action_row_set_subtitle(ADW_ACTION_ROW(r), subtitle);
    adw_switch_row_set_active(ADW_SWITCH_ROW(r), active);
    return r;
}

gboolean row_switch_get(GtkWidget *row) { return adw_switch_row_get_active(ADW_SWITCH_ROW(row)); }

static GDateTime *dt_of(Day d) {
    int y, m, dd;
    day_to_ymd(d, &y, &m, &dd);
    return g_date_time_new_local(y, m, dd, 12, 0, 0);
}

static void calendar_picked(GtkCalendar *cal, GtkWidget *target) {
    g_autoptr(GDateTime) dt = gtk_calendar_get_date(cal);
    Day d = day_from_ymd(g_date_time_get_year(dt), g_date_time_get_month(dt), g_date_time_get_day_of_month(dt));
    char b[11];
    gtk_editable_set_text(GTK_EDITABLE(target), day_br(d, b));
    GtkWidget *pop = gtk_widget_get_ancestor(GTK_WIDGET(cal), GTK_TYPE_POPOVER);
    if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
}

static void calendar_show(GtkPopover *pop, GtkWidget *target) {
    GtkCalendar *cal = GTK_CALENDAR(g_object_get_data(G_OBJECT(pop), "calendar"));
    Day d;
    if (!day_parse_br(gtk_editable_get_text(GTK_EDITABLE(target)), &d)) d = APP ? APP->today : day_today();
    g_autoptr(GDateTime) dt = dt_of(d);
    g_signal_handlers_block_by_func(cal, calendar_picked, target);
    gtk_calendar_select_day(cal, dt);
    g_signal_handlers_unblock_by_func(cal, calendar_picked, target);
}

static void clear_date(GtkButton *b, GtkWidget *target) {
    gtk_editable_set_text(GTK_EDITABLE(target), "");
    GtkWidget *pop = gtk_widget_get_ancestor(GTK_WIDGET(b), GTK_TYPE_POPOVER);
    if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
}

static GtkWidget *calendar_button(GtkWidget *target, gboolean allow_clear) {
    GtkWidget *mb = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(mb), "fin-calendar-month-symbolic");
    gtk_widget_add_css_class(mb, "flat");
    gtk_widget_set_valign(mb, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(mb, "Escolher no calendário");
    GtkWidget *pop = gtk_popover_new();
    GtkWidget *box = w_vbox(6);
    GtkWidget *cal = gtk_calendar_new();
    w_add(box, cal);
    if (allow_clear) {
        GtkWidget *clr = gtk_button_new_with_label("Sem data");
        gtk_widget_add_css_class(clr, "flat");
        g_signal_connect(clr, "clicked", G_CALLBACK(clear_date), target);
        w_add(box, clr);
    }
    gtk_popover_set_child(GTK_POPOVER(pop), box);
    g_object_set_data(G_OBJECT(pop), "calendar", cal);
    g_signal_connect(cal, "day-selected", G_CALLBACK(calendar_picked), target);
    g_signal_connect(pop, "show", G_CALLBACK(calendar_show), target);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(mb), pop);
    return mb;
}

GtkWidget *row_date(const char *title, Day d, gboolean allow_clear) {
    GtkWidget *r = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
    row_date_set(r, d);
    adw_entry_row_add_suffix(ADW_ENTRY_ROW(r), calendar_button(r, allow_clear));
    gtk_widget_set_tooltip_text(r, "Formato DD/MM/AAAA");
    return r;
}

gboolean row_date_get(GtkWidget *row, Day *out) {
    g_autofree char *t = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(row))));
    if (!*t) { *out = DAY_NONE; return TRUE; }
    return day_parse_br(t, out);
}

void row_date_set(GtkWidget *row, Day d) {
    char b[11];
    gtk_editable_set_text(GTK_EDITABLE(row), d == DAY_NONE ? "" : day_br(d, b));
}

typedef struct { FinFn fn; gpointer data; } FieldCb;

static void field_changed(GtkEditable *e, FieldCb *cb) {
    Day d;
    g_autofree char *t = g_strstrip(g_strdup(gtk_editable_get_text(e)));
    /* só avisa quando o texto é vazio ou uma data completa válida */
    if (!*t || day_parse_br(t, &d)) {
        gtk_widget_remove_css_class(GTK_WIDGET(e), "error");
        if (cb->fn) cb->fn(cb->data);
    } else gtk_widget_add_css_class(GTK_WIDGET(e), "error");
}

GtkWidget *date_field(const char *placeholder, Day d, FinFn changed, gpointer data) {
    GtkWidget *box = w_hbox(0);
    gtk_widget_add_css_class(box, "linked");
    GtkWidget *e = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e), placeholder);
    gtk_editable_set_width_chars(GTK_EDITABLE(e), 10);
    gtk_widget_set_hexpand(e, TRUE);
    gtk_entry_set_max_length(GTK_ENTRY(e), 10);
    char b[11];
    if (d != DAY_NONE) gtk_editable_set_text(GTK_EDITABLE(e), day_br(d, b));
    g_object_set(e, "tooltip-text", "DD/MM/AAAA", NULL);
    gtk_accessible_update_property(GTK_ACCESSIBLE(e), GTK_ACCESSIBLE_PROPERTY_LABEL, placeholder, -1);
    FieldCb *cb = g_new0(FieldCb, 1);
    cb->fn = changed; cb->data = data;
    g_signal_connect_data(e, "changed", G_CALLBACK(field_changed), cb, (GClosureNotify)(void (*)(void))g_free, 0);
    w_add(box, e);
    w_add(box, calendar_button(e, TRUE));
    g_object_set_data(G_OBJECT(box), "entry", e);
    return box;
}

gboolean date_field_get(GtkWidget *field, Day *out) {
    GtkWidget *e = g_object_get_data(G_OBJECT(field), "entry");
    g_autofree char *t = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(e))));
    if (!*t) { *out = DAY_NONE; return TRUE; }
    return day_parse_br(t, out);
}

void date_field_set(GtkWidget *field, Day d) {
    GtkWidget *e = g_object_get_data(G_OBJECT(field), "entry");
    char b[11];
    gtk_editable_set_text(GTK_EDITABLE(e), d == DAY_NONE ? "" : day_br(d, b));
}

/* ------------------------------------------------------------------ FinWidthBin */

#define FIN_TYPE_WIDTH_BIN (fin_width_bin_get_type())
G_DECLARE_FINAL_TYPE(FinWidthBin, fin_width_bin, FIN, WIDTH_BIN, GtkWidget)

struct _FinWidthBin {
    GtkWidget parent;
    GtkWidget *child;
    void (*cb)(int);
    int last;
    int pending;
    guint idle;
};

G_DEFINE_FINAL_TYPE(FinWidthBin, fin_width_bin, GTK_TYPE_WIDGET)

static gboolean width_fire(gpointer p) {
    FinWidthBin *self = p;
    self->idle = 0;
    if (self->cb) self->cb(self->pending);
    return G_SOURCE_REMOVE;
}

static void fin_width_bin_size_allocate(GtkWidget *w, int width, int height, int baseline) {
    FinWidthBin *self = FIN_WIDTH_BIN(w);
    if (self->child) {
        /* o conteúdo pode pedir mais largura que a disponível até o redesenho em uma coluna: corta, sem avisos */
        int min = 0, nat = 0;
        gtk_widget_measure(self->child, GTK_ORIENTATION_HORIZONTAL, -1, &min, &nat, NULL, NULL);
        int cw = MAX(width, min);
        int mh = 0;
        gtk_widget_measure(self->child, GTK_ORIENTATION_VERTICAL, cw, &mh, NULL, NULL, NULL);
        gtk_widget_allocate(self->child, cw, MAX(height, mh), baseline, NULL);
    }
    if (width != self->last) {
        self->last = width;
        self->pending = width;
        if (!self->idle) self->idle = g_idle_add(width_fire, self);
    }
}

static void fin_width_bin_measure(GtkWidget *w, GtkOrientation o, int for_size, int *min, int *nat, int *min_b, int *nat_b) {
    FinWidthBin *self = FIN_WIDTH_BIN(w);
    *min = *nat = 0;
    *min_b = *nat_b = -1;
    if (self->child) gtk_widget_measure(self->child, o, o == GTK_ORIENTATION_HORIZONTAL ? -1 : for_size, min, nat, min_b, nat_b);
    /* a largura mínima declarada é pequena: quem decide as colunas é a largura real (ver size_allocate) */
    if (o == GTK_ORIENTATION_HORIZONTAL) { *min = MIN(*min, 320); *nat = MAX(*nat, *min); }
    else if (self->child) {
        /* altura para a largura real que o filho vai receber */
        int cmin = 0;
        gtk_widget_measure(self->child, GTK_ORIENTATION_HORIZONTAL, -1, &cmin, NULL, NULL, NULL);
        if (for_size >= 0 && for_size < cmin) gtk_widget_measure(self->child, o, cmin, min, nat, min_b, nat_b);
    }
}

static void fin_width_bin_dispose(GObject *o) {
    FinWidthBin *self = FIN_WIDTH_BIN(o);
    if (self->idle) g_source_remove(self->idle);
    self->idle = 0;
    g_clear_pointer(&self->child, gtk_widget_unparent);
    G_OBJECT_CLASS(fin_width_bin_parent_class)->dispose(o);
}

static void fin_width_bin_class_init(FinWidthBinClass *k) {
    GTK_WIDGET_CLASS(k)->size_allocate = fin_width_bin_size_allocate;
    GTK_WIDGET_CLASS(k)->measure = fin_width_bin_measure;
    G_OBJECT_CLASS(k)->dispose = fin_width_bin_dispose;
}

static void fin_width_bin_init(FinWidthBin *self) {
    self->last = -1;
    gtk_widget_set_overflow(GTK_WIDGET(self), GTK_OVERFLOW_HIDDEN);
}

GtkWidget *fin_width_bin_new(GtkWidget *child, void (*cb)(int)) {
    FinWidthBin *self = g_object_new(FIN_TYPE_WIDTH_BIN, NULL);
    self->child = child;
    self->cb = cb;
    gtk_widget_set_parent(child, GTK_WIDGET(self));
    return GTK_WIDGET(self);
}
