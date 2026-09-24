/* s1-gl.c — GL helpers for the S1 spike.
 *
 * SPDX-License-Identifier: MIT
 */
#include "s1-gl.h"

#include <gio/gio.h>
#include <math.h>

/* Discrete Gaussian weights for i = 0..2*fetch_pairs, normalised, then merged
 * pairwise into bilinear fetches. A line-by-line port of
 * LiquidEffect._computeGaussianKernel() in the extension, so both blur the
 * backdrop identically. */
static void
compute_kernel (double sigma, int fetch_pairs, S1GaussKernel *out)
{
  double raw[2 * S1_GAUSS_MAX_ENTRIES + 2];
  int side_taps = MAX (2, fetch_pairs * 2);
  double sum = 0.0;

  g_assert (fetch_pairs + 1 <= S1_GAUSS_MAX_ENTRIES);

  for (int i = 0; i <= side_taps; i++)
    {
      double w = exp (-(double) (i * i) / (2.0 * sigma * sigma));
      raw[i] = w;
      sum += (i == 0) ? w : w * 2.0;
    }
  for (int i = 0; i <= side_taps; i++)
    raw[i] /= sum;

  out->offsets[0] = 0.0;
  out->weights[0] = raw[0];
  out->n = 1;

  for (int p = 0; p < fetch_pairs; p++)
    {
      int i = p * 2 + 1;
      int j = i + 1;
      double w0 = raw[i];
      double w1 = (j <= side_taps) ? raw[j] : 0.0;
      double w_sum = w0 + w1;

      out->offsets[out->n] = w_sum > 0.0 ? (i * w0 + j * w1) / w_sum : i;
      out->weights[out->n] = w_sum;
      out->n++;
    }

  out->fetch_pairs = fetch_pairs;
  out->sigma = sigma;
}

/* Blur radius (px of the full-resolution backdrop) to kernel, following
 * LiquidEffect._setGaussianBlurRadius(): the radius is a sigma in original
 * pixels, converted to texels of the downscaled capture, capped at 15 texels
 * and floored at 1 texel for the kernel's shape so a tiny radius still
 * absorbs the downscale's aliasing (kernel_scale < 1 then just pulls the
 * taps in).
 *
 * One difference, by construction: the extension keeps the kernel it
 * compiled earlier while the fetch count stays the same, so its exact shape
 * can depend on the radius history. Here the kernel is always built fresh,
 * which is what the extension does at start-up. */
gboolean
s1_gauss_kernel_for_radius (double radius_px, int downscale, S1GaussKernel *out)
{
  const double res_scale = downscale >= 4 ? 4.0 : 2.0;
  const double max_sigma_texel = 15.0;
  const double min_sigma_texel = 1.0;
  double sigma_texel, kernel_sigma;
  int side_taps, fetch_pairs;

  if (radius_px <= 0.0)
    return FALSE;

  sigma_texel = MIN (radius_px / res_scale, max_sigma_texel);
  kernel_sigma = MAX (sigma_texel, min_sigma_texel);
  side_taps = MAX (2, (int) ceil (kernel_sigma * 4.0));
  fetch_pairs = MAX (2, (int) ceil (side_taps / 2.0));

  compute_kernel (kernel_sigma, fetch_pairs, out);
  out->scale = sigma_texel / kernel_sigma;

  return TRUE;
}

/* Numbers go through g_ascii_formatd(): printf's %f follows LC_NUMERIC, and
 * a "0,5" in the source would not compile. */
static const char *
fmt (char *buf, gsize len, double v)
{
  return g_ascii_formatd (buf, len, "%.8f", v);
}

