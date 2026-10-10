/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Temas do Finan+ (as mesmas cores do app Android e da versão web, com contraste ≥ 4,5:1).
 * Cada tema vira uma folha de estilo CSS gerada aqui: as cores nomeadas do libadwaita são
 * redefinidas e as classes .fin-* desenham os cartões no estilo do Finan+.
 */
#include "theme.h"
#include <adwaita.h>

/* bg, surface(ARGB), border(ARGB), text, muted, accent, accent2, on_accent, red, green, track, glowA, glowB */
static const FinPalette PALETTES[] = {
    /* Claro */
    {FALSE, 0xFFEEF4FF, 0xB8FFFFFF, 0xB8FFFFFF, 0xFF182238, 0xFF5B6579, 0xFF3A5FC8, 0xFFDFE8FF, 0xFFFFFFFF, 0xFFB03A4F, 0xFF1B7351, 0xFFDFE5EF, 0xFFD9E5FF, 0xFFFFE7ED},
    /* Material (azul) */
    {FALSE, 0xFFF4F7FF, 0xD1F8FAFF, 0xD1FFFFFF, 0xFF182033, 0xFF5F6778, 0xFF0B57D0, 0xFFDBE7FF, 0xFFFFFFFF, 0xFFBA1A1A, 0xFF146C43, 0xFFDFE5EF, 0xFFDBE7FF, 0xFFE9E7FF},
    /* OLED Cinza */
    {TRUE, 0xFF181A1F, 0xFF23262D, 0xFF353941, 0xFFF1F3F6, 0xFFA8ADB7, 0xFF8AA8FF, 0xFF30343D, 0xFF10131A, 0xFFFF8A91, 0xFF63D6A5, 0xFF353941, 0xFF181A1F, 0xFF181A1F},
    /* Tokyo Night */
    {TRUE, 0xFF1A1B26, 0xDB24283B, 0x247AA2F7, 0xFFC0CAF5, 0xFF9AA5CE, 0xFF7AA2F7, 0xFF292E42, 0xFF1A1B26, 0xFFF7768E, 0xFF9ECE6A, 0xFF3B4261, 0xFF24283B, 0xFF292E42},
    /* Nord */
    {TRUE, 0xFF2E3440, 0xDB3B4252, 0x1FD8DEE9, 0xFFECEFF4, 0xFFB7C0CF, 0xFF88C0D0, 0xFF434C5E, 0xFF2E3440, 0xFFF0A3A9, 0xFFA3BE8C, 0xFF4C566A, 0xFF3B4252, 0xFF354052},
};
enum { P_LIGHT, P_MATERIAL, P_OLED, P_TOKYO, P_NORD };

static GtkCssProvider *provider = NULL;
static const FinPalette *current = &PALETTES[P_LIGHT];

const FinPalette *theme_palette(void) { return current; }

const FinPalette *theme_palette_for(ThemeId t, gboolean system_dark) {
    switch (t) {
    case THEME_LIGHT: return &PALETTES[P_LIGHT];
    case THEME_MATERIAL: return &PALETTES[P_MATERIAL];
    case THEME_OLED: return &PALETTES[P_OLED];
    case THEME_TOKYO: return &PALETTES[P_TOKYO];
    case THEME_NORD: return &PALETTES[P_NORD];
    default: return system_dark ? &PALETTES[P_OLED] : &PALETTES[P_LIGHT];
    }
}

/* "rgba(r,g,b,a)" */
/* Sempre com ponto decimal: com o sistema em português, printf("%.3f") escreve "0,860", o GTK
 * recusa a cor ("Expected ')' at end of rgba()") e o tema inteiro deixava de ser aplicado. */
