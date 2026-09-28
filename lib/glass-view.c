/* glass-view.c — a container that draws glass over its own content
 * (design.md §5, §7).
 *
 * The glass bodies are drawn here, never by the panels: GTK caches each
 * widget's render node, so a panel's own snapshot does not run when only the
 * content under it scrolled. The view is snapshotted whenever any
 * descendant changed, so glass and content can never be a frame apart.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"
#include "render/glass-renderer.h"

#include <math.h>
#include <string.h>

/**
 * GlassView:
 *
 * A container that draws glass over its own content.
 *
 * The content fills the view; overlay children are placed over it like in
 * `GtkOverlay`, by their own `halign`, `valign` and margins. Every
 * [class@Panel] among the overlay children (at any depth) is drawn as glass
 * that refracts the content under it, in the same frame as the content.
 *
 * The view paints its backdrop colour (by default the window background)
 * under the content, so the glass never sees through to nothing. Containers
 * between the view and a panel should have no background of their own.
 *
 * ## CSS nodes
 *
 * `GlassView` has a single CSS node with name `glassview`.
 */

typedef struct {
  GlassPanel       *panel;
  GlassPanelRender *render;
} PanelEntry;

typedef struct {
  GlassEdgeStyle style;
  int            size;
} Edge;

struct _GlassView {
  GtkWidget       parent_instance;

  GtkWidget      *content;
  GPtrArray      *overlays;          /* GtkWidget*, parented to us */
  GHashTable     *offsets;           /* overlay -> graphene_point_t* */
  GtkWidget      *backdrop_node;     /* never drawn: resolves --window-bg-color */
  GdkRGBA         backdrop_color;
  gboolean        backdrop_color_set;
  gboolean        backdrop_capture_only;
  GPtrArray      *entries;           /* PanelEntry* */
  GPtrArray      *captures;          /* GlassCapture*, one per group of panels (GTK draws them) */
  GPtrArray      *composed;          /* GlassCapture*, composed from our own textures */
  GlassPanel     *under_panel;       /* the panel we are in, while rooted */

  GlassRenderer  *renderer;
  gboolean        renderer_failed;
  gboolean        full;              /* panels are drawn by us (not the CSS fallback) */
  Edge            edges[2];          /* top, bottom */

  gint64          hud_last_us;
  char           *hud_text;
  guint           retry_tick;        /* drawing again: a panel's glass was stale */
  gboolean        drawn_since_map;   /* panels mapped before this came with the view */

  /* The adaptive colours under the CSS fallback (design.md §12.1): the
   * backdrop and the panels of the last snapshot, sampled small, at most
   * SAMPLE_INTERVAL_US apart and only when something changed. */
  GskRenderNode  *sample_node;
  GArray         *sample_panels;     /* Sample */
  GskRenderNode  *sample_leaf;       /* what sample_node shows (the key) */
  guint64         sample_hash;       /* ... where, and where the panels are */
  GskRenderNode  *sampled_leaf;      /* the same for the last sample taken */
  guint64         sampled_hash;
  guint           sample_source;
  gint64          sample_last_us;
};

