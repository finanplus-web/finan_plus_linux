/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Despesas compartilhadas (como no app Android 1.8/1.9; DESPESAS-COMPARTILHADAS.md): cartão em Meu dinheiro,
 * histórico com busca e filtros, formulário (divisão igual ou pelo valor de cada um), situação de cada pessoa,
 * pagamentos totais ou parciais, e o comprovante em PDF ou em imagem na identidade do Finan+.
 * As regras e mensagens vêm de core/shared.c. Não mexe no saldo nem nos lançamentos.
 */
#include "pages.h"
#include "theme.h"
#include "widgets.h"
#include "core/shared.h"
#include "shared_receipt.h"
#include <string.h>

typedef struct { char *id; gboolean pdf; } SaveReq;
static void save_req_free(SaveReq *r) { g_free(r->id); g_free(r); }

static void receipt_saved(GObject *src, GAsyncResult *res, gpointer p) {
    SaveReq *r = p;
    g_autoptr(GError) e = NULL;
    app_external_end();
    g_autoptr(GFile) f = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &e);
    SharedExpense *x = app_shared(APP->state, r->id);
    if (f && x && !APP->locked) {
        g_autofree char *path = g_file_get_path(f);
        if (path && shared_receipt_write(x, path, r->pdf, &e)) {
            g_autofree char *name = g_file_get_basename(f);
            app_toast("%s salvo: %s", r->pdf ? "Comprovante" : "Imagem", name);
        } else dlg_notice("Não foi possível salvar", e ? e->message : "Escolha uma pasta deste computador.");
    }
    save_req_free(r);
}

static void receipt_save(const char *id, gboolean pdf) {
    SharedExpense *x = app_shared(APP->state, id);
    if (!x) return;
    g_autofree char *name = pdf ? shared_pdf_name(x) : shared_image_name(x);
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, pdf ? "Salvar comprovante em PDF" : "Salvar imagem para compartilhar");
    gtk_file_dialog_set_initial_name(d, name);
    SaveReq *r = g_new0(SaveReq, 1);
    r->id = g_strdup(id);
    r->pdf = pdf;
    app_external_begin();
    gtk_file_dialog_save(d, APP->window, APP->files, receipt_saved, r);
    g_object_unref(d);
}

/* ================================================================ detalhe da despesa */

static void open_detail(gpointer id);

typedef struct { char *eid; char *pid; } PayCtx;
static void pay_ctx_free(gpointer p) { PayCtx *c = p; g_free(c->eid); g_free(c->pid); g_free(c); }
static PayCtx *pay_ctx(const char *e, const char *p) { PayCtx *c = g_new0(PayCtx, 1); c->eid = g_strdup(e); c->pid = g_strdup(p); return c; }

static AdwDialog *detail_dialog = NULL;
static void close_detail(void) { if (detail_dialog) adw_dialog_force_close(detail_dialog); }

static void reopen(const char *id) {
    g_autofree char *i = g_strdup(id);
    close_detail();
    open_detail(i);
}

static void do_pay(const char *value, gpointer p) {
    PayCtx *c = p;
    OpErr err;
    if (!shared_add_payment(APP->state, c->eid, c->pid, value, APP->today, &err)) { dlg_notice(err.title, err.msg); return; }
    app_commit();
    app_toast("Pagamento registrado");
    reopen(c->eid);
}

static void ask_payment(gpointer p) {
    PayCtx *c = p;
    dlg_input("Registrar pagamento", "Quanto foi devolvido hoje? Pode ser uma parte do que a pessoa deve.", "Ex.: 50,00", "", FALSE,
              do_pay, pay_ctx(c->eid, c->pid), pay_ctx_free);
}

static void settle(gpointer p) {
    PayCtx *c = p;
    OpErr err;
    if (!shared_settle(APP->state, c->eid, c->pid, APP->today, &err)) { dlg_notice(err.title, err.msg); return; }
    app_commit();
    app_toast("Parte quitada");
    reopen(c->eid);
}

static void del_payment(gpointer p) {
    PayCtx *c = p; /* pid = id do pagamento */
    shared_delete_payment(APP->state, c->eid, c->pid);
    app_commit();
    reopen(c->eid);
}

