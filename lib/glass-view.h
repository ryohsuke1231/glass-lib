/* glass-view.h — a container that draws glass over its own content
 * (design.md §6, §7).
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

#define GLASS_TYPE_VIEW (glass_view_get_type ())
G_DECLARE_FINAL_TYPE (GlassView, glass_view, GLASS, VIEW, GtkWidget)

GtkWidget         *glass_view_new                 (void);

GtkWidget         *glass_view_get_content         (GlassView *self);
void               glass_view_set_content         (GlassView *self,
                                                   GtkWidget *content);

void               glass_view_add_overlay         (GlassView *self,
                                                   GtkWidget *widget);
void               glass_view_remove_overlay      (GlassView *self,
                                                   GtkWidget *widget);

void               glass_view_set_backdrop_color  (GlassView     *self,
                                                   const GdkRGBA *color);
gboolean           glass_view_get_backdrop_color  (GlassView *self,
                                                   GdkRGBA   *color);

GlassRendererMode  glass_view_get_active_renderer (GlassView *self);

G_END_DECLS
