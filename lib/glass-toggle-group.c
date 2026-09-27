/* glass-toggle-group.c — a segmented control on glass (design.md §6.6).
 *
 * A capsule of glass holding a row of buttons, one of them active. Under the
 * active one lies a plate that is glass on the glass (a nested GlassPanel,
 * drawn by the view in a later layer, design.md §6.7): it slides to the
 * toggle that becomes active, and swells like a lens while the group is
 * pressed.
 *
 * The plate can also be dragged to another toggle (design.md §6.7). It is
 * not pinned to the pointer but hung from it by springs, one per edge: the
 * edge in front is stiffer than the one behind, so the plate stretches while
 * it moves, and when the pointer stops short the front edge runs on past it
 * before the plate gathers itself - jelly rather than a sliding block.
 *
 * The buttons are plain GtkButtons marked with the style class `.active`,
 * not GtkToggleButtons: a theme's `button:checked` background (even one in
 * ~/.config/gtk-4.0/gtk.css, at USER priority, which nothing can override)
 * would otherwise cover the plate (docs/memo.md 地雷12). Each is drawn
 * inside a pill (GlassPillBox), so its hover background is round.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

#include <adwaita.h>
#include <math.h>

/**
 * GlassToggleGroup:
 *
 * The glass counterpart of `AdwToggleGroup`: a row of toggles on a glass
 * capsule, one of them active, marked by a plate of glass that slides to
 * it and swells while the group is pressed.
 *
 * ## CSS nodes
 *
 * `GlassToggleGroup` is a [class@Panel] (CSS name `glasspanel`) with the
 * style class `.toggle-group`. The plate is a [class@Panel] with the style
 * class `.toggle-plate`; its tint is the `color` of the never-drawn child
 * node `pill`.
 */

struct _GlassToggleGroup {
  GlassPanel      parent_instance;

  GtkWidget      *box;
  GtkWidget      *plate;           /* GlassPanel, glass on our glass */
  GtkWidget      *pill_node;       /* resolves the plate's tint from CSS */
  GPtrArray      *buttons;         /* GtkButton* */
  GPtrArray      *names;           /* char* */
  guint           active;

  AdwAnimation   *animation;
  double          progress;
  graphene_rect_t from;            /* the plate where the animation started */
  gboolean        have_from;
  graphene_rect_t shown;           /* the plate as last allocated */

  /* Dragging the plate */
  GtkGesture     *drag;
  gboolean        drag_armed;      /* pressed on the active toggle */
  gboolean        dragging;        /* moved past the threshold: the plate follows */
  graphene_rect_t drag_origin;     /* the active toggle when the drag began */
  double          pointer_dx, pointer_dy;
  guint           shown_active;    /* the toggle marked as active (while dragging, the nearest) */

  /* The jelly: each edge of the plate and its vertical offset on a spring */
  gboolean        jelly;
  double          edge[2], edge_v[2];         /* left, right: px and px/s */
  double          lift, lift_v;               /* vertical offset of the centre */
  double          target_edge[2], target_lift;
  double          heading;                    /* -1 left .. 1 right, smoothed */
  double          last_target_centre;
  gint64          last_time;
  guint           tick;
};

