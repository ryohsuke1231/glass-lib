/* glass-adaptive.h — picking the foreground colour from what is under the
 * glass (design.md §12). Private; plain C so it can be tested without GTK.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* Relative luminance of the glass (backdrop mixed with the tint) over a grid
 * of cells: the 10th, 50th and 90th percentile of the cells' means. */
typedef struct {
  double p10, p50, p90;
} GlassLumaStats;

/* pixels: premultiplied RGBA8, the capture. The rectangle (x, y, w, h) is the
 * panel in the capture's pixels; cells are at least min_cell_px pixels on a
 * side (the blur radius, so a cell's mean is close to the blurred value).
 * tint: rgb and strength (alpha), as the glass mixes it in. FALSE when the
 * rectangle holds no pixels. */
gboolean glass_adaptive_measure (const guint8   *pixels,
                                 gsize           stride,
                                 int             width,
                                 int             height,
                                 double          x,
                                 double          y,
                                 double          w,
                                 double          h,
                                 double          min_cell_px,
                                 const float     tint[4],
                                 GlassLumaStats *out);

/* The same values as GlassAdaptiveMode / GlassAppearance (glass-enums.h),
 * without depending on it. */
enum {
  GLASS_ADAPTIVE_PREF_NONE = 0,
  GLASS_ADAPTIVE_PREF_LIGHT = 1,   /* ambiguous -> light glass (dark foreground) */
  GLASS_ADAPTIVE_PREF_DARK = 2,
};
enum {
  GLASS_ADAPTIVE_UNKNOWN = 0,
  GLASS_ADAPTIVE_LIGHT = 1,
  GLASS_ADAPTIVE_DARK = 2,
};

typedef struct {
  int            appearance;       /* GLASS_ADAPTIVE_* */
  gboolean       have_ema;
  GlassLumaStats ema;
  gint64         last_sample_us;
  gint64         last_flip_us;
} GlassAdaptiveState;

void   glass_adaptive_state_init (GlassAdaptiveState *state);

/* Feeds one measurement taken at now_us; returns the appearance. */
int    glass_adaptive_update     (GlassAdaptiveState   *state,
                                  const GlassLumaStats *stats,
                                  gint64                now_us,
                                  int                   preference);

/* TRUE while the smoothed values have not caught up with the last sample:
 * the caller feeds that sample again later so a still backdrop converges. */
gboolean glass_adaptive_is_settling (const GlassAdaptiveState *state,
                                     const GlassLumaStats     *last);

double glass_adaptive_srgb_luminance (double r, double g, double b);

G_END_DECLS
