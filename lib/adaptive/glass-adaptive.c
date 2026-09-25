/* glass-adaptive.c — see glass-adaptive.h and design.md §12.
 *
 * The decision is the extension's contrastSampler.ts decideTextColor() with
 * the change design.md §12 makes: each candidate is scored against the part
 * of the background where it is worst (light text against the 90th
 * percentile, dark text against the 10th) instead of one trimmed mean, and
 * the history is an EMA over time instead of over samples.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-adaptive.h"

#include <math.h>
#include <string.h>

/* contrastSampler.ts */
#define SWITCH_ADVANTAGE                   1.2
#define SWITCH_ADVANTAGE_TOWARD_PREFERRED  1.02
#define SWITCH_ADVANTAGE_AGAINST_PREFERRED 1.6
#define AMBIGUOUS_RATIO                    1.15
#define MIN_READABLE_CONTRAST              4.5
#define LIGHT_FG                           0xf2   /* #f2f2f2 */
#define DARK_FG                            0x1a   /* #1a1a1a */

/* design.md §12.3 */
#define EMA_TAU_US   150000.0
#define HOLD_US      400000

#define MAX_CELLS_X 16
#define MAX_CELLS_Y 4
#define MIN_CELLS_X 4
#define MIN_CELLS_Y 2

static double
srgb_to_linear (double c)
{
  return c <= 0.04045 ? c / 12.92 : pow ((c + 0.055) / 1.055, 2.4);
}

double
glass_adaptive_srgb_luminance (double r, double g, double b)
{
  return 0.2126 * srgb_to_linear (r) + 0.7152 * srgb_to_linear (g) + 0.0722 * srgb_to_linear (b);
}

static double
contrast (double a, double b)
{
  return (MAX (a, b) + 0.05) / (MIN (a, b) + 0.05);
}

static int
compare_double (const void *a, const void *b)
{
  double x = *(const double *) a, y = *(const double *) b;

  return x < y ? -1 : x > y ? 1 : 0;
}

/* Nearest-rank percentile of sorted values. */
static double
percentile (const double *sorted, int n, double p)
{
  int i = (int) ceil (p * n) - 1;

  return sorted[CLAMP (i, 0, n - 1)];
}

gboolean
glass_adaptive_measure (const guint8   *pixels,
                        gsize           stride,
                        int             width,
                        int             height,
                        double          x,
                        double          y,
                        double          w,
                        double          h,
                        double          min_cell_px,
                        const float     tint[4],
                        GlassLumaStats *out)
{
  double lumas[MAX_CELLS_X * MAX_CELLS_Y];
  int x0 = MAX (0, (int) floor (x));
  int y0 = MAX (0, (int) floor (y));
  int x1 = MIN (width, (int) ceil (x + w));
  int y1 = MIN (height, (int) ceil (y + h));
  int cells_x, cells_y, n = 0;
  double cell = MAX (min_cell_px, 1.0);

  if (x1 <= x0 || y1 <= y0)
    return FALSE;

  /* A grid that follows the aspect ratio, cells no smaller than `cell`. */
  cells_x = CLAMP ((int) ((x1 - x0) / cell), MIN_CELLS_X, MAX_CELLS_X);
  cells_y = CLAMP ((int) ((y1 - y0) / cell), MIN_CELLS_Y, MAX_CELLS_Y);
  cells_x = MIN (cells_x, x1 - x0);
  cells_y = MIN (cells_y, y1 - y0);

  for (int cy = 0; cy < cells_y; cy++)
    for (int cx = 0; cx < cells_x; cx++)
      {
        int ax = x0 + (x1 - x0) * cx / cells_x;
        int bx = x0 + (x1 - x0) * (cx + 1) / cells_x;
        int ay = y0 + (y1 - y0) * cy / cells_y;
        int by = y0 + (y1 - y0) * (cy + 1) / cells_y;
        double sr = 0, sg = 0, sb = 0, sa = 0;
        int count = 0;

        for (int py = ay; py < by; py++)
          {
            const guint8 *row = pixels + (gsize) py * stride;

            for (int px = ax; px < bx; px++)
              {
                const guint8 *p = row + px * 4;

                sr += p[0];
                sg += p[1];
                sb += p[2];
                sa += p[3];
                count++;
              }
          }
        if (count == 0 || sa <= 0.0)
          continue;

        /* Un-premultiply the cell's mean, then mix in the tint the way the
         * glass does (baseColor = mix(refracted, tint, strength)). */
        {
          double r = sr / sa, g = sg / sa, b = sb / sa;

          r = r * (1.0 - tint[3]) + tint[0] * tint[3];
          g = g * (1.0 - tint[3]) + tint[1] * tint[3];
          b = b * (1.0 - tint[3]) + tint[2] * tint[3];
          lumas[n++] = glass_adaptive_srgb_luminance (r, g, b);
        }
      }

  if (n == 0)
    return FALSE;

  qsort (lumas, n, sizeof lumas[0], compare_double);
  out->p10 = percentile (lumas, n, 0.10);
  out->p50 = percentile (lumas, n, 0.50);
  out->p90 = percentile (lumas, n, 0.90);

  return TRUE;
}

