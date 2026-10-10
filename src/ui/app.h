/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Estado compartilhado da interface. Há uma única janela; todas as telas leem daqui.
 * Toda alteração de dados segue o mesmo caminho: uma função de core/ops.c altera APP->state
 * e a tela chama app_commit(), que grava (criptografado) e redesenha tudo.
 */
#pragma once
#include <adwaita.h>
#include "core/model.h"
#include "core/finance.h"
#include "core/ops.h"
#include "core/assist.h"
#include "core/store.h"
#include "core/prefs.h"

G_BEGIN_DECLS

#define APP_ID "com.finanplus.FinanPlus"
#ifndef APP_VERSION
#define APP_VERSION "1.1.8"
#endif

typedef enum { PAGE_HOME, PAGE_MOVES, PAGE_REPORTS, PAGE_ASSIST, PAGE_SETTINGS, PAGE_COUNT } PageId;

/* largura útil da área de conteúdo: define quantas colunas cada tela usa */
typedef enum { LAYOUT_NARROW, LAYOUT_MEDIUM, LAYOUT_WIDE } Layout;

/* visão da aba Lançamentos */
typedef enum { MOVES_LIST, MOVES_CALENDAR } MovesView;

typedef struct {
    Day from, to;    /* DAY_NONE = sem limite */
    char *query;
    int kind;        /* -1 todos, KIND_INCOME, KIND_EXPENSE */
    int paid;        /* -1 todos, 1 realizados, 0 pendentes */
} Filters;

typedef struct {
    AdwApplication *gapp;
    GtkWindow *window;
    Store *store;
    Prefs *prefs;
    AppState *state;
    Dictionary *dict;
    Day today;

    gboolean locked;
    /* os dados não puderam ser abertos: nada é gravado até o usuário decidir na tela de problema */
    gboolean problem;
    /* cancelado ao bloquear: fecha seletores de arquivo abertos */
    GCancellable *files;
    PinThrottle throttle;
    gint64 inactive_since; /* tempo monotônico em que a janela perdeu o foco; 0 = ativa */

    Filters filters;
    MovesView moves_view;
    /* calendário: mês mostrado (YM_NONE = ainda não aberto: abre no mês de hoje) e dia escolhido */
    Ym cal_ym;
    Day cal_day;
    Layout layout;
    PageId page;

    /* telas (preenchidas por window.c) */
    GtkWidget *page_root[PAGE_COUNT];
    void (*page_refresh[PAGE_COUNT])(void);
    AdwToastOverlay *toasts;
} App;

extern App *APP;

/* ---- ciclo de dados ---- */
void app_commit(void);          /* grava e redesenha */
void app_refresh(void);         /* só redesenha */
void app_refresh_page(PageId p);
gboolean app_save(GError **error);

/* ---- formatação que respeita "Ocultar valores" ---- */
MoneyFmt app_money_fmt(void);
char *app_money(Cents c);       /* g_free */
gboolean app_hidden(void);

/* ---- navegação ---- */
void app_show_page(PageId p);
/* abre Lançamentos já filtrado (usado pelas dicas e respostas do assistente) */
void app_open_moves(const char *query, Day from, Day to, int kind);
/* abre Lançamentos com o filtro "Pendentes" no período atual (botão do cartão Vencimentos) */
void app_open_moves_pending(void);
/* abre Lançamentos › Calendário no mês [ym] */
void app_open_calendar(Ym ym);

/* ---- avisos ---- */
void app_toast(const char *fmt, ...) G_GNUC_PRINTF(1, 2);

/* ---- bloqueio ---- */
void app_lock(void);
void app_unlocked(void);
gboolean app_lock_enabled(void);

/* cria a janela (sinal "activate" do GtkApplication) */
void app_activate(GtkApplication *gapp);

void filters_this_month(Filters *f);

G_END_DECLS
