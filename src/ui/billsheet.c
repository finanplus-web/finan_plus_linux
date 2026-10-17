/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * "Ler fatura ou boleto" (FATURAS.md): escolher o PDF ou colar o código, digitar a senha se o PDF tiver,
 * conferir o que foi encontrado e salvar. Nada é lançado sem a pessoa confirmar. Mesmo fluxo do
 * app Android e da versão web. O PDF é lido numa thread à parte, só na memória.
 */
#include "pages.h"
#include "widgets.h"
#include "core/bill.h"
#include "core/finance.h"
#include <string.h>

typedef struct {
    AdwDialog *dialog;
    GtkWidget *stack;
    char *card_id;          /* "" = boleto ou conta */
    GBytes *pdf;            /* o arquivo escolhido, até ser lido (depois é descartado) */
    gboolean vault_failed;  /* a senha lembrada não abriu esta fatura */
    gboolean remember;
    gboolean busy;
    /* escolher */
    GtkWidget *card_row, *code;
    /* senha */
    GtkWidget *pw_row, *pw_err, *remember_row, *pw_card_box;
    /* conferir */
    GtkWidget *res_box;
    GtkWidget *desc, *value, *due, *cat, *account, *paid, *cmp, *cmp_hint, *card_act;
    GPtrArray *cats;
} BillEd;

static void bill_ed_free(BillEd *e) {
    g_free(e->card_id);
    if (e->pdf) g_bytes_unref(e->pdf);
    if (e->cats) g_ptr_array_unref(e->cats);
    g_free(e);
}

static const Card *ed_card(BillEd *e) { return e->card_id && *e->card_id ? app_card(APP->state, e->card_id) : NULL; }
static gboolean can_remember(void) { return APP->store != NULL; }

static void show(BillEd *e, const char *page) { gtk_stack_set_visible_child_name(GTK_STACK(e->stack), page); }

static GtkWidget *group(GtkWidget *box, const char *title) {
    GtkWidget *g = adw_preferences_group_new();
    if (title) adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(g), title);
    w_add(box, g);
    return g;
}
static void gadd(GtkWidget *g, GtkWidget *row) { adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), row); }

static GtkWidget *note(const char *icon, const char *text, const char *classes) {
    GtkWidget *b = w_hbox(8);
    GtkWidget *i = w_icon(icon, 16);
    gtk_widget_set_valign(i, GTK_ALIGN_START);
    w_add(b, i);
    GtkWidget *l = w_label_wrap(text, classes);
    gtk_widget_set_hexpand(l, TRUE);
    w_add(b, l);
    return b;
}

/* ---------------------------------------------------------------- conferir e salvar */
static void close_ed(BillEd *e) { adw_dialog_force_close(e->dialog); }

static void save_result(BillEd *e, gboolean ok, const OpErr *err, const char *toast) {
    if (!ok) { dlg_notice(err->title, err->msg); return; }
    app_commit();
    close_ed(e);
    app_toast("%s", toast);
}

static void compare_update(BillEd *e) {
    const Card *c = ed_card(e);
    if (!c || !e->cmp) return;
    Day due = DAY_NONE;
    if (!row_date_get(e->due, &due)) due = DAY_NONE;
    Ym ym = bill_invoice_ym(APP->state, c, due, APP->today);
    Cents app = bill_app_total(APP->state, c, ym), bank = 0;
    gboolean has = money_parse(row_text(e->value), &bank);
    g_autofree char *mes = br_month_label(ym), *a = app_money(app), *b = has ? app_money(bank) : NULL;
    g_autofree char *verdict = NULL;
    Cents diff = bank - app;
    if (!has) verdict = g_strdup("Informe o valor para comparar.");
    else if (diff == 0) verdict = g_strdup("Confere com as compras lançadas.");
    else if (diff > 0 && app == 0) verdict = g_strdup("Nenhuma compra deste cartão lançada nesta fatura no Finan+.");
    else if (diff > 0) { g_autofree char *d = app_money(diff); verdict = g_strdup_printf("Faltam %s em compras no Finan+.", d); }
    else { g_autofree char *d = app_money(-diff); verdict = g_strdup_printf("O Finan+ tem %s a mais do que o banco. Confira as compras do cartão.", d); }
    g_autofree char *txt = g_strdup_printf("Fatura de %s · %s\nNo Finan+: %s%s%s\n%s", mes, c->name, a, has ? "\nNo PDF: " : "", has ? b : "", verdict);
    gtk_label_set_text(GTK_LABEL(e->cmp), txt);
    gboolean add = has && diff > 0;
    gtk_button_set_label(GTK_BUTTON(e->card_act), !add ? "Fechar" : app == 0 ? "Lançar a fatura no cartão" : "Lançar a diferença no cartão");
    g_autofree char *hint = add ? g_strdup_printf("Entra como uma compra na fatura de %s, para o Finan+ bater com o banco.", mes) : g_strdup("");
    gtk_label_set_text(GTK_LABEL(e->cmp_hint), hint);
    gtk_widget_set_visible(e->cmp_hint, add);
}
static void on_cmp_changed(GtkEditable *ed, BillEd *e) { (void)ed; compare_update(e); }

