/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "backup.h"
#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>

G_DEFINE_QUARK(finan-backup-error-quark, backup_error)

/* ------------------------------------------------------------------ leitura de valores soltos */

static gboolean is_value(JsonNode *n, GType t) {
    return n && JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == t;
}

static char *num_to_string(double d) {
    if (d == floor(d) && fabs(d) < 1e15) return g_strdup_printf("%" G_GINT64_FORMAT, (gint64)d);
    /* menor representação que volta ao mesmo número (como Double.toString do Kotlin) */
    static const char *const FMT[] = {"%.1g", "%.2g", "%.3g", "%.4g", "%.5g", "%.6g", "%.7g", "%.8g", "%.9g",
                                      "%.10g", "%.11g", "%.12g", "%.13g", "%.14g", "%.15g", "%.16g", "%.17g"};
    char buf[G_ASCII_DTOSTR_BUF_SIZE];
    for (guint i = 0; i < G_N_ELEMENTS(FMT); i++) {
        g_ascii_formatd(buf, sizeof buf, FMT[i], d);
        if (g_ascii_strtod(buf, NULL) == d) break;
    }
    return g_strdup(buf);
}

/* texto do campo (string ou número), aparado e limitado a [max] caracteres */
static char *jstr(JsonNode *n, int max) {
    g_autofree char *raw = NULL;
    if (is_value(n, G_TYPE_STRING)) raw = g_strdup(json_node_get_string(n));
    else if (is_value(n, G_TYPE_INT64)) raw = g_strdup_printf("%" G_GINT64_FORMAT, json_node_get_int(n));
    else if (is_value(n, G_TYPE_DOUBLE)) raw = num_to_string(json_node_get_double(n));
    else raw = g_strdup("");
    return str_clean(raw, max);
}

static gboolean jnum(JsonNode *n, double *out) {
    if (is_value(n, G_TYPE_INT64)) { *out = (double)json_node_get_int(n); return TRUE; }
    if (is_value(n, G_TYPE_DOUBLE)) { *out = json_node_get_double(n); return isfinite(*out); }
    if (is_value(n, G_TYPE_BOOLEAN)) { *out = json_node_get_boolean(n) ? 1 : 0; return TRUE; }
    if (is_value(n, G_TYPE_STRING)) {
        g_autofree char *t = g_strstrip(g_strdup(json_node_get_string(n)));
        if (!*t) return FALSE;
        char *end = NULL;
        double d = g_ascii_strtod(t, &end);
        if (!end || *end || !isfinite(d)) return FALSE;
        *out = d;
        return TRUE;
    }
    return FALSE;
}

static Cents jcents(JsonNode *n) {
    double d;
    return jnum(n, &d) ? money_from_reais(d) : 0;
}

static int jint_in(JsonNode *n, int a, int b, int def) {
    double d;
    if (!jnum(n, &d)) return def;
    double r = floor(d + 0.5);
    if (r < a || r > b) return def;
    return (int)r;
}

static gboolean jis_false(JsonNode *n) { return is_value(n, G_TYPE_BOOLEAN) && !json_node_get_boolean(n); }
static gboolean jis_true(JsonNode *n) { return is_value(n, G_TYPE_BOOLEAN) && json_node_get_boolean(n); }

static gboolean id_ok(const char *s) {
    size_t n = strlen(s);
    if (n < 1 || n > 48) return FALSE;
    for (const char *p = s; *p; p++)
        if (!g_ascii_isalnum(*p) && *p != '_' && *p != '.' && *p != '-') return FALSE;
    return TRUE;
}

static char *jsafe_id(JsonNode *n) {
    g_autofree char *s = NULL;
    if (is_value(n, G_TYPE_STRING)) s = g_strdup(json_node_get_string(n));
    else if (is_value(n, G_TYPE_INT64)) s = g_strdup_printf("%" G_GINT64_FORMAT, json_node_get_int(n));
    else if (is_value(n, G_TYPE_DOUBLE)) s = num_to_string(json_node_get_double(n));
    else return g_strdup("");
    return id_ok(s) ? g_steal_pointer(&s) : g_strdup("");
}

