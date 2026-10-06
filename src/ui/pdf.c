/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Diálogo "Relatório em PDF": atalhos de período, datas, opção de incluir os lançamentos,
 * prévia dos totais e aviso de privacidade. O desenho do PDF está em pdf_render.c.
 */
#include "pages.h"
#include "pdf.h"
#include "widgets.h"
#include "core/report.h"

gboolean pdf_report_write(const char *path, Day from, Day to, gboolean include_txs, GError **error) {
    return pdf_report_render(APP->state, path, from, to, include_txs, APP->today, error);
}

/* ---------------------------------------------------------------- diálogo de exportação */

typedef struct {
    AdwDialog *dialog;
    GtkWidget *from, *to, *withtx, *preview, *generate;
    GtkWidget *pills[5];
} PdfDlg;

static const char *const PRESET_KEYS[5] = {"mes", "anterior", "ano", "12m", "tudo"};
static const char *const PRESET_LABELS[5] = {"Este mês", "Mês passado", "Este ano", "12 meses", "Tudo"};

static void first_last(Day *first, Day *last) {
    *first = *last = DAY_NONE;
    for (guint i = 0; i < APP->state->txs->len; i++) {
        Day d = ((Tx *)APP->state->txs->pdata[i])->date;
        if (*first == DAY_NONE || d < *first) *first = d;
        if (*last == DAY_NONE || d > *last) *last = d;
    }
}

static void pdf_update(PdfDlg *p) {
    Day a, b;
    gboolean ok = row_date_get(p->from, &a) && row_date_get(p->to, &b) && a != DAY_NONE && b != DAY_NONE && b >= a;
    Day first, last;
    first_last(&first, &last);
    for (int i = 0; i < 5; i++) {
        Day x, y;
        report_preset(PRESET_KEYS[i], APP->today, first, last, &x, &y);
        if (ok && x == a && y == b) gtk_widget_add_css_class(p->pills[i], "selected");
        else gtk_widget_remove_css_class(p->pills[i], "selected");
    }
    if (!ok) { gtk_label_set_text(GTK_LABEL(p->preview), "Escolha as datas inicial e final (a final igual ou depois da inicial)."); return; }
    Report *r = report_build(APP->state, a, b, APP->today);
    g_autofree char *i = app_money(r->income), *e = app_money(r->expense), *s = app_money(report_balance(r));
    g_autofree char *t = g_strdup_printf("%u lançamento(s) · receitas %s · despesas %s · saldo %s", r->txs->len, i, e, s);
    gtk_label_set_text(GTK_LABEL(p->preview), t);
    report_free(r);
}

static void on_pdf_changed(GtkEditable *e, PdfDlg *p) { (void)e; pdf_update(p); }

static void preset_clicked(GtkButton *b, PdfDlg *p) {
    int k = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "k"));
    Day first, last, x, y;
    first_last(&first, &last);
    report_preset(PRESET_KEYS[k], APP->today, first, last, &x, &y);
    row_date_set(p->from, x);
    row_date_set(p->to, y);
}

typedef struct { Day from, to; gboolean txs; AdwDialog *dialog; } PdfJob;

static void pdf_saved(GObject *src, GAsyncResult *res, gpointer u) {
    PdfJob *j = u;
    g_autoptr(GError) e = NULL;
    g_autoptr(GFile) f = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &e);
    if (f && !APP->locked) {
        g_autofree char *path = g_file_get_path(f);
        if (path && pdf_report_write(path, j->from, j->to, j->txs, &e)) {
            char a[11], b[11];
            g_autofree char *msg = g_strdup_printf("Relatório de %s a %s salvo em:\n%s", day_br(j->from, a), day_br(j->to, b), path);
            adw_dialog_close(j->dialog);
            dlg_notice("PDF salvo", msg);
        } else dlg_notice("Não foi possível gerar o PDF", e ? e->message : "Escolha uma pasta local para salvar.");
    }
    g_object_unref(j->dialog);
    g_free(j);
}

