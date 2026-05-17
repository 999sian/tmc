# Interp-prediction validation

- Total interp predictions: 309
- Predictions landing in a named USA symbol: 309 (100%)
- Predictions landing in unmapped USA space (likely wrong): 0

## Top symbols hit by interp predictions

| count | USA symbol |
|------:|-----------|
|   136 | `gFuseActions` |
|    18 | `gUnk_additional_8_SimonsSimulation_Main` |
|    13 | `gFrameObjLists` |
|     9 | `gUnk_080FEC28` |
|     5 | `gUnk_080DAB44` |
|     5 | `gObjectDefinition_0` |
|     5 | `gItemMetaData` |
|     4 | `Entities_DeepwoodShrine_Torch_0` |
|     4 | `gUnk_additional_f_HyruleTown_0` |
|     3 | `gUnk_080FE1DD` |
|     3 | `gTreeItemDrops` |
|     3 | `gUnk_080CC954` |
|     3 | `gHoleTransitions` |
|     3 | `gSpriteAnimations_Wheaton` |
|     3 | `gObjectDefinitions` |
|     3 | `gCollisionMtx` |
|     3 | `gSpriteAnimations_322` |
|     2 | `gUnk_080F2FD4` |
|     2 | `Entities_HouseInteriors2_Dampe_1` |
|     2 | `Room_RoyalCrypt_MushroomPit` |
|     2 | `Entities_RoyalCrypt_Entrance_0` |
|     2 | `Entities_FortressOfWindsTop_Main_0` |
|     2 | `Entities_MinishRafters_Bakery_0` |
|     2 | `Room_HouseInteriors1_InnWestRoom` |
|     2 | `Room_HouseInteriors1_InnMiddleRoom` |
|     2 | `Room_HouseInteriors1_InnEastRoom` |
|     2 | `gUnk_080D6784` |
|     2 | `gPalette_12` |
|     2 | `Subtask_FastTravel_Functions` |
|     2 | `gUnk_080F85D8` |

## Sample patch lines per top symbol

### `gFuseActions` (136 predictions)

```
ROM Buildfile.event:101  ORG $C93E4+(8*1)+1; BYTE 0x8; ORG $C93E4+(8*1)+5; BYTE 0x1
ROM Buildfile.event:101  ORG $C93E4+(8*1)+1; BYTE 0x8; ORG $C93E4+(8*1)+5; BYTE 0x1
ROM Buildfile.event:102  ORG $C93E4+(8*2)+1; BYTE 0x8; ORG $C93E4+(8*2)+5; BYTE 0x1
ROM Buildfile.event:102  ORG $C93E4+(8*2)+1; BYTE 0x8; ORG $C93E4+(8*2)+5; BYTE 0x1
ROM Buildfile.event:103  ORG $C93E4+(8*3)+1; BYTE 0x8; ORG $C93E4+(8*3)+5; BYTE 0x1
ROM Buildfile.event:103  ORG $C93E4+(8*3)+1; BYTE 0x8; ORG $C93E4+(8*3)+5; BYTE 0x1
```

### `gUnk_additional_8_SimonsSimulation_Main` (18 predictions)

```
ROM Buildfile.event:622  ORG $F04A2
ROM Buildfile.event:624  ORG $F04AA
ROM Buildfile.event:626  ORG $F04B2
ROM Buildfile.event:629  ORG $F0482
ROM Buildfile.event:631  ORG $F048A
ROM Buildfile.event:634  ORG $F045A
```

### `gFrameObjLists` (13 predictions)

```
ROM Buildfile.event:410  ORG $31EB51; BYTE 0xF6
hash.event:77  ORG $31EECA; BYTE 0x7C
hash.event:78  ORG $31EECF; BYTE 0x7C
hash.event:79  ORG $31EED4; BYTE 0x7C
hash.event:80  ORG $31EED9; BYTE 0x7C
hash.event:81  ORG $31EEDE; BYTE 0x7C
```

