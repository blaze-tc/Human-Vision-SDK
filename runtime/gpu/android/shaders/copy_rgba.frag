#version 450
layout(set = 0, binding = 0) uniform sampler2D source_texture;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 output_color;
layout(push_constant) uniform Conversion { uint flags; } conversion;
void main() {
    // The render area's viewport/scissor origin is (0,0). Integer fragment
    // coordinates select the exact source texel for a same-extent copy.
    vec4 value = (conversion.flags & 2u) != 0u
        ? texelFetch(source_texture, ivec2(gl_FragCoord.xy), 0)
        : texture(source_texture, uv);
    output_color = (conversion.flags & 1u) != 0u ? value.bgra : value;
}
