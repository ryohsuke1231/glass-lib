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

/* A view realized under the CSS fallback gets its renderer when the setting
 * comes back (a page first shown while the fallback was chosen). */
static void
test_fallback_realized (void)
{
  GlassContext *ctx = glass_context_get_default ();
  GtkWidget *view = glass_view_new ();
  GtkWidget *panel = glass_panel_new ();
  GtkWidget *window;

  glass_view_add_overlay (GLASS_VIEW (view), panel);
  window = window_with (view);
  glass_context_set_renderer (ctx, GLASS_RENDERER_MODE_FALLBACK);
  gtk_window_present (GTK_WINDOW (window));
  spin (100);
  g_assert_true (gtk_widget_get_realized (view));
  g_assert_cmpint (glass_view_get_active_renderer (GLASS_VIEW (view)), ==, GLASS_RENDERER_MODE_FALLBACK);

  glass_context_set_renderer (ctx, GLASS_RENDERER_MODE_AUTO);
  if (!glass_renderer_failed (gtk_widget_get_native (view)))
    g_assert_false (gtk_widget_has_css_class (panel, "glass-fallback"));
  g_assert_cmpint (glass_view_get_active_renderer (GLASS_VIEW (view)), ==,
                   glass_renderer_failed (gtk_widget_get_native (view)) ? GLASS_RENDERER_MODE_FALLBACK
                                                                        : GLASS_RENDERER_MODE_FULL);

  gtk_window_destroy (GTK_WINDOW (window));
  spin (50);
}

/* With the CSS fallback the view samples the backdrop itself: the colours
 * still follow what is under the panel (design.md §12.1). */
