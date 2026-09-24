// Compatibility prelude: lets the Cogl-snippet-style reference shader
// (shaders/reference/glass.frag) compile as a standalone GLSL ES 3.00 /
// GLSL 3.30 core fragment shader, without touching a line of it.
//
// The loader prepends the #version line (and the precision statements), then
// this file, then glass.frag, then cogl_postlude.glsl.
//
// What Cogl normally provides and how it is stood in for here:
//   texture2D()            -> texture() (GLSL 1.30+ name)
//   cogl_tex_coord_in[0]   -> a plain global, set by the wrapper main() in
//                             cogl_postlude.glsl before each evaluation
//   cogl_color_out         -> a plain global, read back by that wrapper
//   cogl_color_in          -> constant white: the extension draws its composite
//                             quad with an opaque white pipeline colour, so the
//                             shader's final "* cogl_color_in" is an identity
//   cogl_sampler0/1        -> declared by glass.frag itself (plain uniforms)
//
// glass.frag's own main() is renamed so the wrapper can evaluate it several
// times per pixel (supersampling) - every sample is exactly the reference
// shader, only at a different sub-pixel position.

#define texture2D texture

in vec4 v_tex_coord;
out vec4 glass_frag_color;

vec4 cogl_tex_coord_in[1];
vec4 cogl_color_out;

#define cogl_color_in vec4(1.0)

#define main glass_reference_main
