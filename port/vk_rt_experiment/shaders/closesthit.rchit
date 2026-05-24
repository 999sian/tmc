/* port/vk_rt_experiment/shaders/closesthit.rchit
 *
 * Closest-hit shader. Runs when a ray hits triangle geometry.
 *
 * Responsibilities:
 *   1. Reconstruct the hit position and per-vertex attributes (UV,
 *      emissive flag) by interpolating with the built-in barycentrics.
 *   2. Sample the diffuse atlas at the interpolated UV.
 *   3. If the surface is emissive, treat it as a light source and
 *      add its colour to the payload directly.
 *   4. Otherwise, cast a shadow ray from the hit toward a virtual
 *      sun position and modulate the diffuse colour by visibility.
 *
 * Lighting model: simplified retro-modern — flat-shaded with soft
 * shadows. No Lambertian falloff because the GBA's pixel art was
 * pre-shaded by the original artists; cosine-falloff would
 * darken everything indiscriminately. We instead emit the diffuse
 * colour scaled by ambient (when shadowed) or 1.0 (when lit).
 */
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

/* Must match raygen.rgen's HitPayload exactly. */
struct HitPayload {
    vec3 colour;
    float distance;
};

/* Shadow-ray payload — distinct from primary because we only need
 * "did we hit anything" not a colour. */
struct ShadowPayload {
    bool hit;
};

layout(location = 0) rayPayloadInEXT HitPayload payload;
layout(location = 1) rayPayloadEXT   ShadowPayload shadowPayload;

/* Built-in barycentric attribs for the hit triangle. */
hitAttributeEXT vec2 attribs;

/* Vertex layout MUST match RenderLayerManager::Vertex (24 bytes):
 *   position (3 floats) → 12 bytes
 *   uv       (2 floats) →  8 bytes
 *   emissive (1 float)  →  4 bytes
 * std430 layout because storage-buffer reads default to it. */
struct Vertex {
    float px, py, pz;
    float u, v;
    float emissive;
};

/* Bindings match RayTracingPipeline::createDescriptorLayout. */
layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 2, std430) readonly buffer Vertices { Vertex v[]; } verts;
layout(set = 0, binding = 3, std430) readonly buffer Indices  { uint   i[]; } idx;
layout(set = 0, binding = 4) uniform texture2D atlasTextures[];
layout(set = 0, binding = 5) uniform sampler   atlasSampler;

layout(push_constant) uniform PushConstants {
    vec4 params;  /* x=frameIndex, y=time, z=exposure */
} pc;

/* Virtual sun — fixed position high above the playfield. Direction
 * pointed back toward this from the hit gives the shadow-ray
 * vector. Z is negative so the sun is on the camera side; soft
 * shadows would require multiple jittered samples (skipped here
 * for the scaffold — single shadow ray = hard shadow). */
const vec3 kSunPosition = vec3(80.0, 30.0, -2.0);
const vec3 kSunColour   = vec3(1.0, 0.95, 0.85);
const vec3 kAmbient     = vec3(0.25, 0.27, 0.32);  /* slight cool tint when shadowed */

void main() {
    /* Reconstruct the three vertex indices for the hit triangle. */
    const uint i0 = idx.i[3 * gl_PrimitiveID + 0];
    const uint i1 = idx.i[3 * gl_PrimitiveID + 1];
    const uint i2 = idx.i[3 * gl_PrimitiveID + 2];

    Vertex v0 = verts.v[i0];
    Vertex v1 = verts.v[i1];
    Vertex v2 = verts.v[i2];

    /* attribs = (b1, b2); barycentric b0 = 1 - b1 - b2. */
    const vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

    const vec2 uv = bary.x * vec2(v0.u, v0.v)
                  + bary.y * vec2(v1.u, v1.v)
                  + bary.z * vec2(v2.u, v2.v);

    const float emissive = bary.x * v0.emissive
                         + bary.y * v1.emissive
                         + bary.z * v2.emissive;

    const vec3 hitPos = bary.x * vec3(v0.px, v0.py, v0.pz)
                      + bary.y * vec3(v1.px, v1.py, v1.pz)
                      + bary.z * vec3(v2.px, v2.py, v2.pz);

    /* Sample the diffuse atlas. The first (and only, for now)
     * sampled-image array element holds the world's diffuse atlas. */
    const vec4 diffuse = texture(sampler2D(atlasTextures[0], atlasSampler), uv);

    /* Discard fully-transparent texels (e.g. sprite halos) by
     * marking them as miss-equivalent — payload stays zero. The
     * RT pipeline doesn't support a true "transparent hit + continue",
     * but the alpha-cutoff path here is enough for the scaffold;
     * production would use any-hit + ignoreIntersectionEXT. */
    if (diffuse.a < 0.01) {
        payload.colour = vec3(0.0);
        payload.distance = -1.0;
        return;
    }

    /* Emissive surface — radiance equals the diffuse colour, no
     * shadow ray needed. Light orbs / lava tiles get this. */
    if (emissive > 0.5) {
        payload.colour = diffuse.rgb;
        payload.distance = gl_HitTEXT;
        return;
    }

    /* Shadow ray: from hit toward the sun. If anything occludes,
     * the surface is shadowed and gets ambient only. We use a
     * dedicated miss shader for the shadow ray (miss → hit=false). */
    const vec3 toSun = normalize(kSunPosition - hitPos);
    shadowPayload.hit = true;  /* assume occluded unless miss writes false */
    traceRayEXT(
        topLevelAS,
        gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
        0xFF,
        0,
        0,
        1,                       /* shadow-ray miss index */
        hitPos + toSun * 0.01,   /* nudge to avoid self-hit */
        0.0,
        toSun,
        length(kSunPosition - hitPos),
        1                        /* shadowPayload location */
    );

    const vec3 lit = shadowPayload.hit ? kAmbient : kSunColour;
    payload.colour = diffuse.rgb * lit;
    payload.distance = gl_HitTEXT;
}
