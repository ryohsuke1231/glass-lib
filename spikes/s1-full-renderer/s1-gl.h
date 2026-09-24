/* s1-gl.h — GL helpers for the S1 spike.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <epoxy/gl.h>
#include <glib.h>

G_BEGIN_DECLS

#define S1_GAUSS_MAX_ENTRIES 40

/* A linear-sampling Gaussian kernel: entry 0 is the centre tap, every other
 * entry is one bilinear fetch that stands in for two adjacent taps and is
 * applied on both sides. */
typedef struct {
  int    fetch_pairs;
  double sigma;                          /* sigma the weights were built for, in texels */
  double scale;                          /* kernel_scale uniform: wanted sigma / sigma */
  int    n;                              /* used entries of offsets[] / weights[] */
  double offsets[S1_GAUSS_MAX_ENTRIES];
  double weights[S1_GAUSS_MAX_ENTRIES];
} S1GaussKernel;

gboolean s1_gauss_kernel_for_radius (double         radius_px,
                                     int            downscale,
                                     S1GaussKernel *out);

char    *s1_gauss_fragment_source   (const S1GaussKernel *kernel,
                                     gboolean             horizontal);

GLuint   s1_gl_program_new          (gboolean            use_es,
                                     const char         *vertex_source,
                                     const char * const *fragment_parts,
                                     GError            **error);

G_END_DECLS
