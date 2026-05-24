/* port/vk_rt_experiment/shaders/miss.rmiss
 *
 * Miss shader. Two purposes:
 *   - Primary miss (rgen ray): payload colour stays the clear/sky
 *     colour. The ray-gen reset payload.colour to vec3(0.0) before
 *     traceRayEXT, so we can leave it as black, or paint a sky here.
 *   - Shadow-ray miss (sbtRecordOffset=1 in traceRayEXT): the shadow
 *     payload's "hit" flag is cleared, meaning the surface IS lit.
 *
 * The SBT only contains one miss group. The "miss index" passed to
 * traceRayEXT controls which path runs; closest-hit uses missIndex=1
 * for shadow rays, raygen uses missIndex=0 for primaries.
 *
 * For now both paths land here; the function disambiguates by
 * checking which payload is bound (the language doesn't give us a
 * stage discriminator, so we conservatively write both payloads —
 * each call site only declared the relevant one). The unused payload
 * write is dead code from the GPU's point of view.
 */
#version 460
#extension GL_EXT_ray_tracing : require

struct HitPayload {
    vec3 colour;
    float distance;
};
struct ShadowPayload {
    bool hit;
};

layout(location = 0) rayPayloadInEXT HitPayload    payload;
layout(location = 1) rayPayloadInEXT ShadowPayload shadowPayload;

void main() {
    /* Primary-ray miss: paint sky colour. A light steel-blue
     * matches the GBA's typical out-of-bounds clear. */
    payload.colour = vec3(0.42, 0.55, 0.72);
    payload.distance = -1.0;

    /* Shadow-ray miss: nothing blocked the sun. */
    shadowPayload.hit = false;
}
