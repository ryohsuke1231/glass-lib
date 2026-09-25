/* glass-group.h — panels whose glass flows together (design.md §6.7).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#if !defined(GLASS_INSIDE) && !defined(GLASS_COMPILATION)
#error "Only <glass.h> can be included directly."
#endif

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GLASS_TYPE_GROUP (glass_group_get_type ())
G_DECLARE_FINAL_TYPE (GlassGroup, glass_group, GLASS, GROUP, GtkWidget)

GtkWidget *glass_group_new         (void);

GtkWidget *glass_group_get_child   (GlassGroup *self);
void       glass_group_set_child   (GlassGroup *self,
                                    GtkWidget  *child);

double     glass_group_get_spacing (GlassGroup *self);
void       glass_group_set_spacing (GlassGroup *self,
                                    double      spacing);

G_END_DECLS
