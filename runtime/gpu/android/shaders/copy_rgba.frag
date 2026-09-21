#version 450
layout(set = 0, binding = 0) uniform sampler2D source_texture;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 output_color;
layout(push_constant) uniform Conversion { uint swap_red_blue; } conversion;
void main() {
    vec4 value = texture(source_texture, uv);
    output_color = conversion.swap_red_blue != 0u ? value.bgra : value;
}