static void card_action(GtkButton *btn, BillEd *e) {
    (void)btn;
    const Card *c = ed_card(e);
    if (!c) return;
    Day due = DAY_NONE;
    if (!row_date_get(e->due, &due)) due = DAY_NONE;
    Ym ym = bill_invoice_ym(APP->state, c, due, APP->today);
    Cents app = bill_app_total(APP->state, c, ym), bank = 0;
    if (!money_parse(row_text(e->value), &bank) || bank - app <= 0) { close_ed(e); return; }
    char vb[32];
    money_input(bank - app, vb);
    g_autofree char *mes = br_month_label(ym);
    g_autofree char *desc = app == 0 ? g_strdup(*row_text(e->desc) ? row_text(e->desc) : c->name) : g_strdup_printf("Diferença da fatura %s (%s)", c->name, mes);
    const char *cat = app_first_category(APP->state, KIND_EXPENSE);
    GPtrArray *ex = APP->state->cats[KIND_EXPENSE];
    for (guint i = 0; i < ex->len; i++) if (g_ascii_strcasecmp(ex->pdata[i], "Outros") == 0) cat = ex->pdata[i];
    const Account *acc = g_ptr_array_index(APP->state->accounts, 0);
    TxDraft d = { .kind = KIND_EXPENSE, .desc = desc, .value = vb, .category = cat, .date = bill_date_in_invoice(c, ym), .paid = TRUE,
                  .account_id = acc->id, .card_id = c->id, .reps = 1, .reps_mode = REPS_TOTAL };
    OpErr err;
    gboolean ok = ops_save_tx(APP->state, NULL, &d, APP->today, &err);
    save_result(e, ok, &err, app == 0 ? "Fatura lançada no cartão" : "Diferença lançada no cartão");
}

static void on_paid(GObject *o, GParamSpec *p, BillEd *e) {
    (void)o; (void)p;
    gboolean paid = row_switch_get(e->paid);
    gtk_button_set_label(GTK_BUTTON(e->card_act), paid ? "Salvar despesa" : "Salvar como conta a pagar");
}

static void save_bill(GtkButton *btn, BillEd *e) {
    (void)btn;
    Day due = DAY_NONE;
    if (!row_date_get(e->due, &due)) due = DAY_NONE;
    int ci = row_combo_get(e->cat), ai = row_combo_get(e->account);
    const Account *acc = g_ptr_array_index(APP->state->accounts, ai >= 0 && (guint)ai < APP->state->accounts->len ? (guint)ai : 0);
    TxDraft d = { .kind = KIND_EXPENSE, .desc = row_text(e->desc), .value = row_text(e->value),
                  .category = ci >= 0 && (guint)ci < e->cats->len ? e->cats->pdata[ci] : app_first_category(APP->state, KIND_EXPENSE),
                  .date = due, .paid = row_switch_get(e->paid), .account_id = acc->id, .card_id = "", .reps = 1, .reps_mode = REPS_TOTAL };
    OpErr err;
    gboolean ok = ops_save_tx(APP->state, NULL, &d, APP->today, &err);
    save_result(e, ok, &err, d.paid ? "Despesa salva" : "Conta a pagar salva");
}

static void again(gpointer p) { BillEd *e = p; show(e, "choose"); }

