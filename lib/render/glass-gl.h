/* glass-gl.h — GL helpers of the full renderer (private).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <epoxy/gl.h>
#include <glib.h>

G_BEGIN_DECLS

#define GLASS_GAUSS_MAX_ENTRIES 40

/* A linear-sampling Gaussian kernel: entry 0 is the centre tap, every other
 * entry is one bilinear fetch that stands in for two adjacent taps and is
 * applied on both sides. */
typedef struct {
  int    fetch_pairs;
  double sigma;                          /* sigma the weights were built for, in texels */
  double scale;                          /* kernel_scale uniform: wanted sigma / sigma */
  int    n;                              /* used entries of offsets[] / weights[] */
  double offsets[GLASS_GAUSS_MAX_ENTRIES];
  double weights[GLASS_GAUSS_MAX_ENTRIES];
} GlassGaussKernel;

gboolean glass_gauss_kernel_for_radius (double            radius_px,
                                        int               downscale,
                                        GlassGaussKernel *out);

char    *glass_gauss_fragment_source   (const GlassGaussKernel *kernel,
                                        gboolean                horizontal);

GLuint   glass_gl_program_new          (gboolean            use_es,
                                        const char         *vertex_source,
                                        const char * const *fragment_parts,
                                        GError            **error);

char    *glass_gl_load_shader          (const char *name);

G_END_DECLS
