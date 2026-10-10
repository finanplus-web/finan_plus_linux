/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Peças de interface reutilizadas por todas as telas: cartões, rótulos, valores em reais,
 * barras, ícones, botões, diálogos e campos de formulário.
 */
#pragma once
#include <adwaita.h>
#include "core/model.h"

G_BEGIN_DECLS

typedef void (*FinFn)(gpointer data);
typedef void (*FinTextFn)(const char *text, gpointer data);

/* ---- layout ---- */
GtkWidget *w_box(GtkOrientation o, int spacing);
#define w_vbox(s) w_box(GTK_ORIENTATION_VERTICAL, (s))
#define w_hbox(s) w_box(GTK_ORIENTATION_HORIZONTAL, (s))
void w_clear(GtkWidget *box);
void w_add(GtkWidget *box, GtkWidget *child);
void w_classes(GtkWidget *w, const char *classes); /* "a b c" */
GtkWidget *w_card(const char *extra_classes);
GtkWidget *w_spacer(void); /* expande na horizontal */

/* ---- texto ---- */
GtkWidget *w_label(const char *text, const char *classes);
GtkWidget *w_label_wrap(const char *text, const char *classes);
GtkWidget *w_eyebrow(const char *text);
GtkWidget *w_title(const char *text);
GtkWidget *w_section_head(const char *eyebrow, const char *title);
/* valor em reais (respeita "Ocultar valores") */
GtkWidget *w_money(Cents c, const char *classes);
GtkWidget *w_badge(const char *text, const char *variant);

/* ---- gráficos simples ---- */
GtkWidget *w_level(double fraction, const char *variant);

/* ---- ícones (Material Symbols, nome sem "fin-"/"-symbolic") ---- */
GtkWidget *w_icon(const char *name, int px);
GtkWidget *w_icon_label(const char *icon, const char *text, const char *classes);
char *icon_name(const char *name); /* "home" → "fin-home-symbolic" */
/* ícone de categoria (NULL se não houver: mostra a inicial) */
const char *category_icon(const char *category);
GtkWidget *w_category_glyph(const char *category);

/* ---- botões ---- */
GtkWidget *w_button(const char *label, const char *icon, const char *classes, FinFn fn, gpointer data, GDestroyNotify destroy);
GtkWidget *w_pill(const char *label, FinFn fn, gpointer data, GDestroyNotify destroy);
/* link "Texto ›" (sem fundo), para "Abrir assistente", "Ver no calendário"… */
GtkWidget *w_more_link(const char *label, FinFn fn, gpointer data, GDestroyNotify destroy);
/* botão "Por quê?" que mostra a explicação logo abaixo */
GtkWidget *w_why(const char *why);
/* Cartão que abre e fecha com o botão + / − (o estado fica guardado por [key] durante a sessão). */
GtkWidget *w_collapsible(const char *key, const char *title, const char *summary, GtkWidget **content);

/* ---- diálogos ---- */
void dlg_notice(const char *title, const char *msg);
void dlg_confirm(const char *title, const char *msg, const char *ok, gboolean danger, FinFn on_ok, gpointer data, GDestroyNotify destroy);
/* três saídas: Cancelar / [alt] / [ok] */
void dlg_choice(const char *title, const char *msg, const char *alt, const char *ok, gboolean danger,
                FinFn on_alt, FinFn on_ok, gpointer data, GDestroyNotify destroy);
void dlg_input(const char *title, const char *msg, const char *placeholder, const char *initial, gboolean password,
               FinTextFn on_text, gpointer data, GDestroyNotify destroy);

/* ---- formulários (linhas do libadwaita) ---- */
GtkWidget *row_entry(const char *title, const char *text, int max_chars);
const char *row_text(GtkWidget *row);
GtkWidget *row_combo(const char *title, const char *const *labels, int n, int selected);
int row_combo_get(GtkWidget *row);
GtkWidget *row_switch(const char *title, const char *subtitle, gboolean active);
gboolean row_switch_get(GtkWidget *row);
/* data no formato DD/MM/AAAA, com calendário */
GtkWidget *row_date(const char *title, Day d, gboolean allow_clear);
/* devolve FALSE se o texto não é uma data válida; vazio = DAY_NONE (válido) */
gboolean row_date_get(GtkWidget *row, Day *out);
void row_date_set(GtkWidget *row, Day d);

/* entrada de data solta (De/Até), com calendário */
GtkWidget *date_field(const char *placeholder, Day d, FinFn changed, gpointer data);
gboolean date_field_get(GtkWidget *field, Day *out);
void date_field_set(GtkWidget *field, Day d);

/* ---- largura disponível: chama [cb] quando muda a faixa (estreita/média/larga) ---- */
GtkWidget *fin_width_bin_new(GtkWidget *child, void (*cb)(int width));

G_END_DECLS