static Day jdate(JsonNode *n) {
    Day d;
    if (is_value(n, G_TYPE_STRING) && day_parse_iso(json_node_get_string(n), &d)) return d;
    return DAY_NONE;
}

static Ym jym(JsonNode *n) {
    Ym y;
    if (is_value(n, G_TYPE_STRING) && ym_parse_iso(json_node_get_string(n), &y)) return y;
    return YM_NONE;
}

static JsonObject *jobj(JsonNode *n) { return n && JSON_NODE_HOLDS_OBJECT(n) ? json_node_get_object(n) : NULL; }
static JsonArray *jarr(JsonNode *n) { return n && JSON_NODE_HOLDS_ARRAY(n) ? json_node_get_array(n) : NULL; }
static JsonNode *mem(JsonObject *o, const char *k) { return o && json_object_has_member(o, k) ? json_object_get_member(o, k) : NULL; }

/* Rejeita aninhamento excessivo antes de entregar o texto ao leitor de JSON. */
static gboolean depth_ok(const char *text, gsize len, int max) {
    int depth = 0;
    gboolean in_str = FALSE, esc = FALSE;
    for (gsize i = 0; i < len; i++) {
        char c = text[i];
        if (in_str) {
            if (esc) esc = FALSE;
            else if (c == '\\') esc = TRUE;
            else if (c == '"') in_str = FALSE;
            continue;
        }
        if (c == '"') in_str = TRUE;
        else if (c == '{' || c == '[') { if (++depth > max) return FALSE; }
        else if (c == '}' || c == ']') depth--;
    }
    return TRUE;
}

typedef struct {
    GHashTable *used;
} Ctx;

static char *id_for(Ctx *c, JsonNode *n) {
    char *id = jsafe_id(n);
    if (!*id || g_hash_table_contains(c->used, id)) { g_free(id); id = ids_new(); }
    g_hash_table_add(c->used, g_strdup(id));
    return id;
}

/* ------------------------------------------------------------------ normalização */

static AppState *normalize(JsonNode *root, Dropped *d, GError **error) {
    JsonObject *r = jobj(root);
    if (!r) { g_set_error(error, BACKUP_ERROR, 1, "Formato inválido"); return NULL; }
    JsonArray *raw_txs = jarr(mem(r, "txs"));
    if (!raw_txs) { g_set_error(error, BACKUP_ERROR, 2, "Backup sem lista de lançamentos"); return NULL; }

    Dropped dd = {0};
    Ctx c = {g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL)};
    AppState *s = app_state_new_empty();

    /* categorias */
    for (int k = 0; k < 2; k++) {
        JsonArray *list = jarr(mem(jobj(mem(r, "cats")), kind_json((Kind)k)));
        if (list) {
            GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
            for (guint i = 0; i < json_array_get_length(list); i++) {
                g_autofree char *n = jstr(json_array_get_element(list, i), 40);
                char *low = g_utf8_strdown(n, -1);
                if (*n && !g_hash_table_contains(seen, low)) {
                    g_hash_table_add(seen, low);
                    g_ptr_array_add(s->cats[k], g_steal_pointer(&n));
                } else g_free(low);
            }
            g_hash_table_unref(seen);
        }
        if (s->cats[k]->len == 0) {
            if (k == KIND_EXPENSE) for (guint i = 0; i < G_N_ELEMENTS(DEFAULT_EXPENSE); i++) g_ptr_array_add(s->cats[k], g_strdup(DEFAULT_EXPENSE[i]));
            else for (guint i = 0; i < G_N_ELEMENTS(DEFAULT_INCOME); i++) g_ptr_array_add(s->cats[k], g_strdup(DEFAULT_INCOME[i]));
        }
    }

    /* contas */
    JsonArray *accs = jarr(mem(r, "accounts"));
    for (guint i = 0; accs && i < json_array_get_length(accs); i++) {
        JsonObject *a = jobj(json_array_get_element(accs, i));
        g_autofree char *name = jstr(mem(a, "name"), 40);
        if (!a || !*name) { dd.accounts++; continue; }
        g_autofree char *id = id_for(&c, mem(a, "id"));
        g_ptr_array_add(s->accounts, account_new(id, name, jcents(mem(a, "initial"))));
    }
    if (s->accounts->len == 0) {
        g_hash_table_add(c.used, g_strdup(MAIN_ACCOUNT));
        g_ptr_array_add(s->accounts, account_new(MAIN_ACCOUNT, "Conta principal", 0));
    }
    const char *first_acc = ((Account *)s->accounts->pdata[0])->id;
