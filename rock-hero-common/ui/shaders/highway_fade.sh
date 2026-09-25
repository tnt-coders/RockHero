#ifndef ROCK_HERO_HIGHWAY_FADE_SH
#define ROCK_HERO_HIGHWAY_FADE_SH

// The board's distance fades on world Z, the one statement every highway program reads. Both
// bands ride u_fade_params as an edge and an inverse length, so the zero vector means "no fade" —
// the value the renderer arms for the pixel-space overlay, which shares these programs.
//   .x  near band: the z from which floor furniture is fully opaque
//   .y  near band: 1 / its length (the band ends fully transparent one length nearer the line)
//   .z  far band: the z where content entering at the far edge becomes fully opaque
//   .w  far band: 1 / its length (the band ends fully transparent at the far edge)
// Evaluated per fragment on the interpolated world Z, so a single quad spanning the whole board
// fades only across the band rather than along its full length.
uniform vec4 u_fade_params;

// Floor furniture's fade toward the hit line; only the floor-furniture program applies it.
float highwayNearFade(float world_z)
{
    return saturate(1.0 - ((u_fade_params.x - world_z) * u_fade_params.y));
}

// Everything's fade-in at the far edge, applied by every program.
float highwayFarFade(float world_z)
{
    return saturate(1.0 - ((world_z - u_fade_params.z) * u_fade_params.w));
}

#endif // ROCK_HERO_HIGHWAY_FADE_SH
