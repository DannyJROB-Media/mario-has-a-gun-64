# Contributing to Mario Has a Gun 64

This project is meant to be picked up by the community. You can send pull requests here, or fork it
and take it in your own direction. Both are welcome.

## The design rule

> **GoldenEye *in* Mario 64, not Mario 64 *in* GoldenEye.**

The game should still be Super Mario 64: its levels, music, enemies, bosses, progression, power meter,
HUD and Mario's voice. GoldenEye adds the first-person view, the guns, the gun sounds and the shooting.
When a change could go either way, choose the option that keeps it feeling like Mario 64.

## Two hard rules

1. **Never commit anything from either ROM.** That includes extracted textures, models, sounds,
   converted gun data (`bin/ge64_*.c`, `src/game/ge64_guns/<gun>.inc.h`), the sound samples in
   `sound/samples/sfx_custom_ge/`, and ROM files. `.gitignore` covers the generated paths. If you add
   new assets from either game, generate them in `tools/ge64/setup.sh`.
2. **Contributions are public domain.** By submitting a contribution, you dedicate it to the public
   domain under [CC0 1.0](LICENSE), the same as the rest of the project's original work. Only submit
   work you made yourself or that is already public domain.

## Getting set up

Follow **Building** in [README.md](README.md). In short: put your two ROMs in the repo, run
`tools/ge64/setup.sh`, then run `make`.

- `make -j$(nproc)` builds the normal ROM into `build/us/sm64.us.z64`.
- `make GE64_LEVEL_SELECT=1 -j$(nproc)` builds into `build_levelselect/us/sm64.us.z64`. This build
  boots into the level select and adds the D-pad debug helpers listed in the README. It's the fastest
  way to test a level or boss.
- Run `setup.sh` again after changing anything under `tools/ge64/`. It is safe to re-run, and it
  produces identical output on every machine. If your change doesn't touch generated data, a second
  run should leave `git status` clean.

### Testing

- The Expansion Pak is required. In emulators, enable 8 MB RAM.
- [ares](https://ares-emu.net/) is accurate and has a GDB stub, which is handy for crashes. Some bugs
  only show up on real hardware, such as the RSP reading memory the CPU has just overwritten. If you
  have a flash cart, test on a console before calling something done.
- Before opening a pull request, play through the area you changed in the **normal** build, not
  only in the level-select build.

## Code guidelines

- **C89 only.** The game is compiled with IDO 5.3: declare variables at the top of each block (no
  declarations after statements), and don't use `inline` or designated initialisers. For
  alignment, use the codebase's macros (`ALIGNED16` and so on), not `__attribute__`.
- **Keep hooks into the original code small, and mark them.** New logic belongs in
  `src/game/ge64*.c`. Original files should only gain short calls into it, each with a `GE64` comment,
  so the diff against the decomp stays easy to read.
- **Route through the game's own logic.** Bosses, for example, are beaten by feeding shots into each
  boss's original defeat path (a throw, a ground pound, a bomb hit). That way health, cutscenes and
  star spawns keep working. Prefer this over writing new win conditions.
- **Watch memory.** Gun models are loaded per level into fixed buffers, and the title screen's memory
  pool belongs to Goddard's Mario head. Check pool usage in busy levels when you add anything big.

## Where things live

| Path | What |
| --- | --- |
| `src/game/ge64.c` | First-person actions, camera, controls, weapon table, hitscan, viewmodel, HUD, title-screen gun |
| `src/game/ge64_bosses.c` | Boss adaptations |
| `src/game/ge64_guns/poses.inc.h` | Glove grip and on-screen position per gun (generated from `tools/ge64/gun_poses.json`) |
| `tools/ge64/setup.sh` | The asset pipeline entry point |
| `tools/ge64/convert_ge_gun.py` | GoldenEye gun model (from the 007 decomp) to an SM64 display list segment |
| `tools/ge64/n64bank_extract.py`, `import_ge_sfx.py` | GoldenEye sound bank to SM64 sound player |
| `tools/ge64/getex/` | GoldenEye's texture decompressor, built as a host tool |
| `tools/ge64/gen_logo3d.py`, `title_bg.py`, `art/` | Title logo and backdrop |
| `tools/ge64/mario_suit.py` | The tuxedo (the model edits are already applied; setup only recolours the stud texture) |

## Common tasks

### Adding a GoldenEye gun

1. Find the gun's folder in the 007 decomp under `assets/obseg/gun/` (for example `wppk` is the PP7).
   Add a `folder:identifier` pair to `GUNS` in `tools/ge64/setup.sh`, then re-run setup. This writes
   `bin/ge64_<id>.c` and `src/game/ge64_guns/<id>.inc.h`.
2. Add `MIO0_SEG(ge64_<id>, 0x0E000000)` to `sm64.ld`, next to the other guns.
3. In `src/game/ge64.c`, `#include "ge64_guns/<id>.inc.h"` and add an entry to `sGeWeapons`. The
   fields are documented on `struct GeWeapon`. Every entry after the PP7 is in the random primary
   pool.
4. Add a pose to `tools/ge64/gun_poses.json` (`pos3` is the view position and `hand_grip` is where
   the glove sits). Regenerate the header with
   `python3 tools/ge64/gen_gun_poses.py tools/ge64/gun_poses.json src/game/ge64_guns/poses.inc.h`.
   Expect some trial and error in-game. The level-select build's D-Right rerolls the primary weapon,
   so you can cycle to your gun quickly.
5. If the gun needs a new sound, add it to `SOUNDS` in `tools/ge64/import_ge_sfx.py`, using the index
   from `tools/ge64/ge_sfx_ids.txt`, and re-run setup.

### Adding a GoldenEye sound effect

Add an entry to `SOUNDS` in `tools/ge64/import_ge_sfx.py`, then re-run setup. It regenerates
`include/ge_sounds.h`, the sound bank and the sound player entries, all of which are committed. Then
play the sound with `play_sound(SOUND_GE_<NAME>, ...)`.

### Changing the title art

- **Logo:** `python3 tools/ge64/gen_logo3d.py levels/intro/ge64_logo.inc.c preview.png`. This needs
  the Cantarell font (`fonts-cantarell`).
- **Backdrop:** `python3 tools/ge64/title_bg.py . preview.png`, then copy the four strips from
  `textures/title_screen_bg/` into `tools/ge64/art/`. The backdrop uses random grain, so regenerating
  it changes the PNGs even when nothing else has changed.

## Open ideas

These are things that haven't been done yet. Feel free to take one on.

- Rewrite the tutorial text and signs (Lakitu, signposts, Bob-omb Buddies) to describe the actual
  controls: aiming, the gun jump, switching weapons.
- Verify the final Bowser fight (three bomb hits) and every boss in normal play.
- Support other ROM regions (the build is US-only for both games).
- Bring more of the game into first person: doors, poles, cannons. These currently cut to the
  third-person camera, which is fine but could be fancier.