void
glass_adaptive_state_init (GlassAdaptiveState *state)
{
  memset (state, 0, sizeof *state);
  state->appearance = GLASS_ADAPTIVE_UNKNOWN;
}

/* Scores of the two candidates: light text is worst on the brightest part of
 * the background, dark text on the darkest. */
static void
scores (const GlassLumaStats *s, double *light_fg, double *dark_fg)
{
  static double l_light = -1.0, l_dark;

  if (l_light < 0.0)
    {
      l_light = glass_adaptive_srgb_luminance (LIGHT_FG / 255.0, LIGHT_FG / 255.0, LIGHT_FG / 255.0);
      l_dark = glass_adaptive_srgb_luminance (DARK_FG / 255.0, DARK_FG / 255.0, DARK_FG / 255.0);
    }
  *light_fg = contrast (l_light, s->p90);
  *dark_fg = contrast (s->p10, l_dark);
}

int
glass_adaptive_update (GlassAdaptiveState   *state,
                       const GlassLumaStats *stats,
                       gint64                now_us,
                       int                   preference)
{
  gboolean has_pref = preference != GLASS_ADAPTIVE_PREF_NONE;
  gboolean prefer_light = preference == GLASS_ADAPTIVE_PREF_LIGHT;
  double raw_light_fg, raw_dark_fg, light_fg, dark_fg;
  gboolean ambiguous, is_light;
  int previous = state->appearance;

  /* Time-based EMA (tau 150 ms), so the smoothing does not depend on the
   * frame rate the samples arrive at. */
  if (!state->have_ema)
    {
      state->ema = *stats;
      state->have_ema = TRUE;
    }
  else
    {
      double dt = MAX (0.0, (double) (now_us - state->last_sample_us));
      double a = 1.0 - exp (-dt / EMA_TAU_US);

      state->ema.p10 += (stats->p10 - state->ema.p10) * a;
      state->ema.p50 += (stats->p50 - state->ema.p50) * a;
      state->ema.p90 += (stats->p90 - state->ema.p90) * a;
    }
  state->last_sample_us = now_us;

  /* Hold still for a moment after a flip. */
  if (previous != GLASS_ADAPTIVE_UNKNOWN && now_us - state->last_flip_us < HOLD_US)
    return previous;

  scores (stats, &raw_light_fg, &raw_dark_fg);
  scores (&state->ema, &light_fg, &dark_fg);

  /* Measured from the raw values: the background as it is now. */
  ambiguous = MAX (raw_light_fg, raw_dark_fg) < MIN (raw_light_fg, raw_dark_fg) * AMBIGUOUS_RATIO;

  if (previous == GLASS_ADAPTIVE_UNKNOWN)
    is_light = (ambiguous && has_pref) ? prefer_light : dark_fg > light_fg;
  else if (ambiguous && has_pref)
    is_light = prefer_light;
  else
    {
      double current, alternative, advantage;
      gboolean alternative_is_preferred;

      is_light = previous == GLASS_ADAPTIVE_LIGHT;
      current = is_light ? dark_fg : light_fg;
      alternative = is_light ? light_fg : dark_fg;
      alternative_is_preferred = has_pref && (prefer_light != is_light);
      advantage = !has_pref ? SWITCH_ADVANTAGE
                  : alternative_is_preferred ? SWITCH_ADVANTAGE_TOWARD_PREFERRED
                  : SWITCH_ADVANTAGE_AGAINST_PREFERRED;
      if (alternative > current * advantage)
        is_light = !is_light;
    }

  /* Never keep unreadable text when the other colour is readable, whatever
   * the smoothing or the preference says. */
  {
    double raw_current = is_light ? raw_dark_fg : raw_light_fg;
    double raw_alternative = is_light ? raw_light_fg : raw_dark_fg;

    if (raw_current < MIN_READABLE_CONTRAST && raw_alternative >= MIN_READABLE_CONTRAST)
      is_light = !is_light;
  }

  state->appearance = is_light ? GLASS_ADAPTIVE_LIGHT : GLASS_ADAPTIVE_DARK;
  if (previous != GLASS_ADAPTIVE_UNKNOWN && previous != state->appearance)
    state->last_flip_us = now_us;

  return state->appearance;
}

gboolean
glass_adaptive_is_settling (const GlassAdaptiveState *state,
                            const GlassLumaStats     *last)
{
  const double eps = 0.002;

  return state->have_ema &&
         (fabs (state->ema.p10 - last->p10) > eps ||
          fabs (state->ema.p50 - last->p50) > eps ||
          fabs (state->ema.p90 - last->p90) > eps);
}
