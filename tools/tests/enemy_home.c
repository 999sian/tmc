/* Exercise production home initialization using the room loader's field layout. */
#include "enemy.h"
#include "room.h"
#include "port_generic_entity.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

RoomControls gRoomControls;

int main(void) {
    for (unsigned type = 0; type < 3; ++type) {
        Enemy e = {0};
        e.base.kind = ENEMY;
        e.base.id = CHUCHU;
        e.base.type = type;
        e.base.x.HALF.HI = 4000;
        e.base.y.HALF.HI = 3000;
        gRoomControls.origin_x = 3000;
        gRoomControls.origin_y = 2000;
        /* RegisterRoomEntity stores the id/type immediately before ranges. */
        GE_FIELD(&e.base, field_0x7a)->HALF.LO = CHUCHU;
        GE_FIELD(&e.base, field_0x7a)->HALF.HI = type;
        GE_FIELD(&e.base, field_0x7c)->BYTES.byte2 = 20;
        GE_FIELD(&e.base, field_0x7c)->BYTES.byte3 = 12;
        GE_FIELD(&e.base, cutsceneBeh)->HWORD = 800;
        GE_FIELD(&e.base, field_0x86)->HWORD = 464;
        sub_0804A720(&e.base);
        assert(e.rangeX == 20 && e.rangeY == 12);
        assert(e.homeX == 3800 && e.homeY == 2464);
        assert(e.enemyFlags & EM_FLAG_HAS_HOME);
        /* Initialized bounds survive later reuse of the spawn-data fields. */
        e.field_0x7c.WORD = 0;
        e.cutsceneBeh.HWORD = 0;
        sub_0804A720(&e.base);
        assert(e.rangeX == 20 && e.rangeY == 12 && e.homeX == 3800);

        /* Zero room bounds retain species defaults, centered on the spawn. */
        memset(&e, 0, sizeof(e));
        e.base.kind = ENEMY;
        e.base.id = CHUCHU;
        e.base.x.HALF.HI = 4000;
        e.base.y.HALF.HI = 3000;
        GE_FIELD(&e.base, field_0x7a)->HALF.LO = CHUCHU;
        GE_FIELD(&e.base, field_0x7a)->HALF.HI = type;
        sub_0804A720(&e.base);
        assert(e.rangeX == 64 && e.rangeY == 64);
        assert(e.homeX == 3744 && e.homeY == 2744);
    }
    puts("Enemy home initialization: room bounds, defaults and idempotence PASS");
}
