/* s1-main.c — S1 spike: in-app glass over scrolling content.
 *
 * Run:   ./build/spikes/s1-full-renderer/glass-s1 [--autoscroll] [...]
 * Keys:  Space  glass on/off          C  caches on/off
 *        D      debug view 0/1/2      A  autoscroll on/off
 *        S      supersampling on/off  H  in-app / extension shadow
 *        R      shader core / reference shader
 *        E      edge preset: crisp -> crisp-strong -> crisp-soft -> thinner -> extension
 *        F      measured / estimated (reference) lens footprint
 *        B      blur radius 2 -> 3 -> 4 -> 5 px
 *
 * Prints one line of measurements per second (see README.md).
 *
 * SPDX-License-Identifier: MIT
 */
#include "s1-glass-view.h"

#include <math.h>

static gboolean opt_no_cache;
static gboolean opt_private_renderer;
static gboolean opt_autoscroll;
static gboolean opt_no_glass;
static double   opt_blur_radius = 3.0;
static int      opt_downscale = 2;
static int      opt_debug_view;
static double   opt_quit_after;
static char    *opt_screenshot;
static double   opt_screenshot_at = 1.5;
static gboolean opt_pulse;
static double   opt_scroll_to = -1.0;
static gboolean opt_capture_full;
static gboolean opt_no_supersample;
static int      opt_rim_samples;
static gboolean opt_estimated_footprint;
static gboolean opt_reference;
static char   **opt_params;
static gboolean opt_screenshot_panel;
static char    *opt_preset;
static char    *opt_sweep_dir;
static char   **opt_candidates;
static char    *opt_sweep_scroll;
static double   opt_shadow_radius = -1.0;
static double   opt_shadow_intensity = -1.0;

static const GOptionEntry entries[] = {
  { "autoscroll", 0, 0, G_OPTION_ARG_NONE, &opt_autoscroll, "Scroll the content back and forth continuously", NULL },
  { "no-cache", 0, 0, G_OPTION_ARG_NONE, &opt_no_cache, "Re-capture and re-draw the glass on every snapshot", NULL },
  { "private-renderer", 0, 0, G_OPTION_ARG_NONE, &opt_private_renderer, "Capture with a dedicated GskRenderer instead of the window's", NULL },
  { "no-glass", 0, 0, G_OPTION_ARG_NONE, &opt_no_glass, "Start with the glass switched off", NULL },
  { "blur-radius", 0, 0, G_OPTION_ARG_DOUBLE, &opt_blur_radius, "Blur radius in px (default 3; the extension's dock uses 5)", "PX" },
  { "preset", 0, 0, G_OPTION_ARG_STRING, &opt_preset, "Edge preset: crisp (default), crisp-strong, crisp-soft, thinner, or extension (the extension's values)", "NAME" },
  { "param", 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_params, "Override an optical parameter, e.g. --param rim_width=2.5 (repeatable)", "NAME=VALUE" },
  { "reference", 0, 0, G_OPTION_ARG_NONE, &opt_reference, "Draw with the unmodified reference glass.frag instead of the shader core", NULL },
  { "no-supersample", 0, 0, G_OPTION_ARG_NONE, &opt_no_supersample, "One glass.frag sample per pixel (the extension's sampling)", NULL },
  { "estimated-footprint", 0, 0, G_OPTION_ARG_NONE, &opt_estimated_footprint, "The reference's analytic lens footprint instead of the measured one", NULL },
  { "rim-samples", 0, 0, G_OPTION_ARG_INT, &opt_rim_samples, "Samples per pixel where the lens acts (Hammersley; only if more than elsewhere)", "N" },
  { "shadow-radius", 0, 0, G_OPTION_ARG_DOUBLE, &opt_shadow_radius, "Drop shadow radius in px (in-app default 16; the extension uses 30)", "PX" },
  { "shadow-intensity", 0, 0, G_OPTION_ARG_DOUBLE, &opt_shadow_intensity, "Drop shadow intensity (in-app default 0.20; the extension uses 0.55)", "X" },
  { "capture-full", 0, 0, G_OPTION_ARG_NONE, &opt_capture_full, "Capture at full resolution and box-downsample in GL (the extension's chain)", NULL },
  { "downscale", 0, 0, G_OPTION_ARG_INT, &opt_downscale, "Capture downscale, 2 (default) or 4", "N" },
  { "debug-view", 0, 0, G_OPTION_ARG_INT, &opt_debug_view, "glass.frag debug view: 0 off, 1 masks, 2 raw masks", "N" },
  { "quit-after", 0, 0, G_OPTION_ARG_DOUBLE, &opt_quit_after, "Quit after this many seconds", "SEC" },
  { "screenshot", 0, 0, G_OPTION_ARG_FILENAME, &opt_screenshot, "Save the window content as PNG", "FILE" },
  { "screenshot-at", 0, 0, G_OPTION_ARG_DOUBLE, &opt_screenshot_at, "When to take the screenshot, in seconds (default 1.5)", "SEC" },
  { "screenshot-panel", 0, 0, G_OPTION_ARG_NONE, &opt_screenshot_panel, "Crop the screenshot to the top panel (plus 14 px)", NULL },
  { "sweep", 0, 0, G_OPTION_ARG_FILENAME, &opt_sweep_dir, "Render every --candidate at every --sweep-scroll offset in this window, save panel crops to DIR, then quit", "DIR" },
  { "candidate", 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_candidates, "A named set of parameters for --sweep, e.g. a:rim_width=2,ao_radius=3 (repeatable)", "NAME:K=V,..." },
  { "sweep-scroll", 0, 0, G_OPTION_ARG_STRING, &opt_sweep_scroll, "Scroll offsets for --sweep (default 60,420,740)", "A,B,..." },
  { "scroll-to", 0, 0, G_OPTION_ARG_DOUBLE, &opt_scroll_to, "Start scrolled to this offset (px)", "PX" },
  { "pulse", 0, 0, G_OPTION_ARG_NONE, &opt_pulse, "Redraw the top panel's star 10 times a second (foreground-only changes)", NULL },
  { NULL }
};