static void result_page(BillEd *e, const BillInfo *info) {
    w_clear(e->res_box);
    e->cmp = NULL;
    const Card *c = ed_card(e);
    const char *msg = bill_info_empty(info) && info->dynamic_pix ? "Pix dinâmico: o valor fica no banco de quem cobra e não vem no código. Digite o valor."
                    : bill_info_empty(info) ? "Não encontrei o valor nem o vencimento. Preencha abaixo."
                    : !info->has_amount ? "Não encontrei o valor. Digite abaixo."
                    : !info->has_due ? "Não encontrei o vencimento. Confira a data abaixo." : NULL;
    if (msg) w_add(e->res_box, note("warning", msg, "warning"));

    g_autofree char *desc = c ? g_strdup_printf("Fatura %s", c->name)
                              : g_strdup(info->segment ? info->segment : info->payee && info->amount_src == BILL_SRC_PIX ? info->payee : "Boleto");
    GtkWidget *g = group(e->res_box, NULL);
    e->desc = row_entry("Descrição", desc, 200);
    gadd(g, e->desc);
    char vb[32];
    e->value = row_entry("Valor (R$)", info->has_amount ? money_input(info->amount, vb) : "", 20);
    if (info->has_amount) {
        g_autofree char *from = g_strdup_printf("Do %s: “%s”", bill_source_label(info->amount_src), info->amount_excerpt);
        gtk_widget_set_tooltip_text(e->value, from);
    }
    gadd(g, e->value);
    e->due = row_date("Vencimento", info->has_due ? info->due : APP->today, FALSE);
    gadd(g, e->due);
    /* de onde veio cada valor, para conferir */
    /* valor e vencimento do mesmo trecho (o código do boleto): mostra uma vez só */
    gboolean same = info->has_amount && info->has_due && info->amount_src == info->due_src && g_strcmp0(info->amount_excerpt, info->due_excerpt) == 0;
    if (info->has_amount && !same) {
        g_autofree char *t = g_strdup_printf("Valor do %s: “%s”", bill_source_label(info->amount_src), info->amount_excerpt);
        w_add(e->res_box, w_label_wrap(t, "fin-muted caption"));
    }
    if (info->has_due) {
        g_autofree char *t = g_strdup_printf("%s do %s: “%s”", same ? "Valor e vencimento" : "Vencimento", bill_source_label(info->due_src), info->due_excerpt);
        w_add(e->res_box, w_label_wrap(t, "fin-muted caption"));
    }
    if (!c && info->payee && info->amount_src != BILL_SRC_PIX) {
        g_autofree char *t = g_strdup_printf("Boleto emitido pelo banco %s (nem sempre é quem cobra).", info->payee);
        w_add(e->res_box, w_label_wrap(t, "fin-muted caption"));
    }

    if (c) {
        GtkWidget *card = w_card("fin-hint");
        e->cmp = w_label_wrap("", NULL);
        w_add(card, e->cmp);
        w_add(e->res_box, card);
        e->cmp_hint = w_label_wrap("", "fin-muted caption");
        w_add(e->res_box, e->cmp_hint);
        e->card_act = gtk_button_new_with_label("Fechar");
        gtk_widget_add_css_class(e->card_act, "suggested-action");
        gtk_widget_add_css_class(e->card_act, "pill");
        g_signal_connect(e->card_act, "clicked", G_CALLBACK(card_action), e);
        g_signal_connect(e->value, "changed", G_CALLBACK(on_cmp_changed), e);
        g_signal_connect(e->due, "changed", G_CALLBACK(on_cmp_changed), e);
        w_add(e->res_box, e->card_act);
        compare_update(e);
    } else {
        GtkWidget *g2 = group(e->res_box, NULL);
        if (e->cats) g_ptr_array_unref(e->cats);
        e->cats = g_ptr_array_new();
        GPtrArray *ex = APP->state->cats[KIND_EXPENSE];
        for (guint i = 0; i < ex->len; i++) g_ptr_array_add(e->cats, ex->pdata[i]);
        /* categoria: a sugerida pelo assistente (se ligado e confiante); conta de consumo vai para Moradia */
        int sel = 0;
        g_autofree char *sug = NULL;
        if (APP->prefs->assist_category && APP->dict) {
            Categorizer *cz = categorizer_build(APP->state, KIND_EXPENSE, APP->dict);
            Suggestion *s = categorizer_suggest(cz, desc);
            if (s) sug = g_strdup(s->category);
            suggestion_free(s);
            categorizer_free(cz);
        }
        /* sem sugestão: conta de consumo vai para Moradia; o resto começa em "Outros" (Alimentação quase nunca é boleto) */
        int outros = -1, moradia = -1;
        for (guint i = 0; i < e->cats->len; i++) {
            if (sug && strcmp(e->cats->pdata[i], sug) == 0) sel = (int)i;
            if (g_ascii_strcasecmp(e->cats->pdata[i], "Outros") == 0) outros = (int)i;
            if (strcmp(e->cats->pdata[i], "Moradia") == 0) moradia = (int)i;
        }
        if (!sug) sel = info->segment && moradia >= 0 ? moradia : outros >= 0 ? outros : 0;
        e->cat = row_combo("Categoria", (const char *const *)e->cats->pdata, (int)e->cats->len, sel);
        if (sug) adw_action_row_set_subtitle(ADW_ACTION_ROW(e->cat), "Sugerida pelo assistente");
        gadd(g2, e->cat);
        GPtrArray *names = g_ptr_array_new();
        for (guint i = 0; i < APP->state->accounts->len; i++) g_ptr_array_add(names, ((Account *)APP->state->accounts->pdata[i])->name);
        e->account = row_combo("Conta", (const char *const *)names->pdata, (int)names->len, 0);
        g_ptr_array_unref(names);
        gadd(g2, e->account);
        e->paid = row_switch("Já paga", "Desligado: fica pendente até o vencimento e entra nos avisos", FALSE);
        gadd(g2, e->paid);
        e->card_act = gtk_button_new_with_label("Salvar como conta a pagar");
        gtk_widget_add_css_class(e->card_act, "suggested-action");
        gtk_widget_add_css_class(e->card_act, "pill");
        g_signal_connect(e->card_act, "clicked", G_CALLBACK(save_bill), e);
        g_signal_connect(e->paid, "notify::active", G_CALLBACK(on_paid), e);
        w_add(e->res_box, e->card_act);
    }
    w_add(e->res_box, w_more_link("Ler outra", again, e, NULL));
    show(e, "result");
}

