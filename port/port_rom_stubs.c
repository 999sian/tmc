/*
 * port_rom_stubs.c — Runtime ROM data stubs for PC port
 *
 * Uninitialized BSS buffers for GBA ROM data symbols.
 * Filled from the user's ROM at startup by Port_InitRomStubs().
 * Contains NO copyrighted data in source or binary.
 */

#include "port_rom_stubs.h"
#include <string.h>

u8 gUnk_08001A7C[848] __attribute__((aligned(4)));
u8 gUnk_08001DCC[8312] __attribute__((aligned(4)));
u8 gUnk_0800275C[64] __attribute__((aligned(4)));
u8 gUnk_08003E44[2144] __attribute__((aligned(4)));
u8 gUnk_08007DF4[1096] __attribute__((aligned(4)));
u8 gUnk_0800823C[160] __attribute__((aligned(4)));
u8 gUnk_080082DC[96] __attribute__((aligned(4)));
u8 gUnk_0800833C[96] __attribute__((aligned(4)));
u8 gUnk_0800839C[96] __attribute__((aligned(4)));
u8 gUnk_080083FC[96] __attribute__((aligned(4)));
u8 gUnk_0800845C[96] __attribute__((aligned(4)));
u8 gUnk_080084BC[96] __attribute__((aligned(4)));
u8 gUnk_0800851C[58472] __attribute__((aligned(4)));
u8 gUnk_08016984[65536] __attribute__((aligned(4)));
u8 gUnk_080B4410[72] __attribute__((aligned(4)));
u8 gUnk_080B4458[65536] __attribute__((aligned(4)));
u8 gUnk_080C8F2C[40] __attribute__((aligned(4)));
u8 gUnk_080C8F54[40] __attribute__((aligned(4)));
u8 gUnk_080C8F7C[220] __attribute__((aligned(4)));
u8 gUnk_080C9058[60] __attribute__((aligned(4)));
u8 gUnk_080C9094[4640] __attribute__((aligned(4)));
u8 gUnk_080CA2B4[800] __attribute__((aligned(4)));
u8 gUnk_080CA5D4[256] __attribute__((aligned(4)));
u8 gUnk_080CA6D4[8816] __attribute__((aligned(4)));
u8 gUnk_080CC944[3712] __attribute__((aligned(4)));
u8 gUnk_080CD7C4[32] __attribute__((aligned(4)));
u8 gUnk_080CD7E4[20] __attribute__((aligned(4)));
u8 gUnk_080CD7F8[24] __attribute__((aligned(4)));
u8 gUnk_080CD810[24] __attribute__((aligned(4)));
u8 gUnk_080CD828[24] __attribute__((aligned(4)));
u8 gUnk_080CD840[16] __attribute__((aligned(4)));
u8 gUnk_080CD844[16] __attribute__((aligned(4)));
u8 gUnk_080CD848[16] __attribute__((aligned(4)));
u8 gUnk_080CD850[16] __attribute__((aligned(4)));
u8 gUnk_080CD854[24] __attribute__((aligned(4)));
u8 gUnk_080CD86C[16] __attribute__((aligned(4)));
u8 gUnk_080CD878[16] __attribute__((aligned(4)));
u8 gUnk_080CD884[15664] __attribute__((aligned(4)));
u8 gUnk_080D15B4[19852] __attribute__((aligned(4)));
u8 gUnk_080D6340[32] __attribute__((aligned(4)));
u8 gUnk_080D6360[32] __attribute__((aligned(4)));
u8 gUnk_080D6380[32] __attribute__((aligned(4)));
u8 gUnk_080D63A0[32] __attribute__((aligned(4)));
u8 gUnk_080D63C0[32] __attribute__((aligned(4)));
u8 gUnk_080D63E0[32] __attribute__((aligned(4)));
u8 gUnk_080D6400[32] __attribute__((aligned(4)));
u8 gUnk_080D6420[32] __attribute__((aligned(4)));
u8 gUnk_080D6440[184] __attribute__((aligned(4)));
u8 gUnk_080D64F8[16] __attribute__((aligned(4)));
u8 gUnk_080D6508[80] __attribute__((aligned(4)));
u8 gUnk_080D6558[32] __attribute__((aligned(4)));
u8 gUnk_080D6578[160] __attribute__((aligned(4)));
u8 gUnk_080D6618[32] __attribute__((aligned(4)));
u8 gUnk_080D6638[220] __attribute__((aligned(4)));
u8 gUnk_080D6714[528] __attribute__((aligned(4)));
u8 gUnk_080D6924[336] __attribute__((aligned(4)));
u8 gUnk_080D6A74[164] __attribute__((aligned(4)));
u8 gUnk_080D6B18[160] __attribute__((aligned(4)));
u8 gUnk_080D6BB8[1152] __attribute__((aligned(4)));
u8 gUnk_080D7038[264] __attribute__((aligned(4)));
u8 gUnk_080D7140[48] __attribute__((aligned(4)));
u8 gUnk_080D7170[48] __attribute__((aligned(4)));
u8 gUnk_080D71A0[48] __attribute__((aligned(4)));
u8 gUnk_080D71D0[32] __attribute__((aligned(4)));
u8 gUnk_080D71F0[312] __attribute__((aligned(4)));
u8 gUnk_080D7328[32] __attribute__((aligned(4)));
u8 gUnk_080D7348[104] __attribute__((aligned(4)));
u8 gUnk_080D73B0[48] __attribute__((aligned(4)));
u8 gUnk_080D73E0[48] __attribute__((aligned(4)));
u8 gUnk_080D7410[184] __attribute__((aligned(4)));
u8 gUnk_080D74C8[192] __attribute__((aligned(4)));
u8 gUnk_080D7588[80] __attribute__((aligned(4)));
u8 gUnk_080D75D8[64] __attribute__((aligned(4)));
u8 gUnk_080D7618[1820] __attribute__((aligned(4)));
u8 gUnk_080D7D34[1352] __attribute__((aligned(4)));
u8 gUnk_080D827C[1896] __attribute__((aligned(4)));
u8 gUnk_080D89E4[32] __attribute__((aligned(4)));
u8 gUnk_080D8A04[48] __attribute__((aligned(4)));
u8 gUnk_080D8A34[64] __attribute__((aligned(4)));
u8 gUnk_080D8A74[80] __attribute__((aligned(4)));
u8 gUnk_080D8AC4[96] __attribute__((aligned(4)));
u8 gUnk_080D8B24[152] __attribute__((aligned(4)));
u8 gUnk_080D8BBC[62] __attribute__((aligned(4)));
u8 gUnk_080D8BFA[110] __attribute__((aligned(4)));
u8 gUnk_080D8C68[488] __attribute__((aligned(4)));
u8 gUnk_080D8E50[584] __attribute__((aligned(4)));
u8 gUnk_080D9098[48] __attribute__((aligned(4)));
u8 gUnk_080D90C8[64] __attribute__((aligned(4)));
u8 gUnk_080D9108[544] __attribute__((aligned(4)));
u8 gUnk_080D9328[16] __attribute__((aligned(4)));
u8 gUnk_080D9338[16] __attribute__((aligned(4)));
u8 gUnk_080D9340[16] __attribute__((aligned(4)));
u8 gUnk_080D9348[1208] __attribute__((aligned(4)));
u8 gUnk_080D9800[1080] __attribute__((aligned(4)));
u8 gUnk_080D9C38[144] __attribute__((aligned(4)));
u8 gUnk_080D9CC8[32] __attribute__((aligned(4)));
u8 gUnk_080D9CE8[1352] __attribute__((aligned(4)));
u8 gUnk_080DA230[2324] __attribute__((aligned(4)));
u8 gUnk_080DAB44[32] __attribute__((aligned(4)));
u8 gUnk_080DAB64[32] __attribute__((aligned(4)));
u8 gUnk_080DAB84[64] __attribute__((aligned(4)));
u8 gUnk_080DABC4[64] __attribute__((aligned(4)));
u8 gUnk_080DAC04[80] __attribute__((aligned(4)));
u8 gUnk_080DAC54[64] __attribute__((aligned(4)));
u8 gUnk_080DAC94[64] __attribute__((aligned(4)));
u8 gUnk_080DACD4[80] __attribute__((aligned(4)));
u8 gUnk_080DAD24[64] __attribute__((aligned(4)));
u8 gUnk_080DAD64[64] __attribute__((aligned(4)));
u8 gUnk_080DADA4[324] __attribute__((aligned(4)));
u8 gUnk_080DAEE8[152] __attribute__((aligned(4)));
u8 gUnk_080DAF80[152] __attribute__((aligned(4)));
u8 gUnk_080DB018[544] __attribute__((aligned(4)));
u8 gUnk_080DB238[616] __attribute__((aligned(4)));
u8 gUnk_080DB4A0[48] __attribute__((aligned(4)));
u8 gUnk_080DB4D0[1056] __attribute__((aligned(4)));
u8 gUnk_080DB8F0[32] __attribute__((aligned(4)));
u8 gUnk_080DB910[248] __attribute__((aligned(4)));
u8 gUnk_080DBA08[200] __attribute__((aligned(4)));
u8 gUnk_080DBAD0[64] __attribute__((aligned(4)));
u8 gUnk_080DBB10[64] __attribute__((aligned(4)));
u8 gUnk_080DBB50[32] __attribute__((aligned(4)));
u8 gUnk_080DBB70[32] __attribute__((aligned(4)));
u8 gUnk_080DBB90[2048] __attribute__((aligned(4)));
u8 gUnk_080DC390[96] __attribute__((aligned(4)));
u8 gUnk_080DC3F0[64] __attribute__((aligned(4)));
u8 gUnk_080DC430[64] __attribute__((aligned(4)));
u8 gUnk_080DC470[80] __attribute__((aligned(4)));
u8 gUnk_080DC4C0[112] __attribute__((aligned(4)));
u8 gUnk_080DC530[1504] __attribute__((aligned(4)));
u8 gUnk_080DCB10[1924] __attribute__((aligned(4)));
u8 gUnk_080DD294[208] __attribute__((aligned(4)));
u8 gUnk_080DD364[1004] __attribute__((aligned(4)));
u8 gUnk_080DD750[144] __attribute__((aligned(4)));
u8 gUnk_080DD7E0[96] __attribute__((aligned(4)));
u8 gUnk_080DD840[1608] __attribute__((aligned(4)));
u8 gUnk_080DDE88[384] __attribute__((aligned(4)));
u8 gUnk_080DE008[472] __attribute__((aligned(4)));
u8 gUnk_080DE1E0[32] __attribute__((aligned(4)));
u8 gUnk_080DE200[712] __attribute__((aligned(4)));
u8 gUnk_080DE4C8[844] __attribute__((aligned(4)));
u8 gUnk_080DE814[4964] __attribute__((aligned(4)));
u8 gUnk_080DFB78[5316] __attribute__((aligned(4)));
u8 gUnk_080E103C[10260] __attribute__((aligned(4)));
u8 gUnk_080E3850[5000] __attribute__((aligned(4)));
u8 gUnk_080E4BD8[48] __attribute__((aligned(4)));
u8 gUnk_080E4C08[208] __attribute__((aligned(4)));
u8 gUnk_080E4CD8[32] __attribute__((aligned(4)));
u8 gUnk_080E4CF8[2408] __attribute__((aligned(4)));
u8 gUnk_080E5660[32] __attribute__((aligned(4)));
u8 gUnk_080E5680[1980] __attribute__((aligned(4)));
u8 gUnk_080E5E3C[32] __attribute__((aligned(4)));
u8 gUnk_080E5E5C[2784] __attribute__((aligned(4)));
u8 gUnk_080E693C[2128] __attribute__((aligned(4)));
u8 gUnk_080E718C[32] __attribute__((aligned(4)));
u8 gUnk_080E71AC[280] __attribute__((aligned(4)));
u8 gUnk_080E72C4[11736] __attribute__((aligned(4)));
u8 gUnk_080EA09C[3276] __attribute__((aligned(4)));
u8 gUnk_080EAD68[80] __attribute__((aligned(4)));
u8 gUnk_080EADB8[72] __attribute__((aligned(4)));
u8 gUnk_080EAE00[96] __attribute__((aligned(4)));
u8 gUnk_080EAE60[96] __attribute__((aligned(4)));
u8 gUnk_080EAEC0[96] __attribute__((aligned(4)));
u8 gUnk_080EAF20[1716] __attribute__((aligned(4)));
u8 gUnk_080EB5D4[48] __attribute__((aligned(4)));
u8 gUnk_080EB604[128] __attribute__((aligned(4)));
u8 gUnk_080EB684[880] __attribute__((aligned(4)));
u8 gUnk_080EB9F4[176] __attribute__((aligned(4)));
u8 gUnk_080EBAA4[80] __attribute__((aligned(4)));
u8 gUnk_080EBAF4[2008] __attribute__((aligned(4)));
u8 gUnk_080EC2CC[540] __attribute__((aligned(4)));
u8 gUnk_080EC4E8[824] __attribute__((aligned(4)));
u8 gUnk_080EC820[576] __attribute__((aligned(4)));
u8 gUnk_080ECA60[1388] __attribute__((aligned(4)));
u8 gUnk_080ECFCC[536] __attribute__((aligned(4)));
u8 gUnk_080ED1E4[4400] __attribute__((aligned(4)));
u8 gUnk_080EE314[712] __attribute__((aligned(4)));
u8 gUnk_080EE5DC[320] __attribute__((aligned(4)));
u8 gUnk_080EE71C[368] __attribute__((aligned(4)));
u8 gUnk_080EE88C[112] __attribute__((aligned(4)));
u8 gUnk_080EE8FC[32] __attribute__((aligned(4)));
u8 gUnk_080EE91C[32] __attribute__((aligned(4)));
u8 gUnk_080EE93C[32] __attribute__((aligned(4)));
u8 gUnk_080EE95C[32] __attribute__((aligned(4)));
u8 gUnk_080EE97C[32] __attribute__((aligned(4)));
u8 gUnk_080EE99C[32] __attribute__((aligned(4)));
u8 gUnk_080EE9BC[32] __attribute__((aligned(4)));
u8 gUnk_080EE9DC[32] __attribute__((aligned(4)));
u8 gUnk_080EE9FC[32] __attribute__((aligned(4)));
u8 gUnk_080EEA1C[32] __attribute__((aligned(4)));
u8 gUnk_080EEA3C[32] __attribute__((aligned(4)));
u8 gUnk_080EEA5C[32] __attribute__((aligned(4)));
u8 gUnk_080EEA7C[32] __attribute__((aligned(4)));
u8 gUnk_080EEA9C[32] __attribute__((aligned(4)));
u8 gUnk_080EEABC[176] __attribute__((aligned(4)));
u8 gUnk_080EEB6C[32] __attribute__((aligned(4)));
u8 gUnk_080EEB8C[32] __attribute__((aligned(4)));
u8 gUnk_080EEBAC[272] __attribute__((aligned(4)));
u8 gUnk_080EECBC[112] __attribute__((aligned(4)));
u8 gUnk_080EED2C[78] __attribute__((aligned(4)));
u8 gUnk_080EED7A[18] __attribute__((aligned(4)));
u8 gUnk_080EED8C[6340] __attribute__((aligned(4)));
u8 gUnk_080F0650[432] __attribute__((aligned(4)));
u8 gUnk_080F0800[80] __attribute__((aligned(4)));
u8 gUnk_080F0850[32] __attribute__((aligned(4)));
u8 gUnk_080F0870[32] __attribute__((aligned(4)));
u8 gUnk_080F0890[96] __attribute__((aligned(4)));
u8 gUnk_080F08F0[48] __attribute__((aligned(4)));
u8 gUnk_080F0920[128] __attribute__((aligned(4)));
u8 gUnk_080F09A0[792] __attribute__((aligned(4)));
u8 gUnk_080F0CB8[160] __attribute__((aligned(4)));
u8 gUnk_080F0D58[176] __attribute__((aligned(4)));
u8 gUnk_080F0E08[20] __attribute__((aligned(4)));
u8 gUnk_080F0E1C[3660] __attribute__((aligned(4)));
u8 gUnk_080F1C68[32] __attribute__((aligned(4)));
u8 gUnk_080F1C88[264] __attribute__((aligned(4)));
u8 gUnk_080F1D90[32] __attribute__((aligned(4)));
u8 gUnk_080F1DB0[32] __attribute__((aligned(4)));
u8 gUnk_080F1DD0[932] __attribute__((aligned(4)));
u8 gUnk_080F2174[32] __attribute__((aligned(4)));
u8 gUnk_080F2194[32] __attribute__((aligned(4)));
u8 gUnk_080F21B4[472] __attribute__((aligned(4)));
u8 gUnk_080F238C[48] __attribute__((aligned(4)));
u8 gUnk_080F23BC[436] __attribute__((aligned(4)));
u8 gUnk_080F2570[32] __attribute__((aligned(4)));
u8 gUnk_080F2590[48] __attribute__((aligned(4)));
u8 gUnk_080F25C0[64] __attribute__((aligned(4)));
u8 gUnk_080F2600[408] __attribute__((aligned(4)));
u8 gUnk_080F2798[64] __attribute__((aligned(4)));
u8 gUnk_080F27D8[136] __attribute__((aligned(4)));
u8 gUnk_080F2860[116] __attribute__((aligned(4)));
u8 gUnk_080F28D4[32] __attribute__((aligned(4)));
u8 gUnk_080F28F4[32] __attribute__((aligned(4)));
u8 gUnk_080F2914[1304] __attribute__((aligned(4)));
u8 gUnk_080F2E2C[104] __attribute__((aligned(4)));
u8 gUnk_080F2E94[48] __attribute__((aligned(4)));
u8 gUnk_080F2EC4[272] __attribute__((aligned(4)));
u8 gUnk_080F2FD4[248] __attribute__((aligned(4)));
u8 gUnk_080F30CC[268] __attribute__((aligned(4)));
u8 gUnk_080F31D8[136] __attribute__((aligned(4)));
u8 gUnk_080F3260[564] __attribute__((aligned(4)));
u8 gUnk_080F3494[368] __attribute__((aligned(4)));
u8 gUnk_080F3604[248] __attribute__((aligned(4)));
u8 gUnk_080F36FC[212] __attribute__((aligned(4)));
u8 gUnk_080F37D0[632] __attribute__((aligned(4)));
u8 gUnk_080F3A48[508] __attribute__((aligned(4)));
u8 gUnk_080F3C44[32] __attribute__((aligned(4)));
u8 gUnk_080F3C64[48] __attribute__((aligned(4)));
u8 gUnk_080F3C94[528] __attribute__((aligned(4)));
u8 gUnk_080F3EA4[3300] __attribute__((aligned(4)));
u8 gUnk_080F4B88[456] __attribute__((aligned(4)));
u8 gUnk_080F4D50[96] __attribute__((aligned(4)));
u8 gUnk_080F4DB0[32] __attribute__((aligned(4)));
u8 gUnk_080F4DD0[32] __attribute__((aligned(4)));
u8 gUnk_080F4DF0[32] __attribute__((aligned(4)));
u8 gUnk_080F4E10[160] __attribute__((aligned(4)));
u8 gUnk_080F4EB0[96] __attribute__((aligned(4)));
u8 gUnk_080F4F10[1016] __attribute__((aligned(4)));
u8 gUnk_080F5308[32] __attribute__((aligned(4)));
u8 gUnk_080F5328[32] __attribute__((aligned(4)));
u8 gUnk_080F5348[416] __attribute__((aligned(4)));
u8 gUnk_080F54E8[32] __attribute__((aligned(4)));
u8 gUnk_080F5508[32] __attribute__((aligned(4)));
u8 gUnk_080F5528[48] __attribute__((aligned(4)));
u8 gUnk_080F5558[32] __attribute__((aligned(4)));
u8 gUnk_080F5578[32] __attribute__((aligned(4)));
u8 gUnk_080F5598[32] __attribute__((aligned(4)));
u8 gUnk_080F55B8[32] __attribute__((aligned(4)));
u8 gUnk_080F55D8[136] __attribute__((aligned(4)));
u8 gUnk_080F5660[248] __attribute__((aligned(4)));
u8 gUnk_080F5758[48] __attribute__((aligned(4)));
u8 gUnk_080F5788[32] __attribute__((aligned(4)));
u8 gUnk_080F57A8[32] __attribute__((aligned(4)));
u8 gUnk_080F57C8[32] __attribute__((aligned(4)));
u8 gUnk_080F57E8[64] __attribute__((aligned(4)));
u8 gUnk_080F5828[32] __attribute__((aligned(4)));
u8 gUnk_080F5848[32] __attribute__((aligned(4)));
u8 gUnk_080F5868[32] __attribute__((aligned(4)));
u8 gUnk_080F5888[32] __attribute__((aligned(4)));
u8 gUnk_080F58A8[660] __attribute__((aligned(4)));
u8 gUnk_080F5B3C[660] __attribute__((aligned(4)));
u8 gUnk_080F5DD0[152] __attribute__((aligned(4)));
u8 gUnk_080F5E68[852] __attribute__((aligned(4)));
u8 gUnk_080F61BC[296] __attribute__((aligned(4)));
u8 gUnk_080F62E4[64] __attribute__((aligned(4)));
u8 gUnk_080F6324[576] __attribute__((aligned(4)));
u8 gUnk_080F6564[32] __attribute__((aligned(4)));
u8 gUnk_080F6584[296] __attribute__((aligned(4)));
u8 gUnk_080F66AC[2524] __attribute__((aligned(4)));
u8 gUnk_080F7088[32] __attribute__((aligned(4)));
u8 gUnk_080F70A8[48] __attribute__((aligned(4)));
u8 gUnk_080F70D8[1064] __attribute__((aligned(4)));
u8 gUnk_080F7500[80] __attribute__((aligned(4)));
u8 gUnk_080F7550[304] __attribute__((aligned(4)));
u8 gUnk_080F7680[320] __attribute__((aligned(4)));
u8 gUnk_080F77C0[48] __attribute__((aligned(4)));
u8 gUnk_080F77F0[32] __attribute__((aligned(4)));
u8 gUnk_080F7810[80] __attribute__((aligned(4)));
u8 gUnk_080F7860[64] __attribute__((aligned(4)));
u8 gUnk_080F78A0[992] __attribute__((aligned(4)));
u8 gUnk_080F7C80[80] __attribute__((aligned(4)));
u8 gUnk_080F7CD0[160] __attribute__((aligned(4)));
u8 gUnk_080F7D70[80] __attribute__((aligned(4)));
u8 gUnk_080F7DC0[684] __attribute__((aligned(4)));
u8 gUnk_080F806C[628] __attribute__((aligned(4)));
u8 gUnk_080F82E0[336] __attribute__((aligned(4)));
u8 gUnk_080F8430[424] __attribute__((aligned(4)));
u8 gUnk_080F85D8[32] __attribute__((aligned(4)));
u8 gUnk_080F85F8[3340] __attribute__((aligned(4)));
u8 gUnk_080F9304[2292] __attribute__((aligned(4)));
u8 gUnk_080F9BF8[912] __attribute__((aligned(4)));
u8 gUnk_080F9F88[32] __attribute__((aligned(4)));
u8 gUnk_080F9FA8[1576] __attribute__((aligned(4)));
u8 gUnk_080FA5D0[1768] __attribute__((aligned(4)));
u8 gUnk_080FACB8[144] __attribute__((aligned(4)));
u8 gUnk_080FAD48[668] __attribute__((aligned(4)));
u8 gUnk_080FAFE4[32] __attribute__((aligned(4)));
u8 gUnk_080FB004[8564] __attribute__((aligned(4)));
u8 gUnk_080FEAC8[288] __attribute__((aligned(4)));
u8 gUnk_080FEBE8[64] __attribute__((aligned(4)));
u8 gUnk_080FEC28[160] __attribute__((aligned(4)));
u8 gUnk_080FECC8[80] __attribute__((aligned(4)));
u8 gUnk_080FED18[64] __attribute__((aligned(4)));
u8 gUnk_080FED58[192] __attribute__((aligned(4)));
u8 gUnk_080FEE18[32] __attribute__((aligned(4)));
u8 gUnk_080FEE38[16] __attribute__((aligned(4)));
u8 gUnk_080FEE48[16] __attribute__((aligned(4)));
u8 gUnk_080FEE58[32] __attribute__((aligned(4)));
u8 gUnk_080FEE78[39148] __attribute__((aligned(4)));
u8 gUnk_08108764[1740] __attribute__((aligned(4)));
u8 gUnk_08108E30[24] __attribute__((aligned(4)));
u8 gUnk_08108E48[24] __attribute__((aligned(4)));
u8 gUnk_08108E60[10272] __attribute__((aligned(4)));
u8 gUnk_0810B680[108] __attribute__((aligned(4)));
u8 gUnk_0810B740[16] __attribute__((aligned(4)));
u8 gUnk_0810B748[16] __attribute__((aligned(4)));
u8 gUnk_0810B74A[42] __attribute__((aligned(4)));
u8 gUnk_0810B78C[16] __attribute__((aligned(4)));
u8 gUnk_0810B790[42] __attribute__((aligned(4)));
u8 gUnk_0810B7BA[16] __attribute__((aligned(4)));
u8 gUnk_0810B7C0[2728] __attribute__((aligned(4)));
u8 gUnk_0810C268[13396] __attribute__((aligned(4)));
u8 gUnk_0810F6BC[920] __attribute__((aligned(4)));
u8 gUnk_0810FA54[16] __attribute__((aligned(4)));
u8 gUnk_0810FA5A[5882] __attribute__((aligned(4)));
u8 gUnk_08111154[44260] __attribute__((aligned(4)));
u8 gUnk_0811BE38[16] __attribute__((aligned(4)));
u8 gUnk_0811BE40[18780] __attribute__((aligned(4)));
u8 gUnk_0812079C[16] __attribute__((aligned(4)));
u8 gUnk_081207A4[16] __attribute__((aligned(4)));
u8 gUnk_081207AC[9012] __attribute__((aligned(4)));
u8 gUnk_08122AE0[16] __attribute__((aligned(4)));
u8 gUnk_08122AE8[16] __attribute__((aligned(4)));
u8 gUnk_08122AF8[16] __attribute__((aligned(4)));
u8 gUnk_08122B00[16] __attribute__((aligned(4)));
u8 gUnk_08122B0E[16] __attribute__((aligned(4)));
u8 gUnk_08122B1E[16] __attribute__((aligned(4)));
u8 gUnk_08122B2E[16] __attribute__((aligned(4)));
u8 gUnk_08122B3C[18356] __attribute__((aligned(4)));
u8 gUnk_081272F0[852] __attribute__((aligned(4)));
u8 gUnk_08127644[852] __attribute__((aligned(4)));
u8 gUnk_08127998[852] __attribute__((aligned(4)));
u8 gUnk_08127CEC[20] __attribute__((aligned(4)));
u8 gUnk_08127D00[16] __attribute__((aligned(4)));
u8 gUnk_08127D10[3368] __attribute__((aligned(4)));
u8 gUnk_08128C00[16] __attribute__((aligned(4)));
u8 gUnk_08128C04[16] __attribute__((aligned(4)));
u8 gUnk_08128C14[128] __attribute__((aligned(4)));
u8 gUnk_08128C94[128] __attribute__((aligned(4)));
u8 gUnk_08128D38[16] __attribute__((aligned(4)));
u8 gUnk_08128D3C[16] __attribute__((aligned(4)));
u8 gUnk_08128D43[16] __attribute__((aligned(4)));
u8 gUnk_08128D51[16] __attribute__((aligned(4)));
u8 gUnk_08128D60[16] __attribute__((aligned(4)));
u8 gUnk_08128D70[64] __attribute__((aligned(4)));
u8 gUnk_08128DB8[16] __attribute__((aligned(4)));
u8 gUnk_08128DBC[16] __attribute__((aligned(4)));
u8 gUnk_08128DD4[16] __attribute__((aligned(4)));
u8 gUnk_08128DD8[16] __attribute__((aligned(4)));
u8 gUnk_08128DE8[144] __attribute__((aligned(4)));
u8 gUnk_08128E80[16] __attribute__((aligned(4)));
u8 gUnk_08128E84[16] __attribute__((aligned(4)));
u8 gUnk_08128E94[136] __attribute__((aligned(4)));
u8 gUnk_08128F38[20] __attribute__((aligned(4)));
u8 gUnk_08128F4C[16] __attribute__((aligned(4)));
u8 gUnk_08128F58[80] __attribute__((aligned(4)));
u8 gUnk_08128FA8[24] __attribute__((aligned(4)));
u8 gUnk_08128FC0[24] __attribute__((aligned(4)));
u8 gUnk_08128FD8[44] __attribute__((aligned(4)));
u8 gUnk_08129004[38672] __attribute__((aligned(4)));
u8 gUnk_08132714[1044] __attribute__((aligned(4)));
u8 gUnk_08132B28[9364] __attribute__((aligned(4)));
u8 gUnk_085A97A0[43488] __attribute__((aligned(4)));
u8 gUnk_085B4180[65536] __attribute__((aligned(4)));
u8 gUnk_085C4620[65536] __attribute__((aligned(4)));
u8 gUnk_086D4460[65536] __attribute__((aligned(4)));
u8 gUnk_086E8460[65536] __attribute__((aligned(4)));
u8 gUnk_089FD1B4[320] __attribute__((aligned(4)));
u8 gUnk_089FD2F4[33885] __attribute__((aligned(4)));
u8 gUnk_08A05751[4462] __attribute__((aligned(4)));
u8 gUnk_08A068BF[4096] __attribute__((aligned(4)));