enum {
  PROP_0,
  PROP_ACTIVE,
  PROP_ACTIVE_NAME,
  PROP_N_TOGGLES,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

G_DEFINE_FINAL_TYPE (GlassToggleGroup, glass_toggle_group, GLASS_TYPE_PANEL)

static void append_button (GlassToggleGroup *self, const char *name, GtkWidget *button);
static void jelly_rect (GlassToggleGroup *self, const graphene_rect_t *rest, graphene_rect_t *out);
static void jelly_stop (GlassToggleGroup *self);
static void show_as_active (GlassToggleGroup *self, guint index);

static gboolean
active_rect (GlassToggleGroup *self, graphene_rect_t *out)
{
  GtkWidget *button;

  if (self->active >= self->buttons->len)
    return FALSE;
  button = g_ptr_array_index (self->buttons, self->active);
  return gtk_widget_compute_bounds (button, GTK_WIDGET (self), out);
}

static void
update_plate_tint (GlassToggleGroup *self)
{
  GdkRGBA color;

  gtk_widget_get_color (self->pill_node, &color);
  glass_panel_set_tint (GLASS_PANEL (self->plate), &color);
}

static void
glass_toggle_group_size_allocate (GtkWidget *widget,
                                  int        width,
                                  int        height,
                                  int        baseline)
{
  GlassToggleGroup *self = GLASS_TOGGLE_GROUP (widget);
  graphene_rect_t target, plate;

  GTK_WIDGET_CLASS (glass_toggle_group_parent_class)->size_allocate (widget, width, height, baseline);
  gtk_widget_allocate (self->pill_node, 0, 0, -1, NULL);

  /* The buttons are placed now; the plate follows the active one, from
   * where it was while it slides. */
  if (!active_rect (self, &target))
    {
      gtk_widget_set_child_visible (self->plate, FALSE);
      return;
    }
  if (self->jelly)
    jelly_rect (self, &target, &plate);
  else if (self->have_from && self->progress < 1.0)
    graphene_rect_interpolate (&self->from, &target, self->progress, &plate);
  else
    plate = target;
  self->shown = plate;

  gtk_widget_set_child_visible (self->plate, TRUE);
  /* Whole pixels for the size, the position as it is: the jelly moves by
   * fractions of a pixel. */
  {
    int w = (int) roundf (plate.size.width);
    int h = (int) roundf (plate.size.height);
    float x = plate.origin.x + (plate.size.width - w) / 2.0f;
    float y = plate.origin.y + (plate.size.height - h) / 2.0f;

    gtk_widget_allocate (self->plate, w, h, -1,
                         gsk_transform_translate (NULL, &GRAPHENE_POINT_INIT (x, y)));
  }
}

static void
glass_toggle_group_css_changed (GtkWidget         *widget,
                                GtkCssStyleChange *change)
{
  GTK_WIDGET_CLASS (glass_toggle_group_parent_class)->css_changed (widget, change);
  update_plate_tint (GLASS_TOGGLE_GROUP (widget));
}

static void
glass_toggle_group_root (GtkWidget *widget)
{
  GTK_WIDGET_CLASS (glass_toggle_group_parent_class)->root (widget);
  update_plate_tint (GLASS_TOGGLE_GROUP (widget));
}

static void
glass_toggle_group_unmap (GtkWidget *widget)
{
  GlassToggleGroup *self = GLASS_TOGGLE_GROUP (widget);

  /* The tick would not run; put the marks back where the state is. */
  if (self->dragging || self->jelly)
    {
      jelly_stop (self);
      show_as_active (self, self->active);
      gtk_widget_queue_allocate (widget);
    }
  GTK_WIDGET_CLASS (glass_toggle_group_parent_class)->unmap (widget);
}

static void
animation_value (double value, gpointer data)
{
  GlassToggleGroup *self = data;

  self->progress = value;
  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

static void
mark (GtkWidget *button, gboolean active)
{
  if (active)
    gtk_widget_add_css_class (button, "active");
  else
    gtk_widget_remove_css_class (button, "active");
  gtk_accessible_update_state (GTK_ACCESSIBLE (button),
                               GTK_ACCESSIBLE_STATE_PRESSED,
                               active ? GTK_ACCESSIBLE_TRISTATE_TRUE : GTK_ACCESSIBLE_TRISTATE_FALSE,
                               -1);
}

static void
set_active_internal (GlassToggleGroup *self, guint active, gboolean animate)
{
  if (active >= self->buttons->len || active == self->active)
    return;

  /* A tap (or the app) moves the plate the usual way, from wherever the
   * jelly left it. */
  jelly_stop (self);
  self->have_from = animate && gtk_widget_get_mapped (GTK_WIDGET (self));
  self->from = self->shown;
  if (self->active < self->buttons->len)
    mark (g_ptr_array_index (self->buttons, self->active), FALSE);
  self->active = active;
  self->shown_active = active;
  mark (g_ptr_array_index (self->buttons, active), TRUE);

  if (self->have_from)
    {
      self->progress = 0.0;
      adw_animation_reset (self->animation);
      adw_animation_play (self->animation);
    }
  gtk_widget_queue_allocate (GTK_WIDGET (self));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE_NAME]);
}


/* ── Dragging the plate ────────────────────────────────────────────────────
 *
 * Pressed on the active toggle and moved past DRAG_THRESHOLD, the plate lets
 * go of it and follows the pointer; the toggle nearest the pointer is marked
 * as active meanwhile (its colour only: nothing is notified). On release that
 * toggle becomes active and the plate settles on it. A tap still activates
 * a toggle at once: the drag only claims the press once it moves. */

