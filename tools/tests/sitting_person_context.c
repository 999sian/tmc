#include "src/npc/sittingPerson.c"
#include <assert.h>
#include <string.h>

static bool32 spoken;
u32 CheckLocalFlagB(u32 flag) { return spoken; }
void SetLocalFlagB(u32 flag) { spoken = TRUE; }
void MessageNoOverlap(u32 message, Entity* entity) {}

int main(void) {
    ScriptExecutionContext context;
    SittingPersonEntity npc = { 0 };
    memset(&context, 0, sizeof(context));
    /* Script waits must survive the dialogue callback's condition update. */
    context.wait = 0x1234;
    context.unk_12 = 0x5678;
    npc.unk_84 = (void*)&context;
    sub_0806390C(&npc.base);
    assert(context.condition == 1);
    assert(context.wait == 0x1234 && context.unk_12 == 0x5678);
    assert(spoken);
    context.condition = 0;
    sub_0806390C(&npc.base);
    assert(context.condition == 0); /* repeat dialogue takes the other branch */
    return 0;
}
