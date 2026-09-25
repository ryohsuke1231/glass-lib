/* glass-panel.h — a pane of glass with one child (design.md §6).
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

#define GLASS_TYPE_PANEL (glass_panel_get_type ())
G_DECLARE_DERIVABLE_TYPE (GlassPanel, glass_panel, GLASS, PANEL, GtkWidget)

struct _GlassPanelClass
{
  GtkWidgetClass parent_class;

  /*< private >*/
  gpointer padding[8];
};

GtkWidget         *glass_panel_new               (void);

GtkWidget         *glass_panel_get_child         (GlassPanel *self);
void               glass_panel_set_child         (GlassPanel *self,
                                                  GtkWidget  *child);

GlassMaterial      glass_panel_get_material      (GlassPanel *self);
void               glass_panel_set_material      (GlassPanel    *self,
                                                  GlassMaterial  material);

double             glass_panel_get_corner_radius (GlassPanel *self);
void               glass_panel_set_corner_radius (GlassPanel *self,
                                                  double      radius);

gboolean           glass_panel_get_tint          (GlassPanel *self,
                                                  GdkRGBA    *tint);
void               glass_panel_set_tint          (GlassPanel    *self,
                                                  const GdkRGBA *tint);

gboolean           glass_panel_get_has_shadow    (GlassPanel *self);
void               glass_panel_set_has_shadow    (GlassPanel *self,
                                                  gboolean    has_shadow);

GlassAdaptiveMode  glass_panel_get_adaptive      (GlassPanel *self);
void               glass_panel_set_adaptive      (GlassPanel        *self,
                                                  GlassAdaptiveMode  mode);

GlassAppearance    glass_panel_get_appearance    (GlassPanel *self);

gboolean           glass_panel_get_interactive   (GlassPanel *self);
void               glass_panel_set_interactive   (GlassPanel *self,
                                                  gboolean    interactive);

G_END_DECLS
