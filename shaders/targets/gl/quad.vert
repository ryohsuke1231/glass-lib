// A quad that covers the whole render target, without any vertex buffer:
// draw 4 vertices as GL_TRIANGLE_STRIP and gl_VertexID picks the corner.
//
// Orientation: row 0 of every render target is the TOP of the image. That is
// how GdkGLTexture presents a texture and how uploaded memory images are laid
// out, so nothing downstream ever needs a flip. The texture coordinate grows
// downwards, matching the Cogl coordinates glass.frag was written against.

uniform vec4 u_uv_rect;           // u0, v0 (top), u1, v1 (bottom)

out vec4 v_tex_coord;

void main()
{
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
    v_tex_coord = vec4(mix(u_uv_rect.xy, u_uv_rect.zw, corner), 0.0, 1.0);
}