static void do_delete(gpointer id) {
    shared_delete(APP->state, id);
    app_commit();
    close_detail();
    app_toast("Despesa compartilhada excluída");
}

static void ask_delete(gpointer id) {
    dlg_confirm("Excluir despesa compartilhada", "Excluir esta despesa e os pagamentos registrados? O saldo das contas não muda.",
                "Excluir", TRUE, do_delete, g_strdup(id), g_free);
}

static void edit_it(gpointer id) { g_autofree char *i = g_strdup(id); close_detail(); editor_shared(i); }
static void save_pdf(gpointer id) { receipt_save(id, TRUE); }
static void save_png(gpointer id) { receipt_save(id, FALSE); }

static void on_detail_closed(AdwDialog *d, gpointer u) { (void)u; if (detail_dialog == d) detail_dialog = NULL; }

static void open_detail(gpointer idp) {
    const char *id = idp;
    SharedExpense *e = app_shared(APP->state, id);
    if (!e) return;
    AdwDialog *d = adw_dialog_new();
    detail_dialog = d;
    g_signal_connect(d, "closed", G_CALLBACK(on_detail_closed), NULL);
    adw_dialog_set_title(d, e->name);
    adw_dialog_set_content_width(d, 620);
    adw_dialog_set_content_height(d, 680);
    GtkWidget *tv = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), adw_header_bar_new());
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *c = w_vbox(12);
    gtk_widget_set_margin_start(c, 18);
    gtk_widget_set_margin_end(c, 18);
    gtk_widget_set_margin_top(c, 8);
    gtk_widget_set_margin_bottom(c, 18);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), c);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), sw);
    adw_dialog_set_child(d, tv);

    SharedSummary st;
    shared_summary(e, &st);
    g_autofree char *when = br_full_date(e->date);
    const SharedPerson *payer = NULL;
    for (guint i = 0; i < e->people->len; i++) if (strcmp(((SharedPerson *)e->people->pdata[i])->id, e->payer_id) == 0) payer = e->people->pdata[i];
    g_autofree char *sub = g_strdup_printf("%s · pago por %s · %s", when, payer ? payer->name : "—", e->split == SPLIT_EQUAL ? "dividida igualmente" : "dividida pelo valor de cada um");
    w_add(c, w_label_wrap(sub, "fin-muted"));
    /* resumo */
    GtkWidget *sum = w_hbox(24);
    gtk_widget_add_css_class(sum, "fin-card");
    gtk_widget_add_css_class(sum, "fin-tight");
    struct { const char *l; Cents v; const char *cls; } it[] = {
        {"Total", e->total, "fin-money-mid"},
        {"Recebido", st.received, "fin-money-mid fin-green"},
        {"Pendente", summary_pending(&st), summary_pending(&st) > 0 ? "fin-money-mid fin-red" : "fin-money-mid fin-green"},
    };
    for (guint i = 0; i < 3; i++) {
        GtkWidget *b = w_vbox(1);
        w_add(b, w_label(it[i].l, "fin-muted caption"));
        w_add(b, w_money(it[i].v, it[i].cls));
        w_add(sum, b);
    }
    GtkWidget *badge = w_badge(shared_state_label(summary_state(&st)), summary_state(&st) == SHARED_SETTLED ? "green" : summary_state(&st) == SHARED_PARTIAL ? "accent" : "red");
    gtk_widget_set_valign(badge, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(badge, TRUE);
    gtk_widget_set_halign(badge, GTK_ALIGN_END);
    w_add(sum, badge);
    w_add(c, sum);
    if (e->note[0]) w_add(c, w_label_wrap(e->note, "fin-muted"));
    /* pessoas */
    w_add(c, w_label("Quem deve e quanto", "heading"));
    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(list, "boxed-list");
    for (guint i = 0; i < st.people->len; i++) {
        const PersonStatus *p = &g_array_index(st.people, PersonStatus, i);
        PersonState ps = person_state(p);
        GtkWidget *r = adw_action_row_new();
        g_autofree char *t = g_markup_escape_text(p->person->name, -1);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), t);
        g_autofree char *share = app_money(p->person->share), *rem = app_money(person_remaining(p)), *paid = app_money(p->paid);
        g_autofree char *s2 = p->is_payer ? g_strdup_printf("Pagou a despesa · parte dele %s", share)
            : ps == PERSON_PAID ? g_strdup_printf("Quitado · parte %s", share)
            : ps == PERSON_PARTIAL ? g_strdup_printf("Pagou %s · falta %s", paid, rem)
            : g_strdup_printf("Pendente · deve %s", rem);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(r), s2);
        adw_action_row_add_prefix(ADW_ACTION_ROW(r), w_icon_tile(p->is_payer ? "account-balance-wallet" : ps == PERSON_PAID ? "check" : "group",
                                                                 p->is_payer ? "blue" : ps == PERSON_PAID ? "green" : "orange", FALSE));
        if (!p->is_payer && person_remaining(p) > 0) {
            GtkWidget *b1 = w_pill("Pagamento", ask_payment, pay_ctx(e->id, p->person->id), pay_ctx_free);
            gtk_widget_set_tooltip_text(b1, "Registrar um pagamento total ou parcial");
            gtk_widget_set_valign(b1, GTK_ALIGN_CENTER);
            adw_action_row_add_suffix(ADW_ACTION_ROW(r), b1);
            GtkWidget *b2 = w_button("Quitar", "check", "fin-pill", settle, pay_ctx(e->id, p->person->id), pay_ctx_free);
            gtk_widget_set_valign(b2, GTK_ALIGN_CENTER);
            adw_action_row_add_suffix(ADW_ACTION_ROW(r), b2);
        }
        gtk_list_box_append(GTK_LIST_BOX(list), r);
    }
    w_add(c, list);
    /* pagamentos */
    if (e->payments->len) {
        w_add(c, w_label("Pagamentos registrados", "heading"));
        GtkWidget *pl = gtk_list_box_new();
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(pl), GTK_SELECTION_NONE);
        gtk_widget_add_css_class(pl, "boxed-list");
        for (guint i = 0; i < e->payments->len; i++) {
            SharedPayment *pay = e->payments->pdata[i];
            const SharedPerson *who = NULL;
            for (guint k = 0; k < e->people->len; k++) if (strcmp(((SharedPerson *)e->people->pdata[k])->id, pay->person_id) == 0) who = e->people->pdata[k];
            GtkWidget *r = adw_action_row_new();
            g_autofree char *v = app_money(pay->value);
            char dd[11];
            g_autofree char *t = g_strdup_printf("%s · %s", who ? who->name : "Participante", v);
            g_autofree char *te = g_markup_escape_text(t, -1);
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), te);
            adw_action_row_set_subtitle(ADW_ACTION_ROW(r), day_br(pay->date, dd));
            GtkWidget *del = w_button(NULL, "delete", "flat", del_payment, pay_ctx(e->id, pay->id), pay_ctx_free);
            gtk_widget_set_tooltip_text(del, "Excluir este pagamento");
            gtk_accessible_update_property(GTK_ACCESSIBLE(del), GTK_ACCESSIBLE_PROPERTY_LABEL, "Excluir este pagamento", -1);
            gtk_widget_set_valign(del, GTK_ALIGN_CENTER);
            adw_action_row_add_suffix(ADW_ACTION_ROW(r), del);
            gtk_list_box_append(GTK_LIST_BOX(pl), r);
        }
        w_add(c, pl);
    }
    shared_summary_clear(&st);
    /* ações */
    GtkWidget *share = w_hbox(8);
    gtk_box_set_homogeneous(GTK_BOX(share), TRUE);
    gtk_widget_set_margin_top(share, 6);
    w_add(share, w_button("Salvar imagem", "image", "fin-pill", save_png, g_strdup(e->id), g_free));
    w_add(share, w_button("Comprovante em PDF", "picture-as-pdf", "fin-pill", save_pdf, g_strdup(e->id), g_free));
    w_add(c, share);
    w_add(c, w_label_wrap("A imagem sai na identidade do Finan+, pronta para mandar no WhatsApp, no Telegram ou por e-mail.", "fin-muted caption"));
    GtkWidget *more = w_hbox(8);
    gtk_box_set_homogeneous(GTK_BOX(more), TRUE);
    w_add(more, w_button("Editar", "edit", "fin-pill", edit_it, g_strdup(e->id), g_free));
    w_add(more, w_button("Excluir", "delete", "fin-pill danger", ask_delete, g_strdup(e->id), g_free));
    w_add(c, more);
    adw_dialog_present(d, GTK_WIDGET(APP->window));
}

