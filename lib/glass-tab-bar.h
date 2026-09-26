/* glass-tab-bar.h — a tab bar on glass (design.md §6.8).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#if !defined(GLASS_INSIDE) && !defined(GLASS_COMPILATION)
#error "Only <glass.h> can be included directly."
#endif

#include <adwaita.h>

G_BEGIN_DECLS

#define GLASS_TYPE_TAB_BAR (glass_tab_bar_get_type ())
G_DECLARE_FINAL_TYPE (GlassTabBar, glass_tab_bar, GLASS, TAB_BAR, GtkWidget)

GtkWidget    *glass_tab_bar_new       (void);

AdwViewStack *glass_tab_bar_get_stack (GlassTabBar  *self);
void          glass_tab_bar_set_stack (GlassTabBar  *self,
                                       AdwViewStack *stack);

G_END_DECLS