### `gUnk_080FEC28` (9 predictions)

```
golden.event:66  ORG $FE16C + 4 + (0x10 * 0); SHORT 0 //flag 0x31
golden.event:67  ORG $FE16C + 4 + (0x10 * 1); SHORT 0 //flag 0x32
golden.event:68  ORG $FE16C + 4 + (0x10 * 2); SHORT 0 //flag 0x33
golden.event:69  ORG $FE16C + 4 + (0x10 * 3); SHORT 0 //flag 0x34
golden.event:70  ORG $FE16C + 4 + (0x10 * 4); SHORT 0 //flag 0x35
golden.event:71  ORG $FE16C + 4 + (0x10 * 5); SHORT 0 //flag 0x36
```

### `gUnk_080DAB44` (5 predictions)

```
ROM Buildfile.event:91  ORG $DA280+3; BYTE 0x5B 0x00	//nut
installer.event:169  PUSH; ORG $DA280+2; BYTE 0; ORG $DA280+5; BYTE 4; ORG $DA280+8; SHORT 0x3C; ORG $DA280+0xE; SHORT 0xCA; POP //made the n
installer.event:169  PUSH; ORG $DA280+2; BYTE 0; ORG $DA280+5; BYTE 4; ORG $DA280+8; SHORT 0x3C; ORG $DA280+0xE; SHORT 0xCA; POP //made the n
installer.event:169  PUSH; ORG $DA280+2; BYTE 0; ORG $DA280+5; BYTE 4; ORG $DA280+8; SHORT 0x3C; ORG $DA280+0xE; SHORT 0xCA; POP //made the n
installer.event:169  PUSH; ORG $DA280+2; BYTE 0; ORG $DA280+5; BYTE 4; ORG $DA280+8; SHORT 0x3C; ORG $DA280+0xE; SHORT 0xCA; POP //made the n
```

### `gObjectDefinition_0` (5 predictions)

```
ROM Buildfile.event:391  ORG $125DFC; BYTE 2 //new palette ids
installer.event:249  PUSH; ORG $125EB8; SHORT 0x001 0x401 0x0B8 0x2541 0x001 0x401 0x0B8 0x2541 0x001 0x401 0x0B8 0x2541; POP
progressiveItems.event:259  PUSH; ORG $125B48; BYTE 1 0 1 4 0 0 0x41 1; POP
timedohko.event:51  ORG $125BE0
traps.event:97  PUSH; ORG $125BF8; BYTE 1 0 1 4 0 0 0x41 5; POP
```

### `gItemMetaData` (5 predictions)

```
firerodInventory.event:16  ORG $FCBA8; BYTE 0x12
installer.event:404  ORG $FCE5B; BYTE 0x00
installer.event:405  ORG $FCE63; BYTE 0x00
installer.event:406  ORG $FCE6B; BYTE 0x00
installer.event:407  ORG $FCE73; BYTE 0x00
```

### `Entities_DeepwoodShrine_Torch_0` (4 predictions)

```
installer.event:351  ORG $DE973; BYTE 4
installer.event:352  ORG $DE983; BYTE 4
installer.event:353  ORG $DE993; BYTE 4
installer.event:354  ORG $DE9A3; BYTE 4
```

### `gUnk_additional_f_HyruleTown_0` (4 predictions)

```
installer.event:176  PUSH; ORG $EEA12; SHORT 0xFFFF; POP //soldier that wants you to spin is in all town states
installer.event:178  PUSH; ORG $EEA32; SHORT 0; POP
installer.event:179  PUSH; ORG $EEA82; SHORT 0; POP
installer.event:180  PUSH; ORG $EEA92; SHORT 0; POP
```

### `gUnk_080FE1DD` (3 predictions)

```
ROM Buildfile.event:500  ORG $FD721
ROM Buildfile.event:507  ORG $FD761
ROM Buildfile.event:514  ORG $FD7A1
```