void shared_open(const char *id) { open_detail((gpointer)id); }

/* ================================================================ formulário */

typedef struct {
    AdwDialog *dialog;
    char *id;
    GtkWidget *name, *total, *date, *split, *note, *people_box;
    GPtrArray *rows; /* PersonRow* */
    GtkWidget *payer_group;
} SharedEd;

typedef struct { char *pid; GtkWidget *box, *name, *share, *payer; } PersonRow;
static void person_row_free(PersonRow *r) { g_free(r->pid); g_free(r); }

static void ed_free(SharedEd *e) { g_free(e->id); g_ptr_array_unref(e->rows); g_free(e); }

static void update_split(SharedEd *e) {
    gboolean custom = row_combo_get(e->split) == 1;
    for (guint i = 0; i < e->rows->len; i++) gtk_widget_set_visible(((PersonRow *)e->rows->pdata[i])->share, custom);
}
static void on_split(GObject *o, GParamSpec *p, SharedEd *e) { (void)o; (void)p; update_split(e); }

static void remove_row(GtkButton *b, SharedEd *e) {
    PersonRow *r = g_object_get_data(G_OBJECT(b), "row");
    if (e->rows->len <= 2) { dlg_notice("Participantes", "Mantenha pelo menos duas pessoas: quem pagou e quem vai dividir."); return; }
    gboolean was_payer = gtk_check_button_get_active(GTK_CHECK_BUTTON(r->payer));
    gtk_box_remove(GTK_BOX(e->people_box), r->box);
    g_ptr_array_remove(e->rows, r);
    if (was_payer) gtk_check_button_set_active(GTK_CHECK_BUTTON(((PersonRow *)e->rows->pdata[0])->payer), TRUE);
}

