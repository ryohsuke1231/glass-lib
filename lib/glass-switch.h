/* glass-switch.h — an on/off switch whose knob turns to glass when touched
 * (design.md §6.7).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#if !defined(GLASS_INSIDE) && !defined(GLASS_COMPILATION)
#error "Only <glass.h> can be included directly."
#endif

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GLASS_TYPE_SWITCH (glass_switch_get_type ())
G_DECLARE_FINAL_TYPE (GlassSwitch, glass_switch, GLASS, SWITCH, GtkWidget)

GtkWidget *glass_switch_new        (void);

gboolean   glass_switch_get_active (GlassSwitch *self);
void       glass_switch_set_active (GlassSwitch *self,
                                    gboolean     active);

G_END_DECLS
