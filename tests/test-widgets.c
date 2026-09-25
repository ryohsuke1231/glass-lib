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

/* An overlay panel is the view's, and so is one inside it (glass over glass);
 * a content-side one draws itself with CSS. */
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
  /* Glass over glass: the view draws it too, in a later layer. */
  g_assert_cmpint (glass_panel_get_mode (GLASS_PANEL (inner)), ==, GLASS_PANEL_MODE_VIEW);
  g_assert_false (gtk_widget_has_css_class (inner, "glass-nested"));

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

/* The buttons under @widget with a frame (or without). */
static int
count_buttons (GtkWidget *widget, gboolean framed)
{
  int n = GTK_IS_BUTTON (widget) && gtk_button_get_has_frame (GTK_BUTTON (widget)) == framed;

  for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    n += count_buttons (child, framed);
  return n;
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

  /* Frameless, so a theme paints nothing under them at rest (地雷27). */
  g_assert_cmpint (count_buttons (GTK_WIDGET (group), TRUE), ==, 0);
  g_assert_cmpint (count_buttons (GTK_WIDGET (group), FALSE), ==, 2);

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

/* A header capsule shows while something in it does. */
static void
test_header_capsules (void)
{
  GtkWidget *header = g_object_ref_sink (glass_header_bar_new ());
  GtkWidget *button = gtk_button_new_from_icon_name ("sidebar-show-symbolic");
  GtkWidget *capsule;

  glass_header_bar_pack_start (GLASS_HEADER_BAR (header), button);
  capsule = gtk_widget_get_ancestor (button, GLASS_TYPE_BUTTON_GROUP);
  g_assert_nonnull (capsule);
  g_assert_true (gtk_widget_get_visible (capsule));

  gtk_widget_set_visible (button, FALSE);
  g_assert_false (gtk_widget_get_visible (capsule));
  gtk_widget_set_visible (button, TRUE);
  g_assert_true (gtk_widget_get_visible (capsule));

  glass_header_bar_remove (GLASS_HEADER_BAR (header), button);
  g_assert_false (gtk_widget_get_visible (capsule));

  g_object_unref (header);
}

static void
test_button_group (void)
{
  GlassButtonGroup *group = GLASS_BUTTON_GROUP (g_object_ref_sink (glass_button_group_new ()));
  GtkWidget *a = gtk_button_new_from_icon_name ("go-previous-symbolic");
  GtkWidget *b = gtk_button_new_from_icon_name ("go-next-symbolic");
  GtkWidget *go = g_object_ref_sink (gtk_button_new_with_label ("Go"));

  g_assert_true (glass_button_group_is_empty (group));
  glass_button_group_append (group, a);
  glass_button_group_prepend (group, b);
  g_assert_false (glass_button_group_is_empty (group));

  /* Buttons lose their frame in the group and get it back when removed;
   * suggested ones keep it (地雷27). */
  g_assert_false (gtk_button_get_has_frame (GTK_BUTTON (a)));
  gtk_widget_add_css_class (go, "suggested-action");
  glass_button_group_append (group, go);
  g_assert_true (gtk_button_get_has_frame (GTK_BUTTON (go)));
  glass_button_group_remove (group, go);
  g_object_unref (go);
  g_object_ref (a);
  glass_button_group_remove (group, a);
  g_assert_true (gtk_button_get_has_frame (GTK_BUTTON (a)));
  glass_button_group_append (group, a);
  g_object_unref (a);
  g_assert_true (gtk_widget_get_next_sibling (b) == a);   /* prepended before a */
  g_assert_true (glass_panel_get_interactive (GLASS_PANEL (group)));

  gtk_widget_set_visible (a, FALSE);
  gtk_widget_set_visible (b, FALSE);
  g_assert_false (glass_button_group_has_visible_child (group));
  gtk_widget_set_visible (a, TRUE);
  g_assert_true (glass_button_group_has_visible_child (group));

  glass_button_group_remove (group, a);
  glass_button_group_remove (group, b);
  g_assert_true (glass_button_group_is_empty (group));

  g_object_unref (group);
}

static void
count_notify (GObject *object, GParamSpec *pspec, int *count)
{
  (*count)++;
}

static void
test_switch (void)
{
  GtkWidget *sw = g_object_ref_sink (glass_switch_new ());
  int notified = 0;

  g_assert_cmpint (gtk_accessible_get_accessible_role (GTK_ACCESSIBLE (sw)), ==, GTK_ACCESSIBLE_ROLE_SWITCH);
  g_assert_false (glass_switch_get_active (GLASS_SWITCH (sw)));
  g_signal_connect (sw, "notify::active", G_CALLBACK (count_notify), &notified);
  glass_switch_set_active (GLASS_SWITCH (sw), TRUE);
  glass_switch_set_active (GLASS_SWITCH (sw), TRUE);   /* no change, no notify */
  g_assert_true (glass_switch_get_active (GLASS_SWITCH (sw)));
  g_assert_cmpint (notified, ==, 1);
  g_object_set (sw, "active", FALSE, NULL);
  g_assert_false (glass_switch_get_active (GLASS_SWITCH (sw)));
  g_assert_cmpint (notified, ==, 2);

  g_object_unref (sw);
}

static void
test_slider (void)
{
  GtkWidget *slider = g_object_ref_sink (glass_slider_new_with_range (0.0, 10.0, 1.0));
  GtkAdjustment *adjustment = glass_slider_get_adjustment (GLASS_SLIDER (slider));

  g_assert_cmpint (gtk_accessible_get_accessible_role (GTK_ACCESSIBLE (slider)), ==, GTK_ACCESSIBLE_ROLE_SLIDER);
  g_assert_nonnull (adjustment);
  g_assert_cmpfloat (gtk_adjustment_get_upper (adjustment), ==, 10.0);

  glass_slider_set_value (GLASS_SLIDER (slider), 4.0);
  g_assert_cmpfloat (glass_slider_get_value (GLASS_SLIDER (slider)), ==, 4.0);
  glass_slider_set_value (GLASS_SLIDER (slider), 25.0);   /* clamped */
  g_assert_cmpfloat (glass_slider_get_value (GLASS_SLIDER (slider)), ==, 10.0);

  /* Driven from the adjustment too. */
  gtk_adjustment_set_value (adjustment, 2.0);
  g_assert_cmpfloat (glass_slider_get_value (GLASS_SLIDER (slider)), ==, 2.0);

  g_object_unref (slider);
}

/* Panels in a group are still the view's: the view fuses them. */
static void
test_group (void)
{
  GtkWidget *view = glass_view_new ();
  GtkWidget *group = glass_group_new ();
  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  GtkWidget *a = glass_button_new_from_icon_name ("go-previous-symbolic");
  GtkWidget *b = glass_button_new_from_icon_name ("go-next-symbolic");
  GtkWidget *window;

  gtk_box_append (GTK_BOX (row), a);
  gtk_box_append (GTK_BOX (row), b);
  glass_group_set_child (GLASS_GROUP (group), row);
  g_assert_true (glass_group_get_child (GLASS_GROUP (group)) == row);
  g_assert_cmpfloat (glass_group_get_spacing (GLASS_GROUP (group)), ==, 16.0);
  glass_group_set_spacing (GLASS_GROUP (group), 8.0);
  g_assert_cmpfloat (glass_group_get_spacing (GLASS_GROUP (group)), ==, 8.0);

  glass_view_set_content (GLASS_VIEW (view), gtk_label_new ("content"));
  glass_view_add_overlay (GLASS_VIEW (view), group);
  window = window_with (view);
  g_assert_cmpint (glass_panel_get_mode (GLASS_PANEL (a)), ==, GLASS_PANEL_MODE_VIEW);
  g_assert_cmpint (glass_panel_get_mode (GLASS_PANEL (b)), ==, GLASS_PANEL_MODE_VIEW);

  gtk_window_present (GTK_WINDOW (window));
  spin (300);
  gtk_window_destroy (GTK_WINDOW (window));
  spin (50);
}

static GMenuModel *
test_menu (void)
{
  GMenu *menu = g_menu_new ();
  GMenu *section = g_menu_new ();
  GMenu *sub = g_menu_new ();

  g_menu_append (menu, "New Window", "app.new");
  g_menu_append (section, "Preferences", "app.preferences");
  g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
  g_menu_append (sub, "Small", "app.size::small");
  g_menu_append_submenu (menu, "Size", G_MENU_MODEL (sub));
  g_object_unref (section);
  g_object_unref (sub);
  return G_MENU_MODEL (menu);
}

static void
test_popover_menu (void)
{
  g_autoptr (GMenuModel) model = test_menu ();
  GtkWidget *popover = g_object_ref_sink (glass_popover_new_from_model (model));
  GtkWidget *menu_button = g_object_ref_sink (glass_menu_button_new ());

  g_assert_true (GLASS_IS_POPOVER (popover));
  g_assert_true (gtk_widget_has_css_class (popover, "glass"));
  g_assert_false (gtk_popover_get_has_arrow (GTK_POPOVER (popover)));
  g_assert_nonnull (gtk_popover_get_child (GTK_POPOVER (popover)));

  glass_menu_button_set_icon_name (GLASS_MENU_BUTTON (menu_button), "open-menu-symbolic");
  g_assert_cmpstr (glass_menu_button_get_icon_name (GLASS_MENU_BUTTON (menu_button)), ==, "open-menu-symbolic");
  glass_menu_button_set_menu_model (GLASS_MENU_BUTTON (menu_button), model);
  g_assert_true (glass_menu_button_get_menu_model (GLASS_MENU_BUTTON (menu_button)) == model);
  g_assert_true (GLASS_IS_POPOVER (glass_menu_button_get_popover (GLASS_MENU_BUTTON (menu_button))));

  g_object_unref (menu_button);
  g_object_unref (popover);
}

static void
test_dialog (void)
{
  GtkWidget *dialog = g_object_ref_sink (glass_dialog_new ());
  GtkWidget *label = gtk_label_new ("content");
  int notified = 0;

  g_assert_true (gtk_widget_has_css_class (dialog, "glass"));
  g_assert_false (gtk_widget_has_css_class (dialog, "background"));
  g_signal_connect (dialog, "notify::content", G_CALLBACK (count_notify), &notified);
  glass_dialog_set_content (GLASS_DIALOG (dialog), label);
  g_assert_true (glass_dialog_get_content (GLASS_DIALOG (dialog)) == label);
  glass_dialog_set_content (GLASS_DIALOG (dialog), label);
  g_assert_cmpint (notified, ==, 1);
  glass_dialog_set_content (GLASS_DIALOG (dialog), NULL);
  g_assert_null (glass_dialog_get_content (GLASS_DIALOG (dialog)));

  g_object_unref (dialog);
}

/* Glass over glass that only the background under it changes: a knob's
 * glass (in a view of its own, in a panel) and a dialog's must be drawn
 * again with it, although GTK keeps their nodes (docs/memo.md 地雷24). The
 * background turns from red to blue with only it redrawn; then everything
 * is redrawn by force. The two frames must be the same. */
static gboolean stale_blue;

static void
draw_stripes (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
  for (int x = 0; x < width; x += 16)
    {
      double on = (x / 16) % 2;

      if (stale_blue)
        cairo_set_source_rgb (cr, 0.1, 0.3 + 0.4 * on, 0.95);
      else
        cairo_set_source_rgb (cr, 0.95, 0.3 + 0.4 * on, 0.1);
      cairo_rectangle (cr, x, 0, 16, height);
      cairo_fill (cr);
    }
}

static void
queue_draw_all (GtkWidget *widget)
{
  gtk_widget_queue_draw (widget);
  for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    queue_draw_all (child);
}

static GBytes *
render_window (GtkWidget *window)
{
  GdkPaintable *paintable = gtk_widget_paintable_new (window);
  GtkSnapshot *snapshot = gtk_snapshot_new ();
  GskRenderNode *node;
  GdkTexture *texture;
  GdkTextureDownloader *downloader;
  GBytes *bytes;
  gsize stride;

  gdk_paintable_snapshot (paintable, snapshot, gtk_widget_get_width (window), gtk_widget_get_height (window));
  node = gtk_snapshot_free_to_node (snapshot);
  texture = gsk_renderer_render_texture (gtk_native_get_renderer (GTK_NATIVE (window)), node, NULL);
  downloader = gdk_texture_downloader_new (texture);
  bytes = gdk_texture_downloader_download_bytes (downloader, &stride);

  gdk_texture_downloader_free (downloader);
  g_object_unref (texture);
  gsk_render_node_unref (node);
  g_object_unref (paintable);
  return bytes;
}

/* Renders after the background turned blue, then after a full redraw. */
static void
assert_not_stale (GtkWidget *window, GtkWidget *background)
{
  g_autoptr (GBytes) changed = NULL;
  g_autoptr (GBytes) full = NULL;

  spin (600);
  stale_blue = TRUE;
  gtk_widget_queue_draw (background);
  spin (600);
  changed = render_window (window);

  queue_draw_all (window);
  spin (600);
  full = render_window (window);

  g_assert_true (g_bytes_equal (changed, full));
  stale_blue = FALSE;
}

static GtkWidget *
stale_content (GtkWidget **background, GtkWidget **view)
{
  *background = gtk_drawing_area_new ();
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (*background), draw_stripes, NULL, NULL);
  *view = glass_view_new ();
  glass_view_set_content (GLASS_VIEW (*view), *background);
  return *view;
}

