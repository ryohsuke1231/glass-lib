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
  g_type_ensure (GLASS_TYPE_TAB_BAR);
  g_type_ensure (GLASS_TYPE_SEARCH_ENTRY);
  glass_context_get_default ();

  display = gdk_display_get_default ();
  if (display)
    glass_style_ensure (display);
}

/**
 * glass_is_initialized:
 *
 * Gets whether [func@init] was called.
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
  GHashTable     *radii;            /* class name -> its border-radius value */
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
  ds->radii = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
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
  double radii[4] = { -1.0, -1.0, -1.0, -1.0 };

  return glass_style_corner_radii_class (display, MAX (radius, 0.0), radii);
}

/* A radius in tenths of a px for class names; -1 for a capsule's. */
static int
tenths (double radius)
{
  return radius < 0.0 ? -1 : (int) lround (radius * 10.0);
}

const char *
glass_style_corner_radii_class (GdkDisplay   *display,
                                double        radius,
                                const double  radii[4])
{
  DisplayStyle *ds = display_style (display);
  int t[4];
  g_autofree char *name = NULL;
  GString *value, *css;
  GHashTableIter iter;
  gpointer k, v;
  gpointer found;

  for (int i = 0; i < 4; i++)
    t[i] = tenths (radii[i] >= 0.0 ? radii[i] : radius);

  if (t[0] == t[1] && t[1] == t[2] && t[2] == t[3])
    name = g_strdup_printf ("glass-radius-%d", t[0]);
  else
    name = g_strdup_printf ("glass-radius-%d-%d-%d-%d", t[0], t[1], t[2], t[3]);
  if (g_hash_table_lookup_extended (ds->radii, name, &found, NULL))
    return found;

  /* A capsule's corners are the stylesheet's 9999px. */
  value = g_string_new (NULL);
  for (int i = 0; i < 4; i++)
    {
      char buf[G_ASCII_DTOSTR_BUF_SIZE];

      if (i > 0)
        g_string_append_c (value, ' ');
      if (t[i] < 0)
        g_string_append (value, "9999px");
      else
        g_string_append_printf (value, "%spx", g_ascii_formatd (buf, sizeof buf, "%.1f", t[i] / 10.0));
    }
  g_hash_table_insert (ds->radii, g_strdup (name), g_string_free (value, FALSE));

  css = g_string_new (NULL);
  g_hash_table_iter_init (&iter, ds->radii);
  while (g_hash_table_iter_next (&iter, &k, &v))
    g_string_append_printf (css, "glasspanel.%s { border-radius: %s; }\n", (const char *) k, (const char *) v);
  gtk_css_provider_load_from_string (ds->radius_provider, css->str);
  g_string_free (css, TRUE);

  g_hash_table_lookup_extended (ds->radii, name, &found, NULL);
  return found;
}
