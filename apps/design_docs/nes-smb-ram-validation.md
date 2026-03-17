# NES SMB RAM Validation

This document tracks the Super Mario Bros. RAM fields DirtSim currently reads, what the current
code thinks they mean, what the local Data Crystal RAM map says they mean, and how confident we are
that each interpretation is correct.

The goal is to keep extractor changes tied to evidence instead of folklore, especially because
these fields feed training rewards, special senses, and live latency probes.

## Sources

- Current extractor: [NesSuperMarioBrosRamExtractor.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/nes/NesSuperMarioBrosRamExtractor.cpp)
- Current response probe: [NesSuperMarioBrosResponseProbe.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/nes/NesSuperMarioBrosResponseProbe.cpp)
- Probe tests and artifact generation: [NesSuperMarioBrosRamProbe_test.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/tests/NesSuperMarioBrosRamProbe_test.cpp)
- External reference used for cross-checking in this repo work:
  `/home/oldman/Downloads/Super Mario Bros._RAM map - Data Crystal.html`

## Status Legend

- `Confirmed`: code meaning matches the doc and we have direct probe evidence.
- `Likely`: code meaning matches the doc, but test coverage is still indirect or scenario-limited.
- `Disputed`: current code meaning conflicts with probe evidence or the doc.
- `Unknown`: not enough evidence yet.

## Production Extractor Fields