static const char *css =
  "window { background: #1b1b1f; }\n"
  ".s1-content { padding: 96px 28px 128px 28px; }\n"
  ".s1-block { border-radius: 18px; min-height: 150px; }\n"
  ".s1-stripes-v { background-image: repeating-linear-gradient(90deg, #111 0px, #111 12px, #f5f5f5 12px, #f5f5f5 24px); }\n"
  ".s1-stripes-h { background-image: repeating-linear-gradient(0deg, #e53935 0px, #e53935 10px, #1e88e5 10px, #1e88e5 20px); }\n"
  ".s1-checker { background-color: #fafafa;"
  "  background-image: linear-gradient(45deg, #222 25%, transparent 25%, transparent 75%, #222 75%),"
  "                    linear-gradient(45deg, #222 25%, transparent 25%, transparent 75%, #222 75%);"
  "  background-size: 36px 36px; background-position: 0 0, 18px 18px; }\n"
  ".s1-grad-a { background-image: linear-gradient(120deg, #ff6b6b, #feca57, #48dbfb, #5f27cd); }\n"
  ".s1-grad-b { background-image: radial-gradient(circle at 30% 40%, #fff 0%, #ffd6e7 20%, #5b2a86 60%, #0b0b1e 100%); }\n"
  ".s1-text { background: #fdfdfc; color: #1a1a1a; padding: 22px 26px; border-radius: 18px; }\n"
  ".s1-text-dark { background: #121218; color: #f2f2f2; padding: 22px 26px; border-radius: 18px; }\n"
  ".s1-photo { border-radius: 18px; }\n"
  ".s1-panel { padding: 6px 18px; min-height: 40px; color: #ffffff; }\n"
  ".s1-panel label { font-weight: 700; text-shadow: 0 1px 2px alpha(black, 0.55); }\n"
  ".s1-panel image, .s1-panel button { color: #ffffff; -gtk-icon-shadow: 0 1px 2px alpha(black, 0.55); }\n"
  ".s1-panel button.flat { min-width: 36px; min-height: 36px; }\n";