#define DRAG_THRESHOLD 6.0     /* px */
#define JELLY_OMEGA    36.0    /* rad/s: an edge's spring, with no heading */
#define JELLY_LEAD     0.35    /* the front edge this much stiffer, the back this much softer */
#define JELLY_ZETA     0.45    /* of critical damping: an edge runs on a little past its mark */
#define LIFT_OMEGA     40.0
#define LIFT_ZETA      0.6
#define JELLY_STEP     0.002   /* s: physics substep */

static void
button_bounds (GlassToggleGroup *self,
               guint             index,
               graphene_rect_t  *out)
{
  if (!gtk_widget_compute_bounds (g_ptr_array_index (self->buttons, index), GTK_WIDGET (self), out))
    *out = GRAPHENE_RECT_INIT (0, 0, 0, 0);
}

static int
toggle_at (GlassToggleGroup *self,
           double            x,
           double            y)
{
  for (guint i = 0; i < self->buttons->len; i++)
    {
      graphene_rect_t b;

      button_bounds (self, i, &b);
      if (graphene_rect_contains_point (&b, &GRAPHENE_POINT_INIT ((float) x, (float) y)))
        return (int) i;
    }
  return -1;
}

static guint
nearest_toggle (GlassToggleGroup *self,
                double            centre)
{
  guint best = self->active;
  double best_d = G_MAXDOUBLE;

  for (guint i = 0; i < self->buttons->len; i++)
    {
      graphene_rect_t b;
      double d;

      button_bounds (self, i, &b);
      d = fabs (b.origin.x + b.size.width / 2.0 - centre);
      if (d < best_d)
        {
          best_d = d;
          best = i;
        }
    }
  return best;
}

/* Marks a toggle as active by its colour only (the style class), without
 * touching what is active. */
static void
show_as_active (GlassToggleGroup *self,
                guint             index)
{
  if (index == self->shown_active)
    return;
  if (self->shown_active < self->buttons->len)
    gtk_widget_remove_css_class (g_ptr_array_index (self->buttons, self->shown_active), "active");
  self->shown_active = index;
  if (index < self->buttons->len)
    gtk_widget_add_css_class (g_ptr_array_index (self->buttons, index), "active");
}

/* Past the first and last toggles the plate still gives, less and less. */
static double
rubber_band (double over,
             double width)
{
  double limit = width * 0.35;

  return limit * (1.0 - 1.0 / (over / limit + 1.0));
}

static void
jelly_rect (GlassToggleGroup      *self,
            const graphene_rect_t *rest,
            graphene_rect_t       *out)
{
  double width = self->edge[1] - self->edge[0];
  double squash = CLAMP (rest->size.width / MAX (width, 1.0), 0.84, 1.08);
  double height = rest->size.height * squash;

  *out = GRAPHENE_RECT_INIT ((float) self->edge[0],
                             (float) (rest->origin.y + rest->size.height / 2.0 + self->lift - height / 2.0),
                             (float) width, (float) height);
}

static void
spring (double *x,
        double *v,
        double  target,
        double  omega,
        double  zeta,
        double  dt)
{
  double a = omega * omega * (target - *x) - 2.0 * zeta * omega * *v;

  *v += a * dt;
  *x += *v * dt;
}

