/* glass-dialog.c — a dialog on a sheet of glass (design.md §6.7).
 *
 * An AdwDialog is drawn in its window by libadwaita's dialog host, over the
 * window's content and outside any GlassView. Its child here is a surface
 * that draws thick glass (GlassStandalone) from the window content's render
 * node, then the dialog's content. The host draws that content before the
 * dialog in the same frame, so the node is this frame's.
 * GTK would keep the surface's node while only the content changes (a
 * moving background), so the surface asks to be drawn again in every frame
 * the window paints, before the paint: the same frame. When nothing behind
 * it changed, its capture and its pass are reused.
 *
 * The sheet keeps libadwaita's outline and shadow; its background is not
 * seen (the glass covers it) and the dialog drops the `.background` class
 * that paints it.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

#define SHEET_RADIUS 15.0   /* libadwaita's floating-sheet > sheet */

/* ── The surface: glass, then the content ── */

#define GLASS_TYPE_DIALOG_SURFACE (glass_dialog_surface_get_type ())
G_DECLARE_FINAL_TYPE (GlassDialogSurface, glass_dialog_surface, GLASS, DIALOG_SURFACE, GtkWidget)

struct _GlassDialogSurface {
  GtkWidget        parent_instance;

  GtkWidget       *content;
  GtkWidget       *bg_node;          /* never drawn: var(--dialog-bg-color) */
  GlassStandalone *glass;
  GdkFrameClock   *clock;            /* while mapped */
  gulong           before_paint;
};

G_DEFINE_FINAL_TYPE (GlassDialogSurface, glass_dialog_surface, GTK_TYPE_WIDGET)

/* The window content behind the dialog: the dialog host's child that is not
 * a dialog. */
static GtkWidget *
content_behind (GtkWidget *dialog)
{
  GtkWidget *host = dialog;

  while (host && g_strcmp0 (gtk_widget_get_css_name (host), "dialog-host") != 0)
    host = gtk_widget_get_parent (host);
  if (host == NULL)
    return NULL;

  for (GtkWidget *child = gtk_widget_get_first_child (host); child; child = gtk_widget_get_next_sibling (child))
    if (!ADW_IS_DIALOG (child) && !gtk_widget_is_ancestor (dialog, child) && child != dialog &&
        gtk_widget_get_mapped (child))
      return child;
  return NULL;
}

static void
glass_dialog_surface_snapshot (GtkWidget   *widget,
                               GtkSnapshot *snapshot)
{
  GlassDialogSurface *self = GLASS_DIALOG_SURFACE (widget);
  GtkWidget *dialog = gtk_widget_get_ancestor (widget, ADW_TYPE_DIALOG);
  GtkWidget *behind = dialog ? content_behind (dialog) : NULL;
  graphene_rect_t box = GRAPHENE_RECT_INIT (0, 0, gtk_widget_get_width (widget), gtk_widget_get_height (widget));
  graphene_rect_t where;
  GskRenderNode *backdrop = NULL;
  double radii[4] = { SHEET_RADIUS, SHEET_RADIUS, SHEET_RADIUS, SHEET_RADIUS };
  GdkRGBA bg;

  if (behind && gtk_widget_compute_bounds (behind, widget, &where))
    backdrop = glass_standalone_backdrop (behind, &where.origin);

  /* libadwaita's sheet: rounded all round when floating; along the bottom
   * of the window, only the top corners are (dialog.bottom-sheet). */
  if (dialog && gtk_widget_has_css_class (dialog, "bottom-sheet"))
    radii[2] = radii[3] = 0.0;

  gtk_widget_get_color (self->bg_node, &bg);
  if (backdrop == NULL ||
      !glass_standalone_draw (self->glass, widget, snapshot, backdrop, &box, radii,
                              GLASS_MATERIAL_THICK, &bg, FALSE))
    {
      /* No glass: the sheet's usual opaque background. */
      GskRoundedRect sheet;

      gsk_rounded_rect_init (&sheet, &box,
                             &GRAPHENE_SIZE_INIT (radii[0], radii[0]), &GRAPHENE_SIZE_INIT (radii[1], radii[1]),
                             &GRAPHENE_SIZE_INIT (radii[2], radii[2]), &GRAPHENE_SIZE_INIT (radii[3], radii[3]));
      gtk_snapshot_push_rounded_clip (snapshot, &sheet);
      bg.alpha = 1.0f;
      gtk_snapshot_append_color (snapshot, &bg, &box);
      gtk_snapshot_pop (snapshot);
    }
  g_clear_pointer (&backdrop, gsk_render_node_unref);

  if (self->content)
    gtk_widget_snapshot_child (widget, self->content, snapshot);
}

static void
glass_dialog_surface_measure (GtkWidget *widget, GtkOrientation orientation, int for_size,
                              int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
  GtkWidget *content = GLASS_DIALOG_SURFACE (widget)->content;

  *minimum = *natural = 0;
  if (content)
    gtk_widget_measure (content, orientation, for_size, minimum, natural, NULL, NULL);
}

static void
glass_dialog_surface_size_allocate (GtkWidget *widget, int width, int height, int baseline)
{
  GlassDialogSurface *self = GLASS_DIALOG_SURFACE (widget);

  if (self->content)
    gtk_widget_allocate (self->content, width, height, baseline, NULL);
  gtk_widget_allocate (self->bg_node, 0, 0, -1, NULL);
}

static void
before_paint (GdkFrameClock *clock,
              GtkWidget     *widget)
{
  gtk_widget_queue_draw (widget);
}