#define ACC_OF(node) ({ g_autofree char *_v = jstr((node), 48); app_account(s, _v) ? g_strdup(_v) : g_strdup(first_acc); })
#define CARD_OF(node) ({ g_autofree char *_v = jstr((node), 48); app_card(s, _v) ? g_strdup(_v) : g_strdup(""); })

    /* cartões */
    JsonArray *cards = jarr(mem(r, "cards"));
    for (guint i = 0; cards && i < json_array_get_length(cards); i++) {
        JsonObject *o = jobj(json_array_get_element(cards, i));
        g_autofree char *name = jstr(mem(o, "name"), 40);
        if (!o || !*name) { dd.cards++; continue; }
        g_autofree char *id = id_for(&c, mem(o, "id"));
        g_ptr_array_add(s->cards, card_new(id, name, MAX(0, jcents(mem(o, "limit"))),
                                           jint_in(mem(o, "close"), 1, 31, 5), jint_in(mem(o, "due"), 1, 31, 12)));
    }

    /* recorrências */
    JsonArray *recs = jarr(mem(r, "recurring"));
    for (guint i = 0; recs && i < json_array_get_length(recs); i++) {
        JsonObject *o = jobj(json_array_get_element(recs, i));
        Kind kind;
        JsonNode *kn = mem(o, "kind");
        gboolean kind_ok = is_value(kn, G_TYPE_STRING) && kind_parse(json_node_get_string(kn), &kind);
        Cents value = jcents(mem(o, "value"));
        g_autofree char *desc = jstr(mem(o, "desc"), 120);
        if (!o || !kind_ok || value <= 0 || !*desc) { dd.recurring++; continue; }
        g_autofree char *id = id_for(&c, mem(o, "id"));
        g_autofree char *cat = jstr(mem(o, "category"), 40);
        g_autofree char *acc = ACC_OF(mem(o, "accountId"));
        g_autofree char *card = kind == KIND_EXPENSE ? CARD_OF(mem(o, "cardId")) : g_strdup("");
        g_ptr_array_add(s->recurring, recurring_new(id, kind, desc, value, *cat ? cat : app_first_category(s, kind), acc, card,
                                                    jint_in(mem(o, "day"), 1, 31, 1), !jis_false(mem(o, "active")),
                                                    jdate(mem(o, "start")), jym(mem(o, "last"))));
    }

    /* lançamentos */
    for (guint i = 0; i < json_array_get_length(raw_txs); i++) {
        JsonObject *o = jobj(json_array_get_element(raw_txs, i));
        Kind kind;
        JsonNode *kn = mem(o, "kind");
        gboolean kind_ok = is_value(kn, G_TYPE_STRING) && kind_parse(json_node_get_string(kn), &kind);
        Cents value = jcents(mem(o, "value"));
        Day date = jdate(mem(o, "date"));
        if (!o || !kind_ok || value <= 0 || date == DAY_NONE) { dd.txs++; continue; }
        g_autofree char *card_id = kind == KIND_EXPENSE ? CARD_OF(mem(o, "cardId")) : g_strdup("");
        g_autofree char *card_pay = (kind == KIND_EXPENSE && !*card_id) ? CARD_OF(mem(o, "cardPayment")) : g_strdup("");
        JsonObject *parcel = jobj(mem(o, "parcel"));
        int ptotal = parcel ? jint_in(mem(parcel, "total"), 1, 120, 0) : 0;
        int pn = parcel ? jint_in(mem(parcel, "n"), 1, 120, 0) : 0;
        gboolean pok = ptotal > 0 && pn >= 1 && pn <= ptotal;
        g_autofree char *id = id_for(&c, mem(o, "id"));
        g_autofree char *desc = jstr(mem(o, "desc"), 200);
        g_autofree char *cat = jstr(mem(o, "category"), 40);
        g_autofree char *acc = ACC_OF(mem(o, "accountId"));
        Tx *t = tx_new(id, kind, value, date, *desc ? desc : "Sem descrição",
                       *cat ? cat : (*card_pay ? CARD_PAYMENT_CAT : "Outros"),
                       *card_id ? TRUE : !jis_false(mem(o, "paid")), acc, card_id);
        tx_set_str(&t->card_payment, card_pay);
        g_autofree char *rid = jsafe_id(mem(o, "recurringId"));
        g_autofree char *gid = jsafe_id(mem(o, "groupId"));
        tx_set_str(&t->recurring_id, rid);
        tx_set_str(&t->group_id, gid);
        t->parcel_n = pok ? pn : 0;
        t->parcel_total = pok ? ptotal : 0;
        g_ptr_array_add(s->txs, t);
    }

    /* metas */
    JsonArray *goals = jarr(mem(r, "goals"));
    for (guint i = 0; goals && i < json_array_get_length(goals); i++) {
        JsonObject *g = jobj(json_array_get_element(goals, i));
        Cents target = jcents(mem(g, "target"));
        g_autofree char *name = jstr(mem(g, "name"), 60);
        if (!g || !*name || target <= 0) { dd.goals++; continue; }
        g_autofree char *id = id_for(&c, mem(g, "id"));
        g_ptr_array_add(s->goals, goal_new(id, name, target, MAX(0, jcents(mem(g, "saved"))), jdate(mem(g, "deadline")),
                                           MAX(0, jcents(mem(g, "monthly")))));
    }

    /* limites (a mesma categoria repetida fica com o último valor, na posição original) */
    JsonObject *lim = jobj(mem(r, "limits"));
    if (lim) {
        GList *keys = json_object_get_members(lim);
        for (GList *l = keys; l; l = l->next) {
            g_autofree char *cat = str_clean(l->data, 40);
            Cents n = jcents(json_object_get_member(lim, l->data));
            if (!*cat || n <= 0) continue;
            gboolean found = FALSE;
            for (guint i = 0; i < s->limits->len; i++) {
                Limit *x = s->limits->pdata[i];
                if (strcmp(x->category, cat) == 0) { x->value = n; found = TRUE; break; }
            }
            if (!found) g_ptr_array_add(s->limits, limit_new(cat, n));
        }
        g_list_free(keys);
    }

    double al;
    int auto_lock = 0;
    if (jnum(mem(r, "autoLock"), &al)) {
        int v = (int)floor(al + 0.5);
        for (guint i = 0; i < G_N_ELEMENTS(AUTOLOCK_OPTIONS); i++) if (AUTOLOCK_OPTIONS[i] == v) auto_lock = v;
    }
    s->auto_lock = auto_lock;
    s->privacy = jis_true(mem(r, "privacy"));
    JsonNode *th = mem(r, "theme");
    s->theme = theme_parse(is_value(th, G_TYPE_STRING) ? json_node_get_string(th) : NULL);