typedef struct {
    void* dst;
    u32 rom_offset;
    u32 size;
} RomStubEntry;

static const RomStubEntry sRomStubs[] = {
    { gUnk_08001A7C, 0x001A7Cu, 848u },
    { gUnk_08001DCC, 0x001DCCu, 8312u },
    { gUnk_0800275C, 0x00275Cu, 64u },
    { gUnk_08003E44, 0x003E44u, 2144u },
    { gUnk_08007DF4, 0x007DF4u, 1096u },
    { gUnk_0800823C, 0x00823Cu, 160u },
    { gUnk_080082DC, 0x0082DCu, 96u },
    { gUnk_0800833C, 0x00833Cu, 96u },
    { gUnk_0800839C, 0x00839Cu, 96u },
    { gUnk_080083FC, 0x0083FCu, 96u },
    { gUnk_0800845C, 0x00845Cu, 96u },
    { gUnk_080084BC, 0x0084BCu, 96u },
    { gUnk_0800851C, 0x00851Cu, 58472u },
    { gUnk_08016984, 0x016984u, 65536u },
    { gUnk_080B4410, 0x0B4410u, 72u },
    { gUnk_080B4458, 0x0B4458u, 65536u },
    { gUnk_080C8F2C, 0x0C8F2Cu, 40u },
    { gUnk_080C8F54, 0x0C8F54u, 40u },
    { gUnk_080C8F7C, 0x0C8F7Cu, 220u },
    { gUnk_080C9058, 0x0C9058u, 60u },
    { gUnk_080C9094, 0x0C9094u, 4640u },
    { gUnk_080CA2B4, 0x0CA2B4u, 800u },
    { gUnk_080CA5D4, 0x0CA5D4u, 256u },
    { gUnk_080CA6D4, 0x0CA6D4u, 8816u },
    { gUnk_080CC944, 0x0CC944u, 3712u },
    { gUnk_080CD7C4, 0x0CD7C4u, 32u },
    { gUnk_080CD7E4, 0x0CD7E4u, 20u },
    { gUnk_080CD7F8, 0x0CD7F8u, 24u },
    { gUnk_080CD810, 0x0CD810u, 24u },
    { gUnk_080CD828, 0x0CD828u, 24u },
    { gUnk_080CD840, 0x0CD840u, 16u },
    { gUnk_080CD844, 0x0CD844u, 16u },
    { gUnk_080CD848, 0x0CD848u, 16u },
    { gUnk_080CD850, 0x0CD850u, 16u },
    { gUnk_080CD854, 0x0CD854u, 24u },
    { gUnk_080CD86C, 0x0CD86Cu, 16u },
    { gUnk_080CD878, 0x0CD878u, 16u },
    { gUnk_080CD884, 0x0CD884u, 15664u },
    { gUnk_080D15B4, 0x0D15B4u, 19852u },
    { gUnk_080D6340, 0x0D6340u, 32u },
    { gUnk_080D6360, 0x0D6360u, 32u },
    { gUnk_080D6380, 0x0D6380u, 32u },
    { gUnk_080D63A0, 0x0D63A0u, 32u },
    { gUnk_080D63C0, 0x0D63C0u, 32u },
    { gUnk_080D63E0, 0x0D63E0u, 32u },
    { gUnk_080D6400, 0x0D6400u, 32u },
    { gUnk_080D6420, 0x0D6420u, 32u },
    { gUnk_080D6440, 0x0D6440u, 184u },
    { gUnk_080D64F8, 0x0D64F8u, 16u },
    { gUnk_080D6508, 0x0D6508u, 80u },
    { gUnk_080D6558, 0x0D6558u, 32u },
    { gUnk_080D6578, 0x0D6578u, 160u },
    { gUnk_080D6618, 0x0D6618u, 32u },
    { gUnk_080D6638, 0x0D6638u, 220u },
    { gUnk_080D6714, 0x0D6714u, 528u },
    { gUnk_080D6924, 0x0D6924u, 336u },
    { gUnk_080D6A74, 0x0D6A74u, 164u },
    { gUnk_080D6B18, 0x0D6B18u, 160u },
    { gUnk_080D6BB8, 0x0D6BB8u, 1152u },
    { gUnk_080D7038, 0x0D7038u, 264u },
    { gUnk_080D7140, 0x0D7140u, 48u },
    { gUnk_080D7170, 0x0D7170u, 48u },
    { gUnk_080D71A0, 0x0D71A0u, 48u },
    { gUnk_080D71D0, 0x0D71D0u, 32u },
    { gUnk_080D71F0, 0x0D71F0u, 312u },
    { gUnk_080D7328, 0x0D7328u, 32u },
    { gUnk_080D7348, 0x0D7348u, 104u },
    { gUnk_080D73B0, 0x0D73B0u, 48u },
    { gUnk_080D73E0, 0x0D73E0u, 48u },
    { gUnk_080D7410, 0x0D7410u, 184u },
    { gUnk_080D74C8, 0x0D74C8u, 192u },
    { gUnk_080D7588, 0x0D7588u, 80u },
    { gUnk_080D75D8, 0x0D75D8u, 64u },
    { gUnk_080D7618, 0x0D7618u, 1820u },
    { gUnk_080D7D34, 0x0D7D34u, 1352u },
    { gUnk_080D827C, 0x0D827Cu, 1896u },
    { gUnk_080D89E4, 0x0D89E4u, 32u },
    { gUnk_080D8A04, 0x0D8A04u, 48u },
    { gUnk_080D8A34, 0x0D8A34u, 64u },
    { gUnk_080D8A74, 0x0D8A74u, 80u },
    { gUnk_080D8AC4, 0x0D8AC4u, 96u },
    { gUnk_080D8B24, 0x0D8B24u, 152u },
    { gUnk_080D8BBC, 0x0D8BBCu, 62u },
    { gUnk_080D8BFA, 0x0D8BFAu, 110u },
    { gUnk_080D8C68, 0x0D8C68u, 488u },
    { gUnk_080D8E50, 0x0D8E50u, 584u },
    { gUnk_080D9098, 0x0D9098u, 48u },
    { gUnk_080D90C8, 0x0D90C8u, 64u },
    { gUnk_080D9108, 0x0D9108u, 544u },
    { gUnk_080D9328, 0x0D9328u, 16u },
    { gUnk_080D9338, 0x0D9338u, 16u },
    { gUnk_080D9340, 0x0D9340u, 16u },
    { gUnk_080D9348, 0x0D9348u, 1208u },
    { gUnk_080D9800, 0x0D9800u, 1080u },
    { gUnk_080D9C38, 0x0D9C38u, 144u },
    { gUnk_080D9CC8, 0x0D9CC8u, 32u },
    { gUnk_080D9CE8, 0x0D9CE8u, 1352u },
    { gUnk_080DA230, 0x0DA230u, 2324u },
    { gUnk_080DAB44, 0x0DAB44u, 32u },
    { gUnk_080DAB64, 0x0DAB64u, 32u },
    { gUnk_080DAB84, 0x0DAB84u, 64u },
    { gUnk_080DABC4, 0x0DABC4u, 64u },
    { gUnk_080DAC04, 0x0DAC04u, 80u },
    { gUnk_080DAC54, 0x0DAC54u, 64u },
    { gUnk_080DAC94, 0x0DAC94u, 64u },
    { gUnk_080DACD4, 0x0DACD4u, 80u },
    { gUnk_080DAD24, 0x0DAD24u, 64u },
    { gUnk_080DAD64, 0x0DAD64u, 64u },
    { gUnk_080DADA4, 0x0DADA4u, 324u },
    { gUnk_080DAEE8, 0x0DAEE8u, 152u },
    { gUnk_080DAF80, 0x0DAF80u, 152u },
    { gUnk_080DB018, 0x0DB018u, 544u },
    { gUnk_080DB238, 0x0DB238u, 616u },
    { gUnk_080DB4A0, 0x0DB4A0u, 48u },
    { gUnk_080DB4D0, 0x0DB4D0u, 1056u },
    { gUnk_080DB8F0, 0x0DB8F0u, 32u },
    { gUnk_080DB910, 0x0DB910u, 248u },
    { gUnk_080DBA08, 0x0DBA08u, 200u },
    { gUnk_080DBAD0, 0x0DBAD0u, 64u },
    { gUnk_080DBB10, 0x0DBB10u, 64u },
    { gUnk_080DBB50, 0x0DBB50u, 32u },
    { gUnk_080DBB70, 0x0DBB70u, 32u },
    { gUnk_080DBB90, 0x0DBB90u, 2048u },
    { gUnk_080DC390, 0x0DC390u, 96u },
    { gUnk_080DC3F0, 0x0DC3F0u, 64u },
    { gUnk_080DC430, 0x0DC430u, 64u },
    { gUnk_080DC470, 0x0DC470u, 80u },
    { gUnk_080DC4C0, 0x0DC4C0u, 112u },
    { gUnk_080DC530, 0x0DC530u, 1504u },
    { gUnk_080DCB10, 0x0DCB10u, 1924u },
    { gUnk_080DD294, 0x0DD294u, 208u },
    { gUnk_080DD364, 0x0DD364u, 1004u },
    { gUnk_080DD750, 0x0DD750u, 144u },
    { gUnk_080DD7E0, 0x0DD7E0u, 96u },
    { gUnk_080DD840, 0x0DD840u, 1608u },
    { gUnk_080DDE88, 0x0DDE88u, 384u },
    { gUnk_080DE008, 0x0DE008u, 472u },
    { gUnk_080DE1E0, 0x0DE1E0u, 32u },
    { gUnk_080DE200, 0x0DE200u, 712u },
    { gUnk_080DE4C8, 0x0DE4C8u, 844u },
    { gUnk_080DE814, 0x0DE814u, 4964u },
    { gUnk_080DFB78, 0x0DFB78u, 5316u },
    { gUnk_080E103C, 0x0E103Cu, 10260u },
    { gUnk_080E3850, 0x0E3850u, 5000u },
    { gUnk_080E4BD8, 0x0E4BD8u, 48u },
    { gUnk_080E4C08, 0x0E4C08u, 208u },
    { gUnk_080E4CD8, 0x0E4CD8u, 32u },
    { gUnk_080E4CF8, 0x0E4CF8u, 2408u },
    { gUnk_080E5660, 0x0E5660u, 32u },
    { gUnk_080E5680, 0x0E5680u, 1980u },
    { gUnk_080E5E3C, 0x0E5E3Cu, 32u },
    { gUnk_080E5E5C, 0x0E5E5Cu, 2784u },
    { gUnk_080E693C, 0x0E693Cu, 2128u },
    { gUnk_080E718C, 0x0E718Cu, 32u },
    { gUnk_080E71AC, 0x0E71ACu, 280u },
    { gUnk_080E72C4, 0x0E72C4u, 11736u },
    { gUnk_080EA09C, 0x0EA09Cu, 3276u },
    { gUnk_080EAD68, 0x0EAD68u, 80u },
    { gUnk_080EADB8, 0x0EADB8u, 72u },
    { gUnk_080EAE00, 0x0EAE00u, 96u },
    { gUnk_080EAE60, 0x0EAE60u, 96u },
    { gUnk_080EAEC0, 0x0EAEC0u, 96u },
    { gUnk_080EAF20, 0x0EAF20u, 1716u },
    { gUnk_080EB5D4, 0x0EB5D4u, 48u },
    { gUnk_080EB604, 0x0EB604u, 128u },
    { gUnk_080EB684, 0x0EB684u, 880u },
    { gUnk_080EB9F4, 0x0EB9F4u, 176u },
    { gUnk_080EBAA4, 0x0EBAA4u, 80u },
    { gUnk_080EBAF4, 0x0EBAF4u, 2008u },
    { gUnk_080EC2CC, 0x0EC2CCu, 540u },
    { gUnk_080EC4E8, 0x0EC4E8u, 824u },
    { gUnk_080EC820, 0x0EC820u, 576u },
    { gUnk_080ECA60, 0x0ECA60u, 1388u },
    { gUnk_080ECFCC, 0x0ECFCCu, 536u },
    { gUnk_080ED1E4, 0x0ED1E4u, 4400u },
    { gUnk_080EE314, 0x0EE314u, 712u },
    { gUnk_080EE5DC, 0x0EE5DCu, 320u },
    { gUnk_080EE71C, 0x0EE71Cu, 368u },
    { gUnk_080EE88C, 0x0EE88Cu, 112u },
    { gUnk_080EE8FC, 0x0EE8FCu, 32u },
    { gUnk_080EE91C, 0x0EE91Cu, 32u },
    { gUnk_080EE93C, 0x0EE93Cu, 32u },
    { gUnk_080EE95C, 0x0EE95Cu, 32u },
    { gUnk_080EE97C, 0x0EE97Cu, 32u },
    { gUnk_080EE99C, 0x0EE99Cu, 32u },
    { gUnk_080EE9BC, 0x0EE9BCu, 32u },
    { gUnk_080EE9DC, 0x0EE9DCu, 32u },
    { gUnk_080EE9FC, 0x0EE9FCu, 32u },
    { gUnk_080EEA1C, 0x0EEA1Cu, 32u },
    { gUnk_080EEA3C, 0x0EEA3Cu, 32u },
    { gUnk_080EEA5C, 0x0EEA5Cu, 32u },
    { gUnk_080EEA7C, 0x0EEA7Cu, 32u },
    { gUnk_080EEA9C, 0x0EEA9Cu, 32u },
    { gUnk_080EEABC, 0x0EEABCu, 176u },
    { gUnk_080EEB6C, 0x0EEB6Cu, 32u },
    { gUnk_080EEB8C, 0x0EEB8Cu, 32u },
    { gUnk_080EEBAC, 0x0EEBACu, 272u },
    { gUnk_080EECBC, 0x0EECBCu, 112u },
    { gUnk_080EED2C, 0x0EED2Cu, 78u },
    { gUnk_080EED7A, 0x0EED7Au, 18u },
    { gUnk_080EED8C, 0x0EED8Cu, 6340u },
    { gUnk_080F0650, 0x0F0650u, 432u },
    { gUnk_080F0800, 0x0F0800u, 80u },
    { gUnk_080F0850, 0x0F0850u, 32u },
    { gUnk_080F0870, 0x0F0870u, 32u },
    { gUnk_080F0890, 0x0F0890u, 96u },
    { gUnk_080F08F0, 0x0F08F0u, 48u },
    { gUnk_080F0920, 0x0F0920u, 128u },
    { gUnk_080F09A0, 0x0F09A0u, 792u },
    { gUnk_080F0CB8, 0x0F0CB8u, 160u },
    { gUnk_080F0D58, 0x0F0D58u, 176u },
    { gUnk_080F0E08, 0x0F0E08u, 20u },
    { gUnk_080F0E1C, 0x0F0E1Cu, 3660u },
    { gUnk_080F1C68, 0x0F1C68u, 32u },
    { gUnk_080F1C88, 0x0F1C88u, 264u },
    { gUnk_080F1D90, 0x0F1D90u, 32u },
    { gUnk_080F1DB0, 0x0F1DB0u, 32u },
    { gUnk_080F1DD0, 0x0F1DD0u, 932u },
    { gUnk_080F2174, 0x0F2174u, 32u },
    { gUnk_080F2194, 0x0F2194u, 32u },
    { gUnk_080F21B4, 0x0F21B4u, 472u },
    { gUnk_080F238C, 0x0F238Cu, 48u },
    { gUnk_080F23BC, 0x0F23BCu, 436u },
    { gUnk_080F2570, 0x0F2570u, 32u },
    { gUnk_080F2590, 0x0F2590u, 48u },
    { gUnk_080F25C0, 0x0F25C0u, 64u },
    { gUnk_080F2600, 0x0F2600u, 408u },
    { gUnk_080F2798, 0x0F2798u, 64u },
    { gUnk_080F27D8, 0x0F27D8u, 136u },
    { gUnk_080F2860, 0x0F2860u, 116u },
    { gUnk_080F28D4, 0x0F28D4u, 32u },
    { gUnk_080F28F4, 0x0F28F4u, 32u },
    { gUnk_080F2914, 0x0F2914u, 1304u },
    { gUnk_080F2E2C, 0x0F2E2Cu, 104u },
    { gUnk_080F2E94, 0x0F2E94u, 48u },
    { gUnk_080F2EC4, 0x0F2EC4u, 272u },
    { gUnk_080F2FD4, 0x0F2FD4u, 248u },
    { gUnk_080F30CC, 0x0F30CCu, 268u },
    { gUnk_080F31D8, 0x0F31D8u, 136u },
    { gUnk_080F3260, 0x0F3260u, 564u },
    { gUnk_080F3494, 0x0F3494u, 368u },
    { gUnk_080F3604, 0x0F3604u, 248u },
    { gUnk_080F36FC, 0x0F36FCu, 212u },
    { gUnk_080F37D0, 0x0F37D0u, 632u },
    { gUnk_080F3A48, 0x0F3A48u, 508u },
    { gUnk_080F3C44, 0x0F3C44u, 32u },
    { gUnk_080F3C64, 0x0F3C64u, 48u },
    { gUnk_080F3C94, 0x0F3C94u, 528u },
    { gUnk_080F3EA4, 0x0F3EA4u, 3300u },
    { gUnk_080F4B88, 0x0F4B88u, 456u },
    { gUnk_080F4D50, 0x0F4D50u, 96u },
    { gUnk_080F4DB0, 0x0F4DB0u, 32u },
    { gUnk_080F4DD0, 0x0F4DD0u, 32u },
    { gUnk_080F4DF0, 0x0F4DF0u, 32u },
    { gUnk_080F4E10, 0x0F4E10u, 160u },
    { gUnk_080F4EB0, 0x0F4EB0u, 96u },
    { gUnk_080F4F10, 0x0F4F10u, 1016u },
    { gUnk_080F5308, 0x0F5308u, 32u },
    { gUnk_080F5328, 0x0F5328u, 32u },
    { gUnk_080F5348, 0x0F5348u, 416u },
    { gUnk_080F54E8, 0x0F54E8u, 32u },
    { gUnk_080F5508, 0x0F5508u, 32u },
    { gUnk_080F5528, 0x0F5528u, 48u },
    { gUnk_080F5558, 0x0F5558u, 32u },
    { gUnk_080F5578, 0x0F5578u, 32u },
    { gUnk_080F5598, 0x0F5598u, 32u },
    { gUnk_080F55B8, 0x0F55B8u, 32u },
    { gUnk_080F55D8, 0x0F55D8u, 136u },
    { gUnk_080F5660, 0x0F5660u, 248u },
    { gUnk_080F5758, 0x0F5758u, 48u },
    { gUnk_080F5788, 0x0F5788u, 32u },
    { gUnk_080F57A8, 0x0F57A8u, 32u },
    { gUnk_080F57C8, 0x0F57C8u, 32u },
    { gUnk_080F57E8, 0x0F57E8u, 64u },
    { gUnk_080F5828, 0x0F5828u, 32u },
    { gUnk_080F5848, 0x0F5848u, 32u },
    { gUnk_080F5868, 0x0F5868u, 32u },
    { gUnk_080F5888, 0x0F5888u, 32u },
    { gUnk_080F58A8, 0x0F58A8u, 660u },
    { gUnk_080F5B3C, 0x0F5B3Cu, 660u },
    { gUnk_080F5DD0, 0x0F5DD0u, 152u },
    { gUnk_080F5E68, 0x0F5E68u, 852u },
    { gUnk_080F61BC, 0x0F61BCu, 296u },
    { gUnk_080F62E4, 0x0F62E4u, 64u },
    { gUnk_080F6324, 0x0F6324u, 576u },
    { gUnk_080F6564, 0x0F6564u, 32u },
    { gUnk_080F6584, 0x0F6584u, 296u },
    { gUnk_080F66AC, 0x0F66ACu, 2524u },
    { gUnk_080F7088, 0x0F7088u, 32u },
    { gUnk_080F70A8, 0x0F70A8u, 48u },
    { gUnk_080F70D8, 0x0F70D8u, 1064u },
    { gUnk_080F7500, 0x0F7500u, 80u },
    { gUnk_080F7550, 0x0F7550u, 304u },
    { gUnk_080F7680, 0x0F7680u, 320u },
    { gUnk_080F77C0, 0x0F77C0u, 48u },
    { gUnk_080F77F0, 0x0F77F0u, 32u },
    { gUnk_080F7810, 0x0F7810u, 80u },
    { gUnk_080F7860, 0x0F7860u, 64u },
    { gUnk_080F78A0, 0x0F78A0u, 992u },
    { gUnk_080F7C80, 0x0F7C80u, 80u },
    { gUnk_080F7CD0, 0x0F7CD0u, 160u },
    { gUnk_080F7D70, 0x0F7D70u, 80u },
    { gUnk_080F7DC0, 0x0F7DC0u, 684u },
    { gUnk_080F806C, 0x0F806Cu, 628u },
    { gUnk_080F82E0, 0x0F82E0u, 336u },
    { gUnk_080F8430, 0x0F8430u, 424u },
    { gUnk_080F85D8, 0x0F85D8u, 32u },
    { gUnk_080F85F8, 0x0F85F8u, 3340u },
    { gUnk_080F9304, 0x0F9304u, 2292u },
    { gUnk_080F9BF8, 0x0F9BF8u, 912u },
    { gUnk_080F9F88, 0x0F9F88u, 32u },
    { gUnk_080F9FA8, 0x0F9FA8u, 1576u },
    { gUnk_080FA5D0, 0x0FA5D0u, 1768u },
    { gUnk_080FACB8, 0x0FACB8u, 144u },
    { gUnk_080FAD48, 0x0FAD48u, 668u },
    { gUnk_080FAFE4, 0x0FAFE4u, 32u },
    { gUnk_080FB004, 0x0FB004u, 8564u },
    { gUnk_080FEAC8, 0x0FEAC8u, 288u },
    { gUnk_080FEBE8, 0x0FEBE8u, 64u },
    { gUnk_080FEC28, 0x0FEC28u, 160u },
    { gUnk_080FECC8, 0x0FECC8u, 80u },
    { gUnk_080FED18, 0x0FED18u, 64u },
    { gUnk_080FED58, 0x0FED58u, 192u },
    { gUnk_080FEE18, 0x0FEE18u, 32u },
    { gUnk_080FEE38, 0x0FEE38u, 16u },
    { gUnk_080FEE48, 0x0FEE48u, 16u },
    { gUnk_080FEE58, 0x0FEE58u, 32u },
    { gUnk_080FEE78, 0x0FEE78u, 39148u },
    { gUnk_08108764, 0x108764u, 1740u },
    { gUnk_08108E30, 0x108E30u, 24u },
    { gUnk_08108E48, 0x108E48u, 24u },
    { gUnk_08108E60, 0x108E60u, 10272u },
    { gUnk_0810B680, 0x10B680u, 108u },
    { gUnk_0810B740, 0x10B740u, 16u },
    { gUnk_0810B748, 0x10B748u, 16u },
    { gUnk_0810B74A, 0x10B74Au, 42u },
    { gUnk_0810B78C, 0x10B78Cu, 16u },
    { gUnk_0810B790, 0x10B790u, 42u },
    { gUnk_0810B7BA, 0x10B7BAu, 16u },
    { gUnk_0810B7C0, 0x10B7C0u, 2728u },
    { gUnk_0810C268, 0x10C268u, 13396u },
    { gUnk_0810F6BC, 0x10F6BCu, 920u },
    { gUnk_0810FA54, 0x10FA54u, 16u },
    { gUnk_0810FA5A, 0x10FA5Au, 5882u },
    { gUnk_08111154, 0x111154u, 44260u },
    { gUnk_0811BE38, 0x11BE38u, 16u },
    { gUnk_0811BE40, 0x11BE40u, 18780u },
    { gUnk_0812079C, 0x12079Cu, 16u },
    { gUnk_081207A4, 0x1207A4u, 16u },
    { gUnk_081207AC, 0x1207ACu, 9012u },
    { gUnk_08122AE0, 0x122AE0u, 16u },
    { gUnk_08122AE8, 0x122AE8u, 16u },
    { gUnk_08122AF8, 0x122AF8u, 16u },
    { gUnk_08122B00, 0x122B00u, 16u },
    { gUnk_08122B0E, 0x122B0Eu, 16u },
    { gUnk_08122B1E, 0x122B1Eu, 16u },
    { gUnk_08122B2E, 0x122B2Eu, 16u },
    { gUnk_08122B3C, 0x122B3Cu, 18356u },
    { gUnk_081272F0, 0x1272F0u, 852u },
    { gUnk_08127644, 0x127644u, 852u },
    { gUnk_08127998, 0x127998u, 852u },
    { gUnk_08127CEC, 0x127CECu, 20u },
    { gUnk_08127D00, 0x127D00u, 16u },
    { gUnk_08127D10, 0x127D10u, 3368u },
    { gUnk_08128C00, 0x128C00u, 16u },
    { gUnk_08128C04, 0x128C04u, 16u },
    { gUnk_08128C14, 0x128C14u, 128u },
    { gUnk_08128C94, 0x128C94u, 128u },
    { gUnk_08128D38, 0x128D38u, 16u },
    { gUnk_08128D3C, 0x128D3Cu, 16u },
    { gUnk_08128D43, 0x128D43u, 16u },
    { gUnk_08128D51, 0x128D51u, 16u },
    { gUnk_08128D60, 0x128D60u, 16u },
    { gUnk_08128D70, 0x128D70u, 64u },
    { gUnk_08128DB8, 0x128DB8u, 16u },
    { gUnk_08128DBC, 0x128DBCu, 16u },
    { gUnk_08128DD4, 0x128DD4u, 16u },
    { gUnk_08128DD8, 0x128DD8u, 16u },
    { gUnk_08128DE8, 0x128DE8u, 144u },
    { gUnk_08128E80, 0x128E80u, 16u },
    { gUnk_08128E84, 0x128E84u, 16u },
    { gUnk_08128E94, 0x128E94u, 136u },
    { gUnk_08128F38, 0x128F38u, 20u },
    { gUnk_08128F4C, 0x128F4Cu, 16u },
    { gUnk_08128F58, 0x128F58u, 80u },
    { gUnk_08128FA8, 0x128FA8u, 24u },
    { gUnk_08128FC0, 0x128FC0u, 24u },
    { gUnk_08128FD8, 0x128FD8u, 44u },
    { gUnk_08129004, 0x129004u, 38672u },
    { gUnk_08132714, 0x132714u, 1044u },
    { gUnk_08132B28, 0x132B28u, 9364u },
    { gUnk_085A97A0, 0x5A97A0u, 43488u },
    { gUnk_085B4180, 0x5B4180u, 65536u },
    { gUnk_085C4620, 0x5C4620u, 65536u },
    { gUnk_086D4460, 0x6D4460u, 65536u },
    { gUnk_086E8460, 0x6E8460u, 65536u },
    { gUnk_089FD1B4, 0x9FD1B4u, 320u },
    { gUnk_089FD2F4, 0x9FD2F4u, 33885u },
    { gUnk_08A05751, 0xA05751u, 4462u },
    { gUnk_08A068BF, 0xA068BFu, 4096u },
};

void Port_InitRomStubs(const u8* romData, u32 romSize) {
    if (!romData || romSize == 0) return;
    for (size_t i = 0; i < sizeof(sRomStubs) / sizeof(sRomStubs[0]); i++) {
        if (sRomStubs[i].rom_offset + sRomStubs[i].size <= romSize) {
            memcpy(sRomStubs[i].dst, romData + sRomStubs[i].rom_offset, sRomStubs[i].size);
        }
    }
}