static void
test_nested_view_redraw (void)
{
  GtkWidget *background, *view, *window;
  GtkWidget *sw = glass_switch_new ();
  GtkWidget *panel = glass_panel_new ();
  GtkWidget *knob = NULL;

  stale_content (&background, &view);
  glass_switch_set_active (GLASS_SWITCH (sw), TRUE);
  gtk_widget_set_margin_top (sw, 20);
  gtk_widget_set_margin_bottom (sw, 20);
  gtk_widget_set_margin_start (sw, 20);
  gtk_widget_set_margin_end (sw, 20);
  glass_panel_set_child (GLASS_PANEL (panel), sw);
  gtk_widget_set_halign (panel, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (panel, GTK_ALIGN_CENTER);
  glass_view_add_overlay (GLASS_VIEW (view), panel);
  window = window_with (view);
  gtk_window_present (GTK_WINDOW (window));
  spin (600);

  /* A clear knob, so that what is under it shows. */
  for (GtkWidget *w = gtk_widget_get_first_child (gtk_widget_get_first_child (sw)); w; w = gtk_widget_get_next_sibling (w))
    if (gtk_widget_has_css_class (w, "switch-knob"))
      knob = w;
  g_assert_nonnull (knob);
  glass_panel_set_tint (GLASS_PANEL (knob), &(GdkRGBA) { 1, 1, 1, 0.1f });

  assert_not_stale (window, background);
  gtk_window_destroy (GTK_WINDOW (window));
}

static void
test_dialog_redraw (void)
{
  GtkWidget *background, *view;
  GtkWidget *window = adw_window_new ();
  GtkWidget *dialog = glass_dialog_new ();

  gtk_window_set_default_size (GTK_WINDOW (window), 400, 300);
  adw_window_set_content (ADW_WINDOW (window), stale_content (&background, &view));
  gtk_window_present (GTK_WINDOW (window));
  glass_dialog_set_content (GLASS_DIALOG (dialog), gtk_label_new ("On glass"));
  adw_dialog_set_content_width (ADW_DIALOG (dialog), 240);
  adw_dialog_present (ADW_DIALOG (dialog), window);

  assert_not_stale (window, background);
  gtk_window_destroy (GTK_WINDOW (window));
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
  g_test_add_func ("/widgets/header-capsules", test_header_capsules);
  g_test_add_func ("/widgets/button-group", test_button_group);
  g_test_add_func ("/widgets/switch", test_switch);
  g_test_add_func ("/widgets/slider", test_slider);
  g_test_add_func ("/widgets/group", test_group);
  g_test_add_func ("/widgets/popover-menu", test_popover_menu);
  g_test_add_func ("/widgets/dialog", test_dialog);
  g_test_add_func ("/widgets/nested-view-redraw", test_nested_view_redraw);
  g_test_add_func ("/widgets/dialog-redraw", test_dialog_redraw);

  return g_test_run ();
}
