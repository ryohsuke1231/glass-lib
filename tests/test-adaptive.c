/* test-adaptive.c — the adaptive foreground decision against
 * spec/adaptive-vectors.json, and the cell statistics (design.md §12, §16).
 *
 * SPDX-License-Identifier: MIT
 */
#include <glib.h>
#include <math.h>

#include "adaptive/glass-adaptive.h"
#include "adaptive-vectors.h"

static const char *
appearance_name (int a)
{
  return a == GLASS_ADAPTIVE_LIGHT ? "light" : a == GLASS_ADAPTIVE_DARK ? "dark" : "unknown";
}

static void
test_vector (gconstpointer data)
{
  const VectorCase *c = data;
  GlassAdaptiveState state;

  glass_adaptive_state_init (&state);
  for (int i = 0; i < c->n; i++)
    {
      const VectorSample *s = &c->samples[i];
      GlassLumaStats stats = { s->luma[0], s->luma[1], s->luma[2] };
      int got = glass_adaptive_update (&state, &stats, s->t_ms * 1000, c->pref);

      if (got != s->expect)
        g_error ("%s, sample %d (t = %" G_GINT64_FORMAT " ms): expected %s, got %s",
                 c->name, i, s->t_ms, appearance_name (s->expect), appearance_name (got));
    }
}

/* A uniform image measures as its own luminance, the tint mixed in. */
static void
test_measure_uniform (void)
{
  guint8 pixels[16 * 8 * 4];
  const float no_tint[4] = { 1, 1, 1, 0 };
  const float half_white[4] = { 1, 1, 1, 0.5f };
  GlassLumaStats stats;
  double expected;

  for (gsize i = 0; i < sizeof pixels; i += 4)
    {
      pixels[i] = pixels[i + 1] = pixels[i + 2] = 128;
      pixels[i + 3] = 255;
    }

  g_assert_true (glass_adaptive_measure (pixels, 16 * 4, 16, 8, 0, 0, 16, 8, 2, no_tint, &stats));
  expected = glass_adaptive_srgb_luminance (128 / 255.0, 128 / 255.0, 128 / 255.0);
  g_assert_cmpfloat_with_epsilon (stats.p10, expected, 1e-9);
  g_assert_cmpfloat_with_epsilon (stats.p90, expected, 1e-9);

  g_assert_true (glass_adaptive_measure (pixels, 16 * 4, 16, 8, 0, 0, 16, 8, 2, half_white, &stats));
  expected = glass_adaptive_srgb_luminance ((128 / 255.0 + 1.0) / 2, (128 / 255.0 + 1.0) / 2, (128 / 255.0 + 1.0) / 2);
  g_assert_cmpfloat_with_epsilon (stats.p50, expected, 1e-9);

  /* Nothing to measure outside the image. */
  g_assert_false (glass_adaptive_measure (pixels, 16 * 4, 16, 8, 20, 0, 4, 4, 2, no_tint, &stats));
}

/* Left half black, right half white: the percentiles see both. */
static void
test_measure_split (void)
{
  guint8 pixels[32 * 8 * 4];
  const float no_tint[4] = { 1, 1, 1, 0 };
  GlassLumaStats stats;

  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 32; x++)
      {
        guint8 *p = pixels + (y * 32 + x) * 4;

        p[0] = p[1] = p[2] = x < 16 ? 0 : 255;
        p[3] = 255;
      }

  g_assert_true (glass_adaptive_measure (pixels, 32 * 4, 32, 8, 0, 0, 32, 8, 2, no_tint, &stats));
  g_assert_cmpfloat (stats.p10, <, 0.01);
  g_assert_cmpfloat (stats.p90, >, 0.99);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  for (gsize i = 0; i < G_N_ELEMENTS (vector_cases); i++)
    {
      g_autofree char *path = g_strdup_printf ("/adaptive/vectors/%s", vector_cases[i].name);

      g_strdelimit (path + strlen ("/adaptive/vectors/"), "/", '-');
      g_test_add_data_func (path, &vector_cases[i], test_vector);
    }
  g_test_add_func ("/adaptive/measure/uniform", test_measure_uniform);
  g_test_add_func ("/adaptive/measure/split", test_measure_split);

  return g_test_run ();
}
