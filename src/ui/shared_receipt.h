/* Finan+ — Copyright (C) 2026 Juscelino Be
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <gtk/gtk.h>
#include "core/model.h"

G_BEGIN_DECLS

/* grava o comprovante de [e] em [path]: PDF ([pdf]) ou PNG de 1200 px de largura */
gboolean shared_receipt_write(const SharedExpense *e, const char *path, gboolean pdf, GError **error);

G_END_DECLS
