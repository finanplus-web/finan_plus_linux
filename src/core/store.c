/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "store.h"
#include <glib/gstdio.h>
#include <libsecret/secret.h>
#include <sodium.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

G_DEFINE_QUARK(finan-store-error-quark, store_error)

/* Cabeçalho do arquivo: identifica o formato e entra como dado autenticado na cifra. */
static const char MAGIC[8] = {'F', 'I', 'N', 'A', 'N', '+', '1', '\n'};
#define NONCE_LEN crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
#define KEY_LEN crypto_aead_xchacha20poly1305_ietf_KEYBYTES
#define TAG_LEN crypto_aead_xchacha20poly1305_ietf_ABYTES

struct Store {
    char *dir;
    char *data_path;
    char *key_path;
    guint8 *key; /* sodium_malloc: memória protegida, zerada ao liberar */
    KeySource source;
    gboolean have_key;
    /* o arquivo existente foi aberto com sucesso nesta sessão (ou não havia arquivo): só então pode ser regravado */
    gboolean verified;
};

static const SecretSchema *key_schema(void) {
    static const SecretSchema schema = {
        .name = "com.finanplus.FinanPlus.ChaveDados",
        .flags = SECRET_SCHEMA_NONE,
        .attributes = {{"app", SECRET_SCHEMA_ATTRIBUTE_STRING}, {"pasta", SECRET_SCHEMA_ATTRIBUTE_STRING}, {NULL, 0}},
    };
    return &schema;
}

/* FINAN_PLUS_NO_KEYRING=1 desliga o chaveiro (usado pelos testes, para nunca tocar no chaveiro real) */
static gboolean use_keyring(void) {
    const char *v = g_getenv("FINAN_PLUS_NO_KEYRING");
    return !(v && *v && strcmp(v, "0") != 0);
}

static gboolean decode_key(const char *b64, guint8 *out) {
    gsize n = 0;
    g_autofree guchar *raw = g_base64_decode(b64, &n);
    if (n != KEY_LEN) { if (raw) sodium_memzero(raw, n); return FALSE; }
    memcpy(out, raw, KEY_LEN);
    sodium_memzero(raw, n);
    return TRUE;
}

/* grava um arquivo de forma atômica, com permissão 0600 */
static gboolean write_atomic(const char *path, const guint8 *data, gsize len, GError **error) {
    g_autofree char *tmp = g_strdup_printf("%s.tmp-XXXXXX", path);
    int fd = g_mkstemp_full(tmp, O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) {
        g_set_error(error, STORE_ERROR, STORE_ERROR_IO, "Não foi possível criar %s: %s", tmp, g_strerror(errno));
        return FALSE;
    }
    gsize done = 0;
    while (done < len) {
        ssize_t w = write(fd, data + done, len - done);
        if (w < 0) {
            if (errno == EINTR) continue;
            int e = errno;
            close(fd);
            g_unlink(tmp);
            g_set_error(error, STORE_ERROR, STORE_ERROR_IO, "Erro ao gravar: %s", g_strerror(e));
            return FALSE;
        }
        done += (gsize)w;
    }
    if (fsync(fd) != 0 || close(fd) != 0) {
        int e = errno;
        g_unlink(tmp);
        g_set_error(error, STORE_ERROR, STORE_ERROR_IO, "Erro ao gravar: %s", g_strerror(e));
        return FALSE;
    }
    if (g_rename(tmp, path) != 0) {
        int e = errno;
        g_unlink(tmp);
        g_set_error(error, STORE_ERROR, STORE_ERROR_IO, "Erro ao gravar: %s", g_strerror(e));
        return FALSE;
    }
    /* garante que a renomeação chegou ao disco */
    g_autofree char *dir = g_path_get_dirname(path);
    int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) { fsync(dfd); close(dfd); }
    return TRUE;
}

