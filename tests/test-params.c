/* test-params.c — GlassContext's parameters and how materials resolve them
 * (design.md §11, §16).
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

#include <math.h>

static void
test_list (void)
{
  GlassContext *ctx = glass_context_get_default ();
  const char * const *keys = glass_context_list_params (ctx);
  guint n = g_strv_length ((char **) keys);

  g_assert_cmpuint (n, ==, GLASS_N_PARAMS);
  for (guint i = 0; i < n; i++)
    {
      double min, max, def;

      g_assert_true (glass_context_get_param_range (ctx, keys[i], &min, &max, &def));
      g_assert_cmpfloat (min, <=, def);
      g_assert_cmpfloat (def, <=, max);
      g_assert_false (glass_context_is_param_set (ctx, keys[i]));
      g_assert_cmpfloat (glass_context_get_param (ctx, keys[i]), ==, def);
    }
}

/* The extension's defaults (design.md §11.1). */
static void
test_defaults (void)
{
  GlassContext *ctx = glass_context_get_default ();

  g_assert_cmpfloat (glass_context_get_param (ctx, "max-z"), ==, 88.0);
  g_assert_cmpfloat (glass_context_get_param (ctx, "displacement-scale"), ==, 10.5);
  g_assert_cmpfloat (glass_context_get_param (ctx, "rim-width"), ==, 2.3);
  g_assert_cmpfloat (glass_context_get_param (ctx, "shadow-intensity"), ==, 0.22);
  g_assert_cmpfloat (glass_context_get_param (ctx, "light-angle-deg"), ==, 90.0);
  g_assert_true (isnan (glass_context_get_param (ctx, "no-such-key")));
}

/* Materials: the edge measured on macOS 27 (apple-s; apple-l for CLEAR),
 * with the blur, tint and shadow the user chose. */
static void
test_materials (void)
{
  GlassContext *ctx = glass_context_get_default ();
  double v[GLASS_N_PARAMS];

  glass_context_resolve (ctx, GLASS_MATERIAL_REGULAR, v);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_BLUR_RADIUS], ==, 2.0);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_PROFILE_SHAPE_N], ==, 3.6);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_MAX_Z], ==, 88.0);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_RIM_DIRECTIONAL_POWER], ==, 1.9);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_TINT_STRENGTH], ==, 0.12);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_SHEEN_INTENSITY], ==, 0.0);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_SHADOW_INTENSITY], ==, 0.07);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_DISPLACEMENT_SCALE], ==, 10.5);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_RIM_WIDTH], ==, 2.3);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_CHROMA_STRENGTH], ==, 0.0);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_SHADOW_RADIUS], ==, 16.0);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_IOR], ==, 2.4);               /* not a material key */
  g_assert_cmpfloat (v[GLASS_PARAM_ID_BLUR_DOWNSCALE], ==, 2.0);

  glass_context_resolve (ctx, GLASS_MATERIAL_CLEAR, v);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_BLUR_DOWNSCALE], ==, 1.0);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_TINT_STRENGTH], ==, 0.04);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_MAX_Z], ==, 100.0);           /* the thick lens */
  g_assert_cmpfloat (v[GLASS_PARAM_ID_DISPLACEMENT_SCALE], ==, 26.0);

  glass_context_resolve (ctx, GLASS_MATERIAL_THICK, v);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_BLUR_RADIUS], ==, 12.0);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_TINT_STRENGTH], ==, 0.55);
  g_assert_true (glass_material_specs[GLASS_MATERIAL_THICK].tint_from_theme);
  g_assert_false (glass_material_specs[GLASS_MATERIAL_THICK].adaptive);

  /* Menus: lighter than THICK, the theme's colours. */
  g_assert_cmpint (GLASS_N_MATERIALS, ==, GLASS_MATERIAL_PROMINENT + 1);
  glass_context_resolve (ctx, GLASS_MATERIAL_MENU, v);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_BLUR_RADIUS], ==, 8.0);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_MAX_Z], ==, 88.0);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_TINT_STRENGTH], ==, 0.45);
  g_assert_true (glass_material_specs[GLASS_MATERIAL_MENU].tint_from_theme);
  g_assert_false (glass_material_specs[GLASS_MATERIAL_MENU].adaptive);

  /* The button that confirms: the accent, strong, frosted. */
  glass_context_resolve (ctx, GLASS_MATERIAL_PROMINENT, v);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_TINT_STRENGTH], ==, 0.92);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_BLUR_RADIUS], ==, 10.0);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_MAX_Z], ==, 88.0);
  g_assert_true (glass_material_specs[GLASS_MATERIAL_PROMINENT].tint_from_accent);
  g_assert_false (glass_material_specs[GLASS_MATERIAL_PROMINENT].tint_from_theme);
  g_assert_false (glass_material_specs[GLASS_MATERIAL_PROMINENT].adaptive);
  for (int m = 0; m < GLASS_MATERIAL_PROMINENT; m++)
    g_assert_false (glass_material_specs[m].tint_from_accent);
}