static GtkWidget *
block (const char *css_class, int height)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  gtk_widget_add_css_class (box, "s1-block");
  gtk_widget_add_css_class (box, css_class);
  gtk_widget_set_size_request (box, -1, height);

  return box;
}

static GtkWidget *
text_block (gboolean dark, int index)
{
  static const char *texts[] = {
    "<span size='xx-large' weight='bold'>ガラスの向こうの文字</span>\n"
    "The quick brown fox jumps over the lazy dog. 素早い茶色の狐がのろまな犬を飛び越える。"
    "細い線と太い線、明るい面と暗い面が、ガラスの縁でどう曲がるかを見るための段落です。",
    "<span size='xx-large' weight='bold'>Refraction test</span>\n"
    "Scroll this under the panels. The edge of the glass should bend the text inwards, and the "
    "bend must move together with the text in the same frame, never a frame behind.",
  };
  GtkWidget *label = gtk_label_new (NULL);

  gtk_label_set_markup (GTK_LABEL (label), texts[index % G_N_ELEMENTS (texts)]);
  gtk_label_set_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_widget_add_css_class (label, dark ? "s1-text-dark" : "s1-text");

  return label;
}

/* Wallpapers shipped with the system, scaled down on load. Nothing is
 * bundled with the spike (licensing). */
static GPtrArray *
load_photos (int max_count)
{
  GPtrArray *photos = g_ptr_array_new_with_free_func (g_object_unref);
  GPtrArray *dirs = g_ptr_array_new_with_free_func (g_free);

  g_ptr_array_add (dirs, g_strdup ("/usr/share/backgrounds"));

  for (guint d = 0; d < dirs->len && (int) photos->len < max_count; d++)
    {
      GDir *dir = g_dir_open (g_ptr_array_index (dirs, d), 0, NULL);
      const char *name;

      if (dir == NULL)
        continue;

      while ((name = g_dir_read_name (dir)) && (int) photos->len < max_count)
        {
          g_autofree char *path = g_build_filename (g_ptr_array_index (dirs, d), name, NULL);
          g_autofree char *lower = g_ascii_strdown (name, -1);
          GdkPixbuf *pixbuf;

          if (g_file_test (path, G_FILE_TEST_IS_DIR))
            {
              if (dirs->len < 8)
                g_ptr_array_add (dirs, g_steal_pointer (&path));
              continue;
            }
          if (!g_str_has_suffix (lower, ".png") && !g_str_has_suffix (lower, ".jpg") &&
              !g_str_has_suffix (lower, ".jpeg"))
            continue;

          pixbuf = gdk_pixbuf_new_from_file_at_scale (path, 720, -1, TRUE, NULL);
          if (pixbuf == NULL)
            continue;

          {
            GBytes *bytes = gdk_pixbuf_read_pixel_bytes (pixbuf);
            GdkTexture *texture = gdk_memory_texture_new (
                gdk_pixbuf_get_width (pixbuf), gdk_pixbuf_get_height (pixbuf),
                gdk_pixbuf_get_has_alpha (pixbuf) ? GDK_MEMORY_R8G8B8A8 : GDK_MEMORY_R8G8B8,
                bytes, gdk_pixbuf_get_rowstride (pixbuf));

            g_ptr_array_add (photos, texture);
            g_bytes_unref (bytes);
          }
          g_object_unref (pixbuf);
        }
      g_dir_close (dir);
    }

  g_ptr_array_unref (dirs);
  return photos;
}

static GtkWidget *
photo_row (GPtrArray *photos, guint *next)
{
  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);

  gtk_box_set_homogeneous (GTK_BOX (row), TRUE);
  for (int i = 0; i < 2; i++)
    {
      GtkWidget *picture;

      if (photos->len > 0)
        {
          picture = gtk_picture_new_for_paintable (g_ptr_array_index (photos, *next % photos->len));
          gtk_picture_set_content_fit (GTK_PICTURE (picture), GTK_CONTENT_FIT_COVER);
          (*next)++;
        }
      else
        picture = block ((*next)++ % 2 ? "s1-grad-a" : "s1-grad-b", 280);

      gtk_widget_set_size_request (picture, -1, 280);
      gtk_widget_add_css_class (picture, "s1-photo");
      gtk_widget_set_overflow (picture, GTK_OVERFLOW_HIDDEN);
      gtk_box_append (GTK_BOX (row), picture);
    }

  return row;
}