static gboolean load_or_create_key(Store *st, GError **error) {
    st->have_key = FALSE;
    g_autoptr(GError) kerr = NULL;
    /* 1. chaveiro do sistema (uma chave por pasta de dados) */
    g_autofree char *b64 = use_keyring() ? secret_password_lookup_sync(key_schema(), NULL, &kerr, "app", "finan-plus", "pasta", st->dir, NULL) : NULL;
    if (b64 && decode_key(b64, st->key)) {
        secret_password_wipe(g_steal_pointer(&b64));
        st->source = KEY_SOURCE_KEYRING;
        st->have_key = TRUE;
        return TRUE;
    }
    if (b64) secret_password_wipe(g_steal_pointer(&b64));
    /* 2. arquivo de chave (quando não há chaveiro) */
    g_autofree char *fb64 = NULL;
    gsize flen = 0;
    if (g_file_get_contents(st->key_path, &fb64, &flen, NULL)) {
        g_strstrip(fb64);
        gboolean ok = decode_key(fb64, st->key);
        sodium_memzero(fb64, flen);
        if (ok) { st->source = KEY_SOURCE_FILE; st->have_key = TRUE; return TRUE; }
        g_set_error(error, STORE_ERROR, STORE_ERROR_UNREADABLE, "O arquivo de chave %s está danificado.", st->key_path);
        return FALSE;
    }
    /* 3. primeira vez: chave nova. Só cria se ainda não houver dados (senão os dados ficariam ilegíveis). */
    if (g_file_test(st->data_path, G_FILE_TEST_EXISTS)) {
        g_set_error(error, STORE_ERROR, STORE_ERROR_UNREADABLE,
                    "Há dados salvos, mas a chave não foi encontrada no chaveiro do sistema%s.",
                    kerr ? " (chaveiro indisponível)" : "");
        return FALSE;
    }
    randombytes_buf(st->key, KEY_LEN);
    g_autofree char *nb64 = g_base64_encode(st->key, KEY_LEN);
    g_autoptr(GError) serr = NULL;
    if (use_keyring() && secret_password_store_sync(key_schema(), SECRET_COLLECTION_DEFAULT, "Finan+ — chave dos dados", nb64, NULL,
                                                    &serr, "app", "finan-plus", "pasta", st->dir, NULL)) {
        st->source = KEY_SOURCE_KEYRING;
    } else {
        if (!write_atomic(st->key_path, (const guint8 *)nb64, strlen(nb64), error)) { sodium_memzero(nb64, strlen(nb64)); return FALSE; }
        st->source = KEY_SOURCE_FILE;
    }
    sodium_memzero(nb64, strlen(nb64));
    st->have_key = TRUE;
    st->verified = TRUE; /* chave nova: não havia dados */
    return TRUE;
}

Store *store_open(const char *dir, GError **error) {
    if (sodium_init() < 0) {
        g_set_error(error, STORE_ERROR, STORE_ERROR_NO_CRYPTO, "Não foi possível iniciar a biblioteca de criptografia.");
        return NULL;
    }
    Store *st = g_new0(Store, 1);
    const char *env = g_getenv("FINAN_PLUS_DATA_DIR");
    st->dir = g_strdup(dir ? dir : env && *env ? env : NULL);
    if (!st->dir) st->dir = g_build_filename(g_get_user_data_dir(), "finan-plus", NULL);
    st->data_path = g_build_filename(st->dir, "dados.fin", NULL);
    st->key_path = g_build_filename(st->dir, "chave", NULL);
    st->key = sodium_malloc(KEY_LEN);
    if (g_mkdir_with_parents(st->dir, 0700) != 0) {
        g_set_error(error, STORE_ERROR, STORE_ERROR_IO, "Não foi possível criar a pasta %s: %s", st->dir, g_strerror(errno));
        store_free(st);
        return NULL;
    }
    g_chmod(st->dir, 0700);
    if (!st->key || !load_or_create_key(st, error)) {
        /* mantém o Store aberto sem chave? Não: quem chama mostra o problema e pode colocar o arquivo de lado. */
        if (error && *error && (*error)->code == STORE_ERROR_UNREADABLE) {
            /* devolve o Store mesmo assim para permitir store_quarantine(); a chave fica zerada */
            sodium_memzero(st->key, KEY_LEN);
            return st;
        }
        store_free(st);
        return NULL;
    }
    return st;
}

void store_free(Store *st) {
    if (!st) return;
    if (st->key) sodium_free(st->key);
    g_free(st->dir);
    g_free(st->data_path);
    g_free(st->key_path);
    g_free(st);
}

KeySource store_key_source(const Store *st) { return st->source; }
const char *store_dir(const Store *st) { return st->dir; }
const char *store_data_path(const Store *st) { return st->data_path; }

GBytes *store_seal(Store *st, const char *plain, gsize len) {
    gsize total = sizeof MAGIC + NONCE_LEN + len + TAG_LEN;
    guint8 *buf = g_malloc(total);
    memcpy(buf, MAGIC, sizeof MAGIC);
    guint8 *nonce = buf + sizeof MAGIC;
    randombytes_buf(nonce, NONCE_LEN);
    unsigned long long clen = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(nonce + NONCE_LEN, &clen, (const guint8 *)plain, len,
                                               (const guint8 *)MAGIC, sizeof MAGIC, NULL, nonce, st->key);
    return g_bytes_new_take(buf, sizeof MAGIC + NONCE_LEN + clen);
}

char *store_open_sealed(Store *st, const guint8 *data, gsize len, gsize *out_len) {
    if (len < sizeof MAGIC + NONCE_LEN + TAG_LEN || memcmp(data, MAGIC, sizeof MAGIC) != 0) return NULL;
    if (!st->have_key) return NULL;
    const guint8 *nonce = data + sizeof MAGIC;
    const guint8 *c = nonce + NONCE_LEN;
    gsize clen = len - sizeof MAGIC - NONCE_LEN;
    char *plain = g_malloc(clen - TAG_LEN + 1);
    unsigned long long plen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt((guint8 *)plain, &plen, NULL, c, clen, (const guint8 *)MAGIC, sizeof MAGIC,
                                                   nonce, st->key) != 0) {
        g_free(plain);
        return NULL;
    }
    plain[plen] = '\0';
    if (out_len) *out_len = (gsize)plen;
    return plain;
}

