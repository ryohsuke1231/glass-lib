/* glass-context.h — library-wide settings (design.md §6.1, §11.1).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#if !defined(GLASS_INSIDE) && !defined(GLASS_COMPILATION)
#error "Only <glass.h> can be included directly."
#endif

#include <gtk/gtk.h>

#include "glass-enums.h"

G_BEGIN_DECLS

#define GLASS_TYPE_CONTEXT (glass_context_get_type ())
G_DECLARE_FINAL_TYPE (GlassContext, glass_context, GLASS, CONTEXT, GObject)

GlassContext       *glass_context_get_default              (void);

GlassRendererMode   glass_context_get_renderer             (GlassContext *self);
void                glass_context_set_renderer             (GlassContext      *self,
                                                            GlassRendererMode  mode);

gboolean            glass_context_get_reduce_transparency  (GlassContext *self);
void                glass_context_set_reduce_transparency  (GlassContext *self,
                                                            gboolean      reduce);

gboolean            glass_context_set_param                (GlassContext *self,
                                                            const char   *key,
                                                            double        value);
double              glass_context_get_param                (GlassContext *self,
                                                            const char   *key);
gboolean            glass_context_is_param_set             (GlassContext *self,
                                                            const char   *key);
double              glass_context_get_effective_param      (GlassContext  *self,
                                                            GlassMaterial  material,
                                                            const char    *key);
void                glass_context_reset_param              (GlassContext *self,
                                                            const char   *key);
const char * const *glass_context_list_params              (GlassContext *self);
gboolean            glass_context_get_param_range          (GlassContext *self,
                                                            const char   *key,
                                                            double       *min,
                                                            double       *max,
                                                            double       *default_value);

G_END_DECLS
