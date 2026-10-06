/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <gtk/gtk.h>
#include "core/model.h"

G_BEGIN_DECLS

/* cores em ARGB (0xAARRGGBB) */
typedef struct {
    gboolean dark;
    guint32 bg, surface, border, text, muted, accent, accent2, on_accent, red, green, track, glow_a, glow_b;
} FinPalette;

void theme_apply(ThemeId t);
const FinPalette *theme_palette(void);
const FinPalette *theme_palette_for(ThemeId t, gboolean system_dark);
void theme_rgba(guint32 argb, GdkRGBA *out);
void theme_cairo(cairo_t *cr, guint32 argb);
/* duas amostras de cor para o seletor de tema */
void theme_swatch(ThemeId t, guint32 *a, guint32 *b);
/* cores das séries dos gráficos (as mesmas do PDF) */
extern const guint32 CHART_SERIES[8];

G_END_DECLS
