# Natural Minish Woods ChuChu regression, observing normal top-level updates.
# Copy the executable into a fresh temporary directory to isolate runtime
# files. Run with TMC_BASEROM=/absolute/retail/ROM, TMC_AUTOPLAY=1,
# TMC_ROOMCAP=1 TMC_ROOMCAP_WARP=0,0,0x3a8,0x250,1 TMC_ROOMCAP_SETTLE=400,
# SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy, then:
# gdb -batch -x /absolute/path/to/this/file /temporary/directory/tmc_pc
# Only normal progression flags are supplied. No enemy states/timers are forced.
set pagination off
set confirm off
set print thread-events off
python
import gdb
observed = set()
target = None
class ChuProbe(gdb.Breakpoint):
    def stop(self):
        global target
        e = gdb.parse_and_eval('this')
        b = e.dereference()['base']
        if target is None and int(b['action']) == 0:
            x = int(b['x']['HALF']['HI']) - int(gdb.parse_and_eval('gRoomControls.origin_x'))
            y = int(b['y']['HALF']['HI']) - int(gdb.parse_and_eval('gRoomControls.origin_y'))
            if x == 0x3a0 and y == 0x250:
                target = int(e)
        if int(e) == target:
            action = int(b['action'])
            if action not in observed:
                observed.add(action)
                print('Natural ChuChu action %d; ranges %s, %s' % (
                    action, gdb.parse_and_eval('((Enemy*)this)->rangeX'),
                    gdb.parse_and_eval('((Enemy*)this)->rangeY')))
        return False
ChuProbe('Chuchu')
end
break LoadRoom
commands
silent
call SetGlobalFlag(TABIDACHI)
call SetGlobalFlag(EZERO_1ST)
continue
end
run --no-audio
python
assert target is not None, 'natural ChuChu was not loaded'
assert {1, 2, 3, 4}.issubset(observed), 'natural ChuChu never emerged/walked/attacked: %s' % observed
print('PASS: natural Minish Woods ChuChu emerged, walked and attacked')
end
quit
