/* glass-popover.h — a popover of glass, and menus on it (design.md §6.7).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#if !defined(GLASS_INSIDE) && !defined(GLASS_COMPILATION)
#error "Only <glass.h> can be included directly."
#endif

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GLASS_TYPE_POPOVER (glass_popover_get_type ())
G_DECLARE_FINAL_TYPE (GlassPopover, glass_popover, GLASS, POPOVER, GtkPopover)

GtkWidget *glass_popover_new            (void);
GtkWidget *glass_popover_new_from_model (GMenuModel *model);

G_END_DECLS
