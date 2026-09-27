/* glass-toggle-group.h — a segmented control on glass (design.md §6.6).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#if !defined(GLASS_INSIDE) && !defined(GLASS_COMPILATION)
#error "Only <glass.h> can be included directly."
#endif

#include "glass-panel.h"

G_BEGIN_DECLS

#define GLASS_TYPE_TOGGLE_GROUP (glass_toggle_group_get_type ())
G_DECLARE_FINAL_TYPE (GlassToggleGroup, glass_toggle_group, GLASS, TOGGLE_GROUP, GlassPanel)

GtkWidget  *glass_toggle_group_new             (void);

void        glass_toggle_group_append          (GlassToggleGroup *self,
                                                const char       *name,
                                                const char       *label,
                                                const char       *icon_name);

void        glass_toggle_group_remove          (GlassToggleGroup *self,
                                                guint             index);
void        glass_toggle_group_remove_all      (GlassToggleGroup *self);

const char *glass_toggle_group_get_tooltip     (GlassToggleGroup *self,
                                                guint             index);
void        glass_toggle_group_set_tooltip     (GlassToggleGroup *self,
                                                guint             index,
                                                const char       *tooltip);

guint       glass_toggle_group_get_n_toggles   (GlassToggleGroup *self);

guint       glass_toggle_group_get_active      (GlassToggleGroup *self);
void        glass_toggle_group_set_active      (GlassToggleGroup *self,
                                                guint             active);

const char *glass_toggle_group_get_active_name (GlassToggleGroup *self);
void        glass_toggle_group_set_active_name (GlassToggleGroup *self,
                                                const char       *name);

G_END_DECLS
