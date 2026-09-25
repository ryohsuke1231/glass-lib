/* glass-toolbar-view.h — content with bars floating over it (design.md §6.6).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#if !defined(GLASS_INSIDE) && !defined(GLASS_COMPILATION)
#error "Only <glass.h> can be included directly."
#endif

#include <gtk/gtk.h>

#include "glass-enums.h"

G_BEGIN_DECLS

#define GLASS_TYPE_TOOLBAR_VIEW (glass_toolbar_view_get_type ())
G_DECLARE_FINAL_TYPE (GlassToolbarView, glass_toolbar_view, GLASS, TOOLBAR_VIEW, GtkWidget)

GtkWidget      *glass_toolbar_view_new                   (void);

GtkWidget      *glass_toolbar_view_get_content           (GlassToolbarView *self);
void            glass_toolbar_view_set_content           (GlassToolbarView *self,
                                                          GtkWidget        *content);

void            glass_toolbar_view_add_top_bar           (GlassToolbarView *self,
                                                          GtkWidget        *widget);
void            glass_toolbar_view_add_bottom_bar        (GlassToolbarView *self,
                                                          GtkWidget        *widget);
void            glass_toolbar_view_remove                (GlassToolbarView *self,
                                                          GtkWidget        *widget);

GlassEdgeStyle  glass_toolbar_view_get_top_edge_style    (GlassToolbarView *self);
void            glass_toolbar_view_set_top_edge_style    (GlassToolbarView *self,
                                                          GlassEdgeStyle    style);
GlassEdgeStyle  glass_toolbar_view_get_bottom_edge_style (GlassToolbarView *self);
void            glass_toolbar_view_set_bottom_edge_style (GlassToolbarView *self,
                                                          GlassEdgeStyle    style);

int             glass_toolbar_view_get_top_bar_height    (GlassToolbarView *self);
int             glass_toolbar_view_get_bottom_bar_height (GlassToolbarView *self);

G_END_DECLS