static gboolean
jelly_tick (GtkWidget     *widget,
            GdkFrameClock *clock,
            gpointer       data)
{
  GlassToggleGroup *self = GLASS_TOGGLE_GROUP (widget);
  gint64 now = gdk_frame_clock_get_frame_time (clock);
  double elapsed, centre, want, rest_width;
  gboolean settled;

  if (self->last_time == 0)
    self->last_time = now - 16667;
  elapsed = MIN ((now - self->last_time) / 1e6, 0.05);
  self->last_time = now;
  if (elapsed <= 0.0)
    return G_SOURCE_CONTINUE;

  /* Which way the plate is heading: quick to take up, slow to let go, so
   * the front edge stays the stiff one while the plate stops. */
  centre = (self->target_edge[0] + self->target_edge[1]) / 2.0;
  want = tanh ((centre - self->last_target_centre) / elapsed / 700.0);
  self->last_target_centre = centre;
  self->heading += (want - self->heading) *
                   (1.0 - exp (-elapsed / (fabs (want) > fabs (self->heading) ? 0.03 : 0.12)));

  rest_width = self->target_edge[1] - self->target_edge[0];
  for (double t = 0.0; t < elapsed; t += JELLY_STEP)
    {
      double dt = MIN (JELLY_STEP, elapsed - t);

      spring (&self->edge[0], &self->edge_v[0], self->target_edge[0],
              JELLY_OMEGA * (1.0 - JELLY_LEAD * self->heading), JELLY_ZETA, dt);
      spring (&self->edge[1], &self->edge_v[1], self->target_edge[1],
              JELLY_OMEGA * (1.0 + JELLY_LEAD * self->heading), JELLY_ZETA, dt);
      spring (&self->lift, &self->lift_v, self->target_lift, LIFT_OMEGA, LIFT_ZETA, dt);

      /* A plate, not two loose ends: no thinner than 0.6 and no longer than
       * 1.7 of its width at rest. */
      {
        double w = self->edge[1] - self->edge[0];
        double c = (self->edge[0] + self->edge[1]) / 2.0;
        double clamped = CLAMP (w, rest_width * 0.6, rest_width * 1.7);

        if (clamped != w)
          {
            self->edge[0] = c - clamped / 2.0;
            self->edge[1] = c + clamped / 2.0;
          }
      }
    }
  gtk_widget_queue_allocate (widget);

  settled = !self->dragging;
  for (int e = 0; e < 2 && settled; e++)
    settled = fabs (self->edge[e] - self->target_edge[e]) < 0.05 && fabs (self->edge_v[e]) < 2.0;
  settled = settled && fabs (self->lift - self->target_lift) < 0.05 && fabs (self->lift_v) < 2.0;
  if (settled)
    {
      self->jelly = FALSE;
      self->tick = 0;
      return G_SOURCE_REMOVE;
    }
  return G_SOURCE_CONTINUE;
}

static void
jelly_start (GlassToggleGroup *self)
{
  if (!self->jelly)
    {
      graphene_rect_t rest;

      self->edge[0] = self->shown.origin.x;
      self->edge[1] = self->shown.origin.x + self->shown.size.width;
      self->edge_v[0] = self->edge_v[1] = 0.0;
      self->lift = self->lift_v = 0.0;
      if (active_rect (self, &rest))
        self->lift = self->shown.origin.y + self->shown.size.height / 2.0 -
                     (rest.origin.y + rest.size.height / 2.0);
      self->heading = 0.0;
      self->last_target_centre = (self->edge[0] + self->edge[1]) / 2.0;
      self->jelly = TRUE;
    }
  /* The springs take over from a slide in progress. */
  adw_animation_reset (self->animation);
  self->have_from = FALSE;
  self->progress = 1.0;
  self->last_time = 0;
  if (self->tick == 0)
    self->tick = gtk_widget_add_tick_callback (GTK_WIDGET (self), jelly_tick, NULL, NULL);
}

static void
jelly_stop (GlassToggleGroup *self)
{
  if (self->tick)
    gtk_widget_remove_tick_callback (GTK_WIDGET (self), self->tick);
  self->tick = 0;
  self->jelly = FALSE;
  self->dragging = FALSE;
  self->drag_armed = FALSE;
}

/* While dragging: the plate's place at rest is the pointer's. */
static void
jelly_follow_pointer (GlassToggleGroup *self)
{
  graphene_rect_t first, last;
  double width = self->drag_origin.size.width;
  double centre = self->drag_origin.origin.x + width / 2.0 + self->pointer_dx;
  double lo, hi;

  button_bounds (self, 0, &first);
  button_bounds (self, self->buttons->len - 1, &last);
  lo = first.origin.x + first.size.width / 2.0;
  hi = last.origin.x + last.size.width / 2.0;
  if (centre < lo)
    centre = lo - rubber_band (lo - centre, width);
  else if (centre > hi)
    centre = hi + rubber_band (centre - hi, width);

  self->target_edge[0] = centre - width / 2.0;
  self->target_edge[1] = centre + width / 2.0;
  self->target_lift = copysign (MIN (fabs (self->pointer_dy) * 0.12, 3.0), self->pointer_dy);
}

/* After the drag: the plate settles on a toggle. */
static void
jelly_settle_on (GlassToggleGroup *self,
                 guint             index)
{
  graphene_rect_t b;

  button_bounds (self, index, &b);
  self->target_edge[0] = b.origin.x;
  self->target_edge[1] = b.origin.x + b.size.width;
  self->target_lift = 0.0;
}

