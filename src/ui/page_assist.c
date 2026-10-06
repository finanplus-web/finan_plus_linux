/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Assistente: perguntas rápidas, resumo do mês e dicas. Tudo é calculado neste computador,
 * sem internet, a partir dos lançamentos. Cada resultado tem "Por quê?" com a regra usada.
 */
#include "pages.h"
#include "widgets.h"

GtkWidget *tip_item(const Insight *t, gboolean dismissible);

static struct {
    GtkWidget *cols, *left, *right, *entry, *answer, *off_note;
    char *asked;
    gboolean show_dismissed;
} A;

static void open_answer(gpointer p) {
    AskParsed *ap = p;
    const char *q = ap->category ? ap->category : ap->words[0];
    app_open_moves(q, ap->from, ap->to, ap->has_kind ? (int)ap->kind : -1);
}

static void render_answer(void) {
    w_clear(A.answer);
    if (!A.asked || !*A.asked) { gtk_widget_set_visible(A.answer, FALSE); return; }
    gtk_widget_set_visible(A.answer, TRUE);
    AskAnswer *a = ask_answer(A.asked, APP->state, APP->today, app_money_fmt());
    g_autofree char *q = g_strdup_printf("“%s”", A.asked);
    w_add(A.answer, w_label_wrap(q, "fin-muted caption"));
    w_add(A.answer, w_label_wrap(a->text, "heading"));
    w_add(A.answer, w_label_wrap(a->understood, "fin-muted caption"));
    if (a->matches->len) {
        GtkWidget *b = w_pill("Ver lançamentos", open_answer, a->parsed, (GDestroyNotify)ask_parsed_free);
        a->parsed = NULL; /* agora pertence ao botão */
        gtk_widget_set_halign(b, GTK_ALIGN_START);
        w_add(A.answer, b);
    }
    ask_answer_free(a);
}

static void ask(const char *q) {
    g_free(A.asked);
    A.asked = g_strstrip(g_strdup(q));
    render_answer();
}

static void on_ask(GtkWidget *w, gpointer u) { (void)w; (void)u; ask(gtk_editable_get_text(GTK_EDITABLE(A.entry))); }

static void on_example(gpointer text) {
    gtk_editable_set_text(GTK_EDITABLE(A.entry), text);
    ask(text);
}

void page_assist_focus_question(void) {
    if (A.entry && gtk_widget_get_visible(A.left)) gtk_widget_grab_focus(A.entry);
}

static void toggle_dismissed(gpointer u) { (void)u; A.show_dismissed = !A.show_dismissed; page_assist_refresh(); }

static void restore_tip(gpointer id) {
    prefs_restore_tip(APP->prefs, id);
    prefs_save(APP->prefs, NULL);
    app_refresh_page(PAGE_ASSIST);
    app_refresh_page(PAGE_HOME);
}

static void go_settings(gpointer u) { (void)u; app_show_page(PAGE_SETTINGS); }