/* ---------------------------------------------------------------- PDF (numa thread à parte) */
typedef struct { GBytes *pdf; char *password; BillPdfStatus status; char *text; char *message; } ReadJob;
static void read_job_free(ReadJob *j) {
    g_bytes_unref(j->pdf);
    if (j->password) { memset(j->password, 0, strlen(j->password)); g_free(j->password); }
    g_free(j->text); g_free(j->message); g_free(j);
}
static void read_thread(GTask *t, gpointer src, gpointer data, GCancellable *c) {
    (void)src; (void)c;
    ReadJob *j = data;
    j->status = bill_pdf_read(j->pdf, j->password, &j->text, &j->message);
    g_task_return_boolean(t, TRUE);
}

static void open_pdf(BillEd *e, const char *password, gboolean from_vault);

static void password_page(BillEd *e, const char *error) {
    const Card *c = ed_card(e);
    gtk_editable_set_text(GTK_EDITABLE(e->pw_row), "");
    gtk_label_set_text(GTK_LABEL(e->pw_err), error ? error : "");
    gtk_widget_set_visible(e->pw_err, error != NULL);
    gboolean rem = c && can_remember();
    gtk_widget_set_visible(e->pw_card_box, rem);
    if (rem) {
        g_autofree char *t = g_strdup_printf("Lembrar a senha para %s", c->name);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(e->remember_row), t);
        adw_switch_row_set_active(ADW_SWITCH_ROW(e->remember_row), e->remember);
    }
    show(e, "password");
    gtk_widget_grab_focus(e->pw_row);
}

typedef struct { BillEd *e; gboolean from_vault; } ReadCtx;