static void
drag_begin (GtkGestureDrag   *gesture,
            double            x,
            double            y,
            GlassToggleGroup *self)
{
  int index = toggle_at (self, x, y);

  self->dragging = FALSE;
  self->drag_armed = index >= 0 && (guint) index == self->active && self->buttons->len > 1;
  if (!self->drag_armed)
    gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_DENIED);
}

static void
drag_update (GtkGestureDrag   *gesture,
             double            dx,
             double            dy,
             GlassToggleGroup *self)
{
  if (!self->drag_armed)
    return;
  if (!self->dragging)
    {
      if (hypot (dx, dy) < DRAG_THRESHOLD)
        return;
      /* Ours now: the toggle under the press will not be clicked. */
      gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
      if (!active_rect (self, &self->drag_origin))
        return;
      self->dragging = TRUE;
      jelly_start (self);
    }
  self->pointer_dx = dx;
  self->pointer_dy = dy;
  jelly_follow_pointer (self);
  show_as_active (self, nearest_toggle (self, (self->target_edge[0] + self->target_edge[1]) / 2.0));
}

static void
drag_finish (GlassToggleGroup *self,
             gboolean          commit)
{
  guint to;

  if (!self->dragging)
    {
      self->drag_armed = FALSE;
      return;
    }
  to = commit ? nearest_toggle (self, (self->target_edge[0] + self->target_edge[1]) / 2.0) : self->active;
  self->dragging = FALSE;
  self->drag_armed = FALSE;
  jelly_settle_on (self, to);
  show_as_active (self, to);

  if (to != self->active)
    {
      mark (g_ptr_array_index (self->buttons, self->active), FALSE);
      self->active = to;
      mark (g_ptr_array_index (self->buttons, to), TRUE);
      self->shown_active = to;
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE]);
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE_NAME]);
    }
}

static void
drag_end (GtkGestureDrag   *gesture,
          double            dx,
          double            dy,
          GlassToggleGroup *self)
{
  drag_finish (self, TRUE);
}

static void
drag_cancel (GtkGesture       *gesture,
             GdkEventSequence *sequence,
             GlassToggleGroup *self)
{
  drag_finish (self, FALSE);
}

static void
button_clicked (GtkButton        *button,
                GlassToggleGroup *self)
{
  guint index;

  if (g_ptr_array_find (self->buttons, button, &index))
    set_active_internal (self, index, TRUE);
}

/* The plate lies under the buttons, so it never sees the press: the group
 * watches it (in the capture phase, without taking it) for the plate. */
static gboolean
press_event (GtkEventControllerLegacy *controller,
             GdkEvent                 *event,
             GlassToggleGroup         *self)
{
  switch ((int) gdk_event_get_event_type (event))
    {
    case GDK_BUTTON_PRESS:
    case GDK_TOUCH_BEGIN:
      glass_panel_set_pressed (GLASS_PANEL (self->plate), TRUE);
      break;
    case GDK_BUTTON_RELEASE:
    case GDK_TOUCH_END:
    case GDK_TOUCH_CANCEL:
    case GDK_GRAB_BROKEN:
      glass_panel_set_pressed (GLASS_PANEL (self->plate), FALSE);
      break;
    default:
      break;
    }
  return GDK_EVENT_PROPAGATE;
}

static void
glass_toggle_group_dispose (GObject *object)
{
  GlassToggleGroup *self = GLASS_TOGGLE_GROUP (object);

  jelly_stop (self);
  g_clear_object (&self->animation);
  g_clear_pointer (&self->pill_node, gtk_widget_unparent);
  g_clear_pointer (&self->plate, gtk_widget_unparent);

  G_OBJECT_CLASS (glass_toggle_group_parent_class)->dispose (object);
}

static void
glass_toggle_group_finalize (GObject *object)
{
  GlassToggleGroup *self = GLASS_TOGGLE_GROUP (object);

  g_ptr_array_unref (self->buttons);
  g_ptr_array_unref (self->names);

  G_OBJECT_CLASS (glass_toggle_group_parent_class)->finalize (object);
}