static GtkWidget *
build_content (GtkWidget **out_scrolled)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 22);
  GtkWidget *scrolled = gtk_scrolled_window_new ();
  GPtrArray *photos = load_photos (8);
  guint next_photo = 0;

  gtk_widget_add_css_class (box, "s1-content");

  for (int rep = 0; rep < 3; rep++)
    {
      gtk_box_append (GTK_BOX (box), block ("s1-stripes-v", 160));
      gtk_box_append (GTK_BOX (box), text_block (FALSE, rep));
      gtk_box_append (GTK_BOX (box), photo_row (photos, &next_photo));
      gtk_box_append (GTK_BOX (box), block ("s1-checker", 180));
      gtk_box_append (GTK_BOX (box), text_block (TRUE, rep + 1));
      gtk_box_append (GTK_BOX (box), block ("s1-stripes-h", 140));
      gtk_box_append (GTK_BOX (box), photo_row (photos, &next_photo));
      gtk_box_append (GTK_BOX (box), block ("s1-grad-a", 180));
      gtk_box_append (GTK_BOX (box), block ("s1-grad-b", 220));
    }

  g_ptr_array_unref (photos);

  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), box);
  *out_scrolled = scrolled;

  return scrolled;
}

static GtkWidget *
icon_button (const char *icon)
{
  GtkWidget *button = gtk_button_new_from_icon_name (icon);

  gtk_widget_add_css_class (button, "flat");
  gtk_widget_add_css_class (button, "circular");

  return button;
}

static GtkWidget *
top_panel (void)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *label = gtk_label_new ("Glass Gallery — S1");

  gtk_widget_add_css_class (box, "s1-panel");
  gtk_box_append (GTK_BOX (box), gtk_image_new_from_icon_name ("view-grid-symbolic"));
  gtk_box_append (GTK_BOX (box), label);
  gtk_box_append (GTK_BOX (box), icon_button ("starred-symbolic"));
  gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (box, GTK_ALIGN_START);
  gtk_widget_set_margin_top (box, 16);

  return box;
}

static GtkWidget *
bottom_panel (void)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  const char *icons[] = { "go-previous-symbolic", "media-playback-start-symbolic",
                          "starred-symbolic", "folder-symbolic", "user-trash-symbolic" };

  gtk_widget_add_css_class (box, "s1-panel");
  for (guint i = 0; i < G_N_ELEMENTS (icons); i++)
    gtk_box_append (GTK_BOX (box), icon_button (icons[i]));
  gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (box, GTK_ALIGN_END);
  gtk_widget_set_margin_bottom (box, 22);

  return box;
}

/* ── Measurement loop ─────────────────────────────────────────────────────── */

typedef struct {
  GtkApplication *app;
  GtkWidget      *window;
  S1GlassView    *view;
  GtkWidget      *scrolled;
  GtkWidget      *top_panel;
  gboolean        autoscroll;
  gboolean        printed_header;
  const char     *preset;
  double          app_shadow_radius;
  double          app_shadow_intensity;
  guint           frames;       /* frame-clock paints since the last report */
} S1App;

