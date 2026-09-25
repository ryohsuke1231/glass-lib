/* glass-dialog.h — a dialog on a sheet of glass (design.md §6.7).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#if !defined(GLASS_INSIDE) && !defined(GLASS_COMPILATION)
#error "Only <glass.h> can be included directly."
#endif

#include <adwaita.h>

G_BEGIN_DECLS

#define GLASS_TYPE_DIALOG (glass_dialog_get_type ())
G_DECLARE_FINAL_TYPE (GlassDialog, glass_dialog, GLASS, DIALOG, AdwDialog)

GtkWidget *glass_dialog_new         (void);

GtkWidget *glass_dialog_get_content (GlassDialog *self);
void       glass_dialog_set_content (GlassDialog *self,
                                     GtkWidget   *content);

G_END_DECLS