static char *css(guint32 argb) {
    char alpha[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(alpha, sizeof alpha, "%.3f", ((argb >> 24) & 255) / 255.0);
    return g_strdup_printf("rgba(%u,%u,%u,%s)", (argb >> 16) & 255, (argb >> 8) & 255, argb & 255, alpha);
}

/* cor opaca resultante de [fg] (com alfa) sobre [bg] */
static guint32 over(guint32 fg, guint32 bg) {
    double a = ((fg >> 24) & 255) / 255.0;
    guint32 out = 0xFF000000;
    for (int sh = 0; sh <= 16; sh += 8) {
        double f = (fg >> sh) & 255, b = (bg >> sh) & 255;
        out |= ((guint32)(f * a + b * (1 - a) + 0.5) & 255) << sh;
    }
    return out;
}

void theme_rgba(guint32 argb, GdkRGBA *out) {
    out->red = ((argb >> 16) & 255) / 255.0;
    out->green = ((argb >> 8) & 255) / 255.0;
    out->blue = (argb & 255) / 255.0;
    out->alpha = ((argb >> 24) & 255) / 255.0;
}

void theme_cairo(cairo_t *cr, guint32 argb) {
    cairo_set_source_rgba(cr, ((argb >> 16) & 255) / 255.0, ((argb >> 8) & 255) / 255.0, (argb & 255) / 255.0, ((argb >> 24) & 255) / 255.0);
}

static char *build_css(const FinPalette *p) {
    guint32 solid = over(p->surface, p->bg);
    guint32 popover = p->dark ? solid : 0xFFF7F9FF;
    g_autofree char *bg = css(p->bg), *surface = css(p->surface), *border = css(p->border), *text = css(p->text),
                    *muted = css(p->muted), *accent = css(p->accent), *accent2 = css(p->accent2), *on = css(p->on_accent),
                    *red = css(p->red), *green = css(p->green), *track = css(p->track), *ga = css(p->glow_a), *gb = css(p->glow_b),
                    *sol = css(solid), *pop = css(popover);
    guint32 accent_soft_argb = (p->accent & 0x00FFFFFF) | 0x24000000;
    guint32 green_soft_argb = (p->green & 0x00FFFFFF) | 0x24000000;
    guint32 red_soft_argb = (p->red & 0x00FFFFFF) | 0x1F000000;
    g_autofree char *accent_soft = css(accent_soft_argb), *green_soft = css(green_soft_argb), *red_soft = css(red_soft_argb);
    guint32 stat_argb = p->dark ? 0x0FFFFFFF : 0x8CFFFFFF;
    g_autofree char *stat = css(stat_argb);
    guint32 sidebar_argb = p->dark ? over(0x14FFFFFF, p->bg) : over(0x66FFFFFF, p->bg);
    g_autofree char *sidebar = css(sidebar_argb);

    return g_strdup_printf(
        /* cores nomeadas do libadwaita */
        "/* Finan+ %17$s */\n"
        "@define-color window_bg_color %1$s;\n"
        "@define-color window_fg_color %4$s;\n"
        "@define-color view_bg_color %14$s;\n"
        "@define-color view_fg_color %4$s;\n"
        "@define-color headerbar_bg_color %1$s;\n"
        "@define-color headerbar_fg_color %4$s;\n"
        "@define-color headerbar_backdrop_color %1$s;\n"
        "@define-color sidebar_bg_color %18$s;\n"
        "@define-color sidebar_fg_color %4$s;\n"
        "@define-color sidebar_backdrop_color %18$s;\n"
        "@define-color card_bg_color %14$s;\n"
        "@define-color card_fg_color %4$s;\n"
        "@define-color dialog_bg_color %15$s;\n"
        "@define-color dialog_fg_color %4$s;\n"
        "@define-color popover_bg_color %15$s;\n"
        "@define-color popover_fg_color %4$s;\n"
        "@define-color accent_bg_color %6$s;\n"
        "@define-color accent_fg_color %8$s;\n"
        "@define-color accent_color %6$s;\n"
        "@define-color destructive_bg_color %9$s;\n"
        "@define-color destructive_color %9$s;\n"
        "@define-color success_color %10$s;\n"
        "@define-color error_color %9$s;\n"
        /* fundo com brilhos suaves, como no app */
        ".fin-content { background-color: %1$s; background-image: radial-gradient(circle at 8%% 0%%, %12$s, transparent 38%%),"
        " radial-gradient(circle at 96%% 6%%, %13$s, transparent 32%%); }\n"
        ".fin-card { background-color: %2$s; border: 1px solid %3$s; border-radius: 24px; padding: 18px;"
        " box-shadow: 0 6px 22px rgba(0,0,0,%19$s); }\n"
        ".fin-card.fin-hero { border-radius: 30px; padding: 22px; }\n"
        ".fin-card.fin-flat { box-shadow: none; }\n"
        ".fin-card.fin-tight { padding: 14px; border-radius: 20px; }\n"
        ".fin-stat { background-color: %16$s; border-radius: 20px; padding: 14px; }\n"
        ".fin-soft { background-color: %7$s; border-radius: 16px; padding: 12px; }\n"
        ".fin-tip { background-color: alpha(%7$s, 0.55); border-radius: 18px; padding: 12px; }\n"
        ".fin-why { background-color: alpha(%7$s, 0.75); border-radius: 12px; padding: 10px; }\n"
        ".fin-hint { background-color: %20$s; border-radius: 16px; padding: 8px 12px; }\n"
        ".fin-eyebrow { font-size: 0.78em; font-weight: 700; letter-spacing: 0.06em; color: %5$s; }\n"
        ".fin-title { font-size: 1.25em; font-weight: 800; }\n"
        ".fin-page-title { font-size: 1.9em; font-weight: 800; }\n"
        ".fin-muted { color: %5$s; }\n"
        "button.fin-link { padding: 4px 6px; margin-left: -6px; min-height: 0; font-weight: 700; }\n"
        ".fin-green { color: %10$s; }\n"
        ".fin-red { color: %9$s; }\n"
        ".fin-accent { color: %6$s; }\n"
        ".fin-money { font-weight: 800; font-feature-settings: 'tnum'; }\n"
        ".fin-money-big { font-size: 1.75em; font-weight: 800; font-feature-settings: 'tnum'; }\n"
        ".fin-money-mid { font-size: 1.2em; font-weight: 800; font-feature-settings: 'tnum'; }\n"
        ".fin-badge { border-radius: 99px; padding: 4px 10px; font-size: 0.85em; font-weight: 700; }\n"
        ".fin-badge.green { background-color: %21$s; color: %10$s; }\n"
        ".fin-badge.accent { background-color: %20$s; color: %6$s; }\n"
        ".fin-badge.red { background-color: %22$s; color: %9$s; }\n"
        ".fin-quick { background-color: %7$s; color: %4$s; border-radius: 17px; padding: 12px 10px; font-weight: 700; border: none; box-shadow: none; }\n"
        ".fin-quick:hover { background-color: alpha(%6$s, 0.20); }\n"
        ".fin-pill { border-radius: 99px; padding: 6px 14px; background-color: %7$s; color: %4$s; font-weight: 600; border: none; box-shadow: none; min-height: 0; }\n"
        ".fin-pill:hover { background-color: alpha(%6$s, 0.22); }\n"
        ".fin-pill:checked, .fin-pill.selected { background-color: %6$s; color: %8$s; }\n"
        ".fin-pill.danger { color: %9$s; }\n"
        ".fin-round { border-radius: 99px; min-width: 38px; min-height: 38px; padding: 0; background-color: %7$s; color: %4$s; border: none; box-shadow: none; }\n"
        ".fin-round:hover { background-color: alpha(%6$s, 0.22); }\n"
        ".fin-glyph { background-color: %7$s; border-radius: 14px; min-width: 42px; min-height: 42px; color: %4$s; font-weight: 800; }\n"
        ".fin-bar { min-height: 10px; }\n"
        ".fin-row { background-color: %2$s; border: 1px solid %3$s; border-radius: 18px; padding: 10px 12px; }\n"
        ".fin-row:hover { background-color: alpha(%6$s, 0.08); }\n"
        "list.fin-list { background: transparent; }\n"
        "list.fin-list > row { padding: 3px 0; background: none; border-radius: 18px; }\n"
        "list.fin-list > row:hover { background: none; }\n"
        "list.fin-list > row:focus-visible > .fin-row { outline: 2px solid %6$s; outline-offset: -2px; }\n"
        ".fin-check { min-width: 30px; min-height: 30px; border-radius: 99px; border: 2px solid %5$s; padding: 0; background: none; box-shadow: none; }\n"
        ".fin-check.on { background-color: %10$s; border-color: %10$s; color: %8$s; }\n"
        ".fin-late { color: %9$s; font-weight: 700; }\n"
        ".fin-swatch { min-width: 22px; min-height: 9px; border-radius: 99px; border: 1px solid alpha(%5$s, 0.5); }\n"
        ".fin-theme-btn { border-radius: 17px; padding: 12px; background-color: %7$s; color: %4$s; border: 2px solid transparent; box-shadow: none; }\n"
        ".fin-theme-btn.selected { border-color: %6$s; background-color: alpha(%6$s, 0.18); }\n"
        ".fin-sidebar-total { background-color: %2$s; border: 1px solid %3$s; border-radius: 18px; padding: 12px; margin: 10px; }\n"
        ".navigation-sidebar > row { border-radius: 12px; margin: 2px 8px; padding: 8px 10px; }\n"
        ".fin-lock { background-color: %1$s; background-image: radial-gradient(circle at 20%% 10%%, %12$s, transparent 45%%),"
        " radial-gradient(circle at 80%% 90%%, %13$s, transparent 40%%); }\n"
        ".fin-pin-dot { min-width: 14px; min-height: 14px; border-radius: 99px; background-color: %11$s; }\n"
        ".fin-pin-dot.on { background-color: %6$s; }\n"
        ".fin-mono { font-family: monospace; font-size: 0.85em; }\n"
        "levelbar.fin-level trough { min-height: 9px; border-radius: 99px; background-color: %11$s; }\n"
        "levelbar.fin-level block.filled { border-radius: 99px; background-color: %6$s; }\n"
        "levelbar.fin-level.warn block.filled { background-color: #E6A23C; }\n"
        "levelbar.fin-level.over block.filled { background-color: %9$s; }\n"
        "levelbar.fin-level.green block.filled { background-color: %10$s; }\n"
        "levelbar.fin-level.red block.filled { background-color: %9$s; }\n"
        "levelbar.fin-level block.empty { background-color: transparent; }\n",
        bg, surface, border, text, muted, accent, accent2, on, red, green, track, ga, gb, sol, pop, stat, "",
        sidebar, p->dark ? "0.22" : "0.06", accent_soft, green_soft, red_soft);
}

/* libadwaita 1.6 em diante pinta os componentes com variáveis CSS (--window-bg-color…), e não mais
 * com as cores nomeadas (@window_bg_color). Para o tema funcionar nas duas gerações, as mesmas
 * cores também são definidas como variáveis quando a biblioteca é nova. */
static char *build_vars(const FinPalette *p) {
    guint32 solid = over(p->surface, p->bg);
    guint32 popover = p->dark ? solid : 0xFFF7F9FF;
    guint32 sidebar_argb = p->dark ? over(0x14FFFFFF, p->bg) : over(0x66FFFFFF, p->bg);
    g_autofree char *bg = css(p->bg), *text = css(p->text), *accent = css(p->accent), *on = css(p->on_accent),
                    *red = css(p->red), *green = css(p->green), *sol = css(solid), *pop = css(popover), *side = css(sidebar_argb);
    return g_strdup_printf(
        ":root {\n"
        "  --window-bg-color: %1$s; --window-fg-color: %2$s;\n"
        "  --view-bg-color: %3$s; --view-fg-color: %2$s;\n"
        "  --headerbar-bg-color: %1$s; --headerbar-fg-color: %2$s; --headerbar-backdrop-color: %1$s;\n"
        "  --sidebar-bg-color: %4$s; --sidebar-fg-color: %2$s; --sidebar-backdrop-color: %4$s;\n"
        "  --secondary-sidebar-bg-color: %4$s; --secondary-sidebar-fg-color: %2$s;\n"
        "  --card-bg-color: %3$s; --card-fg-color: %2$s;\n"
        "  --dialog-bg-color: %5$s; --dialog-fg-color: %2$s;\n"
        "  --popover-bg-color: %5$s; --popover-fg-color: %2$s;\n"
        "  --accent-bg-color: %6$s; --accent-fg-color: %7$s; --accent-color: %6$s;\n"
        "  --destructive-bg-color: %8$s; --destructive-color: %8$s; --error-color: %8$s; --success-color: %9$s;\n"
        "}\n",
        bg, text, sol, side, pop, accent, on, red, green);
}

/* Regras diretas para os componentes que levam a cor de destaque. Temas de terceiros instalados em
 * ~/.config/gtk-4.0/gtk.css (ex.: Blackline, cores do KDE) costumam pintar esses componentes com
 * valores fixos, que as cores nomeadas não alcançam; aqui o Finan+ define os próprios. */
static char *build_widgets(const FinPalette *p) {
    guint32 solid = over(p->surface, p->bg);
    g_autofree char *bg = css(p->bg), *text = css(p->text), *muted = css(p->muted), *accent = css(p->accent), *on = css(p->on_accent),
                    *accent2 = css(p->accent2), *sol = css(solid), *border = css(p->border), *track = css(p->track);
    return g_strdup_printf(
        "window.background { background-color: %1$s; color: %2$s; }\n"
        "button.suggested-action { background-color: %4$s; color: %5$s; background-image: none; border: none; }\n"
        "button.suggested-action:hover { background-color: shade(%4$s, 1.08); }\n"
        "button.suggested-action:active { background-color: shade(%4$s, 0.92); }\n"
        "button.destructive-action { color: %10$s; }\n"
        /* listas arredondadas (boxed-list: ex. "O que há de novo"/"Detalhes" em Sobre, editores): temas do sistema
           como o Blackline pintam o fundo da lista sem cantos arredondados, aparecendo um quadrado atrás do cartão */
        "list.boxed-list { background-color: %7$s; border-radius: 12px; box-shadow: 0 0 0 1px %8$s; border: none; }\n"
        "list.boxed-list > row { background-color: transparent; }\n"
        "list.boxed-list > row:first-child { border-top-left-radius: 12px; border-top-right-radius: 12px; }\n"
        "list.boxed-list > row:last-child { border-bottom-left-radius: 12px; border-bottom-right-radius: 12px; }\n"
        "list.boxed-list > row.activatable:hover { background-color: alpha(%2$s, 0.05); }\n"
        "list.boxed-list > row.activatable:active { background-color: alpha(%2$s, 0.09); }\n"
        ".navigation-sidebar { background-color: transparent; }\n"
        ".navigation-sidebar > row { background-color: transparent; }\n"
        ".navigation-sidebar > row:hover { background-color: alpha(%2$s, 0.06); }\n"
        ".navigation-sidebar > row:selected { background-color: alpha(%4$s, 0.16); color: %2$s; }\n"
        ".navigation-sidebar > row:selected:hover { background-color: alpha(%4$s, 0.22); }\n"
        ".navigation-sidebar > row:selected image { color: %4$s; }\n"
        "entry, spinbutton, textview, dropdown > button { background-color: %7$s; color: %2$s; border-color: %8$s; }\n"
        "entry:focus-within { outline-color: alpha(%4$s, 0.6); }\n"
        "dropdown > button, dropdown popover { color: %2$s; }\n"
        "popover > contents, popover > arrow { background-color: %7$s; color: %2$s; }\n"
        "switch { background-color: %9$s; }\n"
        "switch:checked { background-color: %4$s; }\n"
        "checkbutton check:checked, checkbutton radio:checked { background-color: %4$s; color: %5$s; }\n"
        "toast { background-color: alpha(%2$s, 0.92); color: %1$s; }\n"
        "label.dim-label, .dim-label { color: %3$s; }\n"
        "selection { background-color: alpha(%4$s, 0.30); }\n"
        "link, button.link { color: %4$s; }\n"
        ".fin-pill:checked, .fin-pill.selected { background-color: %4$s; color: %5$s; }\n"
        ".linked > togglebutton:checked, .linked > button:checked { background-color: %4$s; color: %5$s; background-image: none; }\n"
        ".linked > togglebutton:checked:hover { background-color: shade(%4$s, 1.08); }\n"
        "/* %6$s */\n",
        bg, text, muted, accent, on, accent2, sol, border, track, css(p->red));
}

/* Calendário, período, resumo e simulador (1.2.0). Só cores da paleta do tema; o roxo do cartão é o mesmo
 * pontinho do app Android e da versão web. */
static char *build_extra(const FinPalette *p) {
    g_autofree char *text = css(p->text), *muted = css(p->muted), *accent = css(p->accent), *on = css(p->on_accent),
                    *accent2 = css(p->accent2), *red = css(p->red), *green = css(p->green), *track = css(p->track);
    const char *purple = p->dark ? "#C3A6FF" : "#7446D0";
    return g_strdup_printf(
        "button.fin-cal-day { border-radius: 14px; padding: 6px 2px; min-height: 62px; background: none; border: none; box-shadow: none; color: %1$s; }\n"
        "button.fin-cal-day:hover { background-color: alpha(%3$s, 0.10); }\n"
        "button.fin-cal-day.today { box-shadow: inset 0 0 0 2px %3$s; }\n"
        "button.fin-cal-day.sel { background-color: %3$s; color: %4$s; }\n"
        "button.fin-cal-day.sel label, button.fin-cal-day.sel .fin-green, button.fin-cal-day.sel .fin-red, button.fin-cal-day.sel image { color: %4$s; }\n"
        "button.fin-cal-day.sel .fin-dot { background-color: %4$s; }\n"
        ".fin-cal-num { font-weight: 800; }\n"
        ".fin-cal-val { font-size: 0.78em; font-weight: 700; font-feature-settings: 'tnum'; }\n"
        ".fin-cal-wd { font-size: 0.75em; font-weight: 700; color: %2$s; letter-spacing: 0.04em; }\n"
        ".fin-cal-late { color: %6$s; }\n"
        ".fin-dot { min-width: 6px; min-height: 6px; border-radius: 99px; }\n"
        ".fin-dot.inc { background-color: %7$s; }\n"
        ".fin-dot.exp { background-color: %6$s; }\n"
        ".fin-dot.card { background-color: %9$s; }\n"
        ".fin-card-purple { color: %9$s; }\n"
        ".fin-day-head { font-weight: 800; padding: 10px 4px 2px 4px; }\n"
        ".fin-round.on { background-color: %3$s; color: %4$s; }\n"
        ".fin-whatif { background-color: %3$s; color: %4$s; border-radius: 22px; padding: 14px 16px; border: none; box-shadow: none; }\n"
        ".fin-whatif:hover { background-color: shade(%3$s, 1.06); }\n"
        ".fin-whatif label, .fin-whatif image { color: %4$s; }\n"
        ".fin-safe { background-color: alpha(%7$s, 0.14); color: %7$s; border-radius: 99px; padding: 6px 12px; font-weight: 700; }\n"
        ".fin-safe label, .fin-safe image { color: %7$s; }\n"
        ".fin-simbox { background-color: alpha(%5$s, 0.6); border-radius: 20px; padding: 14px; }\n"
        ".fin-sim-big { font-size: 1.9em; font-weight: 850; }\n"
        ".fin-scen { background-color: alpha(%5$s, 0.35); border-radius: 18px; padding: 12px; border: none; box-shadow: none; color: %1$s; }\n"
        ".fin-scen:hover { background-color: alpha(%3$s, 0.14); }\n"
        ".fin-scen.on { box-shadow: inset 0 0 0 2px %3$s; }\n"
        ".fin-scen-ico { background-color: %5$s; border-radius: 14px; min-width: 42px; min-height: 42px; color: %3$s; }\n"
        ".fin-bar-track { background-color: %8$s; }\n"
        "button.fin-link, button.fin-link label { color: %3$s; }\n",
        text, muted, accent, on, accent2, red, green, track, purple);
}

void theme_apply(ThemeId t) {
    AdwStyleManager *sm = adw_style_manager_get_default();
    if (t == THEME_AUTO) adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_DEFAULT);
    gboolean dark = t == THEME_AUTO ? adw_style_manager_get_dark(sm) : FALSE;
    current = theme_palette_for(t, dark);
    if (t != THEME_AUTO) adw_style_manager_set_color_scheme(sm, current->dark ? ADW_COLOR_SCHEME_FORCE_DARK : ADW_COLOR_SCHEME_FORCE_LIGHT);
    if (!provider) {
        provider = gtk_css_provider_new();
        /* acima da prioridade "do usuário": algumas distribuições e ferramentas de temas gravam
         * ~/.config/gtk-4.0/gtk.css redefinindo as cores do libadwaita, o que impedia a troca de tema */
        gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
                                                   GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
    }
    g_autofree char *base = build_css(current);
    g_autofree char *vars = adw_get_major_version() > 1 || adw_get_minor_version() >= 6 ? build_vars(current) : g_strdup("");
    g_autofree char *widgets = build_widgets(current);
    g_autofree char *extra = build_extra(current);
    g_autofree char *sheet = g_strconcat(base, vars, widgets, extra, NULL);
    gtk_css_provider_load_from_string(provider, sheet);
    if (g_getenv("FINAN_PLUS_DEBUG")) g_printerr("[Finan+] tema %d aplicado (escuro=%d, libadwaita %u.%u)\n", t, current->dark, adw_get_major_version(), adw_get_minor_version());
}

void theme_swatch(ThemeId t, guint32 *a, guint32 *b) {
    static const guint32 S[THEME_COUNT][2] = {
        {0xFFD9E5FF, 0xFF181A1F}, {0xFFEEF4FF, 0xFF3A5FC8}, {0xFFDBE7FF, 0xFF0B57D0},
        {0xFF181A1F, 0xFF8AA8FF}, {0xFF1A1B26, 0xFF7AA2F7}, {0xFF2E3440, 0xFF88C0D0},
    };
    *a = S[t][0];
    *b = S[t][1];
}

const guint32 CHART_SERIES[8] = {0xFF3A5FC8, 0xFFE07A2F, 0xFF1B9E77, 0xFFB03A4F, 0xFF7B61C9, 0xFF2A9DB5, 0xFFC49A1A, 0xFF8A8F99};