/* The lenses: the materials' own, and set together on the context. */
static void
test_lens (void)
{
  GlassContext *ctx = glass_context_get_default ();
  double v[GLASS_N_PARAMS];
  guint gen;

  g_assert_cmpint (GLASS_N_LENSES, ==, GLASS_LENS_THICK + 1);
  /* CLEAR is the thick lens, the others the thin one. */
  glass_context_resolve (ctx, GLASS_MATERIAL_CLEAR, v);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_MAX_Z], ==, glass_lens_specs[GLASS_LENS_THICK].max_z);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_PROFILE_SHAPE_N], ==, glass_lens_specs[GLASS_LENS_THICK].profile_shape_n);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_DISPLACEMENT_SCALE], ==, glass_lens_specs[GLASS_LENS_THICK].displacement_scale);
  glass_context_resolve (ctx, GLASS_MATERIAL_REGULAR, v);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_DISPLACEMENT_SCALE], ==, glass_lens_specs[GLASS_LENS_THIN].displacement_scale);

  /* One change for the three keys. */
  gen = glass_context_get_generation (ctx);
  glass_context_set_lens (ctx, GLASS_LENS_THICK);
  g_assert_cmpuint (glass_context_get_generation (ctx), ==, gen + 1);
  g_assert_true (glass_context_is_param_set (ctx, "max-z"));
  g_assert_true (glass_context_is_param_set (ctx, "profile-shape-n"));
  g_assert_true (glass_context_is_param_set (ctx, "displacement-scale"));
  glass_context_resolve (ctx, GLASS_MATERIAL_REGULAR, v);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_MAX_Z], ==, 100.0);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_PROFILE_SHAPE_N], ==, 1.35);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_DISPLACEMENT_SCALE], ==, 26.0);
  /* The same again changes nothing. */
  gen = glass_context_get_generation (ctx);
  glass_context_set_lens (ctx, GLASS_LENS_THICK);
  g_assert_cmpuint (glass_context_get_generation (ctx), ==, gen);

  glass_context_reset_param (ctx, "max-z");
  glass_context_reset_param (ctx, "profile-shape-n");
  glass_context_reset_param (ctx, "displacement-scale");
  glass_context_resolve (ctx, GLASS_MATERIAL_REGULAR, v);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_MAX_Z], ==, 88.0);
}

/* An explicit value beats every material; reset returns to them. */
static void
test_override (void)
{
  GlassContext *ctx = glass_context_get_default ();
  double v[GLASS_N_PARAMS];
  guint gen = glass_context_get_generation (ctx);

  /* The public constants are the keys (glass-params.h). */
  g_assert_cmpstr (GLASS_PARAM_MAX_Z, ==, "max-z");
  g_assert_cmpstr (GLASS_PARAM_BLUR_RADIUS, ==, glass_param_specs[GLASS_PARAM_ID_BLUR_RADIUS].key);
  g_assert_true (glass_context_set_param (ctx, GLASS_PARAM_MAX_Z, 30.0));
  g_assert_cmpuint (glass_context_get_generation (ctx), >, gen);
  g_assert_true (glass_context_is_param_set (ctx, "max-z"));
  glass_context_resolve (ctx, GLASS_MATERIAL_REGULAR, v);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_MAX_Z], ==, 30.0);
  glass_context_resolve (ctx, GLASS_MATERIAL_THICK, v);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_MAX_Z], ==, 30.0);

  glass_context_reset_param (ctx, "max-z");
  g_assert_false (glass_context_is_param_set (ctx, "max-z"));
  glass_context_resolve (ctx, GLASS_MATERIAL_REGULAR, v);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_MAX_Z], ==, 88.0);
}