static gboolean
autoscroll_tick (GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
  S1App *s = data;
  GtkAdjustment *adj;
  double range, t, period, phase, v;

  /* --scroll-to: applied once the content has a size. */
  if (opt_scroll_to >= 0.0)
    {
      adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (s->scrolled));
      if (gtk_adjustment_get_upper (adj) > gtk_adjustment_get_page_size (adj))
        {
          gtk_adjustment_set_value (adj, opt_scroll_to);
          opt_scroll_to = -1.0;
        }
    }

  if (!s->autoscroll)
    return G_SOURCE_CONTINUE;

  adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (s->scrolled));
  range = gtk_adjustment_get_upper (adj) - gtk_adjustment_get_page_size (adj) - gtk_adjustment_get_lower (adj);
  if (range <= 0.0)
    return G_SOURCE_CONTINUE;

  /* Triangle wave at 900 px/s: constant speed, never stops at the ends for
   * more than a frame. */
  t = gdk_frame_clock_get_frame_time (clock) / 1e6;
  period = 2.0 * range / 900.0;
  phase = fmod (t, period) / period;
  v = phase < 0.5 ? phase * 2.0 : 2.0 - phase * 2.0;
  gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj) + v * range);

  return G_SOURCE_CONTINUE;
}

static void
on_after_paint (GdkFrameClock *clock, gpointer data)
{
  ((S1App *) data)->frames++;
}

static double
per (gint64 us, guint n)
{
  return n ? us / 1000.0 / n : 0.0;
}

static gboolean
report (gpointer data)
{
  S1App *s = data;
  S1Stats st;
  GdkFrameClock *clock = gtk_widget_get_frame_clock (s->window);
  S1Options *opts = s1_glass_view_get_options (s->view);

  if (!s->printed_header)
    {
      GtkNative *native = gtk_widget_get_native (s->window);
      const char *gl = s1_glass_view_get_gl_info (s->view);

      g_print ("[s1] GSK renderer: %s | GL: %s | scale %.2f\n",
               G_OBJECT_TYPE_NAME (gtk_native_get_renderer (native)),
               gl ? gl : "(not created)",
               gdk_surface_get_scale (gtk_native_get_surface (native)));
      s->printed_header = TRUE;
    }

  s1_glass_view_take_stats (s->view, &st);
  g_print ("[s1] %5.1f fps (%3u paints) | glass %s cache %s | snapshots %3u, passes %3u, "
           "captures %3u, capture hits %3u, full hits %3u, pool-full %u | "
           "glass CPU/snapshot avg %.2f ms max %.2f ms | per capture: render_texture %.2f, "
           "download %.2f, upload %.2f ms | GL/pass %.2f ms\n",
           clock ? gdk_frame_clock_get_fps (clock) : 0.0, s->frames,
           opts->glass_enabled ? "on" : "off", opts->use_cache ? "on" : "off",
           st.snapshots, st.glass_passes, st.captures, st.capture_hits, st.full_hits, st.pool_exhausted,
           per (st.us_total, st.snapshots), st.us_total_max / 1000.0,
           per (st.us_render_texture, st.captures), per (st.us_download, st.captures),
           per (st.us_upload, st.captures), per (st.us_gl, st.glass_passes));
  s->frames = 0;

  return G_SOURCE_CONTINUE;
}

static gboolean
take_screenshot (gpointer data)
{
  S1App *s = data;
  GtkWidget *child = gtk_window_get_child (GTK_WINDOW (s->window));
  GdkPaintable *paintable = gtk_widget_paintable_new (child);
  int w = gtk_widget_get_width (child);
  int h = gtk_widget_get_height (child);
  GtkSnapshot *snapshot = gtk_snapshot_new ();
  GskRenderNode *node;
  GdkTexture *texture;
  graphene_rect_t area = GRAPHENE_RECT_INIT (0, 0, w, h);

  /* --screenshot-panel: the same part of the panel whatever size the window
   * manager gave the window. */
  if (opt_screenshot_panel && gtk_widget_compute_bounds (s->top_panel, child, &area))
    {
      graphene_rect_inset (&area, -14, -14);
      area.origin.x = floorf (area.origin.x);
      area.origin.y = floorf (MAX (area.origin.y, 0.0f));
      area.size.width = ceilf (area.size.width);
      area.size.height = ceilf (area.size.height);
    }

  gdk_paintable_snapshot (paintable, snapshot, w, h);
  node = gtk_snapshot_free_to_node (snapshot);
  if (node)
    {
      texture = gsk_renderer_render_texture (gtk_native_get_renderer (gtk_widget_get_native (s->window)),
                                             node, &area);
      w = (int) area.size.width;
      h = (int) area.size.height;
      if (gdk_texture_save_to_png (texture, opt_screenshot))
        g_print ("[s1] screenshot: %s (%dx%d)\n", opt_screenshot, w, h);
      else
        g_printerr ("[s1] screenshot: could not write %s\n", opt_screenshot);
      g_object_unref (texture);
      gsk_render_node_unref (node);
    }
  else
    g_printerr ("[s1] screenshot: the window produced no render node\n");
  g_object_unref (paintable);

  return G_SOURCE_REMOVE;
}

