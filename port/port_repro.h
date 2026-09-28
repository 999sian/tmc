#pragma once
/*
 * port_repro.h — declarations for the headless auto-repro harnesses.
 *
 * Each Port_ReproXxx_Tick is defined in its matching port_repro_xxx.c and
 * self-gates on a TMC_REPRO_* environment variable (a no-op when unset).
 * They are driven once per frame from Port_UpdateInput() in port_bios.c.
 *
 * Owning these declarations here (instead of inline `extern`s at the call
 * site) keeps the call site free of forward decls and lets each definer
 * include this header so the compiler checks the definition against it.
 *
 * The harnesses are only compiled with the xmake option `repro_harness`
 * (TMC_REPRO_HARNESS; default on in debug, off in release). Without it the
 * port_repro_*.c files are left out of the build and every entry point
 * below collapses to an empty inline, so call sites need no #ifdefs.
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef TMC_REPRO_HARNESS
void Port_ReproRando_Tick(unsigned int frame);
void Port_ReproRando_LateTick(void);
void Port_ReproPerfcap_Tick(unsigned int frame);
void Port_ReproA11y_Tick(unsigned int frame);
void Port_ReproRoomCap_Tick(unsigned int frame);
void Port_ReproRollMacro_Tick(unsigned int frame);
void Port_ReproNpcTalk_Tick(unsigned int frame);
void Port_ReproItemGet_Tick(unsigned int frame);
#else
static inline void Port_ReproRando_Tick(unsigned int frame) { (void)frame; }
static inline void Port_ReproRando_LateTick(void) {}
static inline void Port_ReproPerfcap_Tick(unsigned int frame) { (void)frame; }
static inline void Port_ReproA11y_Tick(unsigned int frame) { (void)frame; }
static inline void Port_ReproRoomCap_Tick(unsigned int frame) { (void)frame; }
static inline void Port_ReproRollMacro_Tick(unsigned int frame) { (void)frame; }
static inline void Port_ReproNpcTalk_Tick(unsigned int frame) { (void)frame; }
static inline void Port_ReproItemGet_Tick(unsigned int frame) { (void)frame; }
#endif

#ifdef __cplusplus
}
#endif