| Output field | Address(es) | Current code meaning | Data Crystal meaning | Status | Validation |
| --- | --- | --- | --- | --- | --- |
| `phase` | `0x0770` + setup gate | Gameplay vs non-gameplay. | `0x0770` is game mode. | `Likely` | Coarse behavior works in [NesSuperMarioBrosRamProbe_test.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/tests/NesSuperMarioBrosRamProbe_test.cpp#L522), but semantics are broader than our current name. |
| `lifeState` | `0x000E` | Coarse life-state: `Alive` for normal/pipe/vine/transform/autowalk states, `Dying` for `0x06/0x0B`, `Dead` for the post-life-loss reload state. | General player state (`0x08 = Normal`, `0x06 = Player dies`, pipe/vine/transform states). | `Likely` | Validated against the scripted death sequence with the life-loss artifact and regression tests in [NesSuperMarioBrosRamProbe_test.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/tests/NesSuperMarioBrosRamProbe_test.cpp) and covered by [NesSuperMarioBrosRamExtractor_test.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/tests/NesSuperMarioBrosRamExtractor_test.cpp). |
| `powerupState` | `0x0756` | `0 = Small`, `1 = Big`, `>= 2 = Fire`. | `0 = Small`, `1 = Big`, `> 2 = fiery`. | `Likely` | Matches doc closely. Covered indirectly in baseline and button-mask probes. |
| `airborne` | `0x001D` | `0x01/0x02` means airborne, `0x00` means grounded. | Player float state: jumping/falling vs standing. | `Confirmed` | Validated by the standing-jump artifact test and the automated airborne regression in [NesSuperMarioBrosRamProbe_test.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/tests/NesSuperMarioBrosRamProbe_test.cpp#L954) and [NesSuperMarioBrosRamProbe_test.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/tests/NesSuperMarioBrosRamProbe_test.cpp#L1090). |
| `horizontalSpeedNormalized` | `0x0057` | Signed horizontal speed. | Player horizontal speed. | `Confirmed` | Left-movement probe validates sign and turnaround behavior in [NesSuperMarioBrosRamProbe_test.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/tests/NesSuperMarioBrosRamProbe_test.cpp#L831). |
| `verticalSpeedNormalized` | `0x009F` | Signed vertical speed. | Player vertical velocity in whole pixels. | `Confirmed` | Standing-jump artifact test shows expected upward negative values during takeoff in [NesSuperMarioBrosRamProbe_test.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/tests/NesSuperMarioBrosRamProbe_test.cpp#L954). |
| `world` | `0x075F` | Current world. | World. | `Likely` | Matches doc and baseline probe assumptions in [NesSuperMarioBrosRamProbe_test.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/tests/NesSuperMarioBrosRamProbe_test.cpp#L555). |
| `level` | `0x0760` | Current level. | Level. | `Likely` | Matches doc and baseline probe assumptions in [NesSuperMarioBrosRamProbe_test.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/tests/NesSuperMarioBrosRamProbe_test.cpp#L558). |
| `absoluteX` | `0x006D + 0x0086` | Level-space X position. | Level position plus screen X. | `Confirmed` | Explicitly cross-checked in extractor replay in [NesSuperMarioBrosRamProbe_test.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/tests/NesSuperMarioBrosRamProbe_test.cpp#L480). |
| `facingX` | `0x0033` | Normalized facing direction for training (`1 = right`, `-1 = left`, `0 = unknown`). | Facing direction (`1 = Right`, `2 = Left`, `0 = off-screen`). | `Confirmed` | Backed by the left-turn probe and now fed into the dedicated RNN `facing_x` slot via [NesSuperMarioBrosGameAdapter.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/nes/NesSuperMarioBrosGameAdapter.cpp). |
| `movementX` | `0x0045` | Normalized committed movement direction for training (`1 = right`, `-1 = left`, `0 = unknown`). | Moving direction (`1 = Right`, `2 = Left`). | `Confirmed` | Backed by the left-turn probe and now exposed separately from `facingX` through [NesSuperMarioBrosGameAdapter.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/nes/NesSuperMarioBrosGameAdapter.cpp). |
| `playerXScreen` | `0x0086` | Screen-space X position. | Player x position on screen. | `Likely` | Matches doc and is used by baseline/left probes. |
| `playerYScreen` | `0x00CE` | Screen-space Y position. | Player y position on screen. | `Confirmed` | Standing-jump artifacts visibly match changes in `0x00CE`. |
| `lives` | `0x075A` | Current lives. | Lives. | `Likely` | Baseline probe tracks life loss in [NesSuperMarioBrosRamProbe_test.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/tests/NesSuperMarioBrosRamProbe_test.cpp#L430). |
| `enemyPresent`, nearest enemy slots | `0x000F-0x0013`, `0x0016-0x001A`, `0x006E-0x0072`, `0x0087-0x008B`, `0x00CF-0x00D3` | Active/type/X/Y for nearest two enemies. | Enemy draw/type/position fields. | `Likely` | Dedicated early-1-1 goomba artifacts and a regression now validate the `1 / 1 / 3` enemy windows and nearest-enemy ordering in [NesSuperMarioBrosRamProbe_test.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/tests/NesSuperMarioBrosRamProbe_test.cpp). |

## Probe-Only Fields

These are not currently part of `NesSuperMarioBrosState`, but we read them for response/latency
instrumentation.

| Probe field | Address(es) | Current code meaning | Data Crystal meaning | Status | Validation |
| --- | --- | --- | --- | --- | --- |
| Duck state | `0x0714` | `0x04` means ducking. | Same, with caveats for small Mario and edge-slide behavior. | `Likely` | Works for big-Mario probe cases, but should be validated with a dedicated artifact pass if it becomes training-critical. |
| P1 button mask | `0x074A` | SMB’s internal button mask. | Buttons pressed player 1. | `Confirmed` | Directly validated in [NesSuperMarioBrosRamProbe_test.cpp](/home/data/workspace/dirtsim2/apps/src/core/scenarios/tests/NesSuperMarioBrosRamProbe_test.cpp#L1090). |
| P2 button mask | `0x074B` | SMB’s internal P2 button mask. | Buttons pressed player 2. | `Likely` | Confirmed zero in P1-only tests. |

## Immediate Conclusions

- `airborne` now comes from `0x001D`, which matches the standing-jump validation artifacts.
- `lifeState` is now narrowly derived from the explicit death sequence in `0x000E` instead of
  treating unrelated player modes as dying.
- `0x0033` now feeds the dedicated RNN `facing_x` channel, and `0x0045` is exposed separately as
  a committed movement-direction sense.
- Enemy decoding now has scenario-backed validation, though it still only covers the early 1-1
  goomba windows.

## Suggested Next Changes

1. Add a small transition-state probe for pipe/vine/power-up cases if they become training
   critical.
2. Add a later-level enemy pass if we need stronger confidence for mixed enemy types or larger
   enemy counts.