static void
test_fallback_adaptive (void)
{
  GlassContext *ctx = glass_context_get_default ();
  GtkWidget *view = glass_view_new ();
  GtkWidget *panel = glass_panel_new ();
  GtkWidget *window;
  GdkRGBA white = { 1, 1, 1, 1 }, black = { 0, 0, 0, 1 };

  glass_context_set_renderer (ctx, GLASS_RENDERER_MODE_FALLBACK);
  gtk_widget_set_size_request (panel, 120, 40);
  gtk_widget_set_halign (panel, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (panel, GTK_ALIGN_CENTER);
  glass_panel_set_child (GLASS_PANEL (panel), gtk_label_new ("Aa"));
  glass_view_set_backdrop_color (GLASS_VIEW (view), &white);
  glass_view_add_overlay (GLASS_VIEW (view), panel);
  window = window_with (view);
  gtk_window_present (GTK_WINDOW (window));
  spin (900);

  g_assert_true (gtk_widget_has_css_class (panel, "glass-fallback"));
  g_assert_cmpint (glass_panel_get_appearance (GLASS_PANEL (panel)), ==, GLASS_APPEARANCE_LIGHT);
  g_assert_true (gtk_widget_has_css_class (panel, "glass-light"));

  glass_view_set_backdrop_color (GLASS_VIEW (view), &black);
  spin (1200);
  g_assert_cmpint (glass_panel_get_appearance (GLASS_PANEL (panel)), ==, GLASS_APPEARANCE_DARK);
  g_assert_true (gtk_widget_has_css_class (panel, "glass-dark"));
  g_assert_false (gtk_widget_has_css_class (panel, "glass-light"));

  /* Off: the theme's colours. */
  glass_panel_set_adaptive (GLASS_PANEL (panel), GLASS_ADAPTIVE_MODE_OFF);
  g_assert_cmpint (glass_panel_get_appearance (GLASS_PANEL (panel)), ==, GLASS_APPEARANCE_UNKNOWN);

  glass_context_set_renderer (ctx, GLASS_RENDERER_MODE_AUTO);
  gtk_window_destroy (GTK_WINDOW (window));
  spin (50);
}

/* An app replacing the classes (css_classes in a GJS or Python constructor)
 * keeps its own and gets ours back: the shape (else the child is clipped
 * to a capsule), the mode and the part's own class. */
static void
test_css_classes (void)
{
  GtkWidget *button = g_object_ref_sink (glass_button_new_from_icon_name ("edit-find-symbolic"));
  const char *mine[] = { "mine", NULL };

  glass_panel_set_corner_radius (GLASS_PANEL (button), 22.0);
  g_assert_true (gtk_widget_has_css_class (button, "glass-radius-220"));
  gtk_widget_set_css_classes (button, mine);
  g_assert_true (gtk_widget_has_css_class (button, "mine"));
  g_assert_true (gtk_widget_has_css_class (button, "glass-radius-220"));
  g_assert_true (gtk_widget_has_css_class (button, "glass-button"));

  /* Our own changes are not undone by it. */
  glass_panel_set_corner_radius (GLASS_PANEL (button), 10.0);
  g_assert_true (gtk_widget_has_css_class (button, "glass-radius-100"));
  g_assert_false (gtk_widget_has_css_class (button, "glass-radius-220"));

  g_object_unref (button);
}

/* A panel's own values win over the context's, which win over the
 * material's (design.md §11.2); the tint follows the same order. */
static void
test_panel_params (void)
{
  GlassContext *ctx = glass_context_get_default ();
  GlassPanel *panel = GLASS_PANEL (g_object_ref_sink (glass_panel_new ()));
  GdkRGBA tint, blue = { 0.1f, 0.2f, 0.9f, 1.0f }, red = { 0.9f, 0.1f, 0.1f, 0.5f };

  g_assert_cmpfloat (glass_panel_get_effective_param (panel, "max-z"), ==, 88.0);
  glass_context_set_param (ctx, "max-z", 30.0);
  g_assert_cmpfloat (glass_panel_get_effective_param (panel, "max-z"), ==, 30.0);
  g_assert_true (glass_panel_set_param (panel, "max-z", 60.0));
  g_assert_true (glass_panel_is_param_set (panel, "max-z"));
  g_assert_cmpfloat (glass_panel_get_param (panel, "max-z"), ==, 60.0);
  g_assert_cmpfloat (glass_panel_get_effective_param (panel, "max-z"), ==, 60.0);
  glass_panel_reset_param (panel, "max-z");
  g_assert_false (glass_panel_is_param_set (panel, "max-z"));
  g_assert_true (isnan (glass_panel_get_param (panel, "max-z")));
  g_assert_cmpfloat (glass_panel_get_effective_param (panel, "max-z"), ==, 30.0);
  glass_context_reset_param (ctx, "max-z");

  /* Checked like the context's. */
  glass_panel_set_param (panel, "blur-downscale", 3.2);
  g_assert_cmpfloat (glass_panel_get_param (panel, "blur-downscale"), ==, 4.0);
  g_test_expect_message ("glass", G_LOG_LEVEL_WARNING, "*outside*");
  glass_panel_set_param (panel, "ior", 9.0);
  g_test_assert_expected_messages ();
  g_assert_cmpfloat (glass_panel_get_param (panel, "ior"), ==, 4.0);
  g_test_expect_message ("glass", G_LOG_LEVEL_WARNING, "*no parameter*");
  g_assert_false (glass_panel_set_param (panel, "no-such-key", 1.0));
  g_test_assert_expected_messages ();

  /* The tint: the material's white 0.12, the context's strength and
   * colour, the panel's strength, and the panel's own tint over all. */
  g_assert_false (glass_panel_get_tint (panel, &tint));
  g_assert_cmpfloat (tint.red, ==, 1.0f);
  g_assert_cmpfloat (tint.alpha, ==, 0.12f);
  glass_context_set_param (ctx, "tint-strength", 0.3);
  glass_context_set_tint_color (ctx, &blue);
  glass_panel_get_tint (panel, &tint);
  g_assert_cmpfloat (tint.blue, ==, 0.9f);
  g_assert_cmpfloat (tint.alpha, ==, 0.3f);
  glass_panel_set_param (panel, "tint-strength", 0.6);
  glass_panel_get_tint (panel, &tint);
  g_assert_cmpfloat (tint.alpha, ==, 0.6f);
  glass_panel_set_tint (panel, &red);
  g_assert_true (glass_panel_get_tint (panel, &tint));
  g_assert_cmpfloat (tint.red, ==, 0.9f);
  g_assert_cmpfloat (tint.alpha, ==, 0.5f);

  glass_context_reset_param (ctx, "tint-strength");
  glass_context_set_tint_color (ctx, NULL);
  g_object_unref (panel);
}

/* A panel's lens: the three values at once, over the context's; reset goes
 * back to them. */
static void
test_panel_lens (void)
{
  GlassContext *ctx = glass_context_get_default ();
  GlassPanel *panel = GLASS_PANEL (g_object_ref_sink (glass_panel_new ()));

  glass_panel_set_lens (panel, GLASS_LENS_THICK);
  g_assert_cmpfloat (glass_panel_get_param (panel, "max-z"), ==, 100.0);
  g_assert_cmpfloat (glass_panel_get_param (panel, "profile-shape-n"), ==, 1.35);
  g_assert_cmpfloat (glass_panel_get_param (panel, "displacement-scale"), ==, 26.0);
  glass_context_set_lens (ctx, GLASS_LENS_THIN);
  g_assert_cmpfloat (glass_panel_get_effective_param (panel, "displacement-scale"), ==, 26.0);
  glass_panel_reset_param (panel, "displacement-scale");
  g_assert_cmpfloat (glass_panel_get_effective_param (panel, "displacement-scale"), ==, 10.5);

  glass_context_reset_param (ctx, "max-z");
  glass_context_reset_param (ctx, "profile-shape-n");
  glass_context_reset_param (ctx, "displacement-scale");
  g_object_unref (panel);
}

/* PROMINENT: the accent's foreground class, the theme's accent as the tint
 * (not the context's colour), and the panel's own tint over it. */
static void
test_prominent (void)
{
  GlassContext *ctx = glass_context_get_default ();
  GtkWidget *window = gtk_window_new ();
  GtkWidget *button = glass_button_new_with_label ("Done");
  GdkRGBA tint, green = { 0.1f, 0.8f, 0.2f, 1.0f }, blue = { 0.04f, 0.52f, 1.0f, 0.92f };

  gtk_window_set_child (GTK_WINDOW (window), button);
  g_assert_false (gtk_widget_has_css_class (button, "glass-prominent"));
  glass_panel_set_material (GLASS_PANEL (button), GLASS_MATERIAL_PROMINENT);
  g_assert_true (gtk_widget_has_css_class (button, "glass-prominent"));
  /* It survives an app replacing the classes. */
  gtk_widget_set_css_classes (button, (const char *[]) { "custom", NULL });
  g_assert_true (gtk_widget_has_css_class (button, "glass-prominent"));

  glass_context_set_tint_color (ctx, &green);
  g_assert_false (glass_panel_get_tint (GLASS_PANEL (button), &tint));
  g_assert_cmpfloat_with_epsilon (tint.alpha, 0.92f, 1e-6);
  g_assert_false (tint.red == green.red && tint.green == green.green && tint.blue == green.blue);
  glass_context_set_tint_color (ctx, NULL);

  glass_panel_set_tint (GLASS_PANEL (button), &blue);
  g_assert_true (glass_panel_get_tint (GLASS_PANEL (button), &tint));
  g_assert_cmpfloat (tint.blue, ==, 1.0f);

  glass_panel_set_material (GLASS_PANEL (button), GLASS_MATERIAL_REGULAR);
  g_assert_false (gtk_widget_has_css_class (button, "glass-prominent"));
  gtk_window_destroy (GTK_WINDOW (window));
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

static void
count_notify (GObject *object, GParamSpec *pspec, int *count)
{
  (*count)++;
}

static void
test_toggle_group_remove (void)
{
  GlassToggleGroup *group = GLASS_TOGGLE_GROUP (g_object_ref_sink (glass_toggle_group_new ()));
  int notified = 0;

  glass_toggle_group_append (group, "a", "A", NULL);
  glass_toggle_group_append (group, "b", NULL, "view-grid-symbolic");
  glass_toggle_group_append (group, "c", "C", NULL);
  glass_toggle_group_append (group, "d", "D", NULL);

  /* Tooltips: an icon-only toggle has its name until one is set. */
  g_assert_null (glass_toggle_group_get_tooltip (group, 0));
  g_assert_cmpstr (glass_toggle_group_get_tooltip (group, 1), ==, "b");
  glass_toggle_group_set_tooltip (group, 0, "Alpha");
  glass_toggle_group_set_tooltip (group, 1, NULL);
  g_assert_cmpstr (glass_toggle_group_get_tooltip (group, 0), ==, "Alpha");
  g_assert_null (glass_toggle_group_get_tooltip (group, 1));

  g_signal_connect (group, "notify::active", G_CALLBACK (count_notify), &notified);
  glass_toggle_group_set_active_name (group, "c");
  notified = 0;

  /* Before the active one: the same toggle stays active, one place on. */
  glass_toggle_group_remove (group, 0);
  g_assert_cmpuint (glass_toggle_group_get_n_toggles (group), ==, 3);
  g_assert_cmpuint (glass_toggle_group_get_active (group), ==, 1);
  g_assert_cmpstr (glass_toggle_group_get_active_name (group), ==, "c");
  g_assert_cmpint (notified, ==, 1);

  /* After it: nothing changes. */
  glass_toggle_group_remove (group, 2);
  g_assert_cmpstr (glass_toggle_group_get_active_name (group), ==, "c");
  g_assert_cmpint (notified, ==, 1);

  /* The active one, the last: the one before takes over. */
  glass_toggle_group_remove (group, 1);
  g_assert_cmpuint (glass_toggle_group_get_active (group), ==, 0);
  g_assert_cmpstr (glass_toggle_group_get_active_name (group), ==, "b");
  g_assert_cmpint (notified, ==, 2);

  /* The active one in the middle: the next takes its place (and the
   * index stays, but :active is notified). */
  glass_toggle_group_append (group, "e", "E", NULL);
  glass_toggle_group_append (group, "f", "F", NULL);
  glass_toggle_group_set_active_name (group, "e");
  notified = 0;
  glass_toggle_group_remove (group, 1);
  g_assert_cmpuint (glass_toggle_group_get_active (group), ==, 1);
  g_assert_cmpstr (glass_toggle_group_get_active_name (group), ==, "f");
  g_assert_cmpint (notified, ==, 1);
  g_assert_cmpint (count_buttons (GTK_WIDGET (group), FALSE), ==, 2);

  glass_toggle_group_remove_all (group);
  g_assert_cmpuint (glass_toggle_group_get_n_toggles (group), ==, 0);
  g_assert_null (glass_toggle_group_get_active_name (group));
  g_assert_cmpint (count_buttons (GTK_WIDGET (group), FALSE), ==, 0);

  /* It fills again as a new group. */
  glass_toggle_group_append (group, "g", "G", NULL);
  g_assert_cmpstr (glass_toggle_group_get_active_name (group), ==, "g");

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

  /* Glass of its own stands beside the capsule, not on it. */
  {
    GtkWidget *glass = g_object_ref_sink (glass_button_new_from_icon_name ("edit-find-symbolic"));
    GtkWidget *group = glass_group_new ();

    glass_header_bar_pack_end (GLASS_HEADER_BAR (header), glass);
    glass_header_bar_pack_start (GLASS_HEADER_BAR (header), group);
    g_assert_null (gtk_widget_get_ancestor (glass, GLASS_TYPE_BUTTON_GROUP));
    g_assert_null (gtk_widget_get_ancestor (gtk_widget_get_parent (glass), GLASS_TYPE_PANEL));
    g_assert_null (gtk_widget_get_ancestor (group, GLASS_TYPE_BUTTON_GROUP));
    glass_header_bar_remove (GLASS_HEADER_BAR (header), glass);
    glass_header_bar_remove (GLASS_HEADER_BAR (header), group);
    g_assert_null (gtk_widget_get_parent (glass));
    g_object_unref (glass);
  }

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

/* With a boolean stateful action, the switch follows its state, turning it
 * changes the state, and a disabled action makes it insensitive. */
static void
test_switch_action (void)
{
  GtkWidget *box = g_object_ref_sink (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0));
  GtkWidget *sw = glass_switch_new ();
  GSimpleActionGroup *actions = g_simple_action_group_new ();
  GSimpleAction *action = g_simple_action_new_stateful ("flag", NULL, g_variant_new_boolean (FALSE));
  g_autoptr (GVariant) state = NULL;

  g_action_map_add_action (G_ACTION_MAP (actions), G_ACTION (action));
  gtk_widget_insert_action_group (box, "test", G_ACTION_GROUP (actions));
  /* Named before it has a parent, as a builder file or a constructor does
   * (地雷44). */
  gtk_actionable_set_action_name (GTK_ACTIONABLE (sw), "test.flag");
  gtk_box_append (GTK_BOX (box), sw);
  g_assert_cmpstr (gtk_actionable_get_action_name (GTK_ACTIONABLE (sw)), ==, "test.flag");

  g_simple_action_set_state (action, g_variant_new_boolean (TRUE));
  g_assert_true (glass_switch_get_active (GLASS_SWITCH (sw)));

  glass_switch_set_active (GLASS_SWITCH (sw), FALSE);
  state = g_action_get_state (G_ACTION (action));
  g_assert_false (g_variant_get_boolean (state));
  g_assert_false (glass_switch_get_active (GLASS_SWITCH (sw)));

  g_simple_action_set_enabled (action, FALSE);
  g_assert_false (gtk_widget_get_sensitive (sw));
  g_simple_action_set_enabled (action, TRUE);
  g_assert_true (gtk_widget_get_sensitive (sw));

  g_object_unref (action);
  g_object_unref (actions);
  g_object_unref (box);
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
activated (GSimpleAction *action, GVariant *parameter, int *count)
{
  (*count)++;
}

/* The menu button's action: its enabled state and its activation. */
static void
test_menu_button_action (void)
{
  GtkWidget *box = g_object_ref_sink (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0));
  GtkWidget *mb = glass_menu_button_new ();
  GSimpleActionGroup *actions = g_simple_action_group_new ();
  GSimpleAction *action = g_simple_action_new ("menu", NULL);
  GtkWidget *button;
  int count = 0;

  g_signal_connect (action, "activate", G_CALLBACK (activated), &count);
  g_action_map_add_action (G_ACTION_MAP (actions), G_ACTION (action));
  gtk_widget_insert_action_group (box, "test", G_ACTION_GROUP (actions));
  g_object_set (mb, "action-name", "test.menu", NULL);
  gtk_box_append (GTK_BOX (box), mb);
  g_assert_cmpstr (gtk_actionable_get_action_name (GTK_ACTIONABLE (mb)), ==, "test.menu");

  button = gtk_widget_get_first_child (mb);
  g_assert_true (GTK_IS_BUTTON (button));
  g_signal_emit_by_name (button, "clicked");
  g_assert_cmpint (count, ==, 1);

  g_simple_action_set_enabled (action, FALSE);
  g_assert_false (gtk_widget_get_sensitive (button));

  /* GlassButton forwards the same way. */
  g_simple_action_set_enabled (action, TRUE);
  count = 0;
  button = glass_button_new_from_icon_name ("go-next-symbolic");
  gtk_actionable_set_action_name (GTK_ACTIONABLE (button), "test.menu");
  gtk_box_append (GTK_BOX (box), button);
  g_signal_emit_by_name (glass_panel_get_child (GLASS_PANEL (button)), "clicked");
  g_assert_cmpint (count, ==, 1);

  g_object_unref (action);
  g_object_unref (actions);
  g_object_unref (box);
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

/* Radii per corner: set, read back, clamped for the glass, and the CSS
 * shape (fallback and clipping) follows. */
static void
test_corner_radii (void)
{
  GtkWidget *panel = g_object_ref_sink (glass_panel_new ());
  double tl, tr, br, bl, r[4];

  g_assert_false (glass_panel_has_corner_radii (GLASS_PANEL (panel)));
  glass_panel_set_corner_radius (GLASS_PANEL (panel), 12.0);
  glass_panel_set_corner_radii (GLASS_PANEL (panel), 24.0, -5.0, 0.0, -1.0);
  glass_panel_get_corner_radii (GLASS_PANEL (panel), &tl, &tr, &br, &bl);
  g_assert_cmpfloat (tl, ==, 24.0);
  g_assert_cmpfloat (tr, ==, -1.0);    /* negative: the panel's */
  g_assert_cmpfloat (br, ==, 0.0);
  g_assert_cmpfloat (bl, ==, -1.0);
  g_assert_true (glass_panel_has_corner_radii (GLASS_PANEL (panel)));

  glass_panel_resolve_corners (GLASS_PANEL (panel), &GRAPHENE_RECT_INIT (0, 0, 100, 40), r);
  g_assert_cmpfloat (r[0], ==, 20.0);  /* half the shorter side */
  g_assert_cmpfloat (r[1], ==, 12.0);
  g_assert_cmpfloat (r[2], ==, 0.0);
  g_assert_cmpfloat (r[3], ==, 12.0);
  g_assert_cmpfloat (glass_panel_effective_radius (GLASS_PANEL (panel), &GRAPHENE_RECT_INIT (0, 0, 100, 40)), ==, 20.0);

  g_object_get (panel, "bottom-right-radius", &br, NULL);
  g_assert_cmpfloat (br, ==, 0.0);
  g_object_set (panel, "top-left-radius", -1.0, "bottom-right-radius", -1.0, NULL);
  g_assert_false (glass_panel_has_corner_radii (GLASS_PANEL (panel)));

  g_object_unref (panel);
}

/* Morphing (design.md §6.8): in a group a panel shown comes out of its
 * neighbour and one hidden leaves a ghost; a panel with a partner's
 * morph-id takes its glass, and the partner leaves no ghost. */
static void
test_morph (void)
{
  GtkWidget *view = glass_view_new ();
  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *group = glass_group_new ();
  GtkWidget *a = glass_button_new_from_icon_name ("go-previous-symbolic");
  GtkWidget *b = glass_button_new_from_icon_name ("go-next-symbolic");
  GtkWidget *button = glass_button_new_from_icon_name ("edit-find-symbolic");
  GtkWidget *field = glass_search_entry_new ();
  GtkWidget *column = gtk_box_new (GTK_ORIENTATION_VERTICAL, 20);
  GtkWidget *window;
  graphene_rect_t rect, target = GRAPHENE_RECT_INIT (0, 0, 40, 40);
  double corners[4];
  gboolean animated;

  gtk_box_append (GTK_BOX (row), a);
  gtk_box_append (GTK_BOX (row), b);
  gtk_widget_set_visible (b, FALSE);
  glass_group_set_child (GLASS_GROUP (group), row);
  glass_panel_set_morph_id (GLASS_PANEL (button), "search");
  glass_panel_set_morph_id (GLASS_PANEL (field), "search");
  gtk_widget_set_visible (field, FALSE);
  gtk_box_append (GTK_BOX (column), group);
  gtk_box_append (GTK_BOX (column), button);
  gtk_box_append (GTK_BOX (column), field);
  gtk_widget_set_halign (column, GTK_ALIGN_START);
  gtk_widget_set_valign (column, GTK_ALIGN_START);
  glass_view_set_content (GLASS_VIEW (view), gtk_drawing_area_new ());
  glass_view_add_overlay (GLASS_VIEW (view), column);
  window = window_with (view);
  gtk_window_present (GTK_WINDOW (window));
  spin (500);
  animated = adw_get_enable_animations (window);

  /* Nothing moves at rest. */
  g_assert_false (glass_panel_get_morph (GLASS_PANEL (a), &target, &rect, corners));

  gtk_widget_set_visible (b, TRUE);
  if (animated)
    g_assert_true (glass_panel_get_morph (GLASS_PANEL (b), &target, &rect, corners));
  spin (1200);
  g_assert_false (glass_panel_get_morph (GLASS_PANEL (b), &target, &rect, corners));

  gtk_widget_set_visible (b, FALSE);
  if (animated)
    g_assert_true (glass_panel_get_ghost (GLASS_PANEL (b), GLASS_VIEW (view), &rect, corners));
  /* Taken in as it gets there (about 0.2 s), not once the springs have
   * come to rest (0.7 s): over its neighbour whole, the two swell. */
  spin (450);
  g_assert_false (glass_panel_get_ghost (GLASS_PANEL (b), GLASS_VIEW (view), &rect, corners));

  /* The swap: outside a group, no ghost; the field morphs. */
  gtk_widget_set_visible (button, FALSE);
  gtk_widget_set_visible (field, TRUE);
  g_assert_false (glass_panel_get_ghost (GLASS_PANEL (button), GLASS_VIEW (view), &rect, corners));
  if (animated)
    g_assert_true (glass_panel_get_morph (GLASS_PANEL (field), &target, &rect, corners));
  spin (1200);
  g_assert_false (glass_panel_get_morph (GLASS_PANEL (field), &target, &rect, corners));

  gtk_window_destroy (GTK_WINDOW (window));
}

static void
test_morph_id (void)
{
  GtkWidget *panel = g_object_ref_sink (glass_panel_new ());
  int notified = 0;

  g_assert_null (glass_panel_get_morph_id (GLASS_PANEL (panel)));
  g_signal_connect (panel, "notify::morph-id", G_CALLBACK (count_notify), &notified);
  glass_panel_set_morph_id (GLASS_PANEL (panel), "search");
  glass_panel_set_morph_id (GLASS_PANEL (panel), "search");
  g_assert_cmpstr (glass_panel_get_morph_id (GLASS_PANEL (panel)), ==, "search");
  g_assert_cmpint (notified, ==, 1);

  g_object_unref (panel);
}

static GtkWidget *
find_class (GtkWidget  *widget,
            const char *css_class)
{
  if (gtk_widget_has_css_class (widget, css_class))
    return widget;
  for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    {
      GtkWidget *found = find_class (child, css_class);

      if (found)
        return found;
    }
  return NULL;
}

/* The tab bar follows the stack's pages and their selection, both ways. */
static void
test_tab_bar (void)
{
  GtkWidget *stack = g_object_ref_sink (adw_view_stack_new ());
  GtkWidget *bar = g_object_ref_sink (glass_tab_bar_new ());
  AdwViewStackPage *hidden;
  GtkWidget *group;

  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack), gtk_label_new ("1"), "one", "One", "go-home-symbolic");
  adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack), gtk_label_new ("2"), "two", "Two", "go-next-symbolic");
  hidden = adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (stack), gtk_label_new ("3"), "three", "Three",
                                                "go-last-symbolic");
  glass_tab_bar_set_stack (GLASS_TAB_BAR (bar), ADW_VIEW_STACK (stack));
  g_assert_true (glass_tab_bar_get_stack (GLASS_TAB_BAR (bar)) == ADW_VIEW_STACK (stack));

  group = gtk_widget_get_first_child (bar);
  g_assert_true (GLASS_IS_TOGGLE_GROUP (group));
  g_assert_cmpuint (glass_toggle_group_get_n_toggles (GLASS_TOGGLE_GROUP (group)), ==, 3);
  g_assert_cmpint (count_buttons (group, TRUE), ==, 0);   /* frameless (地雷27) */

  /* Hidden pages have no item. */
  adw_view_stack_page_set_visible (hidden, FALSE);
  g_assert_cmpuint (glass_toggle_group_get_n_toggles (GLASS_TOGGLE_GROUP (group)), ==, 2);

  /* The stack drives the bar... */
  adw_view_stack_set_visible_child_name (ADW_VIEW_STACK (stack), "two");
  g_assert_cmpuint (glass_toggle_group_get_active (GLASS_TOGGLE_GROUP (group)), ==, 1);
  /* ... and the bar the stack. */
  glass_toggle_group_set_active (GLASS_TOGGLE_GROUP (group), 0);
  g_assert_cmpstr (adw_view_stack_get_visible_child_name (ADW_VIEW_STACK (stack)), ==, "one");

  /* A page's dot or title changes its item, not the bar: rebuilt, the
   * plate would stop short of the page being shown (a page losing its dot
   * as it is shown). */
  {
    AdwViewStackPage *first = adw_view_stack_get_page (ADW_VIEW_STACK (stack),
                                                       adw_view_stack_get_child_by_name (ADW_VIEW_STACK (stack), "one"));
    GtkWidget *tab = find_class (group, "tab");

    adw_view_stack_page_set_needs_attention (first, TRUE);
    g_assert_true (find_class (group, "tab") == tab);
    g_assert_true (gtk_widget_get_visible (find_class (tab, "tab-dot")));
    adw_view_stack_page_set_title (first, "First");
    g_assert_true (find_class (group, "tab") == tab);
    g_assert_cmpstr (gtk_widget_get_tooltip_text (tab), ==, "First");
    adw_view_stack_page_set_needs_attention (first, FALSE);
    g_assert_false (gtk_widget_get_visible (find_class (tab, "tab-dot")));
  }

  glass_tab_bar_set_stack (GLASS_TAB_BAR (bar), NULL);
  g_assert_cmpuint (glass_toggle_group_get_n_toggles (GLASS_TOGGLE_GROUP (group)), ==, 0);

  g_object_unref (bar);
  g_object_unref (stack);
}

