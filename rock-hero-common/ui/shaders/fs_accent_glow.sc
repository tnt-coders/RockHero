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
// x = reach (world), y = falloff exponent, z = emitter depth (world), w = radiance gain
uniform vec4 u_accent_glow_params;

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

    // A BACK LIGHT, not a rim. The distinction is the whole look and it lives in these four lines.
    //
    // The emitter is a REGION, and the light falls off with distance from that region - not from
    // the silhouette's boundary. `depth` says how far into the subject the emitter reaches, which
    // is the one number that separates a solid object from a frame:
    //
    //   solid (a note head, an open string's bar) - depth past the shape's own inradius, so the
    //       WHOLE interior emits and the only falloff is outward. What you see is a lamp behind
    //       the object, because that is literally the field being described.
    //   frame (a chord box) - depth equal to the frame's thickness, so only the band between the
    //       outer edge and one thickness in emits, and the light spills both ways from it. The
    //       interior stays dark, which is what keeps a box readable THROUGH.
    //
    // The previous field peaked on the boundary and fell off both ways from it, which is a rim by
    // construction. A note head hid its own inner half and so still read as light; a bar 0.1 world
    // thick did not, and showed as two bright lines tracing its outline - a border drawn around
    // the string rather than a light behind it. Both are the same field; only the depth differs.
    float reach = max(u_accent_glow_params.x, 1.0e-4);
    float outward = max(d, 0.0);
    float inward = max(-d - u_accent_glow_params.z, 0.0);
    float from_emitter = max(outward, inward);

    // The exponent is the falloff SHAPE. At 1.0 the ramp is linear, which reads as a gradient
    // rather than as light; above it the core tightens and the toe lengthens, which is how a real
    // falloff distributes its energy. Applied to the ramp rather than to distance so the light
    // still holds full strength across the emitter itself.
    float intensity =
        pow(saturate(1.0 - (from_emitter / reach)), max(u_accent_glow_params.y, 1.0e-4));

    float weight = v_color0.a * intensity;

    // The emitter's radiance is allowed OVER one and then clipped per channel. Those two lines
    // are where "it looks bright" comes from, and the mechanism is worth stating because the
    // obvious alternative is wrong in a way that is hard to see.
    //
    // A bright coloured light has a WHITE-HOT CORE inside a coloured halo - every photograph of a
    // neon sign or a taillight at night shows it. That is not a stylisation: it is what happens
    // when radiance exceeds what the display (or the eye) can represent. The brightest channel
    // clips first and stops rising, while the others keep climbing, so the colour walks toward
    // white exactly where the light is strongest and keeps its hue everywhere it is not.
    //
    // Blending the colour toward white by a constant - the previous design - cannot produce that.
    // It desaturates the FAR halo as hard as the core, which is what made the light read as
    // washed out rather than bright, and it adds no radiance at all: mixing toward white at a
    // fixed weight can only trade saturation for lightness, never exceed the emitter's own
    // brightness. A gain does both, with the falloff deciding where along the ramp each fragment
    // sits. Gain 1.0 reproduces the un-gained light exactly.
    //
    // The pedestal that lets a pure hue reach white at all is applied on the CPU, where the
    // colour is packed - see g_glow_spectral_floor. Without it a string like the red one, whose
    // green and blue are literally zero, would clip its red channel and simply stop, getting no
    // brighter and never desaturating.
    //
    // The result is PREMULTIPLIED, which is what all three blend operators expect.
    vec3 lit = min(v_color0.rgb * (u_accent_glow_params.w * weight), vec3_splat(1.0));
    gl_FragColor = vec4(lit, weight);
}
