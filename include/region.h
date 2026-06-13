#ifndef REGION_H
#define REGION_H

/*
 * region.h — region identity for the multi-region PC port.
 *
 * The Minish Cap differs by region (USA / EU / JP) in gameplay logic, data, and
 * text. Historically those differences are compile-time `#ifdef EU` / `#ifdef JP`
 * blocks, which forces one binary per region. This header lets the SAME source
 * serve two masters:
 *
 *   - DEFAULT (the byte-matching GBA ROM build, or a single-region PC build):
 *     REGION_IS_* are COMPILE-TIME CONSTANTS picked by the -DUSA/-DEU/-DJP define.
 *     Dead branches fold away and the decomp still byte-matches the retail ROM.
 *
 *   - PC_PORT + MULTI_REGION (the fat binary): REGION_IS_* read the runtime
 *     `gActiveRegion`, set from the loaded ROM at boot. One binary plays any ROM.
 *
 * Convert a decomp site mechanically, preprocessor -> C control flow:
 *
 *     #ifdef EU            ->    if (REGION_IS_EU) {
 *         A                          A
 *     #else                ->    } else {
 *         B                          B
 *     #endif               ->    }
 *
 *     #if defined(JP) || defined(EU)   ->   if (REGION_IS_JP || REGION_IS_EU)
 *     #ifndef EU                       ->   if (!REGION_IS_EU)
 *
 * In MULTI_REGION BOTH branches are compiled, so any symbol referenced by a
 * non-active branch must exist for all regions — version-exclusive functions
 * (e.g. AreaAllowsWarp, USA-only) get un-#ifdef'd definitions so they always link.
 *
 * This header is force-included by the build, so it is available everywhere
 * without per-file #includes. In DEFAULT mode it expands to nothing but macros,
 * so it cannot perturb the matching build.
 */

#define TMC_REGION_USA 0
#define TMC_REGION_EU  1
#define TMC_REGION_JP  2

#if defined(PC_PORT) && defined(MULTI_REGION)

#ifdef __cplusplus
extern "C" {
#endif
/* One of TMC_REGION_*. Set once at ROM load (Port_Region_SetActive), from the
 * detected ROM region. Read by the REGION_IS_* macros below. */
extern int gActiveRegion;
#ifdef __cplusplus
}
#endif

#define REGION_IS_USA (gActiveRegion == TMC_REGION_USA)
#define REGION_IS_EU  (gActiveRegion == TMC_REGION_EU)
#define REGION_IS_JP  (gActiveRegion == TMC_REGION_JP)

#else /* compile-time constant — preserves byte-matching */

#if defined(EU)
#define REGION_IS_USA 0
#define REGION_IS_EU  1
#define REGION_IS_JP  0
#elif defined(JP)
#define REGION_IS_USA 0
#define REGION_IS_EU  0
#define REGION_IS_JP  1
#else /* USA / DEMO_USA / default */
#define REGION_IS_USA 1
#define REGION_IS_EU  0
#define REGION_IS_JP  0
#endif

#endif

#endif /* REGION_H */
