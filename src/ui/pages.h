/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Telas e diálogos da interface.
 */
#pragma once
#include "app.h"

G_BEGIN_DECLS

/* cada tela: cria a raiz (uma vez) e redesenha o conteúdo */
GtkWidget *page_home_new(void);
void page_home_refresh(void);
GtkWidget *page_moves_new(void);
void page_moves_refresh(void);
void page_moves_focus_search(void);
GtkWidget *page_reports_new(void);
void page_reports_refresh(void);
GtkWidget *page_assist_new(void);
void page_assist_refresh(void);
void page_assist_focus_question(void);
GtkWidget *page_settings_new(void);
void page_settings_refresh(void);

/* bloqueio por PIN */
GtkWidget *lock_page_new(void);
void lock_page_reset(void);

/* editores (diálogos) — id NULL = novo */
void editor_tx(Kind kind, const char *id);
void editor_goal(const char *id);
void editor_account(const char *id);
void editor_card(const char *id);
void editor_recurring(const char *id);
void editor_limit(const char *category);
void editor_pay_invoice(const char *card_id);

/* dados: arquivos */
void data_export_csv(void);
void data_export_backup(void);
void data_restore_backup(void);
void data_wipe(void);
void report_pdf_dialog(Day from, Day to);
/* escreve o PDF; FALSE + error se falhar */
gboolean pdf_report_write(const char *path, Day from, Day to, gboolean include_txs, GError **error);

/* avisos de vencimento */
void notify_check(gboolean force);
/* modo "--avisos": verifica e sai (usado pelo início automático da sessão) */
int notify_headless(GApplication *app);
gboolean notify_autostart_set(gboolean on, GError **error);
gboolean notify_autostart_enabled(void);

/* sobre e atalhos */
void show_about(void);
void show_shortcuts(void);

/* linha de lançamento (usada na lista e no início) */
GtkWidget *tx_row_new(const Tx *t);
const char *tx_status(const Tx *t, gboolean *late);


G_END_DECLS
