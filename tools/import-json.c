/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Ferramenta de desenvolvimento (não é instalada): grava um backup JSON no armazenamento
 * criptografado, para preparar dados de demonstração.  Uso: import-json arquivo.json
 */
#include "core/backup.h"
#include "core/store.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "uso: %s backup.json\n", argv[0]); return 2; }
    g_autofree char *text = NULL;
    gsize len = 0;
    g_autoptr(GError) e = NULL;
    if (!g_file_get_contents(argv[1], &text, &len, &e)) { fprintf(stderr, "%s\n", e->message); return 1; }
    Dropped d;
    AppState *s = backup_parse(text, (gssize)len, &d, &e);
    if (!s) { fprintf(stderr, "%s\n", e->message); return 1; }
    Store *st = store_open(NULL, &e);
    if (!st || e) { fprintf(stderr, "%s\n", e ? e->message : "erro"); return 1; }
    if (!store_save(st, s, &e)) { fprintf(stderr, "%s\n", e->message); return 1; }
    printf("%u lançamentos gravados em %s (%d descartados)\n", s->txs->len, store_data_path(st), dropped_total(d));
    store_free(st);
    app_state_free(s);
    return 0;
}