static PersonRow *add_row(SharedEd *e, const char *pid, const char *name, Cents share, gboolean payer) {
    PersonRow *r = g_new0(PersonRow, 1);
    r->pid = g_strdup(pid ? pid : "");
    r->box = w_hbox(8);
    r->payer = gtk_check_button_new();
    gtk_widget_set_tooltip_text(r->payer, "Foi esta pessoa que pagou a conta");
    gtk_accessible_update_property(GTK_ACCESSIBLE(r->payer), GTK_ACCESSIBLE_PROPERTY_LABEL, "Pagou a despesa", -1);
    if (e->rows->len) gtk_check_button_set_group(GTK_CHECK_BUTTON(r->payer), GTK_CHECK_BUTTON(((PersonRow *)e->rows->pdata[0])->payer));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(r->payer), payer);
    w_add(r->box, r->payer);
    r->name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(r->name), "Nome");
    gtk_entry_set_max_length(GTK_ENTRY(r->name), SHARED_PERSON_MAX);
    gtk_editable_set_text(GTK_EDITABLE(r->name), name ? name : "");
    gtk_widget_set_hexpand(r->name, TRUE);
    w_add(r->box, r->name);
    r->share = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(r->share), "Parte (R$)");
    gtk_editable_set_width_chars(GTK_EDITABLE(r->share), 10);
    char b[32];
    if (share > 0) gtk_editable_set_text(GTK_EDITABLE(r->share), money_input(share, b));
    w_add(r->box, r->share);
    GtkWidget *del = gtk_button_new_from_icon_name("fin-close-symbolic");
    gtk_widget_add_css_class(del, "flat");
    gtk_widget_set_tooltip_text(del, "Remover pessoa");
    gtk_accessible_update_property(GTK_ACCESSIBLE(del), GTK_ACCESSIBLE_PROPERTY_LABEL, "Remover pessoa", -1);
    g_object_set_data(G_OBJECT(del), "row", r);
    g_signal_connect(del, "clicked", G_CALLBACK(remove_row), e);
    w_add(r->box, del);
    g_ptr_array_add(e->rows, r);
    w_add(e->people_box, r->box);
    return r;
}