void page_assist_refresh(void) {
    if (!A.cols || !APP->state) return;
    Prefs *pr = APP->prefs;
    gtk_orientable_set_orientation(GTK_ORIENTABLE(A.cols), APP->layout == LAYOUT_NARROW ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_visible(A.left, pr->assist_ask);
    gtk_widget_set_visible(A.off_note, !pr->assist_ask && !pr->assist_tips);
    render_answer();

    w_clear(A.right);
    gtk_widget_set_visible(A.right, pr->assist_tips);
    if (!pr->assist_tips) return;
    MoneyFmt money = app_money_fmt();
    MonthReport *r = insights_report(APP->state, APP->today, money);
    GtkWidget *rc = w_card(NULL);
    w_add(rc, w_eyebrow("Resumo"));
    w_add(rc, w_title(r->title));
    for (guint i = 0; i < r->lines->len; i++) {
        g_autofree char *l = g_strdup_printf("• %s", (char *)r->lines->pdata[i]);
        w_add(rc, w_label_wrap(l, NULL));
    }
    w_add(rc, w_why(r->why));
    month_report_free(r);
    w_add(A.right, rc);

    GtkWidget *tc = w_card(NULL);
    w_add(tc, w_eyebrow("Dicas"));
    g_autoptr(GPtrArray) all = insights_tips(APP->state, APP->today, money);
    int visible = 0, dismissed = 0;
    for (guint i = 0; i < all->len; i++) {
        Insight *t = all->pdata[i];
        if (prefs_tip_dismissed(pr, t->id)) { dismissed++; continue; }
        w_add(tc, tip_item(t, TRUE));
        visible++;
    }
    if (!visible) w_add(tc, w_label_wrap("Nenhuma dica no momento: nada fora do padrão nos seus lançamentos.", "fin-muted"));
    if (dismissed) {
        g_autofree char *l = A.show_dismissed ? g_strdup("Esconder dispensadas") : g_strdup_printf("Mostrar dispensadas (%d)", dismissed);
        GtkWidget *p = w_pill(l, toggle_dismissed, NULL, NULL);
        gtk_widget_set_halign(p, GTK_ALIGN_START);
        w_add(tc, p);
        if (A.show_dismissed)
            for (guint i = 0; i < all->len; i++) {
                Insight *t = all->pdata[i];
                if (!prefs_tip_dismissed(pr, t->id)) continue;
                w_add(tc, tip_item(t, FALSE));
                GtkWidget *b = w_pill("Mostrar de novo", restore_tip, g_strdup(t->id), g_free);
                gtk_widget_set_halign(b, GTK_ALIGN_START);
                w_add(tc, b);
            }
    }
    w_add(A.right, tc);
}

GtkWidget *page_assist_new(void) {
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 1400);
    adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp), 1100);
    GtkWidget *body = w_vbox(18);
    gtk_widget_set_margin_start(body, 24);
    gtk_widget_set_margin_end(body, 24);
    gtk_widget_set_margin_top(body, 20);
    gtk_widget_set_margin_bottom(body, 32);

    GtkWidget *head = w_vbox(2);
    GtkWidget *eb = w_hbox(6);
    w_add(eb, w_icon("auto-awesome", 14));
    GtkWidget *ebl = w_eyebrow("Assistente · neste computador");
    gtk_widget_set_hexpand(ebl, TRUE);
    w_add(eb, ebl);
    w_add(head, eb);
    w_add(head, w_label("Assistente", "fin-page-title"));
    w_add(head, w_label_wrap("Tudo é calculado neste computador, sem internet, a partir dos seus lançamentos. "
                             "Clique em “Por quê?” para ver a regra usada.", "fin-muted"));
    w_add(body, head);

    A.off_note = w_card(NULL);
    w_add(A.off_note, w_label_wrap("O resumo, as dicas e as perguntas estão desligados em Ajustes › Assistente.", NULL));
    GtkWidget *gs = w_pill("Abrir Ajustes", go_settings, NULL, NULL);
    gtk_widget_set_halign(gs, GTK_ALIGN_START);
    w_add(A.off_note, gs);
    w_add(body, A.off_note);

    A.cols = w_hbox(18);
    gtk_box_set_homogeneous(GTK_BOX(A.cols), FALSE);

    /* ---- perguntas ---- */
    A.left = w_card(NULL);
    gtk_widget_set_hexpand(A.left, TRUE);
    gtk_widget_set_valign(A.left, GTK_ALIGN_START);
    w_add(A.left, w_eyebrow("Pergunte"));
    w_add(A.left, w_title("Perguntas rápidas"));
    GtkWidget *row = w_hbox(8);
    A.entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(A.entry), "Ex.: quanto gastei com mercado em agosto?");
    gtk_entry_set_max_length(GTK_ENTRY(A.entry), 120);
    gtk_widget_set_hexpand(A.entry, TRUE);
    gtk_accessible_update_property(GTK_ACCESSIBLE(A.entry), GTK_ACCESSIBLE_PROPERTY_LABEL, "Sua pergunta", -1);
    g_signal_connect(A.entry, "activate", G_CALLBACK(on_ask), NULL);
    w_add(row, A.entry);
    GtkWidget *go = gtk_button_new_with_label("Perguntar");
    gtk_widget_add_css_class(go, "suggested-action");
    g_signal_connect(go, "clicked", G_CALLBACK(on_ask), NULL);
    w_add(row, go);
    w_add(A.left, row);
    GtkWidget *ex = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(ex), GTK_SELECTION_NONE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(ex), 6);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(ex), 6);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(ex), 3);
    for (guint i = 0; i < G_N_ELEMENTS(ASK_EXAMPLES); i++)
        gtk_flow_box_append(GTK_FLOW_BOX(ex), w_pill(ASK_EXAMPLES[i], on_example, (gpointer)ASK_EXAMPLES[i], NULL));
    w_add(A.left, ex);
    A.answer = w_vbox(6);
    gtk_widget_add_css_class(A.answer, "fin-tip");
    gtk_widget_set_visible(A.answer, FALSE);
    w_add(A.left, A.answer);
    w_add(A.left, w_label_wrap("Entende períodos (hoje, ontem, esta semana, mês passado, “últimos 30 dias”, nomes de meses e anos), "
                               "categorias suas e palavras da descrição. Não é um modelo de linguagem: a resposta mostra sempre "
                               "“Como entendi”.", "fin-muted caption"));
    w_add(A.cols, A.left);

    /* ---- resumo e dicas ---- */
    A.right = w_vbox(18);
    gtk_widget_set_hexpand(A.right, TRUE);
    gtk_widget_set_valign(A.right, GTK_ALIGN_START);
    w_add(A.cols, A.right);
    w_add(body, A.cols);

    adw_clamp_set_child(ADW_CLAMP(clamp), body);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), clamp);
    return sw;
}
