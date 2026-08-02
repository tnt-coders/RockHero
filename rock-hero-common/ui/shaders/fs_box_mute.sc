$input v_color0, v_texcoord0

// Repeat-box mute mark: lays the chords.png art out along correctly-angled arms. The texture
// is the single source of truth for the look — at load the renderer measures each mark's
// cross-section (colors, rim/core structure, halo, all as painted) into a tiny ramp this
// shader samples by exact distance from the arm centerline. The distance field is EVALUATED
// per fragment in box-local world units because the X's arm angle changes with every box
// aspect — a shape no fixed bitmap contains — so the art's line weights hold exactly on every
// box while its colors come straight from the authored pixels.
#include <bgfx_shader.sh>

SAMPLER2D(s_atlas, 0);

// x, y = glyph rect half extents (world) — the rect the arms are clipped to; the caller
// sizes it against the panel: inset so the falloff completes inside it (full mute) or
// outset so the quad cuts the mark raw at the panel edge (palm mute); z = arm stroke half
// width; w = ramp extent — the painted cross-section's reach from the arm centerline (all
// world units).
uniform vec4 u_box_mute_params;

// xy = arm 1 unit direction (arm 2 mirrors y); z = the arms' horizontal half extent; w = the
// mark's ramp row.
uniform vec4 u_box_mute_arms;

// Distance to one arm: a stripe of constant perpendicular width around a centerline through
// the box center, cut vertically at the arms' horizontal extent. The glyph rect clip in
// main() supplies the matching horizontal cut, so each tip lands as an axis-aligned corner —
// the note art's squared tip shape — with the rim wrapping around both cut edges.
float sdArm(vec2 p, vec2 direction)
{
    float stripe = abs(dot(p, vec2(-direction.y, direction.x))) - u_box_mute_params.z;
    return max(stripe, abs(p.x) - u_box_mute_arms.z);
}

void main()
{
    vec2 p = v_texcoord0;
    vec2 half_extents = u_box_mute_params.xy;
    float stroke_half_width = u_box_mute_params.z;
    float ramp_extent = max(u_box_mute_params.w, 1.0e-4);

    // The X is the union of the two arms, clipped to the glyph rect; the ramp's falloff
    // continues past the rect and either dissolves inside the panel (full mute) or is cut
    // by the quad at its edge (palm mute).
    float d_arm = min(sdArm(p, u_box_mute_arms.xy),
                      sdArm(p, vec2(u_box_mute_arms.x, -u_box_mute_arms.y)));
    vec2 rect_q = abs(p) - half_extents;
    float d_rect = length(max(rect_q, vec2_splat(0.0))) + min(max(rect_q.x, rect_q.y), 0.0);
    float d = max(d_arm, d_rect);

    // Distance from the arm centerline selects the painted cross-section sample; the ramp's
    // tail is transparent by construction, so clamping dissolves everything past the art.
    float u = clamp((stroke_half_width + d) / ramp_extent, 0.0, 1.0);
    vec4 art = texture2D(s_atlas, vec2(u, u_box_mute_arms.w));
    gl_FragColor = art * v_color0;
}