/* Changes only a panel's foreground: the view is snapshotted again, but the
 * content and the panel geometry are not, so the finished glass should be
 * reused as-is ("full hits"). */
static gboolean
pulse (gpointer data)
{
  GtkWidget *widget = data;

  gtk_widget_set_opacity (widget, gtk_widget_get_opacity (widget) > 0.75 ? 0.5 : 1.0);
  return G_SOURCE_CONTINUE;
}

/* ── --sweep: every candidate at every offset, in the same window ─────────
 * The window manager does not always honour the default size, and the
 * content reflows with the width, so candidates are only comparable when
 * they are rendered by the same process in the same window. */

typedef struct {
  S1App  *s;
  char  **offsets;
  guint   candidate;
  guint   offset;
  guint   phase;       /* 0 = apply, 1 = capture */
} Sweep;

/* Besides the optical parameters, a candidate may set three renderer
 * options: ss=0|1 (4x supersampling), rim=N (--rim-samples), fp=0|1
 * (measured lens footprint), blur=PX. */
static void
apply_candidate (S1App *s, const char *spec, char **name_out)
{
  g_auto (GStrv) name_params = g_strsplit (spec, ":", 2);
  S1Options *opts = s1_glass_view_get_options (s->view);

  s1_glass_view_reset_params (s->view);
  opts->supersample = !opt_no_supersample;
  opts->rim_samples = opt_rim_samples;
  opts->measured_footprint = !opt_estimated_footprint;
  opts->footprint_px = 0.0;
  opts->blur_radius = opt_blur_radius;
  *name_out = g_strdup (name_params[0]);

  if (name_params[1] != NULL)
    {
      g_auto (GStrv) pairs = g_strsplit (name_params[1], ",", -1);

      for (int i = 0; pairs[i]; i++)
        {
          g_auto (GStrv) kv = g_strsplit (pairs[i], "=", 2);
          double value;

          if (!kv[0] || !kv[1])
            {
              g_printerr ("[s1] bad entry in candidate %s: %s\n", spec, pairs[i]);
              continue;
            }
          value = g_ascii_strtod (kv[1], NULL);
          if (g_str_equal (kv[0], "ss"))
            opts->supersample = value != 0.0;
          else if (g_str_equal (kv[0], "rim"))
            opts->rim_samples = (int) value;
          else if (g_str_equal (kv[0], "fp"))
            opts->measured_footprint = value != 0.0;
          else if (g_str_equal (kv[0], "fph"))
            opts->footprint_px = value;
          else if (g_str_equal (kv[0], "blur"))
            opts->blur_radius = value;
          else if (!s1_glass_view_set_param (s->view, kv[0], value))
            g_printerr ("[s1] unknown parameter in candidate %s: %s\n", spec, pairs[i]);
        }
    }
  s1_glass_view_options_changed (s->view);
}

