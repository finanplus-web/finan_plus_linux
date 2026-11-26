/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "backup_crypto.h"
#include "backup.h"
#include <json-glib/json-glib.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <string.h>

#define SALT 16
#define IV 12
#define TAG 16
#define MIN_ITER 100000
#define MAX_ITER 5000000

static JsonObject *envelope(const char *text, gssize len, JsonParser **keep) {
    gsize n = len < 0 ? strlen(text) : (gsize)len;
    if (n >= 3 && (guchar)text[0] == 0xEF && (guchar)text[1] == 0xBB && (guchar)text[2] == 0xBF) { text += 3; n -= 3; }
    if (n > BACKUP_MAX_BYTES * 2) return NULL;
    JsonParser *p = json_parser_new_immutable();
    if (!json_parser_load_from_data(p, text, (gssize)n, NULL)) { g_object_unref(p); return NULL; }
    JsonNode *root = json_parser_get_root(p);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) { g_object_unref(p); return NULL; }
    *keep = p;
    return json_node_get_object(root);
}

static const char *ostr(JsonObject *o, const char *k) {
    if (!json_object_has_member(o, k)) return NULL;
    JsonNode *n = json_object_get_member(o, k);
    return JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == G_TYPE_STRING ? json_node_get_string(n) : NULL;
}

static gboolean oint(JsonObject *o, const char *k, gint64 *out) {
    if (!json_object_has_member(o, k)) return FALSE;
    JsonNode *n = json_object_get_member(o, k);
    if (!JSON_NODE_HOLDS_VALUE(n) || json_node_get_value_type(n) != G_TYPE_INT64) return FALSE;
    *out = json_node_get_int(n);
    return TRUE;
}

gboolean backup_crypto_is_encrypted(const char *text, gssize len) {
    if (!text) return FALSE;
    JsonParser *p = NULL;
    JsonObject *o = envelope(text, len, &p);
    gboolean r = o && g_strcmp0(ostr(o, "format"), BACKUP_CRYPTO_FORMAT) == 0;
    if (p) g_object_unref(p);
    return r;
}

static gboolean derive(const char *password, const guint8 *salt, int iter, guint8 key[32]) {
    return PKCS5_PBKDF2_HMAC(password, (int)strlen(password), salt, SALT, iter, EVP_sha256(), 32, key) == 1;
}

char *backup_crypto_encrypt(const char *plain, const char *password, int iter, GError **error) {
    if (iter <= 0) iter = BACKUP_CRYPTO_ITER;
    guint8 salt[SALT], iv[IV], key[32], tag[TAG];
    if (RAND_bytes(salt, SALT) != 1 || RAND_bytes(iv, IV) != 1 || !derive(password, salt, iter, key)) {
        g_set_error(error, BACKUP_ERROR, 20, "Não foi possível criptografar o backup");
        return NULL;
    }
    gsize n = strlen(plain);
    guint8 *out = g_malloc(n + TAG);
    int len = 0, fin = 0;
    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    gboolean ok = c && EVP_EncryptInit_ex(c, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
                  EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, IV, NULL) == 1 &&
                  EVP_EncryptInit_ex(c, NULL, NULL, key, iv) == 1 &&
                  EVP_EncryptUpdate(c, out, &len, (const guint8 *)plain, (int)n) == 1 &&
                  EVP_EncryptFinal_ex(c, out + len, &fin) == 1 &&
                  EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, TAG, tag) == 1;
    EVP_CIPHER_CTX_free(c);
    OPENSSL_cleanse(key, sizeof key);
    if (!ok) { g_free(out); g_set_error(error, BACKUP_ERROR, 20, "Não foi possível criptografar o backup"); return NULL; }
    /* como o Java: o texto cifrado seguido da tag de 16 bytes */
    memcpy(out + len + fin, tag, TAG);
    g_autofree char *s64 = g_base64_encode(salt, SALT), *i64 = g_base64_encode(iv, IV),
                    *d64 = g_base64_encode(out, (gsize)(len + fin) + TAG);
    g_free(out);
    return g_strdup_printf("{\"format\":\"%s\",\"v\":1,\"kdf\":\"PBKDF2-HMAC-SHA256\",\"iter\":%d,\"salt\":\"%s\","
                           "\"cipher\":\"AES-256-GCM\",\"iv\":\"%s\",\"data\":\"%s\"}",
                           BACKUP_CRYPTO_FORMAT, iter, s64, i64, d64);
}

static guint8 *b64(const char *s, gsize *n) {
    *n = 0;
    if (!s) return NULL;
    for (const char *p = s; *p; p++)
        if (!g_ascii_isalnum(*p) && *p != '+' && *p != '/' && *p != '=') return NULL;
    return g_base64_decode(s, n);
}

char *backup_crypto_decrypt(const char *text, gssize len, const char *password, GError **error) {
    JsonParser *p = NULL;
    JsonObject *o = text ? envelope(text, len, &p) : NULL;
    if (!o) { g_set_error(error, BACKUP_ERROR, 21, "Arquivo danificado"); return NULL; }
    char *result = NULL;
    gint64 v = 0, iter = 0;
    gsize ns = 0, ni = 0, nd = 0;
    guint8 *salt = NULL, *iv = NULL, *data = NULL;
    if (g_strcmp0(ostr(o, "format"), BACKUP_CRYPTO_FORMAT) != 0) { g_set_error(error, BACKUP_ERROR, 22, "Não é um backup criptografado do Finan+"); goto out; }
    if (!oint(o, "v", &v) || v != 1) { g_set_error(error, BACKUP_ERROR, 23, "Este backup foi feito por uma versão mais nova do Finan+"); goto out; }
    if (!oint(o, "iter", &iter) || iter < MIN_ITER || iter > MAX_ITER) { g_set_error(error, BACKUP_ERROR, 21, "Arquivo danificado"); goto out; }
    salt = b64(ostr(o, "salt"), &ns);
    iv = b64(ostr(o, "iv"), &ni);
    data = b64(ostr(o, "data"), &nd);
    if (ns != SALT || ni != IV || nd < TAG) { g_set_error(error, BACKUP_ERROR, 21, "Arquivo danificado"); goto out; }
    guint8 key[32];
    if (!derive(password ? password : "", salt, (int)iter, key)) { g_set_error(error, BACKUP_ERROR, 21, "Arquivo danificado"); goto out; }
    gsize clen = nd - TAG;
    char *plain = g_malloc(clen + 1);
    int l = 0, fin = 0;
    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    gboolean ok = c && EVP_DecryptInit_ex(c, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
                  EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, IV, NULL) == 1 &&
                  EVP_DecryptInit_ex(c, NULL, NULL, key, iv) == 1 &&
                  EVP_DecryptUpdate(c, (guint8 *)plain, &l, data, (int)clen) == 1 &&
                  EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, TAG, data + clen) == 1 &&
                  EVP_DecryptFinal_ex(c, (guint8 *)plain + l, &fin) == 1;
    EVP_CIPHER_CTX_free(c);
    OPENSSL_cleanse(key, sizeof key);
    if (!ok) { g_free(plain); g_set_error(error, BACKUP_ERROR, 24, "Senha incorreta ou arquivo danificado"); goto out; }
    plain[l + fin] = '\0';
    if (!g_utf8_validate(plain, l + fin, NULL)) { g_free(plain); g_set_error(error, BACKUP_ERROR, 21, "Arquivo danificado"); goto out; }
    result = plain;
out:
    g_free(salt); g_free(iv); g_free(data);
    g_object_unref(p);
    return result;
}
