/* glass-slider.h — a slider whose knob turns to glass when touched
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

#define GLASS_TYPE_SLIDER (glass_slider_get_type ())
G_DECLARE_FINAL_TYPE (GlassSlider, glass_slider, GLASS, SLIDER, GtkWidget)

GtkWidget     *glass_slider_new            (GtkAdjustment *adjustment);
GtkWidget     *glass_slider_new_with_range (double         min,
                                            double         max,
                                            double         step);

GtkAdjustment *glass_slider_get_adjustment (GlassSlider   *self);
void           glass_slider_set_adjustment (GlassSlider   *self,
                                            GtkAdjustment *adjustment);

double         glass_slider_get_value      (GlassSlider   *self);
void           glass_slider_set_value      (GlassSlider   *self,
                                            double         value);

G_END_DECLS