static void read_done(GObject *src, GAsyncResult *res, gpointer u) {
    (void)src;
    ReadCtx *ctx = u;
    BillEd *e = ctx->e;
    gboolean from_vault = ctx->from_vault;
    g_free(ctx);
    ReadJob *j = g_task_get_task_data(G_TASK(res));
    e->busy = FALSE;
    if (APP->locked) { g_object_unref(e->dialog); return; } /* o bloqueio já fechou a folha */
    const Card *c = ed_card(e);
    switch (j->status) {
    case BILL_PDF_TEXT: {
        if (c && !from_vault && j->password && *j->password && can_remember())
            store_set_bill_password(APP->store, c->id, e->remember ? j->password : NULL, NULL);
        g_clear_pointer(&e->pdf, g_bytes_unref); /* o arquivo não fica guardado depois de lido */
        BillInfo info = { .due = DAY_NONE };
        bill_from_text(j->text, APP->today, &info);
        result_page(e, &info);
        bill_info_clear(&info);
        break;
    }
    case BILL_PDF_NEEDS_PASSWORD: {
        g_autofree char *saved = c && !e->vault_failed && can_remember() ? store_bill_password(APP->store, c->id) : NULL;
        if (saved && !j->password) { open_pdf(e, saved, TRUE); memset(saved, 0, strlen(saved)); break; }
        e->remember = saved != NULL;
        password_page(e, NULL);
        break;
    }
    case BILL_PDF_WRONG_PASSWORD:
        if (from_vault) { e->vault_failed = TRUE; e->remember = TRUE; password_page(e, "A senha lembrada não abriu esta fatura. Digite a senha atual."); }
        else password_page(e, "Senha incorreta. Confira e tente de novo.");
        break;
    case BILL_PDF_NO_TEXT: {
        g_clear_pointer(&e->pdf, g_bytes_unref);
        dlg_notice("PDF sem texto", "Esta fatura parece ser uma imagem (escaneada). Digite o valor e o vencimento, ou cole o código do boleto.");
        BillInfo info = { .due = DAY_NONE };
        result_page(e, &info);
        break;
    }
    default:
        g_clear_pointer(&e->pdf, g_bytes_unref);
        show(e, "choose");
        dlg_notice("Não foi possível ler", j->message ? j->message : "Não foi possível ler este PDF.");
    }
    g_object_unref(e->dialog);
}

static void open_pdf(BillEd *e, const char *password, gboolean from_vault) {
    if (!e->pdf || e->busy) return;
    e->busy = TRUE;
    show(e, "busy");
    ReadJob *j = g_new0(ReadJob, 1);
    j->pdf = g_bytes_ref(e->pdf);
    j->password = password ? g_strdup(password) : NULL;
    ReadCtx *ctx = g_new0(ReadCtx, 1);
    ctx->e = e; ctx->from_vault = from_vault;
    g_object_ref(e->dialog); /* a folha (e o BillEd) vivem até a leitura terminar */
    GTask *t = g_task_new(NULL, NULL, read_done, ctx);
    g_task_set_task_data(t, j, (GDestroyNotify)read_job_free);
    g_task_run_in_thread(t, read_thread);
    g_object_unref(t);
}

static void on_open_pw(GtkWidget *w, BillEd *e) {
    (void)w;
    const char *pw = gtk_editable_get_text(GTK_EDITABLE(e->pw_row));
    if (!*pw) { gtk_label_set_text(GTK_LABEL(e->pw_err), "Digite a senha."); gtk_widget_set_visible(e->pw_err, TRUE); return; }
    e->remember = gtk_widget_get_visible(e->pw_card_box) && adw_switch_row_get_active(ADW_SWITCH_ROW(e->remember_row));
    g_autofree char *copy = g_strdup(pw);
    gtk_editable_set_text(GTK_EDITABLE(e->pw_row), "");
    open_pdf(e, copy, FALSE);
    memset(copy, 0, strlen(copy));
}

static void pw_back(gpointer p) { BillEd *e = p; g_clear_pointer(&e->pdf, g_bytes_unref); show(e, "choose"); }

/* ---------------------------------------------------------------- escolher */
static void sync_card(BillEd *e) {
    g_free(e->card_id);
    int i = e->card_row ? row_combo_get(e->card_row) : 0;
    e->card_id = g_strdup(i > 0 && (guint)i <= APP->state->cards->len ? ((Card *)APP->state->cards->pdata[i - 1])->id : "");
    e->vault_failed = FALSE;
}
static void on_card_changed(GObject *o, GParamSpec *p, BillEd *e) { (void)o; (void)p; sync_card(e); }