static void
count_signal (GtkWidget *widget, int *count)
{
  (*count)++;
}

static void
test_search_entry (void)
{
  GtkWidget *entry = g_object_ref_sink (glass_search_entry_new ());
  int changed = 0;

  g_assert_true (GTK_IS_EDITABLE (entry));
  g_assert_true (GLASS_IS_PANEL (entry));
  g_assert_cmpint (gtk_accessible_get_accessible_role (GTK_ACCESSIBLE (entry)), ==, GTK_ACCESSIBLE_ROLE_SEARCH_BOX);
  g_assert_cmpint (count_buttons (entry, TRUE), ==, 0);

  glass_search_entry_set_placeholder_text (GLASS_SEARCH_ENTRY (entry), "Search");
  g_assert_cmpstr (glass_search_entry_get_placeholder_text (GLASS_SEARCH_ENTRY (entry)), ==, "Search");

  /* No delay: search-changed at once, and the text is the editable's. */
  glass_search_entry_set_search_delay (GLASS_SEARCH_ENTRY (entry), 0);
  g_signal_connect (entry, "search-changed", G_CALLBACK (count_signal), &changed);
  gtk_editable_set_text (GTK_EDITABLE (entry), "glass");
  g_assert_cmpstr (gtk_editable_get_text (GTK_EDITABLE (entry)), ==, "glass");
  g_assert_cmpint (changed, >=, 1);

  /* With a delay, the new text only after it. (set_text() empties the
   * text first, and emptying is reported at once, like GtkSearchEntry.) */
  glass_search_entry_set_search_delay (GLASS_SEARCH_ENTRY (entry), 50);
  gtk_editable_set_text (GTK_EDITABLE (entry), "glass lib");
  {
    int before = changed;

    spin (150);
    g_assert_cmpint (changed, ==, before + 1);
  }

  g_object_unref (entry);
}