static void add_person(GtkButton *b, SharedEd *e) {
    (void)b;
    if (e->rows->len >= SHARED_PEOPLE_MAX) return;
    PersonRow *r = add_row(e, NULL, "", 0, FALSE);
    update_split(e);
    gtk_widget_grab_focus(r->name);
}

static void ed_save(GtkButton *b, SharedEd *e) {
    (void)b;
    Day d;
    if (!row_date_get(e->date, &d)) d = DAY_NONE;
    int n = (int)e->rows->len, payer = -1;
    g_autofree SharedPersonDraft *pd = g_new0(SharedPersonDraft, MAX(n, 1));
    for (int i = 0; i < n; i++) {
        PersonRow *r = e->rows->pdata[i];
        pd[i].id = r->pid;
        pd[i].name = gtk_editable_get_text(GTK_EDITABLE(r->name));
        pd[i].share = gtk_editable_get_text(GTK_EDITABLE(r->share));
        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(r->payer))) payer = i;
    }
    SharedDraft dr = {row_text(e->name), row_text(e->total), d, row_combo_get(e->split) == 1 ? SPLIT_CUSTOM : SPLIT_EQUAL,
                      pd, n, payer, row_text(e->note)};
    OpErr err;
    guint before = APP->state->shared->len;
    if (!shared_save(APP->state, e->id, &dr, &err)) { dlg_notice(err.title, err.msg); return; }
    g_autofree char *id = e->id ? g_strdup(e->id) : APP->state->shared->len > before
                          ? g_strdup(((SharedExpense *)APP->state->shared->pdata[APP->state->shared->len - 1])->id) : NULL;
    app_commit();
    app_toast(e->id ? "Despesa compartilhada atualizada" : "Despesa compartilhada salva");
    adw_dialog_force_close(e->dialog);
    if (id) open_detail(id);
}

void editor_shared(const char *id) {
    SharedExpense *x = id ? app_shared(APP->state, id) : NULL;
    SharedEd *e = g_new0(SharedEd, 1);
    e->id = x ? g_strdup(x->id) : NULL;
    e->rows = g_ptr_array_new_with_free_func((GDestroyNotify)person_row_free);
    AdwDialog *d = adw_dialog_new();
    e->dialog = d;
    g_object_set_data_full(G_OBJECT(d), "ed", e, (GDestroyNotify)ed_free);
    adw_dialog_set_title(d, x ? "Editar despesa compartilhada" : "Nova despesa compartilhada");
    adw_dialog_set_content_width(d, 600);
    adw_dialog_set_content_height(d, 700);
    GtkWidget *tv = adw_toolbar_view_new();
    GtkWidget *hb = adw_header_bar_new();
    adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(hb), FALSE);
    GtkWidget *save = gtk_button_new_with_label("Salvar");
    gtk_widget_add_css_class(save, "suggested-action");
    adw_header_bar_pack_end(ADW_HEADER_BAR(hb), save);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), hb);
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *c = w_vbox(14);
    gtk_widget_set_margin_start(c, 18);
    gtk_widget_set_margin_end(c, 18);
    gtk_widget_set_margin_top(c, 8);
    gtk_widget_set_margin_bottom(c, 18);
    w_add(c, w_label_wrap("Divida uma conta com amigos ou família e acompanhe quem já devolveu. Não mexe no saldo das contas.", "fin-muted"));
    GtkWidget *g = adw_preferences_group_new();
    char b[32];
    e->name = row_entry("Nome", x ? x->name : "", SHARED_NAME_MAX);
    e->total = row_entry("Valor total (R$)", x ? money_input(x->total, b) : "", 20);
    e->date = row_date("Data", x ? x->date : APP->today, FALSE);
    static const char *const SPLIT[] = {"Igual para todos", "Pelo valor de cada um"};
    e->split = row_combo("Divisão", SPLIT, 2, x && x->split == SPLIT_CUSTOM ? 1 : 0);
    e->note = row_entry("Observação (opcional)", x ? x->note : "", SHARED_NOTE_MAX);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), e->name);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), e->total);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), e->date);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), e->split);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), e->note);
    w_add(c, g);
    w_add(c, w_label("Participantes", "heading"));
    w_add(c, w_label_wrap("Marque quem pagou a conta. Na divisão pelo valor de cada um, as partes precisam somar o total.", "fin-muted caption"));
    e->people_box = w_vbox(6);
    w_add(c, e->people_box);
    if (x) {
        for (guint i = 0; i < x->people->len; i++) {
            SharedPerson *p = x->people->pdata[i];
            add_row(e, p->id, p->name, x->split == SPLIT_CUSTOM ? p->share : 0, strcmp(p->id, x->payer_id) == 0);
        }
    } else {
        add_row(e, NULL, "Eu", 0, TRUE);
        add_row(e, NULL, "", 0, FALSE);
    }
    GtkWidget *addb = w_button("Adicionar pessoa", "add", "fin-pill", NULL, NULL, NULL);
    g_signal_connect(addb, "clicked", G_CALLBACK(add_person), e);
    gtk_widget_set_halign(addb, GTK_ALIGN_START);
    w_add(c, addb);
    update_split(e);
    g_signal_connect(e->split, "notify::selected", G_CALLBACK(on_split), e);
    g_signal_connect(save, "clicked", G_CALLBACK(ed_save), e);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), c);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), sw);
    adw_dialog_set_child(d, tv);
    adw_dialog_present(d, GTK_WIDGET(APP->window));
    adw_dialog_set_focus(d, e->name);
}