AppState *store_load(Store *st, Dropped *dropped, GError **error) {
    g_autofree char *raw = NULL;
    gsize len = 0;
    g_autoptr(GError) e = NULL;
    if (!g_file_get_contents(st->data_path, &raw, &len, &e)) {
        if (g_error_matches(e, G_FILE_ERROR, G_FILE_ERROR_NOENT)) { st->verified = TRUE; return NULL; } /* primeiro uso */
        g_set_error(error, STORE_ERROR, STORE_ERROR_IO, "Não foi possível ler %s: %s", st->data_path, e->message);
        return NULL;
    }
    gsize plen = 0;
    g_autofree char *plain = store_open_sealed(st, (const guint8 *)raw, len, &plen);
    if (!plain) {
        g_set_error(error, STORE_ERROR, STORE_ERROR_UNREADABLE,
                    "Os dados salvos não puderam ser abertos com a chave deste computador (chave diferente ou arquivo danificado).");
        return NULL;
    }
    g_autoptr(GError) be = NULL;
    AppState *s = backup_parse(plain, (gssize)plen, dropped, &be);
    sodium_memzero(plain, plen);
    if (!s) {
        g_set_error(error, STORE_ERROR, STORE_ERROR_UNREADABLE, "Os dados salvos estão em um formato inválido: %s", be->message);
        return NULL;
    }
    st->verified = TRUE;
    return s;
}

gboolean store_save(Store *st, const AppState *s, GError **error) {
    if (!st->have_key) {
        /* sem a chave certa, gravar destruiria os dados existentes */
        g_set_error(error, STORE_ERROR, STORE_ERROR_UNREADABLE, "Sem a chave dos dados: nada foi gravado.");
        return FALSE;
    }
    if (!st->verified && g_file_test(st->data_path, G_FILE_TEST_EXISTS)) {
        /* nunca sobrescreve um arquivo que não conseguimos abrir (chave errada ou arquivo danificado) */
        g_set_error(error, STORE_ERROR, STORE_ERROR_UNREADABLE, "Os dados salvos não foram abertos; nada foi gravado.");
        return FALSE;
    }
    g_autofree char *json = backup_to_json(s, NULL);
    gsize n = strlen(json);
    g_autoptr(GBytes) sealed = store_seal(st, json, n);
    sodium_memzero(json, n);
    gsize len = 0;
    const guint8 *data = g_bytes_get_data(sealed, &len);
    /* guarda a versão anterior (uma geração) */
    if (g_file_test(st->data_path, G_FILE_TEST_EXISTS)) {
        g_autofree char *prev = g_strdup_printf("%s.anterior", st->data_path);
        g_unlink(prev);
        if (link(st->data_path, prev) != 0) { /* sem hard link: tudo bem, segue sem a cópia */ }
    }
    return write_atomic(st->data_path, data, len, error);
}

char *store_quarantine(Store *st, GError **error) {
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    g_autofree char *stamp = g_date_time_format(now, "%Y%m%d-%H%M%S");
    char *dest = g_strdup_printf("%s.ilegivel-%s", st->data_path, stamp);
    if (g_rename(st->data_path, dest) != 0) {
        g_set_error(error, STORE_ERROR, STORE_ERROR_IO, "Não foi possível mover o arquivo: %s", g_strerror(errno));
        g_free(dest);
        return NULL;
    }
    /* a cópia anterior foi gravada com a mesma chave: vai junto, para não ser sobrescrita */
    g_autofree char *prev = g_strdup_printf("%s.anterior", st->data_path);
    if (g_file_test(prev, G_FILE_TEST_EXISTS)) {
        g_autofree char *pdest = g_strdup_printf("%s.anterior", dest);
        g_rename(prev, pdest);
    }
    st->verified = TRUE;
    /* prepara a chave para os próximos dados; se o arquivo de chave estiver danificado, ele vai junto da cópia */
    g_autoptr(GError) e = NULL;
    if (!load_or_create_key(st, &e) && g_file_test(st->key_path, G_FILE_TEST_EXISTS)) {
        g_autofree char *kdest = g_strdup_printf("%s.chave", dest);
        g_rename(st->key_path, kdest);
        g_clear_error(&e);
        load_or_create_key(st, &e);
    }
    return dest;
}

gboolean store_wipe(Store *st, GError **error) {
    g_autofree char *prev = g_strdup_printf("%s.anterior", st->data_path);
    if (g_file_test(st->data_path, G_FILE_TEST_EXISTS) && g_unlink(st->data_path) != 0) {
        g_set_error(error, STORE_ERROR, STORE_ERROR_IO, "Não foi possível apagar %s: %s", st->data_path, g_strerror(errno));
        return FALSE;
    }
    g_unlink(prev);
    g_unlink(st->key_path);
    st->verified = TRUE;
    if (use_keyring()) secret_password_clear_sync(key_schema(), NULL, NULL, "app", "finan-plus", "pasta", st->dir, NULL);
    /* nova chave para os próximos dados */
    g_autoptr(GError) e = NULL;
    load_or_create_key(st, &e);
    return TRUE;
}
