/* glass-jelly.c — a rectangle hung on springs (design.md §6.9).
 *
 * The toggle group's plate started it (design.md §6.6): not pinned to the
 * pointer but hung from it, one spring per edge, the edge in front stiffer.
 * The same physics carries morphs and knobs, so all moving glass behaves as
 * one material.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-jelly.h"

#include <adwaita.h>
#include <math.h>

#define JELLY_STEP 0.002   /* s: physics substep */

/* The plate's, as the user tuned them (2026-09-28): omega 48, 0.45 of
 * critical damping, front 35% stiffer and back 35% softer, 0.6 to 1.7 of
 * its width. */
const GlassJellySpec glass_jelly_plate = { 48.0, 0.45, 0.35, 0.6, 1.7 };

/* A tap: what the plate does then is not the user's hand, and a bounce
 * only draws the eye (the user, 2026-09-28). Critically damped, the edges
 * together: 90% of the way in 0.15 s, like the slide of v0.9 (280 ms, ease
 * out), and no further. */
const GlassJellySpec glass_jelly_glide = { 26.0, 1.0, 0.0, 0.6, 1.8 };

/* Travelling glass: liquid_glass_widgets' morph spring (stiffness 120,
 * damping 16, from its iOS 26 captures: omega 11, 0.73 of critical) is the
 * body's; here it is a little stiffer, as the edges add their own lag. */
const GlassJellySpec glass_jelly_travel = { 16.0, 0.62, 0.35, 0.6, 1.8 };

void
glass_jelly_start (GlassJelly            *self,
                   const GlassJellySpec  *spec,
                   const graphene_rect_t *from,
                   gboolean               along_x,
                   gboolean               along_y)
{
  self->spec = *spec;
  self->active = TRUE;
  self->axis[0] = along_x;
  self->axis[1] = along_y;
  self->edge[0] = self->mark[0] = from->origin.x;
  self->edge[1] = self->mark[1] = from->origin.y;
  self->edge[2] = self->mark[2] = from->origin.x + from->size.width;
  self->edge[3] = self->mark[3] = from->origin.y + from->size.height;
  for (int i = 0; i < 4; i++)
    self->vel[i] = 0.0;
  for (int a = 0; a < 2; a++)
    {
      self->heading[a] = 0.0;
      self->last_centre[a] = (self->mark[a] + self->mark[a + 2]) / 2.0;
    }
  self->last_time = 0;
}

void
glass_jelly_stop (GlassJelly *self)
{
  self->active = FALSE;
}

void
glass_jelly_set_mark (GlassJelly            *self,
                      const graphene_rect_t *mark)
{
  self->mark[0] = mark->origin.x;
  self->mark[1] = mark->origin.y;
  self->mark[2] = mark->origin.x + mark->size.width;
  self->mark[3] = mark->origin.y + mark->size.height;
}

void
glass_jelly_snap (GlassJelly *self)
{
  for (int e = 0; e < 4; e++)
    {
      self->edge[e] = self->mark[e];
      self->vel[e] = 0.0;
    }
  for (int a = 0; a < 2; a++)
    {
      self->heading[a] = 0.0;
      self->last_centre[a] = (self->mark[a] + self->mark[a + 2]) / 2.0;
    }
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

gboolean
glass_jelly_step (GlassJelly *self,
                  gint64      now)
{
  double elapsed;
  gboolean moving = FALSE;

  if (!self->active)
    return FALSE;
  if (self->last_time == 0)
    self->last_time = now - 16667;
  elapsed = MIN ((now - self->last_time) / 1e6, 0.05);
  self->last_time = now;
  if (elapsed <= 0.0)
    return TRUE;

  for (int a = 0; a < 2; a++)
    {
      int lo = a, hi = a + 2;
      double centre, want, rest;

      if (!self->axis[a])
        {
          /* This axis keeps the mark. */
          self->edge[lo] = self->mark[lo];
          self->edge[hi] = self->mark[hi];
          self->vel[lo] = self->vel[hi] = 0.0;
          continue;
        }

      /* Which way the mark is heading: quick to take up, slow to let go, so
       * the front edge stays the stiff one while the glass stops. */
      centre = (self->mark[lo] + self->mark[hi]) / 2.0;
      want = tanh ((centre - self->last_centre[a]) / elapsed / 700.0);
      self->last_centre[a] = centre;
      self->heading[a] += (want - self->heading[a]) *
                          (1.0 - exp (-elapsed / (fabs (want) > fabs (self->heading[a]) ? 0.03 : 0.12)));

      rest = self->mark[hi] - self->mark[lo];
      for (double t = 0.0; t < elapsed; t += JELLY_STEP)
        {
          double dt = MIN (JELLY_STEP, elapsed - t);
          double w, c, clamped;

          spring (&self->edge[lo], &self->vel[lo], self->mark[lo],
                  self->spec.omega * (1.0 - self->spec.lead * self->heading[a]), self->spec.zeta, dt);
          spring (&self->edge[hi], &self->vel[hi], self->mark[hi],
                  self->spec.omega * (1.0 + self->spec.lead * self->heading[a]), self->spec.zeta, dt);

          /* One piece of glass, not two loose edges. */
          w = self->edge[hi] - self->edge[lo];
          c = (self->edge[lo] + self->edge[hi]) / 2.0;
          clamped = CLAMP (w, MAX (rest * self->spec.min_stretch, 1.0), MAX (rest * self->spec.max_stretch, 1.0));
          if (clamped != w)
            {
              self->edge[lo] = c - clamped / 2.0;
              self->edge[hi] = c + clamped / 2.0;
            }
        }

      for (int e = lo; e <= hi; e += 2)
        if (fabs (self->edge[e] - self->mark[e]) >= 0.05 || fabs (self->vel[e]) >= 2.0)
          moving = TRUE;
    }

  if (!moving)
    {
      for (int e = 0; e < 4; e++)
        {
          self->edge[e] = self->mark[e];
          self->vel[e] = 0.0;
        }
    }
  return moving;
}

void
glass_jelly_get (const GlassJelly *self,
                 graphene_rect_t  *out)
{
  *out = GRAPHENE_RECT_INIT ((float) self->edge[0], (float) self->edge[1],
                             (float) MAX (self->edge[2] - self->edge[0], 1.0),
                             (float) MAX (self->edge[3] - self->edge[1], 1.0));
}

double
glass_jelly_progress (const GlassJelly      *self,
                      const graphene_rect_t *from)
{
  double start[4] = {
    from->origin.x, from->origin.y,
    from->origin.x + from->size.width, from->origin.y + from->size.height,
  };
  double total = 0.0, left = 0.0;

  for (int e = 0; e < 4; e++)
    {
      total = MAX (total, fabs (self->mark[e] - start[e]));
      left = MAX (left, fabs (self->mark[e] - self->edge[e]));
    }
  if (total < 0.5)
    return 1.0;
  return CLAMP (1.0 - left / total, 0.0, 1.0);
}

gboolean
glass_animations_enabled (GtkWidget *widget)
{
  return adw_get_enable_animations (widget);
}

gboolean
glass_motion_reduced (GtkWidget *widget)
{
  GtkSettings *settings = gtk_widget_get_settings (widget);
  GtkReducedMotion reduced = GTK_REDUCED_MOTION_NO_PREFERENCE;

  if (!adw_get_enable_animations (widget))
    return TRUE;
  if (settings)
    g_object_get (settings, "gtk-interface-reduced-motion", &reduced, NULL);
  return reduced == GTK_REDUCED_MOTION_REDUCE;
}