/* The tint: the context's colour (else the material's, or the theme's for
 * those tinted from it) with the tint-strength in effect. */
static void
test_tint (void)
{
  GlassContext *ctx = glass_context_get_default ();
  GdkRGBA theme = { 0.2f, 0.3f, 0.4f, 1.0f };
  GdkRGBA pink = { 1.0f, 0.4f, 0.6f, 0.3f };   /* alpha is not used */
  GdkRGBA got;
  double v[GLASS_N_PARAMS];
  float t[4];
  guint gen;

  glass_context_resolve (ctx, GLASS_MATERIAL_REGULAR, v);
  glass_context_resolve_tint (ctx, GLASS_MATERIAL_REGULAR, v[GLASS_PARAM_ID_TINT_STRENGTH], &theme, t);
  g_assert_cmpfloat (t[0], ==, 1.0f);
  g_assert_cmpfloat (t[3], ==, 0.12f);
  glass_context_resolve_tint (ctx, GLASS_MATERIAL_THICK, 0.55, &theme, t);
  g_assert_cmpfloat (t[0], ==, 0.2f);
  g_assert_cmpfloat (t[2], ==, 0.4f);

  g_assert_false (glass_context_get_tint_color (ctx, NULL));
  gen = glass_context_get_generation (ctx);
  glass_context_set_tint_color (ctx, &pink);
  g_assert_cmpuint (glass_context_get_generation (ctx), >, gen);
  g_assert_true (glass_context_get_tint_color (ctx, &got));
  g_assert_cmpfloat (got.green, ==, 0.4f);
  g_assert_cmpfloat (got.alpha, ==, 1.0f);
  glass_context_resolve_tint (ctx, GLASS_MATERIAL_THICK, 0.55, &theme, t);
  g_assert_cmpfloat (t[0], ==, 1.0f);
  g_assert_cmpfloat (t[1], ==, 0.4f);
  g_assert_cmpfloat (t[3], ==, 0.55f);

  glass_context_set_param (ctx, "tint-strength", 0.3);
  glass_context_resolve (ctx, GLASS_MATERIAL_THICK, v);
  g_assert_cmpfloat (v[GLASS_PARAM_ID_TINT_STRENGTH], ==, 0.3);

  glass_context_reset_param (ctx, "tint-strength");
  glass_context_set_tint_color (ctx, NULL);
  g_assert_false (glass_context_get_tint_color (ctx, NULL));
}

static void
test_clamp (void)
{
  GlassContext *ctx = glass_context_get_default ();

  g_test_expect_message ("glass", G_LOG_LEVEL_WARNING, "*outside*");
  g_assert_true (glass_context_set_param (ctx, "ior", 9.0));
  g_test_assert_expected_messages ();
  g_assert_cmpfloat (glass_context_get_param (ctx, "ior"), ==, 4.0);
  glass_context_reset_param (ctx, "ior");

  g_test_expect_message ("glass", G_LOG_LEVEL_WARNING, "*no parameter*");
  g_assert_false (glass_context_set_param (ctx, "no-such-key", 1.0));
  g_test_assert_expected_messages ();
}

/* Only meaningful values: the downscale snaps to 1, 2 or 4. */
static void
test_values (void)
{
  GlassContext *ctx = glass_context_get_default ();

  glass_context_set_param (ctx, "blur-downscale", 3.2);
  g_assert_cmpfloat (glass_context_get_param (ctx, "blur-downscale"), ==, 4.0);
  glass_context_set_param (ctx, "blur-downscale", 1.4);
  g_assert_cmpfloat (glass_context_get_param (ctx, "blur-downscale"), ==, 1.0);
  glass_context_reset_param (ctx, "blur-downscale");
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/params/list", test_list);
  g_test_add_func ("/params/defaults", test_defaults);
  g_test_add_func ("/params/materials", test_materials);
  g_test_add_func ("/params/lens", test_lens);
  g_test_add_func ("/params/override", test_override);
  g_test_add_func ("/params/tint", test_tint);
  g_test_add_func ("/params/clamp", test_clamp);
  g_test_add_func ("/params/values", test_values);

  return g_test_run ();
}