#undef ACC_OF
#undef CARD_OF
    g_hash_table_unref(c.used);
    if (d) *d = dd;
    return s;
}

AppState *backup_parse(const char *text, gssize len, Dropped *dropped, GError **error) {
    if (!text) { g_set_error(error, BACKUP_ERROR, 3, "Arquivo vazio"); return NULL; }
    gsize n = len < 0 ? strlen(text) : (gsize)len;
    if (n > BACKUP_MAX_BYTES) { g_set_error(error, BACKUP_ERROR, 4, "Arquivo grande demais"); return NULL; }
    /* pula o BOM UTF-8, se houver */
    if (n >= 3 && (guchar)text[0] == 0xEF && (guchar)text[1] == 0xBB && (guchar)text[2] == 0xBF) { text += 3; n -= 3; }
    if (!g_utf8_validate(text, (gssize)n, NULL)) { g_set_error(error, BACKUP_ERROR, 5, "JSON inválido: texto não é UTF-8"); return NULL; }
    if (!depth_ok(text, n, 64)) { g_set_error(error, BACKUP_ERROR, 6, "JSON inválido: aninhamento excessivo"); return NULL; }
    g_autoptr(JsonParser) p = json_parser_new_immutable();
    g_autoptr(GError) e = NULL;
    if (!json_parser_load_from_data(p, text, (gssize)n, &e)) {
        g_set_error(error, BACKUP_ERROR, 7, "JSON inválido: %s", e->message);
        return NULL;
    }
    return normalize(json_parser_get_root(p), dropped, error);
}

