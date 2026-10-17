/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Gráficos desenhados com Cairo (sem biblioteca externa), nas cores do tema:
 * linha de saldo (Início e Resumo), barras de receitas × despesas por mês (Início e Resumo)
 * e a miniatura de cada tema (Ajustes › Aparência). Todos respeitam "Ocultar valores".
 */
#pragma once
#include <gtk/gtk.h>
#include "core/model.h"
#include "core/finance.h"

G_BEGIN_DECLS

/* Linha com área preenchida. [values] tem [n] pontos; os pontos depois do índice [solid] saem
 * tracejados (previsão). [labels]: rótulo do eixo X de cada ponto, NULL onde não há rótulo.
 * [a11y] descreve o gráfico para leitores de tela. */
GtkWidget *chart_line_new(const Cents *values, int n, int solid, const char *const *labels, int height, const char *a11y);

/* Barras de receitas (verde) e despesas (vermelho) de [n] meses (até 12). */
GtkWidget *chart_month_bars_new(const Ym *yms, const Flow *flows, int n, int height, const char *a11y);

/* Miniatura da janela no tema [t] (barra lateral, cartões e o destaque). */
GtkWidget *theme_preview_new(ThemeId t);

G_END_DECLS
