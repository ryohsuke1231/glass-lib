/* glass-main.c — initialisation, debug flags and the stylesheet.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

#include <adwaita.h>
#include <math.h>

static gboolean initialized;

/**
 * glass_init:
 *
 * Initialises glass-lib: registers its types and loads its stylesheet.
 * Call it once after `gtk_init()` (an `AdwApplication` does that for you);
 * it also initialises libadwaita.
 */
void
glass_init (void)
{
  GdkDisplay *display;

  if (initialized)
    return;
  initialized = TRUE;

  adw_init ();

  g_type_ensure (GLASS_TYPE_CONTEXT);
  g_type_ensure (GLASS_TYPE_VIEW);
  g_type_ensure (GLASS_TYPE_PANEL);
  g_type_ensure (GLASS_TYPE_TOOLBAR_VIEW);
  g_type_ensure (GLASS_TYPE_HEADER_BAR);
  g_type_ensure (GLASS_TYPE_SPLIT_VIEW);
  g_type_ensure (GLASS_TYPE_TOGGLE_GROUP);
  g_type_ensure (GLASS_TYPE_BUTTON);
  g_type_ensure (GLASS_TYPE_BUTTON_GROUP);
  g_type_ensure (GLASS_TYPE_SWITCH);
  g_type_ensure (GLASS_TYPE_SLIDER);
  g_type_ensure (GLASS_TYPE_GROUP);
  g_type_ensure (GLASS_TYPE_POPOVER);
  g_type_ensure (GLASS_TYPE_MENU_BUTTON);
  g_type_ensure (GLASS_TYPE_DIALOG);
  glass_context_get_default ();

  display = gdk_display_get_default ();
  if (display)
    glass_style_ensure (display);
}

/**
 * glass_is_initialized:
 *
 * Returns: whether [func@init] was called
 */
gboolean
glass_is_initialized (void)
{
  return initialized;
}

GlassDebugFlags
glass_get_debug_flags (void)
{
  static const GDebugKey keys[] = {
    { "hud", GLASS_DEBUG_HUD },
    { "no-supersample", GLASS_DEBUG_NO_SUPERSAMPLE },
    { "estimated-footprint", GLASS_DEBUG_ESTIMATED_FOOTPRINT },
    { "no-cache", GLASS_DEBUG_NO_CACHE },
    { "window-capture", GLASS_DEBUG_WINDOW_CAPTURE },
    { "gpu-time", GLASS_DEBUG_GPU_TIME },
  };
  static gsize flags;

  if (g_once_init_enter (&flags))
    {
      const char *env = g_getenv ("GLASS_DEBUG");
      guint parsed = env ? g_parse_debug_string (env, keys, G_N_ELEMENTS (keys)) : 0;

      g_once_init_leave (&flags, parsed | 0x80000000u);
    }

  return (GlassDebugFlags) (flags & ~0x80000000u);
}

/* ── Stylesheet ──
 * The library's CSS at GTK_STYLE_PROVIDER_PRIORITY_SETTINGS (400): above
 * libadwaita (THEME, 200), below the application (600), so apps can always
 * override it (design.md §6.4). Plus one provider per display for the
 * fallback's border-radius, which is per panel. */

typedef struct {
  GtkCssProvider *radius_provider;
  GHashTable     *radii;            /* rounded radius * 10 -> class name */
} DisplayStyle;

static void
display_style_free (gpointer data)
{
  DisplayStyle *ds = data;

  g_clear_object (&ds->radius_provider);
  g_hash_table_unref (ds->radii);
  g_free (ds);
}

static DisplayStyle *
display_style (GdkDisplay *display)
{
  DisplayStyle *ds = g_object_get_data (G_OBJECT (display), "glass-style");
  GtkCssProvider *provider;

  if (ds)
    return ds;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider, "/io/github/ryohsuke1231/GlassLib/style/glass.css");
  gtk_style_context_add_provider_for_display (display, GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_SETTINGS);
  g_object_unref (provider);

  ds = g_new0 (DisplayStyle, 1);
  ds->radius_provider = gtk_css_provider_new ();
  ds->radii = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
  gtk_style_context_add_provider_for_display (display, GTK_STYLE_PROVIDER (ds->radius_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_SETTINGS + 1);
  g_object_set_data_full (G_OBJECT (display), "glass-style", ds, display_style_free);

  return ds;
}

void
glass_style_ensure (GdkDisplay *display)
{
  display_style (display);
}

const char *
glass_style_radius_class (GdkDisplay *display,
                          double      radius)
{
  DisplayStyle *ds = display_style (display);
  int key = (int) lround (MAX (radius, 0.0) * 10.0);
  const char *name = g_hash_table_lookup (ds->radii, GINT_TO_POINTER (key));
  GHashTableIter iter;
  gpointer k, v;
  GString *css;

  if (name)
    return name;

  name = g_strdup_printf ("glass-radius-%d", key);
  g_hash_table_insert (ds->radii, GINT_TO_POINTER (key), (gpointer) name);

  css = g_string_new (NULL);
  g_hash_table_iter_init (&iter, ds->radii);
  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      char buf[G_ASCII_DTOSTR_BUF_SIZE];

      g_string_append_printf (css, "glasspanel.%s { border-radius: %spx; }\n", (const char *) v,
                              g_ascii_formatd (buf, sizeof buf, "%.1f", GPOINTER_TO_INT (k) / 10.0));
    }
  gtk_css_provider_load_from_string (ds->radius_provider, css->str);
  g_string_free (css, TRUE);

  return name;
}