/* ------------------------------------------------------------------ escrita */

static void js(GString *o, const char *s) {
    g_string_append_c(o, '"');
    for (const guchar *p = (const guchar *)s; *p; p++) {
        switch (*p) {
        case '"': g_string_append(o, "\\\""); break;
        case '\\': g_string_append(o, "\\\\"); break;
        case '\n': g_string_append(o, "\\n"); break;
        case '\r': g_string_append(o, "\\r"); break;
        case '\t': g_string_append(o, "\\t"); break;
        default:
            if (*p < 0x20) g_string_append_printf(o, "\\u%04x", *p);
            else g_string_append_c(o, (char)*p);
        }
    }
    g_string_append_c(o, '"');
}

static void jk(GString *o, const char *key, gboolean comma) {
    if (comma) g_string_append_c(o, ',');
    js(o, key);
    g_string_append_c(o, ':');
}

static void jkm(GString *o, const char *key, Cents c) {
    char b[32];
    jk(o, key, TRUE);
    g_string_append(o, money_reais_json(c, b));
}

static void jks(GString *o, const char *key, const char *v) { jk(o, key, TRUE); js(o, v); }

char *backup_to_json(const AppState *s, const char *app_version) {
    GString *o = g_string_sized_new(4096 + s->txs->len * 220);
    char b[32];
    g_string_append(o, "{\"txs\":[");
    for (guint i = 0; i < s->txs->len; i++) {
        Tx *t = s->txs->pdata[i];
        if (i) g_string_append_c(o, ',');
        g_string_append(o, "{\"id\":"); js(o, t->id);
        jks(o, "kind", kind_json(t->kind));
        jkm(o, "value", t->value);
        jks(o, "date", day_iso(t->date, b));
        jks(o, "desc", t->desc);
        jks(o, "category", t->category);
        jk(o, "paid", TRUE); g_string_append(o, t->paid ? "true" : "false");
        jks(o, "accountId", t->account_id);
        jks(o, "cardId", t->card_id);
        if (t->card_payment[0]) jks(o, "cardPayment", t->card_payment);
        if (t->recurring_id[0]) jks(o, "recurringId", t->recurring_id);
        if (t->group_id[0]) jks(o, "groupId", t->group_id);
        if (t->parcel_total > 0) g_string_append_printf(o, ",\"parcel\":{\"n\":%d,\"total\":%d}", t->parcel_n, t->parcel_total);
        g_string_append_c(o, '}');
    }
    g_string_append(o, "],\"goals\":[");
    for (guint i = 0; i < s->goals->len; i++) {
        Goal *g = s->goals->pdata[i];
        if (i) g_string_append_c(o, ',');
        g_string_append(o, "{\"id\":"); js(o, g->id);
        jks(o, "name", g->name);
        jkm(o, "target", g->target);
        jkm(o, "saved", g->saved);
        jks(o, "deadline", g->deadline == DAY_NONE ? "" : day_iso(g->deadline, b));
        jkm(o, "monthly", g->monthly);
        g_string_append_c(o, '}');
    }
    g_string_append(o, "],\"accounts\":[");
    for (guint i = 0; i < s->accounts->len; i++) {
        Account *a = s->accounts->pdata[i];
        if (i) g_string_append_c(o, ',');
        g_string_append(o, "{\"id\":"); js(o, a->id);
        jks(o, "name", a->name);
        jkm(o, "initial", a->initial);
        g_string_append_c(o, '}');
    }
    g_string_append(o, "],\"cards\":[");
    for (guint i = 0; i < s->cards->len; i++) {
        Card *c = s->cards->pdata[i];
        if (i) g_string_append_c(o, ',');
        g_string_append(o, "{\"id\":"); js(o, c->id);
        jks(o, "name", c->name);
        jkm(o, "limit", c->limit);
        g_string_append_printf(o, ",\"close\":%d,\"due\":%d}", c->close, c->due);
    }
    g_string_append(o, "],\"recurring\":[");
    for (guint i = 0; i < s->recurring->len; i++) {
        Recurring *r = s->recurring->pdata[i];
        if (i) g_string_append_c(o, ',');
        g_string_append(o, "{\"id\":"); js(o, r->id);
        jks(o, "kind", kind_json(r->kind));
        jks(o, "desc", r->desc);
        jkm(o, "value", r->value);
        jks(o, "category", r->category);
        jks(o, "accountId", r->account_id);
        jks(o, "cardId", r->card_id);
        g_string_append_printf(o, ",\"day\":%d,\"active\":%s", r->day, r->active ? "true" : "false");
        jks(o, "start", r->start == DAY_NONE ? "" : day_iso(r->start, b));
        jks(o, "last", r->last == YM_NONE ? "" : ym_iso(r->last, b));
        g_string_append_c(o, '}');
    }
    g_string_append(o, "],\"cats\":{");
    for (int k = 1; k >= 0; k--) { /* "expense" primeiro, como o Finan+ web */
        jk(o, kind_json((Kind)k), k == 0);
        g_string_append_c(o, '[');
        for (guint i = 0; i < s->cats[k]->len; i++) {
            if (i) g_string_append_c(o, ',');
            js(o, s->cats[k]->pdata[i]);
        }
        g_string_append_c(o, ']');
    }
    g_string_append(o, "},\"limits\":{");
    for (guint i = 0; i < s->limits->len; i++) {
        Limit *l = s->limits->pdata[i];
        jk(o, l->category, i > 0);
        g_string_append(o, money_reais_json(l->value, b));
    }
    g_string_append_printf(o, "},\"privacy\":%s,\"autoLock\":%d", s->privacy ? "true" : "false", s->auto_lock);
    jks(o, "theme", theme_json(s->theme));
    g_string_append_printf(o, ",\"backupVersion\":%d", BACKUP_VERSION);
    if (app_version) {
        g_autoptr(GDateTime) now = g_date_time_new_now_utc();
        g_autofree char *iso = g_date_time_format_iso8601(now);
        g_string_append(o, ",\"_backup\":{\"app\":\"Finan+\"");
        g_string_append_printf(o, ",\"version\":%d", BACKUP_VERSION);
        jks(o, "appVersion", app_version);
        jks(o, "platform", "linux");
        jks(o, "createdAt", iso);
        g_string_append_c(o, '}');
    }
    g_string_append_c(o, '}');
    return g_string_free(o, FALSE);
}
