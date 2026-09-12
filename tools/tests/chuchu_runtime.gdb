# Synthetic ChuChu state-machine probe using a loaded retail ROM.
# Run from a fresh temporary directory with TMC_BASEROM pointing at USA/EU/JP:
# TMC_AUTOPLAY=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
#   gdb -batch -x /absolute/path/to/chuchu_runtime.gdb /absolute/path/to/tmc_pc
# Tests real targeting, home range, movement, and ROM animation decoding.
# This does not reproduce a particular saved room or verify contact damage.
set pagination off
set confirm off
break AgbMain
run --no-audio
python
import gdb
def ev(s): return gdb.parse_and_eval(s)
def cmd(s): gdb.execute(s)
cmd('set $e = (ChuchuEntity*)&gEntities[0]')
cmd('set $e->base.id=1')
cmd('set $e->base.spriteIndex=SPRITE_CHUCHU')
cmd('set $e->base.collisionLayer=1')
cmd('set $e->base.hitbox=&gHitbox_23')
cmd('set $e->base.x.HALF.HI=400')
cmd('set $e->base.y.HALF.HI=400')
cmd('set gPlayerEntity.base.x.HALF.HI=420')
cmd('set gPlayerEntity.base.y.HALF.HI=400')
cmd('set gPlayerEntity.base.collisionLayer=1')
cmd('set gPlayerState.flags=0')
cmd('set gPlayerState.killed=0')
# AgbMain has not seeded the PRNG yet; zero is an absorbing RNG state.
cmd('set gRand=1')
cmd('call sub_0801F0A4($e)')
cmd('call sub_0801F3AC($e)')
assert int(ev('$e->base.action'))==2,'not emerged'
for i in range(100):
    cmd('call sub_0801F0C8($e)')
    if int(ev('$e->base.action'))==3: break
assert int(ev('$e->base.action'))==3,'not walking'
assert int(ev('$e->base.animPtr != 0')), 'missing ROM walk animation'
cmd('set $e->base.timer=1')
cmd('call sub_0801F12C($e)')
assert int(ev('$e->base.action'))==4,'not attacking'
assert int(ev('$e->base.animIndex'))==3,'wrong attack animation'
assert int(ev('$e->base.animPtr != 0')), 'missing ROM attack animation'
assert int(ev('$e->base.speed')) == 0x180, 'wrong jump speed'
assert int(ev('$e->base.zVelocity')) == 0x20000, 'wrong jump velocity'
# Drive the attack to its airborne animation event, using the real gravity routine.
for i in range(100):
    cmd('call sub_0801F1B0($e)')
    if int(ev('$e->base.hitType')) == 90: break
assert int(ev('$e->base.hitType')) == 90, 'attack never became damaging'
print('CHUCHU type 0 PASS: emerge, walk, jump, damaging frame')
# Type 1 uses the same animation but randomly chooses a jump or a dive.
# Retry its decision point; do not require a particular RNG seed.
cmd('set $e = (ChuchuEntity*)&gEntities[1]')
for field, value in [('id', '1'), ('type', '1'), ('spriteIndex', 'SPRITE_CHUCHU'),
                     ('collisionLayer', '1'), ('hitbox', '&gHitbox_23'),
                     ('x.HALF.HI', '400'), ('y.HALF.HI', '400')]:
    cmd('set $e->base.'+field+'='+value)
cmd('call sub_0801F428($e)')
cmd('call sub_0801F764($e)')
assert int(ev('$e->base.action')) == 2, 'type 1 did not emerge'
for i in range(100):
    cmd('call sub_0801F494($e)')
    if int(ev('$e->base.action')) == 3: break
assert int(ev('$e->base.action')) == 3, 'type 1 emerge animation stuck'
cmd('set $e->base.subtimer=1')
cmd('call sub_0801F4EC($e)')
assert int(ev('$e->base.action')) == 4, 'type 1 not walking'
for i in range(64):
    cmd('set $e->base.action=4')
    cmd('set $e->base.timer=7')
    cmd('call sub_0801F508($e)')
    if int(ev('$e->base.action')) == 5: break
assert int(ev('$e->base.action')) == 5, 'type 1 never chose jump'
for i in range(100):
    cmd('call sub_0801F584($e)')
    if int(ev('$e->base.hitType')) == 91: break
assert int(ev('$e->base.hitType')) == 91, 'type 1 attack never became damaging'
print('CHUCHU type 1 PASS: emerge, walk, jump decision, damaging frame')
# Type 2 charge creates an FX through the entity allocator, which has not yet
# initialized at AgbMain. Probe the charged attack state without creating that FX.
cmd('set $e->base.type=2')
cmd('set $e->base.action=4')
cmd('set $e->base.timer=7')
cmd('call sub_0801F8C0($e)')
assert int(ev('$e->base.action')) == 5, 'charged type 2 did not jump'
assert int(ev('$e->base.animIndex')) == 3, 'charged type 2 attack animation'
assert int(ev('$e->base.zVelocity')) == 0x20000, 'charged type 2 jump velocity'
print('CHUCHU type 2 PASS: charged-state jump; region='+str(ev('gRomRegion')))
end
quit
