// glass_core.glsl — the glass shader core, assembled.
//
// A target includes this file after defining GLASS_SAMPLE(uv) (a vec4 read
// of the blurred backdrop at a texture coordinate), then calls
// glass_shade(uv) from its own main(). See shaders/targets/gl/glass.frag.
//
// #include is expanded at build time by tools/glsl-include.py.

#include "glass_params.glsl"
#include "glass_shape.glsl"
#include "glass_surface.glsl"
#include "glass_optics.glsl"
#include "glass_shade.glsl"