static void picked(GObject *src, GAsyncResult *res, gpointer u) {
    BillEd *e = u;
    app_external_end();
    g_autoptr(GError) err = NULL;
    g_autoptr(GFile) f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, &err);
    if (f && !APP->locked) {
        g_autoptr(GFileInfo) info = g_file_query_info(f, G_FILE_ATTRIBUTE_STANDARD_SIZE, 0, NULL, NULL);
        char *data = NULL;
        gsize len = 0;
        if (info && g_file_info_get_size(info) > BILL_PDF_MAX_BYTES) dlg_notice("Não foi possível ler", "O arquivo é grande demais para uma fatura (mais de 25 MB).");
        else if (!g_file_load_contents(f, NULL, &data, &len, NULL, &err)) dlg_notice("Não foi possível ler", err->message);
        else {
            if (e->pdf) g_bytes_unref(e->pdf);
            e->pdf = g_bytes_new_take(data, len);
            e->vault_failed = FALSE;
            open_pdf(e, NULL, FALSE);
        }
    }
    g_object_unref(e->dialog);
}

static void pick_pdf(gpointer p) {
    BillEd *e = p;
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, "Escolher a fatura em PDF");
    GtkFileFilter *flt = gtk_file_filter_new();
    gtk_file_filter_add_mime_type(flt, "application/pdf");
    gtk_file_filter_add_pattern(flt, "*.pdf");
    gtk_file_filter_add_pattern(flt, "*.PDF");
    gtk_file_filter_set_name(flt, "PDF (*.pdf)");
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, flt);
    gtk_file_dialog_set_filters(d, G_LIST_MODEL(filters));
    app_external_begin();
    g_object_ref(e->dialog);
    gtk_file_dialog_open(d, APP->window, APP->files, picked, e);
    g_object_unref(filters);
    g_object_unref(flt);
    g_object_unref(d);
}

static void read_code(gpointer p) {
    BillEd *e = p;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(e->code));
    GtkTextIter a, b;
    gtk_text_buffer_get_bounds(buf, &a, &b);
    g_autofree char *t = gtk_text_buffer_get_text(buf, &a, &b, FALSE);
    BillInfo info = { .due = DAY_NONE };
    if (!bill_from_code(t, APP->today, &info)) {
        dlg_notice("Código não reconhecido", "Confira se copiou a linha digitável inteira (47 ou 48 números) ou o Pix copia e cola completo. Um número errado não passa na conferência.");
        return;
    }
    result_page(e, &info);
    bill_info_clear(&info);
}

static GtkWidget *page_box(void) {
    GtkWidget *b = w_vbox(14);
    gtk_widget_set_margin_start(b, 18);
    gtk_widget_set_margin_end(b, 18);
    gtk_widget_set_margin_top(b, 12);
    gtk_widget_set_margin_bottom(b, 18);
    return b;
}

