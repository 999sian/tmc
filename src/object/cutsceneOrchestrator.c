/**
 * @file cutsceneOrchestrator.c
 * @ingroup Objects
 *
 * @brief Cutscene Orchestrator object
 */
#include "entity.h"
#include "script.h"
#include "hitbox.h"

void CutsceneOrchestrator(Entity* this) {
#ifdef PC_PORT
    extern void Port_TrackOrch(Entity* ent);
    Port_TrackOrch(this);
#endif
    if ((this->flags & ENT_SCRIPTED) != 0) {
        if (this->action == 0) {
            this->action = 1;
            this->hitbox = (Hitbox*)&gHitbox_2;
            InitScriptForNPC(this);
        } else {
            ExecuteScriptAndHandleAnimation(this, NULL);
        }
    } else {
        this->action = 1;
    }
}