static void
glass_toggle_group_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GlassToggleGroup *self = GLASS_TOGGLE_GROUP (object);

  switch (prop_id)
    {
    case PROP_ACTIVE:
      g_value_set_uint (value, self->active);
      break;
    case PROP_ACTIVE_NAME:
      g_value_set_string (value, glass_toggle_group_get_active_name (self));
      break;
    case PROP_N_TOGGLES:
      g_value_set_uint (value, self->buttons->len);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_toggle_group_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  GlassToggleGroup *self = GLASS_TOGGLE_GROUP (object);

  switch (prop_id)
    {
    case PROP_ACTIVE:
      glass_toggle_group_set_active (self, g_value_get_uint (value));
      break;
    case PROP_ACTIVE_NAME:
      glass_toggle_group_set_active_name (self, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_toggle_group_class_init (GlassToggleGroupClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = glass_toggle_group_dispose;
  object_class->finalize = glass_toggle_group_finalize;
  object_class->get_property = glass_toggle_group_get_property;
  object_class->set_property = glass_toggle_group_set_property;

  widget_class->size_allocate = glass_toggle_group_size_allocate;
  widget_class->css_changed = glass_toggle_group_css_changed;
  widget_class->root = glass_toggle_group_root;
  widget_class->unmap = glass_toggle_group_unmap;

  /**
   * GlassToggleGroup:active:
   *
   * The index of the active toggle.
   */
  props[PROP_ACTIVE] =
    g_param_spec_uint ("active", NULL, NULL, 0, G_MAXUINT, 0,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassToggleGroup:active-name:
   *
   * The name of the active toggle.
   */
  props[PROP_ACTIVE_NAME] =
    g_param_spec_string ("active-name", NULL, NULL, NULL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassToggleGroup:n-toggles:
   *
   * How many toggles there are.
   */
  props[PROP_N_TOGGLES] =
    g_param_spec_uint ("n-toggles", NULL, NULL, 0, G_MAXUINT, 0,
                       G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
glass_toggle_group_init (GlassToggleGroup *self)
{
  AdwAnimationTarget *target;
  GtkEventController *press;

  self->buttons = g_ptr_array_new ();
  self->names = g_ptr_array_new_with_free_func (g_free);
  self->progress = 1.0;

  /* Each toggle is drawn inside a pill, like the plate under it. */
  self->box = glass_pill_box_new ();
  glass_pill_box_set_homogeneous (GLASS_PILL_BOX (self->box), TRUE);
  glass_panel_set_child (GLASS_PANEL (self), self->box);
  glass_panel_add_own_class (GLASS_PANEL (self), "toggle-group");

  /* The plate: glass on our glass, under the buttons (drawn first). */
  self->plate = glass_panel_new ();
  gtk_widget_add_css_class (self->plate, "toggle-plate");
  glass_panel_set_has_shadow (GLASS_PANEL (self->plate), FALSE);
  glass_panel_set_adaptive (GLASS_PANEL (self->plate), GLASS_ADAPTIVE_MODE_OFF);
  glass_panel_set_press_grow (GLASS_PANEL (self->plate), 12.0, 0.2);
  gtk_widget_set_can_target (self->plate, FALSE);
  gtk_widget_insert_before (self->plate, GTK_WIDGET (self), self->box);

  /* Visible (an invisible node's style is not kept up to date) but never
   * drawn: it resolves the plate's tint from CSS. */
  self->pill_node = glass_style_node_new ("pill");
  gtk_widget_set_parent (self->pill_node, GTK_WIDGET (self));

  /* Dragging the plate: in the capture phase, so it sees the press before
   * the toggle under it, and claims it only once the pointer moves. */
  self->drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (self->drag), GDK_BUTTON_PRIMARY);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (self->drag), GTK_PHASE_CAPTURE);
  g_signal_connect (self->drag, "drag-begin", G_CALLBACK (drag_begin), self);
  g_signal_connect (self->drag, "drag-update", G_CALLBACK (drag_update), self);
  g_signal_connect (self->drag, "drag-end", G_CALLBACK (drag_end), self);
  g_signal_connect (self->drag, "cancel", G_CALLBACK (drag_cancel), self);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (self->drag));

  press = gtk_event_controller_legacy_new ();
  gtk_event_controller_set_propagation_phase (press, GTK_PHASE_CAPTURE);
  g_signal_connect (press, "event", G_CALLBACK (press_event), self);
  gtk_widget_add_controller (GTK_WIDGET (self), press);

  target = adw_callback_animation_target_new (animation_value, self, NULL);
  self->animation = adw_timed_animation_new (GTK_WIDGET (self), 0.0, 1.0, 280, target);
  adw_timed_animation_set_easing (ADW_TIMED_ANIMATION (self->animation), ADW_EASE_OUT_CUBIC);
}

/**
 * glass_toggle_group_new:
 *
 * Creates a new toggle group.
 *
 * Returns: a new toggle group
 */
GtkWidget *
glass_toggle_group_new (void)
{
  return g_object_new (GLASS_TYPE_TOGGLE_GROUP, NULL);
}

/**
 * glass_toggle_group_append:
 * @self: a toggle group
 * @name: (nullable): the toggle's name
 * @label: (nullable): its text
 * @icon_name: (nullable): its icon, used when there is no @label
 *
 * Adds a toggle at the end. The first one added is active.
 */
void
glass_toggle_group_append (GlassToggleGroup *self,
                           const char       *name,
                           const char       *label,
                           const char       *icon_name)
{
  GtkWidget *button;

  g_return_if_fail (GLASS_IS_TOGGLE_GROUP (self));
  g_return_if_fail (label != NULL || icon_name != NULL);

  button = label ? gtk_button_new_with_label (label) : gtk_button_new_from_icon_name (icon_name);
  if (!label && name)
    gtk_widget_set_tooltip_text (button, name);
  append_button (self, name, button);
}

/* Private (glass-private.h): a toggle showing @content (a tab's icon and
 * title). */
GtkWidget *
glass_toggle_group_append_item (GlassToggleGroup *self,
                                const char       *name,
                                GtkWidget        *content)
{
  GtkWidget *button = gtk_button_new ();

  gtk_button_set_child (GTK_BUTTON (button), content);
  append_button (self, name, button);
  return button;
}

/**
 * glass_toggle_group_remove:
 * @self: a toggle group
 * @index: the index of the toggle to remove
 *
 * Removes a toggle. If it was the active one, the toggle that takes its
 * place (or the one before it, if it was the last) becomes active.
 */
void
glass_toggle_group_remove (GlassToggleGroup *self,
                           guint             index)
{
  GtkWidget *button;
  guint old_active;

  g_return_if_fail (GLASS_IS_TOGGLE_GROUP (self));
  g_return_if_fail (index < self->buttons->len);

  jelly_stop (self);
  show_as_active (self, self->active);
  old_active = self->active;
  button = g_ptr_array_steal_index (self->buttons, index);
  g_signal_handlers_disconnect_by_func (button, button_clicked, self);
  glass_pill_box_remove (GLASS_PILL_BOX (self->box), button);
  g_ptr_array_remove_index (self->names, index);

  if (index < old_active)
    self->active--;
  else if (index == old_active)
    {
      /* The plate jumps: it has nowhere to slide from. */
      self->active = MIN (index, self->buttons->len > 0 ? self->buttons->len - 1 : 0);
      self->have_from = FALSE;
      if (self->active < self->buttons->len)
        mark (g_ptr_array_index (self->buttons, self->active), TRUE);
    }
  self->shown_active = self->active;
  gtk_widget_queue_allocate (GTK_WIDGET (self));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_N_TOGGLES]);
  /* Also when the index stays: another toggle is active now. */
  if (self->active != old_active || index == old_active)
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE]);
  if (index == old_active)
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE_NAME]);
}