void bill_import_open(const char *card_id) {
    BillEd *e = g_new0(BillEd, 1);
    e->card_id = g_strdup(card_id && app_card(APP->state, card_id) ? card_id : "");
    e->dialog = adw_dialog_new();
    adw_dialog_set_title(e->dialog, "Ler fatura ou boleto");
    adw_dialog_set_content_width(e->dialog, 560);
    adw_dialog_set_content_height(e->dialog, 640);
    g_object_set_data_full(G_OBJECT(e->dialog), "ed", e, (GDestroyNotify)bill_ed_free);
    GtkWidget *tv = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), adw_header_bar_new());
    e->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(e->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);

    /* escolher */
    GtkWidget *choose = page_box();
    w_add(choose, w_label_wrap("O valor e o vencimento saem do PDF ou do código. Você confere antes de salvar.", "fin-muted"));
    if (APP->state->cards->len) {
        GPtrArray *names = g_ptr_array_new();
        g_ptr_array_add(names, "Não (boleto ou conta)");
        int sel = 0;
        for (guint i = 0; i < APP->state->cards->len; i++) {
            Card *c = APP->state->cards->pdata[i];
            g_ptr_array_add(names, c->name);
            if (strcmp(c->id, e->card_id) == 0) sel = (int)i + 1;
        }
        GtkWidget *g = group(choose, NULL);
        e->card_row = row_combo("É a fatura de um cartão?", (const char *const *)names->pdata, (int)names->len, sel);
        g_ptr_array_unref(names);
        g_signal_connect(e->card_row, "notify::selected", G_CALLBACK(on_card_changed), e);
        gadd(g, e->card_row);
    }
    GtkWidget *pick = w_button("Escolher PDF", "receipt-long", "suggested-action pill", pick_pdf, e, NULL);
    w_add(choose, pick);
    w_add(choose, w_label("Ou cole o código do boleto ou o Pix copia e cola", "heading"));
    e->code = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(e->code), GTK_WRAP_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(e->code), TRUE);
    gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(e->code), FALSE);
    gtk_widget_add_css_class(e->code, "card");
    gtk_widget_set_size_request(e->code, -1, 64);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(e->code), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(e->code), 8);
    gtk_accessible_update_property(GTK_ACCESSIBLE(e->code), GTK_ACCESSIBLE_PROPERTY_LABEL, "Código do boleto ou Pix copia e cola", -1);
    w_add(choose, e->code);
    w_add(choose, w_button("Ler código", NULL, "pill", read_code, e, NULL));
    w_add(choose, note("shield", "Tudo acontece neste computador, sem internet. O PDF é lido só na memória e não fica guardado.", "fin-muted caption"));
    gtk_stack_add_named(GTK_STACK(e->stack), choose, "choose");

    /* lendo */
    GtkWidget *busy = page_box();
    GtkWidget *sp = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(sp));
    gtk_widget_set_size_request(sp, 32, 32);
    gtk_widget_set_margin_top(sp, 40);
    w_add(busy, sp);
    w_add(busy, w_label("Lendo a fatura…", "fin-muted"));
    gtk_stack_add_named(GTK_STACK(e->stack), busy, "busy");

    /* senha */
    GtkWidget *pwp = page_box();
    w_add(pwp, w_label_wrap("Esta fatura tem senha. Digite para abrir.", NULL));
    GtkWidget *g = group(pwp, NULL);
    e->pw_row = adw_password_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(e->pw_row), "Senha do PDF");
    g_signal_connect(e->pw_row, "entry-activated", G_CALLBACK(on_open_pw), e);
    gadd(g, e->pw_row);
    w_add(pwp, w_label_wrap("Muitos bancos usam os primeiros dígitos do seu CPF.", "fin-muted caption"));
    e->pw_err = w_label_wrap("", "error");
    w_add(pwp, e->pw_err);
    e->pw_card_box = w_vbox(6);
    GtkWidget *g2 = group(e->pw_card_box, NULL);
    e->remember_row = row_switch("Lembrar a senha", "Fica cifrada só neste computador e não vai para o backup. Para esquecer, desligue aqui ou use “Esquecer senha” no cartão.", FALSE);
    gadd(g2, e->remember_row);
    w_add(pwp, e->pw_card_box);
    GtkWidget *row = w_hbox(10);
    gtk_widget_set_halign(row, GTK_ALIGN_END);
    w_add(row, w_button("Voltar", NULL, "pill", pw_back, e, NULL));
    GtkWidget *openb = gtk_button_new_with_label("Abrir");
    gtk_widget_add_css_class(openb, "suggested-action");
    gtk_widget_add_css_class(openb, "pill");
    g_signal_connect(openb, "clicked", G_CALLBACK(on_open_pw), e);
    w_add(row, openb);
    w_add(pwp, row);
    gtk_stack_add_named(GTK_STACK(e->stack), pwp, "password");

    /* conferir */
    e->res_box = page_box();
    gtk_stack_add_named(GTK_STACK(e->stack), e->res_box, "result");

    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), e->stack);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), sw);
    adw_dialog_set_child(e->dialog, tv);
    show(e, "choose");
    adw_dialog_present(e->dialog, GTK_WIDGET(APP->window));
}

/* ---------------------------------------------------------------- senha lembrada no cartão */
gboolean bill_card_has_password(const char *card_id) {
    if (!can_remember() || !card_id) return FALSE;
    g_autofree char *p = store_bill_password(APP->store, card_id);
    gboolean has = p != NULL;
    if (p) memset(p, 0, strlen(p));
    return has;
}

void bill_forget_password(const char *card_id) {
    if (can_remember() && card_id) store_set_bill_password(APP->store, card_id, NULL, NULL);
}
