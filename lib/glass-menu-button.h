/* glass-menu-button.h — a button that opens a glass popover or menu
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

#define GLASS_TYPE_MENU_BUTTON (glass_menu_button_get_type ())
G_DECLARE_FINAL_TYPE (GlassMenuButton, glass_menu_button, GLASS, MENU_BUTTON, GtkWidget)

GtkWidget  *glass_menu_button_new            (void);

const char *glass_menu_button_get_icon_name  (GlassMenuButton *self);
void        glass_menu_button_set_icon_name  (GlassMenuButton *self,
                                              const char      *icon_name);
const char *glass_menu_button_get_label      (GlassMenuButton *self);
void        glass_menu_button_set_label      (GlassMenuButton *self,
                                              const char      *label);

GMenuModel *glass_menu_button_get_menu_model (GlassMenuButton *self);
void        glass_menu_button_set_menu_model (GlassMenuButton *self,
                                              GMenuModel      *model);
GtkPopover *glass_menu_button_get_popover    (GlassMenuButton *self);
void        glass_menu_button_set_popover    (GlassMenuButton *self,
                                              GtkPopover      *popover);

void        glass_menu_button_popup          (GlassMenuButton *self);
void        glass_menu_button_popdown        (GlassMenuButton *self);

G_END_DECLS
