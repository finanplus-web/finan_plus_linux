/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <cairo.h>
#include "core/model.h"

G_BEGIN_DECLS

/* Relatório em PDF (A4) do período. FALSE + error se falhar. */
gboolean pdf_report_render(const AppState *s, const char *path, Day from, Day to, gboolean include_txs, Day today, GError **error);

/* gráfico de rosca (tela e PDF usam as mesmas cores) */
void chart_donut(cairo_t *cr, double cx, double cy, double diameter, double thickness, const Cents *values, int n, Cents total);

G_END_DECLS
