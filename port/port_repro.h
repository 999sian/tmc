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
 */

#ifdef __cplusplus
extern "C" {
#endif

void Port_ReproRando_Tick(unsigned int frame);
void Port_ReproRando_LateTick(void);
void Port_ReproMazaal_Tick(unsigned int frame);
void Port_ReproLitArea_Tick(unsigned int frame);
void Port_ReproCloneButton_Tick(unsigned int frame);
void Port_ReproTakeover_Tick(unsigned int frame);
void Port_ReproJailBars_Tick(unsigned int frame);
void Port_ReproAngryStatue_Tick(unsigned int frame);
void Port_ReproVaati_Tick(unsigned int frame);
void Port_ReproCredits_Tick(unsigned int frame);
void Port_ReproPerfcap_Tick(unsigned int frame);
void Port_ReproA11y_Tick(unsigned int frame);
void Port_ReproCatPerson_Tick(unsigned int frame);
void Port_ReproRoomCap_Tick(unsigned int frame);
void Port_ReproIntroDbg_Tick(unsigned int frame);

#ifdef __cplusplus
}
#endif