enum {
  PROP_0,
  PROP_CONTENT,
  PROP_BACKDROP_COLOR,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

static void glass_view_buildable_init (GtkBuildableIface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (GlassView, glass_view, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, glass_view_buildable_init))

static GtkBuildableIface *parent_buildable_iface;

/* ── Panels ───────────────────────────────────────────────────────────────── */

static void
entry_free (PanelEntry *entry, GlassRenderer *renderer)
{
  glass_panel_render_free (entry->render, renderer);
  g_free (entry);
}

static gboolean
wants_full (GlassView *self)
{
  GlassContext *context = glass_context_get_default ();

  return glass_context_get_wanted_renderer (context) == GLASS_RENDERER_MODE_FULL &&
         !glass_context_get_high_contrast (context) &&
         !self->renderer_failed;
}

/* Like GtkGLArea: the context is made once the surface exists, and only
 * when the full renderer is wanted. A view realized under the CSS fallback
 * gets it when the setting comes back to full; without that, its panels
 * were left to the view with nothing to draw them. */
static void
ensure_renderer (GlassView *self)
{
  GtkWidget *widget = GTK_WIDGET (self);

  if (self->renderer || self->renderer_failed || !gtk_widget_get_realized (widget) ||
      glass_context_get_wanted_renderer (glass_context_get_default ()) != GLASS_RENDERER_MODE_FULL)
    return;
  self->renderer = glass_renderer_acquire (gtk_widget_get_native (widget));
  self->renderer_failed = self->renderer == NULL;
}

static void
update_panel_modes (GlassView *self)
{
  GlassPanelMode mode;

  ensure_renderer (self);
  self->full = wants_full (self);
  mode = self->full ? GLASS_PANEL_MODE_VIEW : GLASS_PANEL_MODE_FALLBACK;
  for (guint i = 0; i < self->entries->len; i++)
    {
      PanelEntry *entry = g_ptr_array_index (self->entries, i);

      glass_panel_set_mode (entry->panel, mode);
    }
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
glass_view_register_panel (GlassView  *self,
                           GlassPanel *panel)
{
  PanelEntry *entry;

  for (guint i = 0; i < self->entries->len; i++)
    if (((PanelEntry *) g_ptr_array_index (self->entries, i))->panel == panel)
      return;

  entry = g_new0 (PanelEntry, 1);
  entry->panel = panel;
  entry->render = glass_panel_render_new ();
  g_ptr_array_add (self->entries, entry);

  self->full = wants_full (self);
  glass_panel_set_mode (panel, self->full ? GLASS_PANEL_MODE_VIEW : GLASS_PANEL_MODE_FALLBACK);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

GlassPanel *
glass_view_get_panel (GlassView *self,
                      guint      index)
{
  if (index >= self->entries->len)
    return NULL;
  return ((PanelEntry *) g_ptr_array_index (self->entries, index))->panel;
}

void
glass_view_unregister_panel (GlassView  *self,
                             GlassPanel *panel)
{
  for (guint i = 0; i < self->entries->len; i++)
    {
      PanelEntry *entry = g_ptr_array_index (self->entries, i);

      if (entry->panel == panel)
        {
          glass_panel_set_output (panel, NULL, NULL);
          g_ptr_array_remove_index (self->entries, i);
          entry_free (entry, self->renderer);
          gtk_widget_queue_draw (GTK_WIDGET (self));
          return;
        }
    }
}

/* TRUE if widget is (inside) one of our overlay children, as opposed to the
 * content: only those can be glass over the content (design.md §7.2). */
gboolean
glass_view_is_overlay_descendant (GlassView *self,
                                  GtkWidget *widget)
{
  for (guint i = 0; i < self->overlays->len; i++)
    {
      GtkWidget *overlay = g_ptr_array_index (self->overlays, i);

      if (widget == overlay || gtk_widget_is_ancestor (widget, overlay))
        return TRUE;
    }
  return FALSE;
}

static void
context_changed (GlassContext *context,
                 GlassView    *self)
{
  update_panel_modes (self);
}

/* ── Renderer lifetime ────────────────────────────────────────────────────── */

static void
release_renderer (GlassView *self)
{
  if (self->renderer == NULL)
    return;

  glass_renderer_make_current (self->renderer);
  for (guint i = 0; i < self->entries->len; i++)
    glass_panel_render_release_gl (((PanelEntry *) g_ptr_array_index (self->entries, i))->render);
  for (guint i = 0; i < self->captures->len; i++)
    glass_capture_release_gl (g_ptr_array_index (self->captures, i));
  for (guint i = 0; i < self->composed->len; i++)
    glass_capture_release_gl (g_ptr_array_index (self->composed, i));
  gdk_gl_context_clear_current ();
  g_clear_pointer (&self->renderer, glass_renderer_release);
}

static void
glass_view_map (GtkWidget *widget)
{
  GLASS_VIEW (widget)->drawn_since_map = FALSE;
  GTK_WIDGET_CLASS (glass_view_parent_class)->map (widget);
}

gboolean
glass_view_has_drawn_since_map (GlassView *self)
{
  return self->drawn_since_map;
}

static void
glass_view_realize (GtkWidget *widget)
{
  GlassView *self = GLASS_VIEW (widget);

  GTK_WIDGET_CLASS (glass_view_parent_class)->realize (widget);
  update_panel_modes (self);
}

static void
glass_view_unrealize (GtkWidget *widget)
{
  GlassView *self = GLASS_VIEW (widget);

  if (self->retry_tick)
    gtk_widget_remove_tick_callback (widget, self->retry_tick);
  self->retry_tick = 0;
  release_renderer (self);
  self->renderer_failed = FALSE;
  GTK_WIDGET_CLASS (glass_view_parent_class)->unrealize (widget);
}

/* ── Backdrop colour and edge effects ─────────────────────────────────────── */

static gboolean
inside_panel (GlassView *self)
{
  return gtk_widget_get_ancestor (GTK_WIDGET (self), GLASS_TYPE_PANEL) != NULL;
}

static void
resolve_backdrop_color (GlassView *self, GdkRGBA *out)
{
  if (self->backdrop_color_set)
    *out = self->backdrop_color;
  else if (inside_panel (self))
    *out = (GdkRGBA) { 0, 0, 0, 0 };   /* do not hide the glass we are on */
  else
    gtk_widget_get_color (self->backdrop_node, out);
}

void
glass_view_set_edge (GlassView       *self,
                     GtkPositionType  position,
                     GlassEdgeStyle   style,
                     int              size)
{
  Edge *edge;

  g_return_if_fail (position == GTK_POS_TOP || position == GTK_POS_BOTTOM);

  edge = &self->edges[position == GTK_POS_TOP ? 0 : 1];
  if (edge->style == style && edge->size == size)
    return;
  edge->style = style;
  edge->size = size;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

/* The scroll edge effect (design.md §6.6): the content under the bar,
 * blurred and washed towards the window background. SOFT fades out below
 * the bar; HARD stops at the bar with a hairline. */
static GskRenderNode *
edge_node (GlassView     *self,
           GskRenderNode *content,
           const Edge    *edge,
           gboolean       top,
           float          width,
           float          height,
           const GdkRGBA *bg)
{
  const float blur = edge->style == GLASS_EDGE_STYLE_HARD ? 16.0f : 10.0f;
  float band = edge->style == GLASS_EDGE_STYLE_HARD ? edge->size : edge->size + MIN (edge->size, 32);
  graphene_rect_t rect, source;
  GtkSnapshot *snapshot;
  GdkRGBA wash = *bg;

  band = MIN (band, height);
  if (band <= 0.0f || edge->style == GLASS_EDGE_STYLE_NONE)
    return NULL;

  rect = GRAPHENE_RECT_INIT (0, top ? 0 : height - band, width, band);
  source = rect;
  graphene_rect_inset (&source, 0, -3.0f * blur);

  snapshot = gtk_snapshot_new ();
  gtk_snapshot_push_clip (snapshot, &rect);

  if (edge->style == GLASS_EDGE_STYLE_SOFT)
    {
      /* Opaque down to half the bar, gone at the end of the band. */
      GskColorStop stops[3] = {
        { 0.0f, { 0, 0, 0, 1.0f } },
        { edge->size * 0.5f / band, { 0, 0, 0, 0.92f } },
        { 1.0f, { 0, 0, 0, 0.0f } },
      };
      graphene_point_t from = GRAPHENE_POINT_INIT (0, top ? rect.origin.y : rect.origin.y + band);
      graphene_point_t to = GRAPHENE_POINT_INIT (0, top ? rect.origin.y + band : rect.origin.y);

      gtk_snapshot_push_mask (snapshot, GSK_MASK_MODE_ALPHA);
      gtk_snapshot_append_linear_gradient (snapshot, &rect, &from, &to, stops, G_N_ELEMENTS (stops));
      gtk_snapshot_pop (snapshot);
      wash.alpha *= 0.55f;
    }
  else
    wash.alpha *= 0.72f;

  gtk_snapshot_append_color (snapshot, bg, &rect);
  gtk_snapshot_push_blur (snapshot, blur);
  gtk_snapshot_push_clip (snapshot, &source);
  gtk_snapshot_append_node (snapshot, content);
  gtk_snapshot_pop (snapshot);
  gtk_snapshot_pop (snapshot);
  gtk_snapshot_append_color (snapshot, &wash, &rect);

  if (edge->style == GLASS_EDGE_STYLE_SOFT)
    gtk_snapshot_pop (snapshot);   /* the mask */
  else
    {
      GdkRGBA line;

      gtk_widget_get_color (GTK_WIDGET (self), &line);
      line.alpha *= 0.15f;
      gtk_snapshot_append_color (snapshot, &line,
                                 &GRAPHENE_RECT_INIT (0, top ? band - 1.0f : rect.origin.y, width, 1.0f));
    }

  gtk_snapshot_pop (snapshot);     /* the clip */
  return gtk_snapshot_free_to_node (snapshot);
}

/* ── Snapshot ─────────────────────────────────────────────────────────────── */

static float
opacity_to (GtkWidget *widget, GtkWidget *ancestor)
{
  float opacity = 1.0f;

  for (; widget && widget != ancestor; widget = gtk_widget_get_parent (widget))
    opacity *= (float) gtk_widget_get_opacity (widget);
  return opacity;
}

typedef struct {
  PanelEntry      *entry;
  graphene_rect_t  P;
  graphene_rect_t  C;           /* what its own glass needs */
  graphene_rect_t  C_tree;      /* ... and every panel nested in it (layer 0 only) */
  double           params[GLASS_N_PARAMS];
  float            tint[4];
  GlassBlurSpec    blur;        /* the capture's (the material's surface) */
  GlassSurfaceTerms surface;    /* the pass's */
  float            opacity;
  guint            layer;       /* GlassPanel ancestors below this view */
  guint            root;        /* index of its layer-0 ancestor item */
  guint            group;       /* layer 0: its capture group */
  GlassCapture    *capture;
  GlassLayerSource output;
  gboolean         has_output;

  /* A GlassGroup's panels: one body of glass (fused shapes); or a morph
   * with its partner's drop still behind it (two shapes of one panel). */
  GtkWidget       *fuse_group;
  guint            n_shapes;
  graphene_rect_t  shapes[GLASS_MAX_SHAPES];
  float            radii[GLASS_MAX_SHAPES];
  PanelEntry      *members[GLASS_MAX_SHAPES];
  float            merge_k;     /* logical px */

  /* Materializing or dissolving (design.md §6.8): how much glass there is. */
  double           visibility;

  /* Morphing or a ghost (design.md §6.8): its corners this frame. */
  gboolean         shaped;
  double           corners[4];
} Item;

typedef struct {
  graphene_rect_t C;
  GlassBlurSpec   blur;
  int             downscale;
} Group;

static float
area (const graphene_rect_t *r)
{
  return r->size.width * r->size.height;
}

/* Panels next to each other share one capture (design.md §8.2): a capture's
 * cost is mostly fixed (render_texture), so a row of header capsules costs
 * one instead of four. Two captures merge when they blur alike and their
 * union is not much bigger than the two together; the top and the bottom
 * bar never do. */
#define GROUP_SLACK 1.35f

static guint
group_for (GArray *groups, const Item *item)
{
  int downscale = (int) item->params[GLASS_PARAM_ID_BLUR_DOWNSCALE];

  for (guint g = 0; g < groups->len; g++)
    {
      Group *group = &g_array_index (groups, Group, g);
      graphene_rect_t u;

      if (!glass_blur_spec_equal (&group->blur, &item->blur) || group->downscale != downscale)
        continue;
      graphene_rect_union (&group->C, &item->C_tree, &u);
      if (area (&u) <= GROUP_SLACK * (area (&group->C) + area (&item->C_tree)))
        {
          group->C = u;
          return g;
        }
    }

  g_array_append_val (groups, ((Group) { item->C_tree, item->blur, downscale }));
  return groups->len - 1;
}

static guint
panel_layer (GlassView *self, GtkWidget *panel)
{
  guint layer = 0;

  for (GtkWidget *w = gtk_widget_get_parent (panel); w && w != GTK_WIDGET (self); w = gtk_widget_get_parent (w))
    if (GLASS_IS_PANEL (w))
      layer++;
  return layer;
}

static gboolean
item_contains (const Item *item, GtkWidget *widget)
{
  if (gtk_widget_is_ancestor (widget, GTK_WIDGET (item->entry->panel)))
    return TRUE;
  for (guint m = 1; m < item->n_shapes; m++)
    if (gtk_widget_is_ancestor (widget, GTK_WIDGET (item->members[m]->panel)))
      return TRUE;
  return FALSE;
}

/* The GlassGroup a panel belongs to in this view, if any. */
static GtkWidget *
fuse_group_of (GlassView *self, GtkWidget *panel)
{
  GtkWidget *group = gtk_widget_get_ancestor (panel, GLASS_TYPE_GROUP);

  return group && gtk_widget_is_ancestor (group, GTK_WIDGET (self)) ? group : NULL;
}

static GlassCapture *
composed_capture (GlassView *self, guint index)
{
  while (self->composed->len <= index)
    g_ptr_array_add (self->composed, glass_capture_new ());
  return g_ptr_array_index (self->composed, index);
}

/* The glass of the panel this view sits in, as a layer in this view's
 * coordinates: the view's own backdrop is transparent there, and its panels
 * are glass over that glass (design.md §6.7). */
static gboolean
ancestor_glass (GlassView *self, GlassLayerSource *out)
{
  GtkWidget *panel = gtk_widget_get_ancestor (GTK_WIDGET (self), GLASS_TYPE_PANEL);
  GlassView *owner;
  graphene_rect_t bounds;

  if (panel == NULL || !glass_panel_get_output (GLASS_PANEL (panel), &owner, out) || owner == NULL)
    return FALSE;
  if (!gtk_widget_compute_bounds (GTK_WIDGET (self), GTK_WIDGET (owner), &bounds))
    return FALSE;
  out->rect.origin.x -= bounds.origin.x;
  out->rect.origin.y -= bounds.origin.y;
  return TRUE;
}

static gboolean
retry_tick (GtkWidget     *widget,
            GdkFrameClock *clock,
            gpointer       data)
{
  GLASS_VIEW (widget)->retry_tick = 0;
  gtk_widget_queue_draw (widget);
  return G_SOURCE_REMOVE;
}

/* A panel got last frame's glass (no free output texture): draw again on
 * the next frame, and every frame after, until they catch up. */
static void
retry_next_frame (GlassView *self)
{
  if (self->retry_tick == 0)
    self->retry_tick = gtk_widget_add_tick_callback (GTK_WIDGET (self), retry_tick, NULL, NULL);
}

static void
draw_item (GlassView             *self,
           GtkSnapshot           *snapshot,
           Item                  *item,
           const graphene_rect_t *view_rect,
           double                 scale)
{
  GlassRenderRequest req = {
    .view_rect = *view_rect,
    .panel = item->P,
    .scale = scale,
    .corner_radius = glass_panel_get_corner_radius (item->entry->panel),
    .has_corner_radii = item->n_shapes == 0 && glass_panel_has_corner_radii (item->entry->panel),
    .params = item->params,
    .has_shadow = glass_panel_get_has_shadow (item->entry->panel),
    .n_shapes = item->n_shapes,
    .merge_k = item->merge_k,
  };
  GlassRenderResult res;

  if (item->shaped && item->n_shapes == 0)
    {
      req.has_corner_radii = TRUE;
      memcpy (req.corner_radii, item->corners, sizeof req.corner_radii);
    }
  else if (req.has_corner_radii)
    glass_panel_get_corner_radii (item->entry->panel, &req.corner_radii[0], &req.corner_radii[1],
                                  &req.corner_radii[2], &req.corner_radii[3]);

  /* Where morphs to and from these panels start (design.md §6.8). */
  for (guint m = 0; m < MAX (item->n_shapes, 1); m++)
    {
      if (item->n_shapes)
        glass_panel_set_drawn (item->members[m]->panel, &item->shapes[m], item->radii[m]);
      else
        glass_panel_set_drawn (item->entry->panel, &item->P,
                               item->shaped ? MAX (MAX (item->corners[0], item->corners[1]),
                                                   MAX (item->corners[2], item->corners[3]))
                                            : glass_panel_effective_radius (item->entry->panel, &item->P));
    }

  memcpy (req.tint, item->tint, sizeof req.tint);
  req.surface = item->surface;
  memcpy (req.shapes, item->shapes, sizeof req.shapes);
  memcpy (req.radii, item->radii, sizeof req.radii);
  if (!glass_renderer_render_panel (self->renderer, item->entry->render, item->capture, &req, &res))
    {
      glass_panel_set_output (item->entry->panel, NULL, NULL);
      return;
    }
  if (res.stale)
    retry_next_frame (self);

  if (item->opacity < 1.0f)
    gtk_snapshot_push_opacity (snapshot, item->opacity);
  gtk_snapshot_append_texture (snapshot, res.texture, &res.rect);
  if (item->opacity < 1.0f)
    gtk_snapshot_pop (snapshot);

  item->has_output = glass_panel_render_get_output (item->entry->render, &item->output);

  for (guint m = 0; m < MAX (item->n_shapes, 1); m++)
    {
      GlassPanel *panel = item->n_shapes ? item->members[m]->panel : item->entry->panel;
      const graphene_rect_t *own = item->n_shapes ? &item->shapes[m] : &item->P;

      glass_panel_set_output (panel, self, item->has_output ? &item->output : NULL);

      /* Only stored here; the colours change before the next frame. */
      if (glass_capture_is_fresh (item->capture) && glass_panel_uses_adaptive (panel))
        {
          GlassLumaStats luma;

          if (glass_capture_measure (item->capture, own, item->tint, &luma))
            glass_panel_push_luma (panel, &luma);
        }
    }
}

/* Which panels to draw, and the captures they need, read now (§5.3-2).
 * Returns the highest layer. */
static guint
plan_panels (GlassView             *self,
             const graphene_rect_t *view_rect,
             const GdkRGBA         *theme_bg,
             GArray                *items,
             GArray                *groups)
{
  GtkWidget *widget = GTK_WIDGET (self);
  GlassContext *context = glass_context_get_default ();
  gboolean reduce = glass_context_get_reduce_transparency (context);
  guint max_layer = 0;

  /* 1. The visible panels, in the overlays' order then registration order
   * (the stacking order), with everything read now (§5.3-2). */
  for (guint o = 0; o < self->overlays->len; o++)
    {
      GtkWidget *overlay = g_ptr_array_index (self->overlays, o);

      for (guint i = 0; i < self->entries->len; i++)
        {
          PanelEntry *entry = g_ptr_array_index (self->entries, i);
          GtkWidget *panel = GTK_WIDGET (entry->panel);
          Item item = { .entry = entry, .visibility = 1.0 };
          graphene_rect_t anchor;
          double anchor_radius;
          double radius;
          double vs;

          if (!(panel == overlay || gtk_widget_is_ancestor (panel, overlay)))
            continue;
          if (!gtk_widget_get_mapped (panel))
            {
              glass_panel_set_output (entry->panel, NULL, NULL);
              /* Hidden: its glass, drawn into its neighbour in a group, or
               * dissolving where it was (design.md §6.8). */
              if (!glass_panel_get_ghost (entry->panel, self, &item.P, item.corners))
                continue;
              item.shaped = TRUE;
              item.opacity = opacity_to (panel, widget);
              item.visibility = glass_panel_get_ghost_visibility (entry->panel);
            }
          else
            {
              graphene_rect_t target;

              item.opacity = opacity_to (panel, widget);
              if (item.opacity <= 0.0f)
                continue;
              if (!gtk_widget_compute_bounds (panel, widget, &item.P))
                continue;
              if (item.P.size.width < 1.0f || item.P.size.height < 1.0f)
                continue;

              /* A knob: its glass hangs from its place on springs. */
              glass_panel_follow (entry->panel, &item.P);

              /* The press bulge: the glass grows around its centre. */
              vs = glass_panel_get_visual_scale (entry->panel);
              if (vs != 1.0)
                graphene_rect_inset (&item.P, -item.P.size.width * (float) (vs - 1.0) / 2.0f,
                                     -item.P.size.height * (float) (vs - 1.0) / 2.0f);

              /* A morph: the glass on its way here from its partner's. */
              target = item.P;
              item.shaped = glass_panel_get_morph (entry->panel, &target, &item.P, item.corners);

              /* Materializing: fading up, settling in from a little larger. */
              {
                double grow;

                item.visibility = glass_panel_get_visibility (entry->panel, &grow);
                if (grow != 1.0)
                  graphene_rect_inset (&item.P, -item.P.size.width * (float) (grow - 1.0) / 2.0f,
                                       -item.P.size.height * (float) (grow - 1.0) / 2.0f);
              }
            }
          if (item.opacity <= 0.0f)
            continue;
          radius = item.shaped ? MAX (MAX (item.corners[0], item.corners[1]), MAX (item.corners[2], item.corners[3]))
                               : glass_panel_effective_radius (entry->panel, &item.P);
          item.layer = panel_layer (self, panel);

          /* A GlassGroup's panels join one body of glass. */
          item.fuse_group = fuse_group_of (self, panel);
          if (item.fuse_group)
            {
              Item *leader = NULL;

              for (guint k = 0; k < items->len; k++)
                {
                  Item *other = &g_array_index (items, Item, k);

                  if (other->fuse_group == item.fuse_group && other->layer == item.layer)
                    leader = other;
                }
              if (leader && leader->n_shapes < GLASS_MAX_SHAPES)
                {
                  leader->shapes[leader->n_shapes] = item.P;
                  leader->radii[leader->n_shapes] = (float) radius;
                  leader->members[leader->n_shapes] = entry;
                  leader->n_shapes++;
                  graphene_rect_union (&leader->P, &item.P, &leader->P);
                  continue;
                }
              item.n_shapes = 1;
              item.shapes[0] = item.P;
              item.radii[0] = (float) radius;
              item.members[0] = entry;
              item.merge_k = (float) glass_group_get_spacing (GLASS_GROUP (item.fuse_group));
            }
          else if (gtk_widget_get_mapped (panel) &&
                   glass_panel_get_morph_anchor (entry->panel, &anchor, &anchor_radius))
            {
              /* A morph-id swap: the partner's glass stays behind as a drop,
               * and ours is drawn out of it, the two one body while they
               * part (liquid_glass_widgets' teardrop). The drop comes
               * first: what the view records as drawn is ours. */
              item.n_shapes = 2;
              item.shapes[0] = anchor;
              item.radii[0] = (float) anchor_radius;
              item.shapes[1] = item.P;
              item.radii[1] = (float) radius;
              item.members[0] = item.members[1] = entry;
              item.merge_k = (float) glass_panel_get_anchor_merge (entry->panel);
              graphene_rect_union (&anchor, &item.P, &item.P);
            }

          glass_panel_resolve_params (entry->panel, context, item.params);
          glass_panel_get_tint_rgba (entry->panel, item.params, theme_bg, item.tint);
          if (reduce)
            glass_reduce_transparency (item.params, item.tint);
          {
            GlassSurfaceSpec surface;

            glass_panel_resolve_surface (entry->panel, context, &surface);
            glass_surface_apply (&surface, item.params, &item.blur, &item.surface);
          }
          /* Part of the glass (materializing, dissolving): every term of it
           * scales down together, and what is drawn fades with it. The blur
           * stays (a new blur every frame would rebuild its kernel). */
          if (item.visibility < 1.0)
            {
              double v = MAX (item.visibility, 0.0);

              item.opacity *= (float) v;
              item.params[GLASS_PARAM_ID_DISPLACEMENT_SCALE] *= v;
              item.params[GLASS_PARAM_ID_SHADOW_INTENSITY] *= v;
              item.tint[3] *= (float) v;
              item.surface.frost_opacity *= (float) v;
              item.surface.rim_shade *= (float) v;
              item.surface.rim_light *= (float) v;
              item.surface.edge_absorption *= (float) v;
              if (item.opacity <= 0.0f)
                continue;
            }
          max_layer = MAX (max_layer, item.layer);
          g_array_append_val (items, item);
        }
    }

  /* What each needs captured (a fused group's bounds are final now). */
  for (guint i = 0; i < items->len; i++)
    {
      Item *item = &g_array_index (items, Item, i);

      if (!glass_capture_rect_for_panel (&item->P, glass_blur_spec_reach (&item->blur),
                                         view_rect, &item->C))
        {
          g_array_remove_index (items, i--);
          continue;
        }
      item->C_tree = item->C;
    }

  /* 2. Nested panels hang off their layer-0 ancestor: its capture must
   * cover them too, since their backdrop is made from it. */
  for (guint i = 0; i < items->len; i++)
    {
      Item *item = &g_array_index (items, Item, i);

      item->root = i;
      if (item->layer == 0)
        continue;
      for (guint j = 0; j < items->len; j++)
        {
          Item *other = &g_array_index (items, Item, j);

          if (other->layer == 0 && item_contains (other, GTK_WIDGET (item->entry->panel)))
            {
              item->root = j;
              graphene_rect_union (&other->C_tree, &item->C, &other->C_tree);
              break;
            }
        }
    }
  for (guint i = 0; i < items->len; i++)
    {
      Item *item = &g_array_index (items, Item, i);

      if (item->layer == 0)
        item->group = group_for (groups, item);
    }

  return max_layer;
}

static void
draw_panels (GlassView             *self,
             GtkSnapshot           *snapshot,
             GskRenderNode         *backdrop,
             GskRenderNode         *content_node,
             guint                  key_extra,
             const graphene_rect_t *view_rect,
             const GdkRGBA         *theme_bg)
{
  GtkWidget *widget = GTK_WIDGET (self);
  GlassRenderStats *stats = glass_renderer_get_stats (self->renderer);
  double scale = gdk_surface_get_scale (gtk_native_get_surface (gtk_widget_get_native (widget)));
  gint64 t0 = g_get_monotonic_time ();
  g_autoptr (GArray) items = g_array_new (FALSE, TRUE, sizeof (Item));
  g_autoptr (GArray) groups = g_array_new (FALSE, TRUE, sizeof (Group));
  GlassLayerSource under;
  gboolean has_under = ancestor_glass (self, &under);
  guint max_layer, composed_index = 0;
  GskRenderNode *leaf;
  float dx, dy;

  max_layer = plan_panels (self, view_rect, theme_bg, items, groups);

  leaf = glass_unwrap_node (content_node, &dx, &dy);
  glass_renderer_make_current (self->renderer);

  /* 3. Layer 0: one capture per group, reused while nothing under it
   * changed; in a view inside a panel, composed over that panel's glass. */
  while (self->captures->len < groups->len)
    g_ptr_array_add (self->captures, glass_capture_new ());
  while (self->captures->len > groups->len)
    {
      glass_capture_free (g_ptr_array_index (self->captures, self->captures->len - 1), self->renderer);
      g_ptr_array_remove_index (self->captures, self->captures->len - 1);
      glass_renderer_make_current (self->renderer);
    }

  {
    g_autofree GlassCaptureRequest *reqs = g_new0 (GlassCaptureRequest, MAX (groups->len, 1));
    g_autofree GlassCapture **effective = g_new0 (GlassCapture *, MAX (groups->len, 1));

    for (guint g = 0; g < groups->len; g++)
      {
        Group *group = &g_array_index (groups, Group, g);

        reqs[g] = (GlassCaptureRequest) {
          .backdrop = backdrop,
          .content_key = leaf,
          .key_dx = dx,
          .key_dy = dy,
          .key_extra = key_extra,
          .rect = group->C,
          .scale = scale,
          .blur = group->blur,
          .downscale = group->downscale,
        };
      }
    glass_renderer_capture_all (self->renderer, (GlassCapture **) self->captures->pdata, reqs, groups->len);

    for (guint g = 0; g < groups->len; g++)
      {
        Group *group = &g_array_index (groups, Group, g);
        GlassCapture *raw = g_ptr_array_index (self->captures, g);

        effective[g] = raw;
        if (has_under)
          {
            GlassLayerSource sources[2] = { under };
            GlassCapture *composed = composed_capture (self, composed_index++);

            glass_capture_get_source (raw, &sources[1]);
            if (glass_renderer_compose (self->renderer, composed, &group->C, scale, group->downscale,
                                        &group->blur, sources, 2))
              effective[g] = composed;
          }
      }

    for (guint i = 0; i < items->len; i++)
      {
        Item *item = &g_array_index (items, Item, i);

        if (item->layer == 0)
          {
            item->capture = effective[item->group];
            draw_item (self, snapshot, item, view_rect, scale);
          }
      }

    /* 4. Later layers: glass over glass. The backdrop is the layer-0
     * capture with the glass of every lower layer over it, composed from
     * our own textures (no capture: design.md §6.7). */
    for (guint layer = 1; layer <= max_layer; layer++)
      for (guint i = 0; i < items->len; i++)
        {
          Item *item = &g_array_index (items, Item, i);
          Item *root = &g_array_index (items, Item, item->root);
          g_autoptr (GArray) sources = NULL;
          GlassLayerSource base;

          if (item->layer != layer || root->layer != 0 || root->capture == NULL)
            continue;

          sources = g_array_new (FALSE, FALSE, sizeof (GlassLayerSource));
          glass_capture_get_source (root->capture, &base);
          g_array_append_val (sources, base);
          for (guint j = 0; j < items->len; j++)
            {
              Item *lower = &g_array_index (items, Item, j);

              if (lower->layer < layer && lower->has_output &&
                  graphene_rect_intersection (&lower->output.rect, &item->C, NULL))
                g_array_append_val (sources, lower->output);
            }

          item->capture = composed_capture (self, composed_index++);
          if (!glass_renderer_compose (self->renderer, item->capture, &item->C, scale,
                                       (int) item->params[GLASS_PARAM_ID_BLUR_DOWNSCALE],
                                       &item->blur,
                                       (GlassLayerSource *) sources->data, sources->len))
            continue;
          draw_item (self, snapshot, item, view_rect, scale);
        }
  }

  while (self->composed->len > composed_index)
    {
      glass_capture_free (g_ptr_array_index (self->composed, self->composed->len - 1), self->renderer);
      g_ptr_array_remove_index (self->composed, self->composed->len - 1);
      glass_renderer_make_current (self->renderer);
    }
  glass_renderer_flush (self->renderer);

  stats->snapshots++;
  stats->us_total += g_get_monotonic_time () - t0;
}

static void
draw_hud (GlassView *self, GtkSnapshot *snapshot)
{
  GlassRenderStats *s;
  gint64 now = g_get_monotonic_time ();
  PangoLayout *layout;
  GdkRGBA bg = { 0, 0, 0, 0.6f }, fg = { 1, 1, 1, 1 };
  int w, h;

  if (self->renderer == NULL)
    return;
  s = glass_renderer_get_stats (self->renderer);

  if (self->hud_text == NULL || now - self->hud_last_us > G_USEC_PER_SEC)
    {
      double secs = self->hud_last_us ? (now - self->hud_last_us) / (double) G_USEC_PER_SEC : 1.0;
      guint captures = MAX (s->captures, 1);

      g_free (self->hud_text);
      self->hud_text = g_strdup_printf (
        "%s\n%.0f snapshots/s · glass %.2f ms/snapshot\n"
        "renders %u (regions %u, hits %u, unchanged %u) · passes %u (hits %u)\n"
        "per render: render %.2f · download %.2f · upload+blur %.2f ms",
        glass_renderer_get_info (self->renderer),
        s->snapshots / secs, s->snapshots ? s->us_total / 1000.0 / s->snapshots : 0.0,
        s->captures, s->regions, s->capture_hits, s->capture_unchanged, s->passes, s->pass_hits,
        s->us_render_texture / 1000.0 / captures, s->us_download / 1000.0 / captures,
        s->us_gl / 1000.0 / captures);
      g_debug ("hud: %s", self->hud_text);
      memset (s, 0, sizeof *s);
      self->hud_last_us = now;
    }

  layout = gtk_widget_create_pango_layout (GTK_WIDGET (self), self->hud_text);
  pango_layout_get_pixel_size (layout, &w, &h);
  gtk_snapshot_append_color (snapshot, &bg, &GRAPHENE_RECT_INIT (8, 8, w + 12, h + 8));
  gtk_snapshot_save (snapshot);
  gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (14, 12));
  gtk_snapshot_append_layout (snapshot, layout, &fg);
  gtk_snapshot_restore (snapshot);
  g_object_unref (layout);
}

/* ── Adaptive colours under the CSS fallback (design.md §12.1) ─────────────
 * Without our renderer there is no capture to measure, so the view samples
 * the backdrop of its last snapshot itself: the panels' rectangles (read in
 * that snapshot, §5.3-2), rendered small in one render_texture() by the
 * window's renderer, outside the snapshot, at most 10 times a second and
 * only when the content or the panels moved. The panels' foreground is not
 * in the backdrop (§5.3-3), so their colour cannot feed back. */

#define SAMPLE_INTERVAL_US  (G_USEC_PER_SEC / 10)
#define SAMPLE_SCALE        0.25f     /* of a logical px */
#define SAMPLE_MAX_PX       256.0f
/* glasspanel.glass-fallback in glass.css: its blur and its tint. */
#define FALLBACK_BLUR_PX    12.0
static const float fallback_tint[4] = { 1.0f, 1.0f, 1.0f, 0.18f };

typedef struct {
  GlassPanel      *panel;    /* owned */
  graphene_rect_t  rect;
} Sample;

static void
sample_clear (Sample *sample)
{
  g_clear_object (&sample->panel);
}

static void
stop_sampling (GlassView *self)
{
  g_clear_handle_id (&self->sample_source, g_source_remove);
  g_clear_pointer (&self->sample_node, gsk_render_node_unref);
  g_clear_pointer (&self->sample_panels, g_array_unref);
  g_clear_pointer (&self->sample_leaf, gsk_render_node_unref);
  g_clear_pointer (&self->sampled_leaf, gsk_render_node_unref);
}

static guint64
hash_bytes (guint64 hash, gconstpointer data, gsize size)
{
  const guint8 *p = data;

  /* FNV-1a */
  for (gsize i = 0; i < size; i++)
    hash = (hash ^ p[i]) * 0x100000001b3ull;
  return hash;
}

static gboolean
sample_fallback (gpointer data)
{
  GlassView *self = data;
  GtkNative *native = gtk_widget_get_native (GTK_WIDGET (self));
  GskRenderer *renderer = native ? gtk_native_get_renderer (native) : NULL;
  g_autoptr (GArray) panels = g_steal_pointer (&self->sample_panels);
  GskRenderNode *backdrop = g_steal_pointer (&self->sample_node);
  GskRenderNode *clip, *node;
  GskTransform *transform;
  GdkTexture *texture;
  GdkTextureDownloader *downloader;
  g_autofree guint8 *pixels = NULL;
  graphene_rect_t u;
  float k;
  int w, h;

  self->sample_source = 0;
  if (backdrop == NULL || panels == NULL || renderer == NULL || self->full)
    goto out;

  u = g_array_index (panels, Sample, 0).rect;
  for (guint i = 1; i < panels->len; i++)
    graphene_rect_union (&u, &g_array_index (panels, Sample, i).rect, &u);
  k = MIN (SAMPLE_SCALE, SAMPLE_MAX_PX / MAX (u.size.width, u.size.height));
  w = MAX (1, (int) ceilf (u.size.width * k));
  h = MAX (1, (int) ceilf (u.size.height * k));

  clip = gsk_clip_node_new (backdrop, &u);
  transform = gsk_transform_scale (NULL, k, k);
  transform = gsk_transform_translate (transform, &GRAPHENE_POINT_INIT (-u.origin.x, -u.origin.y));
  node = gsk_transform_node_new (clip, transform);
  gsk_transform_unref (transform);
  gsk_render_node_unref (clip);
  texture = gsk_renderer_render_texture (renderer, node, &GRAPHENE_RECT_INIT (0, 0, w, h));
  gsk_render_node_unref (node);
  if (texture == NULL)
    goto out;

  w = gdk_texture_get_width (texture);
  h = gdk_texture_get_height (texture);
  pixels = g_malloc ((gsize) w * h * 4);
  downloader = gdk_texture_downloader_new (texture);
  gdk_texture_downloader_set_format (downloader, GDK_MEMORY_R8G8B8A8_PREMULTIPLIED);
  gdk_texture_downloader_download_into (downloader, pixels, (gsize) w * 4);
  gdk_texture_downloader_free (downloader);
  g_object_unref (texture);

  for (guint i = 0; i < panels->len; i++)
    {
      Sample *s = &g_array_index (panels, Sample, i);
      GlassLumaStats luma;

      if (glass_adaptive_measure (pixels, (gsize) w * 4, w, h,
                                  (s->rect.origin.x - u.origin.x) * k, (s->rect.origin.y - u.origin.y) * k,
                                  s->rect.size.width * k, s->rect.size.height * k,
                                  MAX (1.0, FALLBACK_BLUR_PX * k), fallback_tint, &luma))
        glass_panel_push_luma (s->panel, &luma);
    }

  self->sample_last_us = g_get_monotonic_time ();
  g_clear_pointer (&self->sampled_leaf, gsk_render_node_unref);
  self->sampled_leaf = g_steal_pointer (&self->sample_leaf);
  self->sampled_hash = self->sample_hash;

out:
  g_clear_pointer (&backdrop, gsk_render_node_unref);
  g_clear_pointer (&self->sample_leaf, gsk_render_node_unref);
  return G_SOURCE_REMOVE;
}

/* In the snapshot, with the CSS fallback: keeps what to sample. */
static void
queue_fallback_sample (GlassView             *self,
                       GskRenderNode         *backdrop,
                       GskRenderNode         *content_node,
                       guint                  key_extra,
                       const graphene_rect_t *view_rect)
{
  GtkWidget *widget = GTK_WIDGET (self);
  g_autoptr (GArray) panels = NULL;
  GskRenderNode *leaf = NULL;
  float dx = 0.0f, dy = 0.0f;
  guint64 hash = 0xcbf29ce484222325ull;
  gint64 delay;

  /* A view inside a panel has no backdrop of its own to measure (it is the
   * panel's glass), nor has a knob's; high contrast is opaque. */
  if (inside_panel (self) || self->backdrop_capture_only ||
      glass_context_get_high_contrast (glass_context_get_default ()))
    return;

  panels = g_array_new (FALSE, TRUE, sizeof (Sample));
  g_array_set_clear_func (panels, (GDestroyNotify) sample_clear);
  for (guint i = 0; i < self->entries->len; i++)
    {
      GlassPanel *panel = ((PanelEntry *) g_ptr_array_index (self->entries, i))->panel;
      Sample s = { 0 };

      if (!glass_panel_uses_adaptive (panel) || !gtk_widget_get_mapped (GTK_WIDGET (panel)) ||
          !gtk_widget_compute_bounds (GTK_WIDGET (panel), widget, &s.rect) ||
          !graphene_rect_intersection (&s.rect, view_rect, &s.rect))
        continue;
      s.panel = g_object_ref (panel);
      g_array_append_val (panels, s);
      hash = hash_bytes (hash, &s.rect, sizeof s.rect);
    }
  if (panels->len == 0)
    return;

  if (content_node)
    leaf = glass_unwrap_node (content_node, &dx, &dy);
  hash = hash_bytes (hash, &dx, sizeof dx);
  hash = hash_bytes (hash, &dy, sizeof dy);
  hash = hash_bytes (hash, &key_extra, sizeof key_extra);
  if (leaf == self->sampled_leaf && hash == self->sampled_hash)
    return;   /* nothing moved since the last sample */

  g_clear_pointer (&self->sample_node, gsk_render_node_unref);
  self->sample_node = gsk_render_node_ref (backdrop);
  g_clear_pointer (&self->sample_panels, g_array_unref);
  self->sample_panels = g_steal_pointer (&panels);
  g_clear_pointer (&self->sample_leaf, gsk_render_node_unref);
  self->sample_leaf = leaf ? gsk_render_node_ref (leaf) : NULL;
  self->sample_hash = hash;

  if (self->sample_source == 0)
    {
      delay = self->sample_last_us + SAMPLE_INTERVAL_US - g_get_monotonic_time ();
      self->sample_source = g_timeout_add (MAX (delay, 0) / 1000, sample_fallback, self);
    }
}

static guint
hash_backdrop (GlassView *self, const GdkRGBA *bg, float w, float h)
{
  guint hash = gdk_rgba_hash (bg);

  hash = hash * 31 + (guint) (w * 4.0f);
  hash = hash * 31 + (guint) (h * 4.0f);
  for (int i = 0; i < 2; i++)
    hash = hash * 31 + (guint) self->edges[i].style * 4099 + (guint) self->edges[i].size;
  return hash;
}

static void
glass_view_snapshot (GtkWidget   *widget,
                     GtkSnapshot *snapshot)
{
  GlassView *self = GLASS_VIEW (widget);
  float width = gtk_widget_get_width (widget);
  float height = gtk_widget_get_height (widget);
  graphene_rect_t view_rect = GRAPHENE_RECT_INIT (0, 0, width, height);
  GskRenderNode *content_node = NULL;
  GskRenderNode *nodes[4];
  GskRenderNode *backdrop;
  GdkRGBA bg, theme_bg;
  int n_nodes = 0;

  resolve_backdrop_color (self, &bg);
  gtk_widget_get_color (self->backdrop_node, &theme_bg);

  /* 1. Backdrop colour + content + edge effects: what the glass sees, and
   * what is on screen. Transparent parts of the content would otherwise
   * capture as transparent and darken the glass (§7.3). */
  if (self->content)
    {
      GtkSnapshot *content_snapshot = gtk_snapshot_new ();

      gtk_widget_snapshot_child (widget, self->content, content_snapshot);
      content_node = gtk_snapshot_free_to_node (content_snapshot);
    }

  if (bg.alpha > 0.0f && !self->backdrop_capture_only)
    nodes[n_nodes++] = gsk_color_node_new (&bg, &view_rect);
  if (content_node)
    {
      nodes[n_nodes++] = gsk_render_node_ref (content_node);
      for (int i = 0; i < 2; i++)
        {
          GskRenderNode *edge = edge_node (self, content_node, &self->edges[i], i == 0,
                                           width, height, &theme_bg);
          if (edge)
            nodes[n_nodes++] = edge;
        }
    }
  backdrop = gsk_container_node_new (nodes, n_nodes);
  gtk_snapshot_append_node (snapshot, backdrop);

  /* 2. The glass bodies; with the CSS fallback, the panels draw themselves
   * and we only sample what is under them, for their colours. */
  if (!self->full && self->entries->len > 0)
    queue_fallback_sample (self, backdrop, content_node, hash_backdrop (self, &bg, width, height), &view_rect);
  else if (self->full && self->renderer && self->entries->len > 0)
    {
      GskRenderNode *seen = gsk_render_node_ref (backdrop);

      if (self->backdrop_capture_only && bg.alpha > 0.0f)
        {
          GskRenderNode *parts[2] = { gsk_color_node_new (&bg, &view_rect), backdrop };

          gsk_render_node_unref (seen);
          seen = gsk_container_node_new (parts, 2);
          gsk_render_node_unref (parts[0]);
        }
      draw_panels (self, snapshot, seen, content_node,
                   hash_backdrop (self, &bg, width, height), &view_rect, &theme_bg);
      gsk_render_node_unref (seen);
    }

  /* 3. Overlay children: the panels' foreground. */
  for (guint i = 0; i < self->overlays->len; i++)
    gtk_widget_snapshot_child (widget, g_ptr_array_index (self->overlays, i), snapshot);

  if (glass_get_debug_flags () & GLASS_DEBUG_HUD)
    draw_hud (self, snapshot);

  for (int i = 0; i < n_nodes; i++)
    gsk_render_node_unref (nodes[i]);
  g_clear_pointer (&content_node, gsk_render_node_unref);
  gsk_render_node_unref (backdrop);
  self->drawn_since_map = TRUE;
}

/* ── Layout ───────────────────────────────────────────────────────────────── */

static void
glass_view_measure (GtkWidget      *widget,
                    GtkOrientation  orientation,
                    int             for_size,
                    int            *minimum,
                    int            *natural,
                    int            *minimum_baseline,
                    int            *natural_baseline)
{
  GlassView *self = GLASS_VIEW (widget);

  *minimum = *natural = 0;
  if (self->content && gtk_widget_should_layout (self->content))
    gtk_widget_measure (self->content, orientation, for_size, minimum, natural, NULL, NULL);

  /* Overlays count for the minimum only, like GtkOverlay's measured children
   * do not: the view must be able to hold them. */
  for (guint i = 0; i < self->overlays->len; i++)
    {
      GtkWidget *child = g_ptr_array_index (self->overlays, i);
      int child_min = 0;

      if (!gtk_widget_should_layout (child))
        continue;
      gtk_widget_measure (child, orientation, -1, &child_min, NULL, NULL, NULL);
      *minimum = MAX (*minimum, child_min);
      *natural = MAX (*natural, *minimum);
    }
}

/* GtkOverlay's rule: the content fills the view, and each overlay child is
 * given the whole view and places itself by its own halign / valign /
 * margins (gtk_widget_allocate() applies them). */
static void
glass_view_size_allocate (GtkWidget *widget,
                          int        width,
                          int        height,
                          int        baseline)
{
  GlassView *self = GLASS_VIEW (widget);

  if (self->content && gtk_widget_should_layout (self->content))
    gtk_widget_allocate (self->content, width, height, baseline, NULL);

  for (guint i = 0; i < self->overlays->len; i++)
    {
      GtkWidget *child = g_ptr_array_index (self->overlays, i);
      graphene_point_t *offset;
      GskTransform *transform = NULL;

      if (!gtk_widget_should_layout (child))
        continue;
      offset = g_hash_table_lookup (self->offsets, child);
      if (offset && (offset->x != 0.0f || offset->y != 0.0f))
        transform = gsk_transform_translate (NULL, offset);
      gtk_widget_allocate (child, width, height, -1, transform);
    }

  gtk_widget_allocate (self->backdrop_node, 0, 0, -1, NULL);
}

void
glass_view_set_backdrop_capture_only (GlassView *self,
                                      gboolean   capture_only)
{
  self->backdrop_capture_only = !!capture_only;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
glass_view_set_overlay_offset (GlassView *self,
                               GtkWidget *overlay,
                               double     dx,
                               double     dy)
{
  graphene_point_t *offset = g_hash_table_lookup (self->offsets, overlay);

  if (offset == NULL)
    {
      offset = g_new0 (graphene_point_t, 1);
      g_hash_table_insert (self->offsets, overlay, offset);
    }
  if (offset->x == (float) dx && offset->y == (float) dy)
    return;
  offset->x = (float) dx;
  offset->y = (float) dy;
  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

static void
glass_view_css_changed (GtkWidget         *widget,
                        GtkCssStyleChange *change)
{
  GTK_WIDGET_CLASS (glass_view_parent_class)->css_changed (widget, change);
  gtk_widget_queue_draw (widget);
}

static void
glass_view_root (GtkWidget *widget)
{
  GlassView *self = GLASS_VIEW (widget);
  GtkWidget *panel;

  GTK_WIDGET_CLASS (glass_view_parent_class)->root (widget);
  glass_style_ensure (gtk_widget_get_display (widget));

  /* Inside a panel, its glass is under our backdrop: redraw with it. */
  panel = gtk_widget_get_ancestor (widget, GLASS_TYPE_PANEL);
  if (panel)
    {
      self->under_panel = GLASS_PANEL (panel);
      glass_panel_add_nested_view (self->under_panel, self);
    }
}

static void
glass_view_unroot (GtkWidget *widget)
{
  GlassView *self = GLASS_VIEW (widget);

  if (self->under_panel)
    glass_panel_remove_nested_view (self->under_panel, self);
  self->under_panel = NULL;
  stop_sampling (self);
  GTK_WIDGET_CLASS (glass_view_parent_class)->unroot (widget);
}

/* ── GObject ──────────────────────────────────────────────────────────────── */

static void
glass_view_dispose (GObject *object)
{
  GlassView *self = GLASS_VIEW (object);

  stop_sampling (self);
  release_renderer (self);
  for (guint i = 0; i < self->captures->len; i++)
    glass_capture_free (g_ptr_array_index (self->captures, i), NULL);
  g_ptr_array_set_size (self->captures, 0);
  for (guint i = 0; i < self->composed->len; i++)
    glass_capture_free (g_ptr_array_index (self->composed, i), NULL);
  g_ptr_array_set_size (self->composed, 0);
  while (self->entries->len > 0)
    {
      PanelEntry *entry = g_ptr_array_index (self->entries, self->entries->len - 1);

      g_ptr_array_remove_index (self->entries, self->entries->len - 1);
      glass_panel_set_mode (entry->panel, GLASS_PANEL_MODE_NONE);
      entry_free (entry, NULL);
    }
  g_clear_pointer (&self->content, gtk_widget_unparent);
  for (guint i = 0; i < self->overlays->len; i++)
    gtk_widget_unparent (g_ptr_array_index (self->overlays, i));
  g_ptr_array_set_size (self->overlays, 0);
  g_clear_pointer (&self->backdrop_node, gtk_widget_unparent);

  G_OBJECT_CLASS (glass_view_parent_class)->dispose (object);
}

static void
glass_view_finalize (GObject *object)
{
  GlassView *self = GLASS_VIEW (object);

  g_ptr_array_unref (self->entries);
  g_ptr_array_unref (self->captures);
  g_ptr_array_unref (self->composed);
  g_ptr_array_unref (self->overlays);
  g_hash_table_unref (self->offsets);
  g_free (self->hud_text);

  G_OBJECT_CLASS (glass_view_parent_class)->finalize (object);
}

static void
glass_view_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  GlassView *self = GLASS_VIEW (object);

  switch (prop_id)
    {
    case PROP_CONTENT:
      g_value_set_object (value, self->content);
      break;
    case PROP_BACKDROP_COLOR:
      g_value_set_boxed (value, self->backdrop_color_set ? &self->backdrop_color : NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_view_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  GlassView *self = GLASS_VIEW (object);

  switch (prop_id)
    {
    case PROP_CONTENT:
      glass_view_set_content (self, g_value_get_object (value));
      break;
    case PROP_BACKDROP_COLOR:
      glass_view_set_backdrop_color (self, g_value_get_boxed (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_view_class_init (GlassViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = glass_view_dispose;
  object_class->finalize = glass_view_finalize;
  object_class->get_property = glass_view_get_property;
  object_class->set_property = glass_view_set_property;

  widget_class->snapshot = glass_view_snapshot;
  widget_class->measure = glass_view_measure;
  widget_class->size_allocate = glass_view_size_allocate;
  widget_class->map = glass_view_map;
  widget_class->realize = glass_view_realize;
  widget_class->unrealize = glass_view_unrealize;
  widget_class->css_changed = glass_view_css_changed;
  widget_class->root = glass_view_root;
  widget_class->unroot = glass_view_unroot;

  /**
   * GlassView:content:
   *
   * The widget the glass is drawn over. It fills the view.
   */
  props[PROP_CONTENT] =
    g_param_spec_object ("content", NULL, NULL, GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassView:backdrop-color:
   *
   * The colour painted under the content, which the glass sees where the
   * content is transparent. %NULL (the default) is the window background
   * (`--window-bg-color`), or nothing inside a [class@Panel].
   */
  props[PROP_BACKDROP_COLOR] =
    g_param_spec_boxed ("backdrop-color", NULL, NULL, GDK_TYPE_RGBA,
                        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);

  gtk_widget_class_set_css_name (widget_class, "glassview");
}

static void
glass_view_init (GlassView *self)
{
  self->overlays = g_ptr_array_new ();
  self->entries = g_ptr_array_new ();
  self->captures = g_ptr_array_new ();
  self->composed = g_ptr_array_new ();
  self->offsets = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);

  /* A never-drawn node whose CSS colour is var(--window-bg-color) (glass.css):
   * the theme's window background, resolved by GTK (C1). */
  self->backdrop_node = glass_style_node_new ("backdrop");
  gtk_widget_set_parent (self->backdrop_node, GTK_WIDGET (self));

  gtk_widget_set_overflow (GTK_WIDGET (self), GTK_OVERFLOW_HIDDEN);
  self->full = TRUE;

  g_signal_connect_object (glass_context_get_default (), "changed",
                           G_CALLBACK (context_changed), self, 0);
}

/* ── Buildable ────────────────────────────────────────────────────────────── */

static void
glass_view_buildable_add_child (GtkBuildable *buildable,
                                GtkBuilder   *builder,
                                GObject      *child,
                                const char   *type)
{
  GlassView *self = GLASS_VIEW (buildable);

  if (GTK_IS_WIDGET (child) && g_strcmp0 (type, "overlay") == 0)
    glass_view_add_overlay (self, GTK_WIDGET (child));
  else if (GTK_IS_WIDGET (child) && type == NULL)
    glass_view_set_content (self, GTK_WIDGET (child));
  else
    parent_buildable_iface->add_child (buildable, builder, child, type);
}

static void
glass_view_buildable_init (GtkBuildableIface *iface)
{
  parent_buildable_iface = g_type_interface_peek_parent (iface);
  iface->add_child = glass_view_buildable_add_child;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * glass_view_new:
 *
 * Creates a new view.
 *
 * Returns: a new view
 */
GtkWidget *
glass_view_new (void)
{
  return g_object_new (GLASS_TYPE_VIEW, NULL);
}

/**
 * glass_view_get_content:
 * @self: a view
 *
 * Gets the content.
 *
 * Returns: (transfer none) (nullable): the content
 */
GtkWidget *
glass_view_get_content (GlassView *self)
{
  g_return_val_if_fail (GLASS_IS_VIEW (self), NULL);

  return self->content;
}

/**
 * glass_view_set_content:
 * @self: a view
 * @content: (nullable): the widget the glass is drawn over
 *
 * Sets the content.
 */
void
glass_view_set_content (GlassView *self,
                        GtkWidget *content)
{
  g_return_if_fail (GLASS_IS_VIEW (self));
  g_return_if_fail (content == NULL || GTK_IS_WIDGET (content));

  if (self->content == content)
    return;

  g_clear_pointer (&self->content, gtk_widget_unparent);
  if (content)
    {
      self->content = content;
      gtk_widget_insert_after (content, GTK_WIDGET (self), self->backdrop_node);
    }
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CONTENT]);
}

/**
 * glass_view_add_overlay:
 * @self: a view
 * @widget: a widget; the [class@Panel]s in it are drawn as glass
 *
 * Adds an overlay child, above the ones added before.
 */
void
glass_view_add_overlay (GlassView *self,
                        GtkWidget *widget)
{
  g_return_if_fail (GLASS_IS_VIEW (self));
  g_return_if_fail (GTK_IS_WIDGET (widget));
  g_return_if_fail (gtk_widget_get_parent (widget) == NULL);

  g_ptr_array_add (self->overlays, widget);
  gtk_widget_set_parent (widget, GTK_WIDGET (self));
}

/**
 * glass_view_remove_overlay:
 * @self: a view
 * @widget: an overlay child
 *
 * Removes an overlay child.
 */
void
glass_view_remove_overlay (GlassView *self,
                           GtkWidget *widget)
{
  g_return_if_fail (GLASS_IS_VIEW (self));

  if (!g_ptr_array_remove (self->overlays, widget))
    {
      g_critical ("glass_view_remove_overlay: not an overlay child");
      return;
    }
  g_hash_table_remove (self->offsets, widget);
  gtk_widget_unparent (widget);
}

/**
 * glass_view_set_backdrop_color:
 * @self: a view
 * @color: (nullable): the colour, or %NULL for the window background
 *
 * Sets [property@View:backdrop-color].
 */
void
glass_view_set_backdrop_color (GlassView     *self,
                               const GdkRGBA *color)
{
  g_return_if_fail (GLASS_IS_VIEW (self));

  if (color == NULL && !self->backdrop_color_set)
    return;
  self->backdrop_color_set = color != NULL;
  if (color)
    self->backdrop_color = *color;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_BACKDROP_COLOR]);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

/**
 * glass_view_get_backdrop_color:
 * @self: a view
 * @color: (out): the colour in use
 *
 * Gets the colour painted under the content: the one set, or the window
 * background.
 *
 * Returns: %TRUE if the colour was set with
 *   [method@View.set_backdrop_color], %FALSE if it is automatic
 */
gboolean
glass_view_get_backdrop_color (GlassView *self,
                               GdkRGBA   *color)
{
  g_return_val_if_fail (GLASS_IS_VIEW (self), FALSE);

  resolve_backdrop_color (self, color);
  return self->backdrop_color_set;
}

/**
 * glass_view_get_active_renderer:
 * @self: a view
 *
 * Gets how the view's glass is drawn right now: by the full renderer, or by
 * the CSS fallback (no OpenGL, the fallback chosen, high contrast).
 *
 * Returns: %GLASS_RENDERER_MODE_FULL or %GLASS_RENDERER_MODE_FALLBACK: how
 *   this view's glass is drawn right now
 */
GlassRendererMode
glass_view_get_active_renderer (GlassView *self)
{
  g_return_val_if_fail (GLASS_IS_VIEW (self), GLASS_RENDERER_MODE_FALLBACK);

  /* Realized, full needs the renderer that draws it. */
  return self->full && (self->renderer || !gtk_widget_get_realized (GTK_WIDGET (self)))
         ? GLASS_RENDERER_MODE_FULL : GLASS_RENDERER_MODE_FALLBACK;
}
