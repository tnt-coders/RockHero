$input v_color0, v_texcoord0, v_texcoord1

// The accent light: one continuous falloff around the subject's own silhouette, evaluated per
// fragment from a signed distance field.
//
// Why a field rather than more geometry. The three shapes this replaces were all built from
// stacked quads, and every one of them failed the same way. Flat-alpha stages are a STAIRCASE, not
// a falloff, and the eye manufactures a band at every step. Per-side linear ramps have no radial
// term at all, so where two of them meet at a corner they disagree about what "distance from the
// subject" means and leave a hard cut with nothing to soften it. And the head's rim redrew the
// head's own ART on a larger quad, which samples INWARD as it draws OUTWARD — so it brightened
// toward its outer edge, the exact inverse of light, and re-added the atlas's achromatic G lift
// that no colour setting could suppress.
//
// A single scalar d(p) has none of those failure modes available to it: it is defined everywhere,
// its level sets wrap corners as arcs for free, and the colour is the vertex colour alone.
#include <bgfx_shader.sh>

// x = outward reach in world units; y = inward reach (frames only, so the light lands ON the bar
// without spilling into the see-through interior); z = falloff exponent; w = unused.
uniform vec4 u_accent_glow_params;

void main()
{
    vec2 p = abs(v_texcoord0);
    vec2 b = v_texcoord1.xy;
    float corner = v_texcoord1.z;
    float shape = v_texcoord1.w;

    // Rounded box, the standard formulation: correct for the fretted head and for a chord box's
    // outer boundary, and it degenerates to a capsule for the open string's bar when the corner
    // radius equals the half thickness.
    vec2 q = p - b + vec2_splat(corner);
    float d_box = min(max(q.x, q.y), 0.0) + length(max(q, vec2_splat(0.0))) - corner;

    // Rhombus, for the node head: its base is a DIAMOND, and a rounded-box glow around it would
    // visibly detach along the diamond's flats.
    float d_rhombus = (dot(p, vec2(b.y, b.x)) - (b.x * b.y)) / max(length(b), 1.0e-4);

    float d = mix(d_box, d_rhombus, step(0.5, shape));

    // Outside the silhouette the light falls off across the reach; inside, a frame's light holds
    // over its own bar and then stops before the interior. A subject with no inward reach lights
    // its whole inside, which is what a head wants — its own art covers that region anyway.
    float outward = saturate(1.0 - (max(d, 0.0) / max(u_accent_glow_params.x, 1.0e-4)));
    float inward = saturate(1.0 + (min(d, 0.0) / max(u_accent_glow_params.y, 1.0e-4)));

    // Raised to an exponent rather than smoothstepped. smoothstep flattens at BOTH ends, which
    // would kill the gradient exactly where it must be steepest — a glow's apparent edge is where
    // its gradient peaks, so a light that eases in at the silhouette stops belonging to it. This
    // curve peaks at the edge and the exponent sweeps tight-and-hot through wide-and-soft on one
    // knob, which is the whole candidate table collapsed into a number.
    float intensity = pow(outward * inward, max(u_accent_glow_params.z, 1.0e-4));

    // Premultiplied: the blend mode is a sighting axis (additive clips per channel and desaturates
    // toward white; screen is bounded by the source colour so it converges on the STRING'S colour
    // instead), and premultiplied RGB is the one form every one of those modes reads correctly.
    gl_FragColor = vec4(v_color0.rgb * (v_color0.a * intensity), v_color0.a * intensity);
}