static gboolean
sweep_step (gpointer data)
{
  Sweep *sw = data;
  S1App *s = sw->s;
  g_autofree char *name = NULL;

  if (opt_candidates == NULL || opt_candidates[sw->candidate] == NULL)
    {
      sw->candidate = 0;
      sw->offset++;
    }
  if (sw->offsets[sw->offset] == NULL)
    {
      g_print ("[s1] sweep done: %s\n", opt_sweep_dir);
      g_application_quit (G_APPLICATION (s->app));
      return G_SOURCE_REMOVE;
    }

  if (sw->phase == 0)
    {
      /* Apply now, capture on the next tick: a widget paintable is empty
       * while a redraw is pending. */
      apply_candidate (s, opt_candidates[sw->candidate], &name);
      gtk_adjustment_set_value (gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (s->scrolled)),
                                g_ascii_strtod (sw->offsets[sw->offset], NULL));
      sw->phase = 1;
      return G_SOURCE_CONTINUE;
    }

  {
    g_auto (GStrv) name_params = g_strsplit (opt_candidates[sw->candidate], ":", 2);
    name = g_strdup (name_params[0]);
  }

  g_free (opt_screenshot);
  opt_screenshot = g_strdup_printf ("%s/%s-%s.png", opt_sweep_dir, name, sw->offsets[sw->offset]);
  take_screenshot (s);
  sw->phase = 0;
  sw->candidate++;
  return G_SOURCE_CONTINUE;
}

static gboolean
start_sweep (gpointer data)
{
  Sweep *sw = g_new0 (Sweep, 1);

  sw->s = data;
  sw->offsets = g_strsplit (opt_sweep_scroll ? opt_sweep_scroll : "60,420,740", ",", -1);
  opt_screenshot_panel = TRUE;
  g_mkdir_with_parents (opt_sweep_dir, 0755);
  g_timeout_add (250, sweep_step, sw);
  return G_SOURCE_REMOVE;
}

static gboolean
quit_now (gpointer data)
{
  g_application_quit (G_APPLICATION (((S1App *) data)->app));
  return G_SOURCE_REMOVE;
}

static gboolean
on_key (GtkEventControllerKey *controller, guint keyval, guint keycode,
        GdkModifierType state, gpointer data)
{
  S1App *s = data;
  S1Options *opts = s1_glass_view_get_options (s->view);

  switch (keyval)
    {
    case GDK_KEY_space:
      opts->glass_enabled = !opts->glass_enabled;
      break;
    case GDK_KEY_c:
      opts->use_cache = !opts->use_cache;
      break;
    case GDK_KEY_d:
      opts->debug_view = (opts->debug_view + 1) % 3;
      break;
    case GDK_KEY_s:
      opts->supersample = !opts->supersample;
      break;
    case GDK_KEY_f:
      opts->measured_footprint = !opts->measured_footprint;
      break;
    case GDK_KEY_b:
      opts->blur_radius = opts->blur_radius >= 5.0 ? 2.0 : floor (opts->blur_radius) + 1.0;
      break;
    case GDK_KEY_r:
      opts->use_reference = !opts->use_reference;
      break;
    case GDK_KEY_e:
      s->preset = s1_glass_view_next_preset (s->preset);
      s1_glass_view_apply_preset (s->view, s->preset);
      break;
    case GDK_KEY_h:
      if (opts->shadow_intensity > 0.4)
        {
          opts->shadow_radius = s->app_shadow_radius;
          opts->shadow_intensity = s->app_shadow_intensity;
        }
      else
        {
          opts->shadow_radius = 30.0;
          opts->shadow_intensity = 0.55;
        }
      break;
    case GDK_KEY_a:
      s->autoscroll = !s->autoscroll;
      g_print ("[s1] autoscroll %s\n", s->autoscroll ? "on" : "off");
      return TRUE;
    default:
      return FALSE;
    }

  g_print ("[s1] glass %s, cache %s, debug view %d, supersample %s, lens footprint %s, blur %.0f px, shadow %.0f px / %.2f, shader %s, edge %s\n",
           opts->glass_enabled ? "on" : "off", opts->use_cache ? "on" : "off", opts->debug_view,
           opts->supersample ? "4x" : "off", opts->measured_footprint ? "measured" : "estimated (reference)",
           opts->blur_radius, opts->shadow_radius, opts->shadow_intensity,
           opts->use_reference ? "reference" : "core", s->preset);
  s1_glass_view_options_changed (s->view);
  return TRUE;
}

