/* glass-button.h — a button that is itself glass (design.md §6.6).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#if !defined(GLASS_INSIDE) && !defined(GLASS_COMPILATION)
#error "Only <glass.h> can be included directly."
#endif

#include "glass-panel.h"

G_BEGIN_DECLS

#define GLASS_TYPE_BUTTON (glass_button_get_type ())
G_DECLARE_FINAL_TYPE (GlassButton, glass_button, GLASS, BUTTON, GlassPanel)

GtkWidget  *glass_button_new_from_icon_name (const char *icon_name);
GtkWidget  *glass_button_new_with_label     (const char *label);

const char *glass_button_get_icon_name      (GlassButton *self);
void        glass_button_set_icon_name      (GlassButton *self,
                                             const char  *icon_name);

const char *glass_button_get_label          (GlassButton *self);
void        glass_button_set_label          (GlassButton *self,
                                             const char  *label);

G_END_DECLS
