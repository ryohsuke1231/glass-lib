/* hello-glass.c — a complete glass-lib app in C: a header bar and a row of
 * buttons floating on glass over content that scrolls under them.
 *
 *   meson compile -C build && build/examples/hello-glass
 *
 * or, with glass-lib installed:
 *
 *   cc hello-glass.c -o hello-glass $(pkg-config --cflags --libs glass-lib-1)
 *
 * SPDX-License-Identifier: MIT
 */
#include <adwaita.h>
#include <glass.h>

/* Something colourful to scroll under the glass. */
static GtkWidget *
tiles (void)
{
  GtkCssProvider *provider = gtk_css_provider_new ();
  GString *css = g_string_new (".tile { border-radius: 14px; min-height: 110px; }\n");
  GtkWidget *grid;

  for (int i = 0; i < 12; i++)
    g_string_append_printf (css, ".tile-%d { background: hsl(%d 75%% 58%%); }\n", i, i * 30);
  gtk_css_provider_load_from_string (provider, css->str);
  gtk_style_context_add_provider_for_display (gdk_display_get_default (), GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_string_free (css, TRUE);
  g_object_unref (provider);

  grid = g_object_new (GTK_TYPE_FLOW_BOX,
                       "selection-mode", GTK_SELECTION_NONE,
                       "homogeneous", TRUE,
                       "min-children-per-line", 3,
                       "max-children-per-line", 6,
                       "row-spacing", 10,
                       "column-spacing", 10,
                       "valign", GTK_ALIGN_START,
                       "margin-start", 12,
                       "margin-end", 12,
                       "margin-bottom", 96,
                       NULL);
  for (int i = 0; i < 60; i++)
    {
      GtkWidget *tile = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
      char class[16];

      g_snprintf (class, sizeof class, "tile-%d", (i * 5) % 12);
      gtk_widget_add_css_class (tile, "tile");
      gtk_widget_add_css_class (tile, class);
      gtk_flow_box_append (GTK_FLOW_BOX (grid), tile);
    }
  return grid;
}

static void
activate (GtkApplication *app)
{
  const char *icons[] = { "go-previous-symbolic", "starred-symbolic", "user-trash-symbolic", "go-next-symbolic" };
  GtkWidget *grid, *scrolled, *toolbar, *header, *menu, *buttons, *window;

  /* Once, after GTK and libadwaita are up. */
  glass_init ();

  /* The bars of a toolbar view float on glass; the content runs under
   * them, so pad its start by the bars' height. */
  grid = tiles ();
  scrolled = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), grid);
  toolbar = glass_toolbar_view_new ();
  glass_toolbar_view_set_content (GLASS_TOOLBAR_VIEW (toolbar), scrolled);
  g_object_bind_property (toolbar, "top-bar-height", grid, "margin-top", G_BINDING_SYNC_CREATE);

  header = glass_header_bar_new ();
  menu = gtk_button_new_from_icon_name ("open-menu-symbolic");
  gtk_widget_set_tooltip_text (menu, "Menu");
  glass_header_bar_pack_end (GLASS_HEADER_BAR (header), menu);
  glass_toolbar_view_add_top_bar (GLASS_TOOLBAR_VIEW (toolbar), header);

  /* A row of buttons on one capsule of glass, at the bottom. */
  buttons = glass_button_group_new ();
  gtk_widget_set_halign (buttons, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_bottom (buttons, 18);
  for (guint i = 0; i < G_N_ELEMENTS (icons); i++)
    glass_button_group_append (GLASS_BUTTON_GROUP (buttons), gtk_button_new_from_icon_name (icons[i]));
  glass_toolbar_view_add_bottom_bar (GLASS_TOOLBAR_VIEW (toolbar), buttons);

  /* Tuning: this one capsule bends what is under it more than the rest.
   * glass_context_set_param() would change all the glass. */
  glass_panel_set_param (GLASS_PANEL (buttons), GLASS_PARAM_DISPLACEMENT_SCALE, 70.0);

  window = adw_application_window_new (app);
  gtk_window_set_title (GTK_WINDOW (window), "Hello, Glass");
  gtk_window_set_default_size (GTK_WINDOW (window), 720, 540);
  adw_application_window_set_content (ADW_APPLICATION_WINDOW (window), toolbar);
  gtk_window_present (GTK_WINDOW (window));
}

int
main (int argc, char **argv)
{
  g_autoptr (AdwApplication) app = adw_application_new ("org.example.HelloGlass", G_APPLICATION_DEFAULT_FLAGS);

  g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
  return g_application_run (G_APPLICATION (app), argc, argv);
}
