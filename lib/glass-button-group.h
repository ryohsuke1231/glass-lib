/* glass-button-group.h — a row of buttons on one capsule of glass
 * (design.md §6.6).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#if !defined(GLASS_INSIDE) && !defined(GLASS_COMPILATION)
#error "Only <glass.h> can be included directly."
#endif

#include "glass-panel.h"

G_BEGIN_DECLS

#define GLASS_TYPE_BUTTON_GROUP (glass_button_group_get_type ())
G_DECLARE_FINAL_TYPE (GlassButtonGroup, glass_button_group, GLASS, BUTTON_GROUP, GlassPanel)

GtkWidget *glass_button_group_new     (void);

void       glass_button_group_append  (GlassButtonGroup *self,
                                       GtkWidget        *child);
void       glass_button_group_prepend (GlassButtonGroup *self,
                                       GtkWidget        *child);
void       glass_button_group_remove  (GlassButtonGroup *self,
                                       GtkWidget        *child);
gboolean   glass_button_group_is_empty (GlassButtonGroup *self);

G_END_DECLS