/* The jelly (glass-jelly.c, design.md §6.9): sent to a place further
 * along, it stretches while it travels, its front runs on past the mark,
 * and it comes to rest there; snapped, it is there at once. */
static void
test_jelly (void)
{
  const graphene_rect_t from = GRAPHENE_RECT_INIT (0, 10, 80, 30);
  const graphene_rect_t to = GRAPHENE_RECT_INIT (200, 10, 80, 30);
  GlassJelly jelly = { 0 };
  graphene_rect_t r;
  double widest = 0.0, furthest = 0.0, progress = 0.0;
  gint64 now = 1000000;
  gboolean moving = TRUE;
  int frames = 0;

  glass_jelly_start (&jelly, &glass_jelly_plate, &from, TRUE, FALSE);
  g_assert_true (jelly.active);
  glass_jelly_set_mark (&jelly, &to);
  for (; moving && frames < 600; frames++)
    {
      now += 16667;
      moving = glass_jelly_step (&jelly, now);
      glass_jelly_get (&jelly, &r);
      widest = MAX (widest, r.size.width);
      furthest = MAX (furthest, r.origin.x + r.size.width);
      /* Along the row only. */
      g_assert_cmpfloat (r.origin.y, ==, 10.0f);
      g_assert_cmpfloat (r.size.height, ==, 30.0f);
      g_assert_cmpfloat (glass_jelly_progress (&jelly, &from), >=, 0.0);
      progress = glass_jelly_progress (&jelly, &from);
    }
  g_assert_false (moving);
  g_assert_cmpint (frames, <, 120);                    /* at rest within two seconds */
  g_assert_cmpfloat (widest, >, 80.0 * 1.05);          /* it stretched */
  g_assert_cmpfloat (widest, <=, 80.0 * glass_jelly_plate.max_stretch + 0.01);
  g_assert_cmpfloat (furthest, >, 280.0);              /* its front ran on */
  g_assert_cmpfloat (progress, ==, 1.0);
  glass_jelly_get (&jelly, &r);
  g_assert_true (graphene_rect_equal (&r, &to));

  glass_jelly_set_mark (&jelly, &from);
  glass_jelly_snap (&jelly);
  glass_jelly_get (&jelly, &r);
  g_assert_true (graphene_rect_equal (&r, &from));

  /* A tap's glide: there quickly, no stretch, no overshoot. */
  glass_jelly_start (&jelly, &glass_jelly_glide, &from, TRUE, FALSE);
  glass_jelly_set_mark (&jelly, &to);
  widest = furthest = 0.0;
  moving = TRUE;
  now = 1000000;
  for (frames = 0; moving && frames < 600; frames++)
    {
      now += 16667;
      moving = glass_jelly_step (&jelly, now);
      glass_jelly_get (&jelly, &r);
      widest = MAX (widest, r.size.width);
      furthest = MAX (furthest, r.origin.x + r.size.width);
      if (frames == 11)                                /* 0.2 s */
        g_assert_cmpfloat (glass_jelly_progress (&jelly, &from), >, 0.9);
    }
  g_assert_false (moving);
  g_assert_cmpint (frames, <, 30);                     /* at rest within half a second */
  g_assert_cmpfloat (widest, <=, 80.01);
  g_assert_cmpfloat (furthest, <=, 280.01);
}

