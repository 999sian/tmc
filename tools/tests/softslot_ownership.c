#include "port/port_softslots.c"
#include <assert.h>
static unsigned owned[256];
unsigned int GetInventoryValue(unsigned int item) { assert(item < 256); return owned[item]; }
bool Port_Config_SoftSlotPressed(int slot) { return slot == 0; }
int main(void) {
    sLoaded = true;
    sAssignments[0] = 17; /* Previously assigned Gust Jar in another save. */
    Port_SoftSlots_Update();
    assert(!Port_SoftSlots_IsBHeld());
    assert(Port_SoftSlots_GetEffectiveBItem(1) == 1);
    assert(Port_SoftSlots_GetAssignment(0) == 0); /* L-modifier path */
    owned[17] = 1;
    Port_SoftSlots_Update();
    assert(Port_SoftSlots_IsBHeld());
    assert(Port_SoftSlots_GetEffectiveBItem(1) == 17);
    assert(Port_SoftSlots_GetAssignment(0) == 17);
    /* Ownership can change after input polling, e.g. loading another slot. */
    owned[17] = 0;
    assert(Port_SoftSlots_GetEffectiveBItem(1) == 1);
    assert(!Port_SoftSlots_IsBHeld());
    assert(sAssignments[0] == 17); /* Keep the preference for the original save. */
    sAssignments[0] = 255;
    owned[255] = 1;
    Port_SoftSlots_Update();
    assert(!Port_SoftSlots_IsBHeld());
    return 0;
}
