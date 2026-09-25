/* glass-split-view.h — content with a sidebar floating over it (design.md §6.6).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#if !defined(GLASS_INSIDE) && !defined(GLASS_COMPILATION)
#error "Only <glass.h> can be included directly."
#endif

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GLASS_TYPE_SPLIT_VIEW (glass_split_view_get_type ())
G_DECLARE_FINAL_TYPE (GlassSplitView, glass_split_view, GLASS, SPLIT_VIEW, GtkWidget)

GtkWidget  *glass_split_view_new                  (void);

GtkWidget  *glass_split_view_get_sidebar          (GlassSplitView *self);
void        glass_split_view_set_sidebar          (GlassSplitView *self,
                                                   GtkWidget      *sidebar);

GtkWidget  *glass_split_view_get_content          (GlassSplitView *self);
void        glass_split_view_set_content          (GlassSplitView *self,
                                                   GtkWidget      *content);

gboolean    glass_split_view_get_show_sidebar     (GlassSplitView *self);
void        glass_split_view_set_show_sidebar     (GlassSplitView *self,
                                                   gboolean        show_sidebar);

GtkPackType glass_split_view_get_sidebar_position (GlassSplitView *self);
void        glass_split_view_set_sidebar_position (GlassSplitView *self,
                                                   GtkPackType     position);

int         glass_split_view_get_sidebar_width    (GlassSplitView *self);
void        glass_split_view_set_sidebar_width    (GlassSplitView *self,
                                                   int             width);

int         glass_split_view_get_content_inset    (GlassSplitView *self);

G_END_DECLS