char *
s1_gauss_fragment_source (const S1GaussKernel *kernel, gboolean horizontal)
{
  GString *s = g_string_new (NULL);
  char a[G_ASCII_DTOSTR_BUF_SIZE], b[G_ASCII_DTOSTR_BUF_SIZE];

  g_string_append (s,
                   "uniform sampler2D u_src;\n"
                   "uniform vec2 inv_size;      /* 1/width, 1/height of the source */\n"
                   "uniform float kernel_scale; /* wanted sigma / kernel sigma */\n"
                   "in vec4 v_tex_coord;\n"
                   "out vec4 frag_color;\n"
                   "void main() {\n"
                   "  vec2 uv = v_tex_coord.st;\n");
  g_string_append_printf (s, "  vec4 col = texture(u_src, uv) * %s;\n",
                          fmt (a, sizeof a, kernel->weights[0]));

  for (int i = 1; i < kernel->n; i++)
    {
      const char *off = fmt (a, sizeof a, kernel->offsets[i]);
      const char *w = fmt (b, sizeof b, kernel->weights[i]);

      if (horizontal)
        {
          g_string_append_printf (s, "  col += texture(u_src, uv + vec2(%s * kernel_scale * inv_size.x, 0.0)) * %s;\n", off, w);
          g_string_append_printf (s, "  col += texture(u_src, uv - vec2(%s * kernel_scale * inv_size.x, 0.0)) * %s;\n", off, w);
        }
      else
        {
          g_string_append_printf (s, "  col += texture(u_src, uv + vec2(0.0, %s * kernel_scale * inv_size.y)) * %s;\n", off, w);
          g_string_append_printf (s, "  col += texture(u_src, uv - vec2(0.0, %s * kernel_scale * inv_size.y)) * %s;\n", off, w);
        }
    }

  g_string_append (s, "  frag_color = col;\n}\n");

  return g_string_free (s, FALSE);
}

static GLuint
compile_shader (GLenum type, const char * const *parts, int n_parts, GError **error)
{
  GLuint shader = glCreateShader (type);
  GLint ok = GL_FALSE;

  glShaderSource (shader, n_parts, (const GLchar * const *) parts, NULL);
  glCompileShader (shader);
  glGetShaderiv (shader, GL_COMPILE_STATUS, &ok);

  if (!ok)
    {
      GLint len = 0;
      char *log;

      glGetShaderiv (shader, GL_INFO_LOG_LENGTH, &len);
      log = g_malloc0 (MAX (len, 1) + 1);
      glGetShaderInfoLog (shader, len, NULL, log);
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s shader: %s",
                   type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
      g_free (log);
      glDeleteShader (shader);
      return 0;
    }

  return shader;
}

GLuint
s1_gl_program_new (gboolean            use_es,
                   const char         *vertex_source,
                   const char * const *fragment_parts,
                   GError            **error)
{
  const char *header = use_es
    ? "#version 300 es\nprecision highp float;\nprecision highp int;\n"
    : "#version 330 core\n";
  const char *vparts[2] = { header, vertex_source };
  GPtrArray *fparts = g_ptr_array_new ();
  GLuint vs, fs, program;
  GLint ok = GL_FALSE;

  g_ptr_array_add (fparts, (gpointer) header);
  for (int i = 0; fragment_parts[i] != NULL; i++)
    g_ptr_array_add (fparts, (gpointer) fragment_parts[i]);

  vs = compile_shader (GL_VERTEX_SHADER, vparts, 2, error);
  if (vs == 0)
    {
      g_ptr_array_free (fparts, TRUE);
      return 0;
    }

  fs = compile_shader (GL_FRAGMENT_SHADER, (const char * const *) fparts->pdata, fparts->len, error);
  g_ptr_array_free (fparts, TRUE);
  if (fs == 0)
    {
      glDeleteShader (vs);
      return 0;
    }

  program = glCreateProgram ();
  glAttachShader (program, vs);
  glAttachShader (program, fs);
  glLinkProgram (program);
  glDetachShader (program, vs);
  glDetachShader (program, fs);
  glDeleteShader (vs);
  glDeleteShader (fs);

  glGetProgramiv (program, GL_LINK_STATUS, &ok);
  if (!ok)
    {
      GLint len = 0;
      char *log;

      glGetProgramiv (program, GL_INFO_LOG_LENGTH, &len);
      log = g_malloc0 (MAX (len, 1) + 1);
      glGetProgramInfoLog (program, len, NULL, log);
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "link: %s", log);
      g_free (log);
      glDeleteProgram (program);
      return 0;
    }

  return program;
}
