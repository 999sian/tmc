# Configuration reference (v0.1.0)

Open the second-screen panel and select **CONFIG** (in DUAL/FLIP it is the
rightmost tab; settings labels follow the selected LANGUAGE). All settings
apply immediately and persist across restarts unless noted. Values below are
the English labels.

## GAMEPLAY

| Setting | Values | Default | What it does |
|---|---|---|---|
| WINDCREST PINS | ON / OFF | ON | Shows windcrest pins on the second-screen map. |
| FLOOR RETURN | ON / OFF | ON | Dungeon map returns to your current floor automatically after browsing another floor. |
| HOLD TO ADVANCE | ON / OFF | OFF | Hold A to keep advancing dialogue text instead of pressing per box. |
| STORY GUIDE | OFF / HINT / GUIDED | HINT | How much the QUEST tab tells you. HINT answers "what now?" without exact directions; GUIDED is more explicit; OFF hides guidance. |
| ACTION HINT | OFF / CONTEXTUAL / ON | CONTEXTUAL | The R-button action hint on the panel sidebar. OFF never shows it; CONTEXTUAL shows it only when an action is available (original behavior); ON keeps the R plate visible during normal gameplay, with the action label still appearing only when there is one. Visibility only — never changes controls. |
| ENHANCED PROFILE | CRISP / SMOOTH | CRISP | Audio resampler flavor. Only visible while AUDIO MODE is ENHANCED. |

## CONTROLS

| Setting | Values | Default | What it does |
|---|---|---|---|
| CONTROLS REFERENCE | — | — | Read-only reference screen of the full layout. |
| GBA A / GBA B / GBA L / GBA R / START / SELECT | any pad button/stick | A / B / L / R / PLUS / MINUS | Rebind the six GBA controls. |
| TALK TO EZLO | OFF / CONTEXT L / X / Y / ZL / ZR / LSTICK / RSTICK | CONTEXT L | Dedicated Ezlo-talk shortcut (the GBA used SELECT; MINUS opens Settings on this port). CONTEXT L shares L with Fuse contextually. |
| QUICK DISPLAY | PLUS + ZR (fixed) | — | Read-only chord that quickly switches the display mode. |
| X / Y / ZL / ZR SHORTCUT | cycles which item is in the slot | unassigned | Extra item-equip slots on the four free buttons — physical X, Y, ZL and ZR respectively. These rows choose the **item**; the button itself is fixed. A/B stay the primary GBA equipment. |
| RESET CONTROLS | — | — | Restores all bindings to defaults. |

## DISPLAY

| Setting | Values | Default | What it does |
|---|---|---|---|
| FOLLOW CAM | ON / OFF | ON | Second-screen map follows the player. |
| PANEL BACKDROP | parchment pattern / cream / dark | parchment pattern | Background style of the second-screen panel. |
| DISPLAY MODE | NORMAL / DUAL / FLIP 270 | NORMAL | The overall presentation (see README). |
| SCREEN SCALE *(NORMAL only)* | FIT / 2X / 3X / 4X | FIT | Integer scaling for single-screen play. |
| GAME SCALE *(DUAL only)* | 2X / 3X | 2X | Game viewport scale in DUAL (2X = 480×320, 3X = 720×480). |
| PANEL SIZE *(DUAL only)* | 70 / 75 / 80 / 90 % | 90 | Second-screen panel size in DUAL. Choices are capped so the pair always fits the screen at the current GAME SCALE. |
| SCREEN GAP *(DUAL only)* | 0 / 8 / 16 / 24 / 32 / 48 / 64 px | 64 | Space between game and panel in DUAL. |
| PANEL SIZE *(FLIP only)* | 70–100 % in steps of 5 | 75 | Panel size in FLIP 270. |
| SCREEN GAP *(FLIP only)* | 0 / 8 / 16 / 24 / 32 / 48 / 64 / 80 px | 24 | Space between game and panel in FLIP 270. |

Mode-specific rows only appear while that mode is active, so you always see
just the values that can take effect.

## ACHIEVEMENTS

| Setting | Values | Default | What it does |
|---|---|---|---|
| TOAST STYLE | 6 styles | style 2 | Visual style of the unlock notification. |
| ACCOUNT | — | logged out | Log in to RetroAchievements (on-screen keyboard). While logged in it shows your account status. |
| TEST NOTIFICATION | FIRE | — | Previews the unlock toast locally. Never contacts the server and never awards anything. |
| LOG OUT | — | — | Appears only while logged in. Removes only the stored RA token; saves, config and server-side achievements are untouched. |

## SYSTEM

| Setting | Values | Default | What it does |
|---|---|---|---|
| LANGUAGE | ENGLISH / ESPAÑOL / FRANÇAIS / DEUTSCH / ITALIANO | ENGLISH | Language of the port's own UI (panel, settings, Story Guide). The game's original dialogue language is the ROM's own. |
| AUTOSAVE | ON / OFF | OFF | Port-managed autosave to its own file (`autosave.bin`). Never touches the game's normal save. |
| LOAD AUTOSAVE | READY (action) | — | Restores the most recent port autosave. A tap with no autosave yet does nothing. |
| AUDIO MODE | ENHANCED / GBA ACCURATE | ENHANCED | Modern mixing/resampling vs. GBA-faithful audio path. |
| RETURN TO TITLE | CONFIRM | — | Returns safely to the title screen (with confirmation). Config is preserved; unsaved game progress behaves as a normal reset to title. |
| VERSION | V0.1.0 | — | Read-only version display. |

## Notes

- Settings are stored in `/switch/tmc/config.json`. Deleting it restores all
  defaults (bindings included).
- There is no restart requirement for any setting listed above.