static void
on_activate (GtkApplication *app, gpointer data)
{
  S1App *s = data;
  GtkCssProvider *provider = gtk_css_provider_new ();
  GtkEventController *keys;
  GtkWidget *view, *scrolled, *top;
  S1Options *opts;

  gtk_css_provider_load_from_string (provider, css);
  gtk_style_context_add_provider_for_display (gdk_display_get_default (), GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);

  s->app = app;
  s->window = gtk_application_window_new (app);
  gtk_window_set_title (GTK_WINDOW (s->window), "glass-lib S1");
  gtk_window_set_default_size (GTK_WINDOW (s->window), 1100, 820);

  view = s1_glass_view_new ();
  s->view = S1_GLASS_VIEW (view);
  s1_glass_view_set_content (s->view, build_content (&scrolled));
  s->scrolled = scrolled;
  top = top_panel ();
  s->top_panel = top;
  s1_glass_view_add_panel (s->view, top);
  s1_glass_view_add_panel (s->view, bottom_panel ());
  if (opt_pulse)
    g_timeout_add (100, pulse, gtk_widget_get_last_child (top));

  opts = s1_glass_view_get_options (s->view);
  opts->glass_enabled = !opt_no_glass;
  opts->use_cache = !opt_no_cache;
  opts->private_renderer = opt_private_renderer;
  opts->blur_radius = opt_blur_radius;
  opts->downscale = opt_downscale;
  opts->capture_full = opt_capture_full;
  opts->supersample = !opt_no_supersample;
  opts->rim_samples = opt_rim_samples;
  opts->measured_footprint = !opt_estimated_footprint;
  opts->use_reference = opt_reference;
  s->preset = opt_preset ? opt_preset : "crisp";
  if (!s1_glass_view_apply_preset (s->view, s->preset))
    {
      g_printerr ("[s1] unknown --preset %s, using crisp\n", s->preset);
      s->preset = "crisp";
      s1_glass_view_apply_preset (s->view, s->preset);
    }
  for (int i = 0; opt_params && opt_params[i]; i++)
    {
      g_auto (GStrv) kv = g_strsplit (opt_params[i], "=", 2);

      if (!kv[0] || !kv[1] || !s1_glass_view_set_param (s->view, kv[0], g_ascii_strtod (kv[1], NULL)))
        g_printerr ("[s1] unknown --param %s\n", opt_params[i]);
    }
  if (opt_shadow_radius >= 0.0)
    opts->shadow_radius = opt_shadow_radius;
  if (opt_shadow_intensity >= 0.0)
    opts->shadow_intensity = opt_shadow_intensity;
  s->app_shadow_radius = opts->shadow_radius;
  s->app_shadow_intensity = opts->shadow_intensity;
  opts->debug_view = opt_debug_view;
  s1_glass_view_options_changed (s->view);

  gtk_window_set_child (GTK_WINDOW (s->window), view);

  keys = gtk_event_controller_key_new ();
  g_signal_connect (keys, "key-pressed", G_CALLBACK (on_key), s);
  gtk_widget_add_controller (s->window, keys);

  s->autoscroll = opt_autoscroll;
  gtk_widget_add_tick_callback (scrolled, autoscroll_tick, s, NULL);

  gtk_window_present (GTK_WINDOW (s->window));
  g_signal_connect (gtk_widget_get_frame_clock (s->window), "after-paint", G_CALLBACK (on_after_paint), s);

  g_timeout_add_seconds (1, report, s);
  if (opt_sweep_dir)
    g_timeout_add (1200, start_sweep, s);
  else if (opt_screenshot)
    g_timeout_add ((guint) (opt_screenshot_at * 1000), take_screenshot, s);
  if (opt_quit_after > 0)
    g_timeout_add ((guint) (opt_quit_after * 1000), quit_now, s);
}

int
main (int argc, char **argv)
{
  S1App s = { 0 };
  GtkApplication *app;
  int status;

  app = gtk_application_new ("io.github.ryohsuke1231.GlassLib.S1", G_APPLICATION_NON_UNIQUE);
  g_application_add_main_option_entries (G_APPLICATION (app), entries);
  g_signal_connect (app, "activate", G_CALLBACK (on_activate), &s);
  status = g_application_run (G_APPLICATION (app), argc, argv);
  g_object_unref (app);

  return status;
}