/**
 * glass_toggle_group_remove_all:
 * @self: a toggle group
 *
 * Removes every toggle.
 */
void
glass_toggle_group_remove_all (GlassToggleGroup *self)
{
  guint old_active;

  g_return_if_fail (GLASS_IS_TOGGLE_GROUP (self));

  if (self->buttons->len == 0)
    return;
  jelly_stop (self);
  old_active = self->active;
  while (self->buttons->len > 0)
    {
      GtkWidget *button = g_ptr_array_steal_index (self->buttons, self->buttons->len - 1);

      g_signal_handlers_disconnect_by_func (button, button_clicked, self);
      glass_pill_box_remove (GLASS_PILL_BOX (self->box), button);
      g_ptr_array_remove_index (self->names, self->names->len - 1);
    }
  self->active = 0;
  self->shown_active = 0;
  self->have_from = FALSE;
  gtk_widget_queue_allocate (GTK_WIDGET (self));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_N_TOGGLES]);
  if (old_active != 0)
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE_NAME]);
}

/**
 * glass_toggle_group_get_tooltip:
 * @self: a toggle group
 * @index: the index of a toggle
 *
 * Gets a toggle's tooltip.
 *
 * Returns: (nullable): the toggle's tooltip
 */
const char *
glass_toggle_group_get_tooltip (GlassToggleGroup *self,
                                guint             index)
{
  g_return_val_if_fail (GLASS_IS_TOGGLE_GROUP (self), NULL);
  g_return_val_if_fail (index < self->buttons->len, NULL);

  return gtk_widget_get_tooltip_text (g_ptr_array_index (self->buttons, index));
}

