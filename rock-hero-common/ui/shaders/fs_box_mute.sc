$input v_color0, v_texcoord0

// SDF-rendered mute X for repeat chord boxes that reproduces the note heads' mute CELL ART at
// box scale (user rule: the boxes must look like the single-note textures, fitted properly).
// Each arm is a squared-cap oriented box, like the authored glyph's arms; the cross-section is
// the cells' measured two-zone profile — a thin outer zone (the palm X's bright rim / the full
// X's body) around an inner zone (dark body / pale core) — plus the art's soft outer halo.
//
// The distance field is EVALUATED here rather than sampled from a baked texture: the X's arm
// angle changes with every box aspect, so a bitmap field would be distorted by exactly the
// stretch this replaces. Distances live in box-local world units (the vertex stage passes
// world-unit offsets through texcoord), so zone widths hold constant on every box, and
// screen-space derivative antialiasing keeps every boundary crisp at any zoom.
#include <bgfx_shader.sh>

// x, y = box half extents (world); z = arm stroke half width; w = inner-zone inset from the
// arm boundary (all world units).
uniform vec4 u_box_mute_params;

// xy = arm 1 unit direction (arm 2 mirrors y); z = arm half length; w = halo falloff width.
uniform vec4 u_box_mute_arms;

// Inner-zone color (the palm X's dark body / the full X's pale core); a = halo strength.
uniform vec4 u_box_mute_fill;

// Outer-zone color (the palm X's bright rim / the full X's body); a = mark opacity.
uniform vec4 u_box_mute_edge;

// Exact distance to one squared-cap arm: an oriented box of half length u_box_mute_arms.z and
// half width u_box_mute_params.z along the given unit direction, centered on the box center.
float sdArm(vec2 p, vec2 direction)
{
    vec2 local = vec2(dot(p, direction), dot(p, vec2(-direction.y, direction.x)));
    vec2 q = abs(local) - vec2(u_box_mute_arms.z, u_box_mute_params.z);
    return length(max(q, vec2_splat(0.0))) + min(max(q.x, q.y), 0.0);
}

void main()
{
    vec2 p = v_texcoord0;
    vec2 half_extents = u_box_mute_params.xy;
    float inner_inset = u_box_mute_params.w;
    float halo_width = max(u_box_mute_arms.w, 1.0e-4);

    // The X is the union of the two arms, kept strictly inside the box by the box's own SDF.
    float d_arm = min(sdArm(p, u_box_mute_arms.xy),
                      sdArm(p, vec2(u_box_mute_arms.x, -u_box_mute_arms.y)));
    vec2 box_q = abs(p) - half_extents;
    float d_box = length(max(box_q, vec2_splat(0.0))) + min(max(box_q.x, box_q.y), 0.0);
    float d = max(d_arm, d_box);

    // Two-zone cross-section with screen-space antialiased boundaries, exactly the cells'
    // structure: the inner zone takes over one inset inside the arm boundary.
    float aa = max(fwidth(d), 1.0e-5);
    float shape = 1.0 - smoothstep(-aa, aa, d);
    float inner = 1.0 - smoothstep(-inner_inset - aa, -inner_inset + aa, d);
    vec3 zone_rgb = mix(u_box_mute_edge.rgb, u_box_mute_fill.rgb, inner);
    float shape_alpha = u_box_mute_edge.a * shape;

    // The art's soft halo: the outer-zone color falling off outside the shape.
    float halo_alpha =
        u_box_mute_fill.a * exp(-max(d, 0.0) / halo_width) * (1.0 - shape);

    float out_alpha = shape_alpha + (halo_alpha * (1.0 - shape_alpha));
    vec3 out_rgb = ((zone_rgb * shape_alpha) +
                    (u_box_mute_edge.rgb * halo_alpha * (1.0 - shape_alpha))) /
                   max(out_alpha, 1.0e-5);
    gl_FragColor = vec4(out_rgb, out_alpha) * v_color0;
}
