/* test-widgets.c — where panels end up and what the kit reports
 * (design.md §7.2, §6.6, §16). Needs a display; skipped (77) without one.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"
#include "render/glass-renderer.h"

static GtkWidget *
window_with (GtkWidget *child)
{
  GtkWidget *window = gtk_window_new ();

  gtk_window_set_default_size (GTK_WINDOW (window), 400, 300);
  gtk_window_set_child (GTK_WINDOW (window), child);
  return window;
}

static void
spin (int ms)
{
  gint64 end = g_get_monotonic_time () + ms * 1000;

  while (g_get_monotonic_time () < end)
    g_main_context_iteration (NULL, FALSE);
}

/* An overlay panel is the view's; a content-side one draws itself with CSS;
 * one inside another is nested. */
static void
test_registration (void)
{
  GtkWidget *view = glass_view_new ();
  GtkWidget *overlay = glass_panel_new ();
  GtkWidget *content_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *content_panel = glass_panel_new ();
  GtkWidget *inner = glass_panel_new ();
  GtkWidget *window;

  glass_panel_set_child (GLASS_PANEL (overlay), inner);
  gtk_box_append (GTK_BOX (content_box), content_panel);
  glass_view_set_content (GLASS_VIEW (view), content_box);
  glass_view_add_overlay (GLASS_VIEW (view), overlay);
  window = window_with (view);

  g_assert_cmpint (glass_panel_get_mode (GLASS_PANEL (overlay)), ==, GLASS_PANEL_MODE_VIEW);
  g_assert_false (gtk_widget_has_css_class (overlay, "glass-fallback"));
  g_assert_cmpint (glass_panel_get_mode (GLASS_PANEL (content_panel)), ==, GLASS_PANEL_MODE_FALLBACK);
  g_assert_true (gtk_widget_has_css_class (content_panel, "glass-fallback"));
  g_assert_cmpint (glass_panel_get_mode (GLASS_PANEL (inner)), ==, GLASS_PANEL_MODE_NESTED);
  g_assert_true (gtk_widget_has_css_class (inner, "glass-nested"));

  /* Realized, drawn, and torn down: the GL objects go with the view. */
  gtk_window_present (GTK_WINDOW (window));
  spin (300);
  g_assert_cmpint (glass_view_get_active_renderer (GLASS_VIEW (view)), ==,
                   glass_renderer_failed (gtk_widget_get_native (view)) ? GLASS_RENDERER_MODE_FALLBACK
                                                                        : GLASS_RENDERER_MODE_FULL);

  /* Taken out of the view: it draws itself. */
  g_object_ref (overlay);
  glass_view_remove_overlay (GLASS_VIEW (view), overlay);
  g_assert_cmpint (glass_panel_get_mode (GLASS_PANEL (overlay)), ==, GLASS_PANEL_MODE_NONE);
  g_object_unref (overlay);

  gtk_window_destroy (GTK_WINDOW (window));
  spin (50);
}

/* The renderer setting moves every panel to the CSS fallback and back. */
static void
test_fallback_setting (void)
{
  GlassContext *ctx = glass_context_get_default ();
  GtkWidget *view = glass_view_new ();
  GtkWidget *panel = glass_panel_new ();
  GtkWidget *window;

  glass_view_add_overlay (GLASS_VIEW (view), panel);
  window = window_with (view);

  glass_context_set_renderer (ctx, GLASS_RENDERER_MODE_FALLBACK);
  g_assert_true (gtk_widget_has_css_class (panel, "glass-fallback"));
  g_assert_cmpint (glass_view_get_active_renderer (GLASS_VIEW (view)), ==, GLASS_RENDERER_MODE_FALLBACK);
  glass_context_set_renderer (ctx, GLASS_RENDERER_MODE_AUTO);
  g_assert_false (gtk_widget_has_css_class (panel, "glass-fallback"));

  gtk_window_destroy (GTK_WINDOW (window));
  spin (50);
}

static void
test_toggle_group (void)
{
  GlassToggleGroup *group = GLASS_TOGGLE_GROUP (g_object_ref_sink (glass_toggle_group_new ()));

  glass_toggle_group_append (group, "grid", NULL, "view-grid-symbolic");
  glass_toggle_group_append (group, "list", "List", NULL);
  g_assert_cmpuint (glass_toggle_group_get_n_toggles (group), ==, 2);
  g_assert_cmpuint (glass_toggle_group_get_active (group), ==, 0);
  g_assert_cmpstr (glass_toggle_group_get_active_name (group), ==, "grid");

  glass_toggle_group_set_active_name (group, "list");
  g_assert_cmpuint (glass_toggle_group_get_active (group), ==, 1);
  glass_toggle_group_set_active (group, 7);   /* out of range: ignored */
  g_assert_cmpuint (glass_toggle_group_get_active (group), ==, 1);

  g_object_unref (group);
}

/* The toolbar view reports how much the bars cover. */
static void
test_toolbar_view (void)
{
  GtkWidget *toolbar = glass_toolbar_view_new ();
  GtkWidget *header = glass_header_bar_new ();
  GtkWidget *window;

  glass_header_bar_pack_start (GLASS_HEADER_BAR (header), gtk_button_new_from_icon_name ("go-previous-symbolic"));
  glass_toolbar_view_set_content (GLASS_TOOLBAR_VIEW (toolbar), gtk_label_new ("content"));
  glass_toolbar_view_add_top_bar (GLASS_TOOLBAR_VIEW (toolbar), header);
  window = window_with (toolbar);
  gtk_window_present (GTK_WINDOW (window));
  spin (300);

  g_assert_cmpint (glass_toolbar_view_get_top_bar_height (GLASS_TOOLBAR_VIEW (toolbar)), >, 30);
  g_assert_cmpint (glass_toolbar_view_get_bottom_bar_height (GLASS_TOOLBAR_VIEW (toolbar)), ==, 0);

  gtk_window_destroy (GTK_WINDOW (window));
  spin (50);
}

static void
test_split_view (void)
{
  GlassSplitView *split = GLASS_SPLIT_VIEW (g_object_ref_sink (glass_split_view_new ()));

  g_assert_true (glass_split_view_get_show_sidebar (split));
  g_assert_cmpint (glass_split_view_get_content_inset (split), ==, 260 + 16);
  glass_split_view_set_sidebar_width (split, 300);
  g_assert_cmpint (glass_split_view_get_content_inset (split), ==, 300 + 16);

  g_object_unref (split);
}

int
main (int argc, char **argv)
{
  if (!gtk_init_check ())
    return 77;
  g_test_init (&argc, &argv, NULL);
  glass_init ();

  g_test_add_func ("/widgets/registration", test_registration);
  g_test_add_func ("/widgets/fallback-setting", test_fallback_setting);
  g_test_add_func ("/widgets/toggle-group", test_toggle_group);
  g_test_add_func ("/widgets/toolbar-view", test_toolbar_view);
  g_test_add_func ("/widgets/split-view", test_split_view);

  return g_test_run ();
}
