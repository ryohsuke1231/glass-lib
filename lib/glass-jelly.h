/* glass-jelly.h — a rectangle hung on springs (private; design.md §6.9).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* How a jelly moves. Each edge follows its mark on a spring of its own; the
 * edge in front is `lead` stiffer and the one behind `lead` softer, so the
 * rectangle stretches while it moves and the front runs on past the mark
 * when it stops, then the whole gathers itself. */
typedef struct {
  double omega;          /* rad/s: an edge's spring with no heading */
  double zeta;           /* of critical damping: below 1 the edges run on a little */
  double lead;
  double min_stretch;    /* along the motion, the size stays within these */
  double max_stretch;    /* ... times the mark's */
} GlassJellySpec;

/* The toggle group's plate while it is dragged, and the knobs (design.md
 * §6.6): stiff, lively. */
extern const GlassJellySpec glass_jelly_plate;
/* The plate sent to a toggle by a tap or by the app (design.md §6.6): it
 * glides there, with no stretch and no overshoot. */
extern const GlassJellySpec glass_jelly_glide;
/* Glass travelling to its place (morphs, knobs; design.md §6.8, §6.9):
 * softer, so the stretch reads, and less lively. */
extern const GlassJellySpec glass_jelly_travel;

typedef struct {
  GlassJellySpec spec;
  gboolean       active;
  gboolean       axis[2];        /* which axes move (x, y); the others keep the mark */
  double         edge[4];        /* left, top, right, bottom: px */
  double         vel[4];         /* px/s */
  double         mark[4];
  double         heading[2];     /* -1 .. 1 per axis, smoothed */
  double         last_centre[2];
  gint64         last_time;      /* µs; 0 = not stepped yet */
} GlassJelly;

/* Starts at @from with @from as its mark, along the axes asked for. */
void     glass_jelly_start      (GlassJelly            *self,
                                 const GlassJellySpec  *spec,
                                 const graphene_rect_t *from,
                                 gboolean               along_x,
                                 gboolean               along_y);
void     glass_jelly_stop       (GlassJelly            *self);
/* Where it is heading; the heading follows how fast the mark moves. */
void     glass_jelly_set_mark   (GlassJelly            *self,
                                 const graphene_rect_t *mark);
/* Puts every edge on its mark, at rest (less motion: no springs). */
void     glass_jelly_snap       (GlassJelly            *self);
/* Runs the springs to @now (µs, a frame clock's time). TRUE while it has
 * not come to rest at its mark. */
gboolean glass_jelly_step       (GlassJelly            *self,
                                 gint64                 now);
void     glass_jelly_get        (const GlassJelly      *self,
                                 graphene_rect_t       *out);
/* How far it has come from @from to the mark, 0..1 (by the largest edge
 * distance; past the mark it is 1). */
double   glass_jelly_progress   (const GlassJelly      *self,
                                 const graphene_rect_t *from);

/* Motion settings (design.md §13): whether animations are off altogether
 * (gtk-enable-animations), and whether the user asked for less motion
 * (gtk-interface-reduced-motion, GTK 4.22): no stretching, no overshoot,
 * no swelling; fades stay. */
gboolean glass_animations_enabled (GtkWidget *widget);
gboolean glass_motion_reduced     (GtkWidget *widget);

G_END_DECLS