/* Motion settings (design.md §13): animations off, or less motion asked
 * for (GTK 4.22's gtk-interface-reduced-motion). */
static void
test_reduced_motion (void)
{
  GtkSettings *settings = gtk_settings_get_default ();
  GtkWidget *widget = g_object_ref_sink (gtk_label_new (NULL));
  gboolean animations;
  GtkReducedMotion reduced;

  g_object_get (settings, "gtk-enable-animations", &animations, "gtk-interface-reduced-motion", &reduced, NULL);
  g_object_set (settings, "gtk-enable-animations", TRUE,
                "gtk-interface-reduced-motion", GTK_REDUCED_MOTION_NO_PREFERENCE, NULL);
  g_assert_false (glass_motion_reduced (widget));
  g_object_set (settings, "gtk-interface-reduced-motion", GTK_REDUCED_MOTION_REDUCE, NULL);
  g_assert_true (glass_motion_reduced (widget));
  g_assert_true (glass_animations_enabled (widget));
  g_object_set (settings, "gtk-interface-reduced-motion", GTK_REDUCED_MOTION_NO_PREFERENCE,
                "gtk-enable-animations", FALSE, NULL);
  g_assert_true (glass_motion_reduced (widget));
  g_assert_false (glass_animations_enabled (widget));

  g_object_set (settings, "gtk-enable-animations", animations, "gtk-interface-reduced-motion", reduced, NULL);
  g_object_unref (widget);
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
  g_test_add_func ("/widgets/fallback-realized", test_fallback_realized);
  g_test_add_func ("/widgets/fallback-adaptive", test_fallback_adaptive);
  g_test_add_func ("/widgets/panel-params", test_panel_params);
  g_test_add_func ("/widgets/panel-lens", test_panel_lens);
  g_test_add_func ("/widgets/prominent", test_prominent);
  g_test_add_func ("/widgets/css-classes", test_css_classes);
  g_test_add_func ("/widgets/toggle-group", test_toggle_group);
  g_test_add_func ("/widgets/toggle-group-remove", test_toggle_group_remove);
  g_test_add_func ("/widgets/toolbar-view", test_toolbar_view);
  g_test_add_func ("/widgets/split-view", test_split_view);
  g_test_add_func ("/widgets/header-capsules", test_header_capsules);
  g_test_add_func ("/widgets/button-group", test_button_group);
  g_test_add_func ("/widgets/switch", test_switch);
  g_test_add_func ("/widgets/switch-action", test_switch_action);
  g_test_add_func ("/widgets/menu-button-action", test_menu_button_action);
  g_test_add_func ("/widgets/slider", test_slider);
  g_test_add_func ("/widgets/group", test_group);
  g_test_add_func ("/widgets/popover-menu", test_popover_menu);
  g_test_add_func ("/widgets/dialog", test_dialog);
  g_test_add_func ("/widgets/nested-view-redraw", test_nested_view_redraw);
  g_test_add_func ("/widgets/dialog-redraw", test_dialog_redraw);
  g_test_add_func ("/widgets/corner-radii", test_corner_radii);
  g_test_add_func ("/widgets/morph-id", test_morph_id);
  g_test_add_func ("/widgets/morph", test_morph);
  g_test_add_func ("/widgets/tab-bar", test_tab_bar);
  g_test_add_func ("/widgets/search-entry", test_search_entry);
  g_test_add_func ("/widgets/jelly", test_jelly);
  g_test_add_func ("/widgets/reduced-motion", test_reduced_motion);

  return g_test_run ();
}