/* ================================================================ cartão em Meu dinheiro e histórico */

static void new_shared(gpointer u) { (void)u; editor_shared(NULL); }

static GtkWidget *expense_row(const SharedExpense *e) {
    SharedSummary st;
    shared_summary(e, &st);
    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "fin-row");
    GtkWidget *row = w_hbox(12);
    SharedState ss = summary_state(&st);
    w_add(row, w_icon_tile("group", ss == SHARED_SETTLED ? "green" : "orange", FALSE));
    GtkWidget *t = w_vbox(2);
    gtk_widget_set_hexpand(t, TRUE);
    w_add(t, w_label(e->name, "heading"));
    char d[11];
    g_autofree char *pend = app_money(summary_pending(&st));
    g_autofree char *sub = ss == SHARED_SETTLED ? g_strdup_printf("%s · Quitada", day_br(e->date, d))
                                                : g_strdup_printf("%s · %s · falta receber %s", day_br(e->date, d), shared_state_label(ss), pend);
    w_add(t, w_label(sub, ss == SHARED_SETTLED ? "fin-muted caption" : "fin-red caption"));
    w_add(row, t);
    GtkWidget *mv = w_money(e->total, "fin-money-mid");
    gtk_widget_set_valign(mv, GTK_ALIGN_CENTER);
    w_add(row, mv);
    gtk_button_set_child(GTK_BUTTON(btn), row);
    g_signal_connect_data(btn, "clicked", G_CALLBACK(open_detail), g_strdup(e->id), (GClosureNotify)(void (*)(void))g_free, G_CONNECT_SWAPPED);
    shared_summary_clear(&st);
    return btn;
}

/* histórico: busca, situação e pessoa */
typedef struct { GtkWidget *list, *search, *state, *person; char **names; } Hist;
static void hist_free(Hist *h) { g_strfreev(h->names); g_free(h); }

static void hist_fill(Hist *h) {
    w_clear(h->list);
    guint pi = gtk_drop_down_get_selected(GTK_DROP_DOWN(h->person));
    int st = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(h->state)) - 1;
    SharedFilter f = {gtk_editable_get_text(GTK_EDITABLE(h->search)), DAY_NONE, DAY_NONE,
                      pi > 0 && h->names[pi - 1] ? h->names[pi - 1] : NULL, st};
    g_autoptr(GPtrArray) r = shared_filter(APP->state->shared, &f);
    if (!r->len) w_add(h->list, w_label_wrap("Nenhuma despesa encontrada.", "fin-muted"));
    for (guint i = 0; i < r->len; i++) w_add(h->list, expense_row(r->pdata[i]));
}
static void hist_changed(GObject *o, GParamSpec *p, Hist *h) { (void)o; (void)p; hist_fill(h); }
static void hist_search(GtkEditable *e, Hist *h) { (void)e; hist_fill(h); }

