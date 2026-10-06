/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Datas como número de dias desde 1970-01-01 (calendário gregoriano proléptico).
 * Algoritmos "days_from_civil"/"civil_from_days" de Howard Hinnant (domínio público).
 */
#include "model.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

const char *const BR_MONTHS[12] = {
    "janeiro", "fevereiro", "março", "abril", "maio", "junho",
    "julho", "agosto", "setembro", "outubro", "novembro", "dezembro",
};
const char *const BR_MONTHS_SHORT[12] = {
    "jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez",
};

Day day_from_ymd(int y, int m, int d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (Day)(era * 146097 + (int)doe - 719468);
}

void day_to_ymd(Day day, int *y, int *m, int *d) {
    int z = day + 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int yy = (int)yoe + era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    const unsigned dd = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned mm = mp < 10 ? mp + 3 : mp - 9;
    if (y) *y = yy + (mm <= 2);
    if (m) *m = (int)mm;
    if (d) *d = (int)dd;
}

int day_year(Day d) { int y; day_to_ymd(d, &y, NULL, NULL); return y; }
int day_month(Day d) { int m; day_to_ymd(d, NULL, &m, NULL); return m; }
int day_dom(Day d) { int x; day_to_ymd(d, NULL, NULL, &x); return x; }

int day_weekday(Day d) {
    /* 1970-01-01 foi quinta-feira (4) */
    int w = (int)(((int64_t)d % 7 + 7) % 7); /* 0 = quinta */
    return (w + 3) % 7 + 1;
}

Ym ym_make(int y, int m) { return y * 12 + (m - 1); }
int ym_year(Ym ym) { return ym >= 0 ? ym / 12 : -((-ym + 11) / 12); }
int ym_month(Ym ym) { return ym - ym_year(ym) * 12 + 1; }
Ym day_ym(Day d) { int y, m; day_to_ymd(d, &y, &m, NULL); return ym_make(y, m); }

static gboolean leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int ym_len(Ym ym) {
    static const int L[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int m = ym_month(ym);
    return m == 2 && leap(ym_year(ym)) ? 29 : L[m - 1];
}

Day ym_first(Ym ym) { return day_from_ymd(ym_year(ym), ym_month(ym), 1); }
Day ym_last(Ym ym) { return day_from_ymd(ym_year(ym), ym_month(ym), ym_len(ym)); }

Day ym_day_clamped(Ym ym, int day) {
    int len = ym_len(ym);
    if (day < 1) day = 1;
    if (day > len) day = len;
    return day_from_ymd(ym_year(ym), ym_month(ym), day);
}

Day day_plus_months_clamped(Day d, int n) { return ym_day_clamped(day_ym(d) + n, day_dom(d)); }

Day day_today(void) {
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    return day_from_ymd(g_date_time_get_year(now), g_date_time_get_month(now), g_date_time_get_day_of_month(now));
}

gboolean day_is_valid_ymd(int y, int m, int d) {
    if (y < 1 || y > 9999 || m < 1 || m > 12 || d < 1) return FALSE;
    return d <= ym_len(ym_make(y, m));
}

static gboolean digits(const char *s, int n) {
    for (int i = 0; i < n; i++) if (!g_ascii_isdigit(s[i])) return FALSE;
    return TRUE;
}

gboolean day_parse_iso(const char *s, Day *out) {
    if (!s || strlen(s) != 10 || !digits(s, 4) || s[4] != '-' || !digits(s + 5, 2) || s[7] != '-' || !digits(s + 8, 2))
        return FALSE;
    int y = atoi(s), m = (s[5] - '0') * 10 + (s[6] - '0'), d = (s[8] - '0') * 10 + (s[9] - '0');
    if (!day_is_valid_ymd(y, m, d)) return FALSE;
    if (out) *out = day_from_ymd(y, m, d);
    return TRUE;
}

gboolean day_parse_br(const char *s, Day *out) {
    if (!s) return FALSE;
    while (g_ascii_isspace(*s)) s++;
    int d = 0, m = 0, y = 0, n = 0;
    char tail = 0;
    n = sscanf(s, "%2d/%2d/%4d%c", &d, &m, &y, &tail);
    if (n != 3) {
        /* aceita também o formato ISO */
        g_autofree char *t = g_strstrip(g_strdup(s));
        return day_parse_iso(t, out);
    }
    if (y < 100) return FALSE;
    if (!day_is_valid_ymd(y, m, d)) return FALSE;
    if (out) *out = day_from_ymd(y, m, d);
    return TRUE;
}

char *day_iso(Day d, char *buf) {
    int y, m, dd; day_to_ymd(d, &y, &m, &dd);
    g_snprintf(buf, 11, "%04d-%02d-%02d", y, m, dd);
    return buf;
}

char *day_br(Day d, char *buf) {
    int y, m, dd; day_to_ymd(d, &y, &m, &dd);
    g_snprintf(buf, 11, "%02d/%02d/%04d", dd, m, y);
    return buf;
}

char *day_br_short(Day d, char *buf) {
    int m, dd; day_to_ymd(d, NULL, &m, &dd);
    g_snprintf(buf, 6, "%02d/%02d", dd, m);
    return buf;
}

char *ym_iso(Ym ym, char *buf) {
    g_snprintf(buf, 8, "%04d-%02d", ym_year(ym), ym_month(ym));
    return buf;
}

gboolean ym_parse_iso(const char *s, Ym *out) {
    if (!s || strlen(s) != 7 || !digits(s, 4) || s[4] != '-' || !digits(s + 5, 2)) return FALSE;
    int y = atoi(s), m = (s[5] - '0') * 10 + (s[6] - '0');
    if (m < 1 || m > 12 || y < 1) return FALSE;
    if (out) *out = ym_make(y, m);
    return TRUE;
}

const char *br_month(Ym ym) { return BR_MONTHS[ym_month(ym) - 1]; }

char *br_month_year(Ym ym) { return g_strdup_printf("%s de %d", br_month(ym), ym_year(ym)); }

char *br_month_label(Ym ym) { return g_strdup_printf("%s de %d", BR_MONTHS_SHORT[ym_month(ym) - 1], ym_year(ym)); }

char *br_full_date(Day d) {
    int y, m, dd; day_to_ymd(d, &y, &m, &dd);
    /* primeira letra do mês maiúscula ("Outubro"); "março" → "Março" (UTF-8 seguro: só ASCII inicial) */
    const char *name = BR_MONTHS[m - 1];
    return g_strdup_printf("%02d de %c%s de %d", dd, g_ascii_toupper(name[0]), name + 1, y);
}
