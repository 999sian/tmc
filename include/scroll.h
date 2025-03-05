#ifndef SCROLL_H
#define SCROLL_H

#include "global.h"
#include "transitions.h"

extern bool32 DoApplicableTransition(u32, u32, u32, u32);
extern void DoExitTransitionWithType(const Transition* screenTransition, u32 transitionType);

void UpdateIsDiggingCave(void);
void sub_08080930(u32);

extern void sub_080809D4(void);
extern void sub_08080CB4(struct Entity_*);

#endif // SCROLL_H
