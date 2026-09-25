/* glass-header-bar.h — a title bar whose buttons float on glass (design.md §6.6).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#if !defined(GLASS_INSIDE) && !defined(GLASS_COMPILATION)
#error "Only <glass.h> can be included directly."
#endif

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GLASS_TYPE_HEADER_BAR (glass_header_bar_get_type ())
G_DECLARE_FINAL_TYPE (GlassHeaderBar, glass_header_bar, GLASS, HEADER_BAR, GtkWidget)

GtkWidget  *glass_header_bar_new                          (void);

void        glass_header_bar_pack_start                   (GlassHeaderBar *self,
                                                           GtkWidget      *child);
void        glass_header_bar_pack_end                     (GlassHeaderBar *self,
                                                           GtkWidget      *child);
void        glass_header_bar_remove                       (GlassHeaderBar *self,
                                                           GtkWidget      *child);

GtkWidget  *glass_header_bar_get_title_widget             (GlassHeaderBar *self);
void        glass_header_bar_set_title_widget             (GlassHeaderBar *self,
                                                           GtkWidget      *title_widget);

gboolean    glass_header_bar_get_show_title               (GlassHeaderBar *self);
void        glass_header_bar_set_show_title               (GlassHeaderBar *self,
                                                           gboolean        show_title);

gboolean    glass_header_bar_get_show_start_title_buttons (GlassHeaderBar *self);
void        glass_header_bar_set_show_start_title_buttons (GlassHeaderBar *self,
                                                           gboolean        setting);
gboolean    glass_header_bar_get_show_end_title_buttons   (GlassHeaderBar *self);
void        glass_header_bar_set_show_end_title_buttons   (GlassHeaderBar *self,
                                                           gboolean        setting);

const char *glass_header_bar_get_decoration_layout        (GlassHeaderBar *self);
void        glass_header_bar_set_decoration_layout        (GlassHeaderBar *self,
                                                           const char     *layout);

G_END_DECLS