static void
glass_dialog_surface_map (GtkWidget *widget)
{
  GlassDialogSurface *self = GLASS_DIALOG_SURFACE (widget);

  GTK_WIDGET_CLASS (glass_dialog_surface_parent_class)->map (widget);
  self->clock = gtk_widget_get_frame_clock (widget);
  if (self->clock)
    {
      g_object_ref (self->clock);
      self->before_paint = g_signal_connect (self->clock, "before-paint", G_CALLBACK (before_paint), widget);
    }
}

static void
glass_dialog_surface_unmap (GtkWidget *widget)
{
  GlassDialogSurface *self = GLASS_DIALOG_SURFACE (widget);

  if (self->clock)
    g_clear_signal_handler (&self->before_paint, self->clock);
  g_clear_object (&self->clock);
  GTK_WIDGET_CLASS (glass_dialog_surface_parent_class)->unmap (widget);
}

static void
glass_dialog_surface_unrealize (GtkWidget *widget)
{
  glass_standalone_release (GLASS_DIALOG_SURFACE (widget)->glass);
  GTK_WIDGET_CLASS (glass_dialog_surface_parent_class)->unrealize (widget);
}

static void
glass_dialog_surface_dispose (GObject *object)
{
  GlassDialogSurface *self = GLASS_DIALOG_SURFACE (object);

  g_clear_pointer (&self->content, gtk_widget_unparent);
  g_clear_pointer (&self->bg_node, gtk_widget_unparent);
  g_clear_pointer (&self->glass, glass_standalone_free);

  G_OBJECT_CLASS (glass_dialog_surface_parent_class)->dispose (object);
}

static void
glass_dialog_surface_class_init (GlassDialogSurfaceClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  G_OBJECT_CLASS (klass)->dispose = glass_dialog_surface_dispose;
  widget_class->snapshot = glass_dialog_surface_snapshot;
  widget_class->measure = glass_dialog_surface_measure;
  widget_class->size_allocate = glass_dialog_surface_size_allocate;
  widget_class->map = glass_dialog_surface_map;
  widget_class->unmap = glass_dialog_surface_unmap;
  widget_class->unrealize = glass_dialog_surface_unrealize;
  gtk_widget_class_set_css_name (widget_class, "glassdialogsurface");
}

static void
glass_dialog_surface_init (GlassDialogSurface *self)
{
  self->glass = glass_standalone_new ();
  self->bg_node = glass_style_node_new ("backdrop");
  gtk_widget_set_parent (self->bg_node, GTK_WIDGET (self));
}

/* ── The dialog ── */

/**
 * GlassDialog:
 *
 * An `AdwDialog` on a sheet of thick glass over the window, instead of an
 * opaque one. Set what it shows with [method@Dialog.set_content] (not
 * `adw_dialog_set_child()`, which would replace the glass).
 *
 * ## CSS nodes
 *
 * `GlassDialog` is a `dialog` with the style class `.glass`; the glass is a
 * `glassdialogsurface`, tinted with the `color` of its never-drawn child
 * `backdrop` (`var(--dialog-bg-color)`).
 */

struct _GlassDialog {
  AdwDialog  parent_instance;

  GtkWidget *surface;
};

enum {
  PROP_0,
  PROP_CONTENT,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

G_DEFINE_FINAL_TYPE (GlassDialog, glass_dialog, ADW_TYPE_DIALOG)

static void
glass_dialog_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_CONTENT:
      g_value_set_object (value, glass_dialog_get_content (GLASS_DIALOG (object)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_dialog_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_CONTENT:
      glass_dialog_set_content (GLASS_DIALOG (object), g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_dialog_root (GtkWidget *widget)
{
  GTK_WIDGET_CLASS (glass_dialog_parent_class)->root (widget);
  glass_style_ensure (gtk_widget_get_display (widget));
}

static void
glass_dialog_class_init (GlassDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = glass_dialog_get_property;
  object_class->set_property = glass_dialog_set_property;
  GTK_WIDGET_CLASS (klass)->root = glass_dialog_root;

  /**
   * GlassDialog:content:
   *
   * What the dialog shows, on the glass.
   */
  props[PROP_CONTENT] =
    g_param_spec_object ("content", NULL, NULL, GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
glass_dialog_init (GlassDialog *self)
{
  self->surface = g_object_new (GLASS_TYPE_DIALOG_SURFACE, NULL);
  adw_dialog_set_child (ADW_DIALOG (self), self->surface);
  gtk_widget_remove_css_class (GTK_WIDGET (self), "background");
  gtk_widget_add_css_class (GTK_WIDGET (self), "glass");
}

/**
 * glass_dialog_new:
 *
 * Creates a new dialog of glass.
 *
 * Returns: a new dialog of glass
 */
GtkWidget *
glass_dialog_new (void)
{
  return g_object_new (GLASS_TYPE_DIALOG, NULL);
}

/**
 * glass_dialog_get_content:
 * @self: a dialog
 *
 * Gets the content.
 *
 * Returns: (transfer none) (nullable): the content
 */
GtkWidget *
glass_dialog_get_content (GlassDialog *self)
{
  g_return_val_if_fail (GLASS_IS_DIALOG (self), NULL);

  return GLASS_DIALOG_SURFACE (self->surface)->content;
}

/**
 * glass_dialog_set_content:
 * @self: a dialog
 * @content: (nullable): what the dialog shows
 *
 * Sets what the dialog shows, on the glass.
 */
void
glass_dialog_set_content (GlassDialog *self,
                          GtkWidget   *content)
{
  GlassDialogSurface *surface;

  g_return_if_fail (GLASS_IS_DIALOG (self));

  surface = GLASS_DIALOG_SURFACE (self->surface);
  if (surface->content == content)
    return;
  g_clear_pointer (&surface->content, gtk_widget_unparent);
  if (content)
    {
      surface->content = content;
      gtk_widget_insert_before (content, self->surface, surface->bg_node);
    }
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CONTENT]);
}
