/*
 * port_ra_ui.h — RetroAchievements UI surfaces (F8 tab + unlock toasts).
 *
 * Both entry points are safe to call every frame and are compiled as
 * no-ops when TMC_RA is undefined, so the --ra=n build links without
 * any port_ra.c / rcheevos objects.
 */
#ifndef PORT_RA_UI_H
#define PORT_RA_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Draws the body of the "Achievements" ribbon tab (login, status, list). */
void Port_RA_UI_DrawTab(void);

/* Draws the unlock notifications. Call once per ImGui frame, next to the
 * other overlays (practice HUD / FPS counter), before ImGui::Render(). */
void Port_RA_UI_DrawOverlay(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RA_UI_H */