static void open_history(gpointer u) {
    (void)u;
    AdwDialog *d = adw_dialog_new();
    adw_dialog_set_title(d, "Despesas compartilhadas");
    adw_dialog_set_content_width(d, 640);
    adw_dialog_set_content_height(d, 680);
    GtkWidget *tv = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), adw_header_bar_new());
    Hist *h = g_new0(Hist, 1);
    g_object_set_data_full(G_OBJECT(d), "hist", h, (GDestroyNotify)hist_free);
    GtkWidget *c = w_vbox(10);
    gtk_widget_set_margin_start(c, 18);
    gtk_widget_set_margin_end(c, 18);
    gtk_widget_set_margin_bottom(c, 18);
    GtkWidget *bar = w_hbox(8);
    h->search = gtk_search_entry_new();
    g_object_set(h->search, "placeholder-text", "Buscar nome, observação ou pessoa", NULL);
    gtk_widget_set_hexpand(h->search, TRUE);
    w_add(bar, h->search);
    static const char *const STATES[] = {"Todas", "Pendentes", "Parcialmente pagas", "Quitadas", NULL};
    h->state = gtk_drop_down_new_from_strings(STATES);
    w_add(bar, h->state);
    h->names = shared_people_names(APP->state->shared);
    GPtrArray *opts = g_ptr_array_new();
    g_ptr_array_add(opts, "Todas as pessoas");
    for (char **n = h->names; *n; n++) g_ptr_array_add(opts, *n);
    g_ptr_array_add(opts, NULL);
    h->person = gtk_drop_down_new_from_strings((const char *const *)opts->pdata);
    g_ptr_array_free(opts, TRUE);
    w_add(bar, h->person);
    w_add(c, bar);
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(sw, TRUE);
    h->list = w_vbox(6);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), h->list);
    w_add(c, sw);
    g_signal_connect(h->search, "search-changed", G_CALLBACK(hist_search), h);
    g_signal_connect(h->state, "notify::selected", G_CALLBACK(hist_changed), h);
    g_signal_connect(h->person, "notify::selected", G_CALLBACK(hist_changed), h);
    hist_fill(h);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), c);
    adw_dialog_set_child(d, tv);
    adw_dialog_present(d, GTK_WIDGET(APP->window));
}

GtkWidget *shared_card(void) {
    const AppState *s = APP->state;
    GtkWidget *c = w_card(NULL);
    gtk_widget_set_valign(c, GTK_ALIGN_START);
    Cents pend = 0, recv = 0;
    shared_totals(s->shared, &pend, &recv);
    g_autofree char *pv = app_money(pend);
    g_autofree char *sub = s->shared->len ? g_strdup_printf("Falta receber %s", pv) : g_strdup("Divida contas e veja quem deve a quem");
    GtkWidget *head = w_hbox(12);
    w_add(head, w_icon_tile("group", "orange", TRUE));
    GtkWidget *t = w_vbox(1);
    gtk_widget_set_hexpand(t, TRUE);
    gtk_widget_set_valign(t, GTK_ALIGN_CENTER);
    w_add(t, w_label("Despesas compartilhadas", "heading"));
    w_add(t, w_label_wrap(sub, pend > 0 ? "fin-red caption" : "fin-muted caption"));
    w_add(head, t);
    GtkWidget *nb = w_button("Nova", "add", "fin-pill", new_shared, NULL, NULL);
    gtk_widget_set_valign(nb, GTK_ALIGN_CENTER);
    w_add(head, nb);
    w_add(c, head);
    if (!s->shared->len) {
        w_add(c, w_label_wrap("Uma conta paga por uma pessoa e dividida com outras: o Finan+ calcula a parte de cada um, "
                              "registra pagamentos parciais e gera o comprovante. Não mexe no saldo.", "fin-muted caption"));
        return c;
    }
    SharedFilter all = {NULL, DAY_NONE, DAY_NONE, NULL, -1};
    g_autoptr(GPtrArray) r = shared_filter(s->shared, &all);
    for (guint i = 0; i < r->len && i < 4; i++) w_add(c, expense_row(r->pdata[i]));
    if (r->len > 1) {
        g_autofree char *lbl = g_strdup_printf("Ver todas (%u)", r->len);
        GtkWidget *more = w_more_link(lbl, open_history, NULL, NULL);
        gtk_widget_set_halign(more, GTK_ALIGN_END);
        w_add(c, more);
    }
    return c;
}