static void pdf_generate(GtkButton *btn, PdfDlg *p) {
    (void)btn;
    Day a, b;
    if (!row_date_get(p->from, &a) || !row_date_get(p->to, &b) || a == DAY_NONE || b == DAY_NONE) {
        dlg_notice("Período incompleto", "Escolha as datas inicial e final.");
        return;
    }
    if (b < a) { dlg_notice("Período inválido", "A data final deve ser igual ou posterior à inicial."); return; }
    PdfJob *j = g_new0(PdfJob, 1);
    j->from = a;
    j->to = b;
    j->txs = row_switch_get(p->withtx);
    j->dialog = g_object_ref(p->dialog);
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, "Salvar relatório em PDF");
    g_autofree char *name = report_file_name(a, b);
    gtk_file_dialog_set_initial_name(d, name);
    gtk_file_dialog_save(d, APP->window, APP->files, pdf_saved, j);
    g_object_unref(d);
}

void report_pdf_dialog(Day from, Day to) {
    PdfDlg *p = g_new0(PdfDlg, 1);
    p->dialog = adw_dialog_new();
    g_object_set_data_full(G_OBJECT(p->dialog), "pdf", p, g_free);
    adw_dialog_set_title(p->dialog, "Relatório em PDF");
    adw_dialog_set_content_width(p->dialog, 580);
    GtkWidget *tv = adw_toolbar_view_new();
    GtkWidget *hb = adw_header_bar_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), hb);
    GtkWidget *box = w_vbox(12);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 18);
    w_add(box, w_label_wrap("Resumo com receitas, despesas e saldo, gráfico por categoria, evolução mensal, maiores despesas, contas, "
                            "metas e a lista de lançamentos do período.", "fin-muted"));
    GtkWidget *pills = w_hbox(6);
    for (int i = 0; i < 5; i++) {
        p->pills[i] = gtk_button_new_with_label(PRESET_LABELS[i]);
        gtk_widget_add_css_class(p->pills[i], "fin-pill");
        g_object_set_data(G_OBJECT(p->pills[i]), "k", GINT_TO_POINTER(i));
        g_signal_connect(p->pills[i], "clicked", G_CALLBACK(preset_clicked), p);
        w_add(pills, p->pills[i]);
    }
    GtkWidget *psw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(psw), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(psw), pills);
    w_add(box, psw);
    Day mf, mt;
    report_preset("mes", APP->today, DAY_NONE, DAY_NONE, &mf, &mt);
    GtkWidget *g = adw_preferences_group_new();
    p->from = row_date("De", from != DAY_NONE ? from : mf, FALSE);
    p->to = row_date("Até", to != DAY_NONE ? to : mt, FALSE);
    p->withtx = row_switch("Incluir a lista de lançamentos", "Todos os lançamentos do período, inclusive pendentes", TRUE);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), p->from);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), p->to);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), p->withtx);
    w_add(box, g);
    p->preview = w_label_wrap("", "fin-muted caption");
    w_add(box, p->preview);
    w_add(box, w_label_wrap("O PDF mostra os valores mesmo com “Ocultar valores” ligado e não é criptografado: guarde em local seguro e "
                            "cuidado ao compartilhar.", "fin-muted caption"));
    p->generate = gtk_button_new_with_label("Gerar PDF");
    gtk_widget_add_css_class(p->generate, "suggested-action");
    gtk_widget_add_css_class(p->generate, "pill");
    gtk_widget_set_halign(p->generate, GTK_ALIGN_CENTER);
    g_signal_connect(p->generate, "clicked", G_CALLBACK(pdf_generate), p);
    w_add(box, p->generate);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), box);
    adw_dialog_set_child(p->dialog, tv);
    g_signal_connect(p->from, "changed", G_CALLBACK(on_pdf_changed), p);
    g_signal_connect(p->to, "changed", G_CALLBACK(on_pdf_changed), p);
    pdf_update(p);
    adw_dialog_present(p->dialog, GTK_WIDGET(APP->window));
}
