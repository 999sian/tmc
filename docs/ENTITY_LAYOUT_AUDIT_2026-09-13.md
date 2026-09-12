# Entity layout audit — 2026-09-13

Follow-up to the Minish Woods ChuChu activation-range fix. Three parallel
reviews and a coordinator pass checked similar PC64 writer/reader mismatches.
Previously completed local changes were preserved.

## Confirmed fix

`src/npc/sittingPerson.c:sub_0806390C` wrote `unk_84[5] = 1` through a `u32*`
script-context pointer. The intended field is `ScriptExecutionContext.condition`.
The script pointer's expansion moves that field from byte 20 on GBA to byte 24
on PC. The old write instead overwrote PC wait-state fields and left the branch
condition unset. This callback is referenced by the active dialogue table.

The entity now stores a typed `ScriptExecutionContext*` and writes `condition`.
This preserves the GBA field's meaning and the existing first/repeat dialogue
branches. No unrelated AI, dialogue content or allocator behavior changed.

`tools/npc_context_regression_test.py` compiles the production callback for
USA/EU/JP. It checks the condition, unchanged wait/auxiliary fields, and repeat
interaction. The pre-fix callback failed the condition assertion; all fixed
cases pass. The new game build succeeds with existing initializer warnings.

## Coverage

- Enemy subclasses, shared enemy helpers, activation bounds, parent/child
  metadata, pointer arithmetic and heap indexing.
- Room-spawn writes and manager/object handoffs; script-context storage and
  access; raw word-index accesses to expanded structs.
- Objects, NPCs, items and player-item cross-entity casts, including Gyorg,
  Gust Jar particles, enemy drops and Octorok boss handoffs.
- Generic field accessors, player/entity pool operations, physics/collision
  field access, and existing PC guards on GBA raw-offset paths.
- Compiled sizes of 286 source-defined entity/manager subtype layouts: none
  exceeded their entity (184-byte) or manager (128-byte) pool slot. Manager
  header types were checked separately; the largest inspected are 120 bytes.
  This checks compiled sizes, not every possible runtime write.

## Investigated without changes

- Apparent phonograph pointer truncation belongs to a non-PC branch; the PC
  implementation already uses typed indexing.
- Delayed entity loading does not leak a context on entity-allocation failure:
  lookup does not reserve a context, and initialization happens only after
  an entity is created. A production-allocator check confirmed this contract.
- Manager29 has a possible shifted spawn-data layout, but no spawning records
  were found and the source identifies it as unused. No speculative patch.
- The raw-copy helper `sub_080451CC` has no callers or function-table references
  in the inspected tree. No speculative patch.
- Other reviewed suspicious casts matched their producers/consumers or already
  had PC-specific fixes.

This is a bounded source/layout audit, not proof that all gameplay defects are
absent. No complete playthrough or natural dialogue interaction was performed.
The callback regression executes the real production callback with fixture
context and controlled flag/message dependencies.

Temporary size-scan evidence: `/tmp/tmc-layout-scan-h5y1cu05/results.json` plus
its separately compiled `LikeLikeEntity` result (176 bytes). One apparent scan
entry was a commented-out typedef and was excluded. Build log:
`/tmp/tmc-layout-audit-build.log`.
