# Run from an isolated directory with TMC_BASEROM set to a retail ROM.
# Set TMC_AUTOPLAY=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy.
# gdb -batch -x /absolute/path/to/this/file /absolute/path/to/tmc_pc
set pagination off
set confirm off
break AgbMain
run --no-audio
set gMapBottom.mapData[0] = 0
set gMapBottom.tileTypes[0] = 0
set gPlayerEntity.base.collisionLayer = 1
set gPlayerState.swim_state = 0
set gMapBottom.collisionData[0] = 16
python
import gdb
def val(expr): return int(gdb.parse_and_eval(expr))
assert val('(unsigned int)sub_080B1B84(0,1)') == 0, 'tile type 0 properties'
assert val('(unsigned int)sub_080086D8(9,0,gUnk_080082DC)') == 0, 'collision pixel'
for i in range(3): assert val('(unsigned long)gLilypadRails[%d]'%i) != 0, 'rail pointer'
assert val('*(unsigned char*)Port_GetFuserFusionData(0x68)') == 0, 'wall fusion progress'
assert val('((unsigned char*)&gUnk_additional_a_DeepwoodShrineBoss_Main)[0]') == 6, 'boss reward first'
assert val('((unsigned char*)&gUnk_additional_a_DeepwoodShrineBoss_Main)[16]') == 6, 'boss reward second'
assert val('((unsigned char*)&gUnk_additional_a_DeepwoodShrineBoss_Main)[32]') == 255, 'boss reward end'
idx=321 if val('gRomRegion')==2 else 322
assert val('(unsigned long)gMoreSpritePtrs[1]') == val('(unsigned long)gSpritePtrs[%d].frames'%idx), 'HUD frame pointer'
assert val('(unsigned long)gMoreSpritePtrs[1]') != 0, 'HUD frames available'
print('PASS: tile properties, collision pixel, rails, wall fusion, boss rewards, HUD seed')
end
quit
