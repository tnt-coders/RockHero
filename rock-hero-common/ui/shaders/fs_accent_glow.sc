$input v_color0, v_texcoord0, v_texcoord1

#include <bgfx_shader.sh>

// The accent light: a glow whose brightness comes from a SIGNED DISTANCE to the lit object's own
// silhouette, evaluated per fragment.
//
// This replaces stacked flat-alpha quads, and the reason is not tidiness. A stack of quads can
// only step, and the eye reads a step as an EDGE - which is why the previous light looked "boxy"
// and why the chord box's top cap met its side ramps in a hard cut: four independent linear ramps
// have no radial term, so out past a corner the cap carried full alpha where the sides had already
// decayed to zero. A distance field has no corners to disagree about. It is one continuous
// function of position, so the falloff is identical in every direction by construction and there
// is nothing left to seam.
//
// v_texcoord0.xy  fragment offset from the subject's centre, world units, pre-rotation
// v_texcoord1.xy  the subject's half extents
// v_texcoord1.z   corner radius (a capsule is radius == the smaller half extent)
// v_texcoord1.w   which field: 0 = rounded box, 1 = rhombus
//
// The shape rides the VERTEX rather than a uniform so heads, open bars and chord boxes - three
// different silhouettes at three different sizes - still batch into a single draw.
uniform vec4 u_accent_glow_params; // x = reach (world), y = falloff exponent, zw unused

void main()
{
    vec2 p = abs(v_texcoord0.xy);
    vec2 b = v_texcoord1.xy;
    float corner = v_texcoord1.z;

    // Rounded box (iq): inflate by the corner radius, measure to the inflated box, deflate.
    vec2 q = p - b + vec2_splat(corner);
    float d_box = min(max(q.x, q.y), 0.0) + length(max(q, vec2_splat(0.0))) - corner;

    // Rhombus |x|/a + |y|/b = 1, normalised to an approximate euclidean distance. Good to well
    // under a texel near the boundary, which is the only place the falloff reads.
    float d_rhombus = ((dot(p, vec2(b.y, b.x)) - (b.x * b.y)) / max(length(b), 1.0e-4));

    float d = mix(d_box, d_rhombus, step(0.5, v_texcoord1.w));

    // One reach, spent in BOTH directions from the boundary: light does not know which side of an
    // emitter it is on. Outside it is the halo; inside it is what makes a thin frame bar read as
    // emitting rather than merely being brighter. Whatever falls under opaque art is occluded, and
    // that is fine - it costs nothing and needs no second number to agree with the first.
    float reach = max(u_accent_glow_params.x, 1.0e-4);
    float ridge = saturate(1.0 - (abs(d) / reach));

    // The exponent is the falloff SHAPE. At 1.0 the ramp is linear, which reads as a gradient
    // rather than as light; above it the core tightens and the toe lengthens, which is how a real
    // falloff distributes its energy. Applied to the ramp rather than to distance so the light
    // still peaks exactly on the silhouette's edge.
    float intensity = pow(ridge, max(u_accent_glow_params.y, 1.0e-4));

    // PREMULTIPLIED on purpose. The candidate table cycles the blend operator (add / screen /
    // lighten) and all three read premultiplied source the same way, so switching operators
    // compares the operators and nothing else.
    float weight = v_color0.a * intensity;
    gl_FragColor = vec4(v_color0.rgb * weight, weight);
}