/**
 * glass_toggle_group_set_tooltip:
 * @self: a toggle group
 * @index: the index of a toggle
 * @tooltip: (nullable): the tooltip, or %NULL for none
 *
 * Sets a toggle's tooltip. A toggle with an icon and no label has its name
 * as its tooltip until one is set.
 */
void
glass_toggle_group_set_tooltip (GlassToggleGroup *self,
                                guint             index,
                                const char       *tooltip)
{
  g_return_if_fail (GLASS_IS_TOGGLE_GROUP (self));
  g_return_if_fail (index < self->buttons->len);

  gtk_widget_set_tooltip_text (g_ptr_array_index (self->buttons, index), tooltip);
}

static void
append_button (GlassToggleGroup *self,
               const char       *name,
               GtkWidget        *button)
{
  /* Frameless: a theme can paint framed buttons from USER priority, which
   * would put a pill under every toggle at rest (docs/memo.md 地雷27). */
  gtk_button_set_has_frame (GTK_BUTTON (button), FALSE);
  gtk_widget_add_css_class (button, "toggle");
  mark (button, self->buttons->len == 0);

  g_signal_connect_object (button, "clicked", G_CALLBACK (button_clicked), self, 0);
  glass_pill_box_append (GLASS_PILL_BOX (self->box), button);
  g_ptr_array_add (self->buttons, button);
  g_ptr_array_add (self->names, g_strdup (name));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_N_TOGGLES]);
  if (self->buttons->len == 1)
    {
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE]);
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE_NAME]);
    }
}

/**
 * glass_toggle_group_get_n_toggles:
 * @self: a toggle group
 *
 * Gets how many toggles there are.
 *
 * Returns: how many toggles there are
 */
guint
glass_toggle_group_get_n_toggles (GlassToggleGroup *self)
{
  g_return_val_if_fail (GLASS_IS_TOGGLE_GROUP (self), 0);

  return self->buttons->len;
}

/**
 * glass_toggle_group_get_active:
 * @self: a toggle group
 *
 * Gets the index of the active toggle.
 *
 * Returns: the index of the active toggle
 */
guint
glass_toggle_group_get_active (GlassToggleGroup *self)
{
  g_return_val_if_fail (GLASS_IS_TOGGLE_GROUP (self), 0);

  return self->active;
}

/**
 * glass_toggle_group_set_active:
 * @self: a toggle group
 * @active: the index of the toggle to activate
 *
 * Activates a toggle.
 */
void
glass_toggle_group_set_active (GlassToggleGroup *self,
                               guint             active)
{
  g_return_if_fail (GLASS_IS_TOGGLE_GROUP (self));

  set_active_internal (self, active, TRUE);
}

/**
 * glass_toggle_group_get_active_name:
 * @self: a toggle group
 *
 * Gets the name of the active toggle.
 *
 * Returns: (nullable): the name of the active toggle
 */
const char *
glass_toggle_group_get_active_name (GlassToggleGroup *self)
{
  g_return_val_if_fail (GLASS_IS_TOGGLE_GROUP (self), NULL);

  return self->active < self->names->len ? g_ptr_array_index (self->names, self->active) : NULL;
}

/**
 * glass_toggle_group_set_active_name:
 * @self: a toggle group
 * @name: the name of the toggle to activate
 *
 * Activates a toggle by name.
 */
void
glass_toggle_group_set_active_name (GlassToggleGroup *self,
                                    const char       *name)
{
  g_return_if_fail (GLASS_IS_TOGGLE_GROUP (self));

  for (guint i = 0; i < self->names->len; i++)
    if (g_strcmp0 (g_ptr_array_index (self->names, i), name) == 0)
      {
        set_active_internal (self, i, TRUE);
        return;
      }
}
