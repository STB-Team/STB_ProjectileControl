# STB Projectile Control

Hold a key while you loose an arrow and the camera goes with it. Time slows to a crawl,
**WASD steers the arrow in flight**, the mouse swings the view around it, and everything
snaps back the moment the arrow lands.

Works with bows, crossbows and fire-and-forget spells (Firebolt, Fireball, Ice Spike,
Icy Spear... from hands, staves and scrolls) — each switchable in the INI.

## Requirements

- Skyrim SE 1.5.97, AE 1.6.x or 1.7.x (one DLL covers all three)
- SKSE64
- Address Library for SKSE Plugins
- SkyUI — for the keycap art in the button hints (see below)
- *Optional:* SKSE Menu Framework — every setting on a live page, keys bound by pressing
  them. Without it the INI does the same job.

Skyrim VR is not supported.

## How it works

1. Draw and aim as usual.
2. Hold the trigger key (**X** by default) as you release the string.
3. The camera drops in behind the arrow and the world slows down.
   - **W / S** — nose up and down
   - **A / D** — turn left and right
   - **mouse** — look around the arrow without changing its course
   - **left mouse button held + mouse** — steer the arrow with the mouse instead
   - **- / +** — slower or faster slow motion, one step per press; the new value is kept
   - keep holding **W** past the vertical and the arrow flies a full loop, the camera
     turning upside down with it (`bAllowLoops`)
4. The ride ends on impact.

The arrow keeps its normal drop while you fly it. Set `fGravityScale = 0` in the INI to
make it hold whatever line you put it on.

## Range

The world is only loaded around the player, and a shot that leaves that area has nothing
to hit. The ride hands back at `fMaxDistance` (0 = no cap, the default) and
whenever the shot leaves the loaded area.

`fLifeScale` stretches how far the projectile may fly before the game removes it (spells
run out after a while), for as long as you steer it: 1 = as the game has it, 0 = it never
runs out; 5 by default. Let go and it is back to its normal range.

## Keycap art

No artwork ships with this mod. The button hints borrow the keycap art your interface
already has, so the keys look like the rest of your UI: Untarnished UI and Dear Diary
(`buttonArtDD.swf`), DearDiaryHUD (`buttonArtDDW.swf`) and SkyUI itself
(`skyui/buttonart.swf`, read straight out of SkyUI_SE.bsa) are all found automatically,
in that order — so the keys match whichever of them you run.

Nothing is redistributed here: the art stays in the mod it came with.

## Settings

`Data/SKSE/Plugins/STB_ProjectileControl.ini`, read when the game loads. Every value is commented
in the file itself: trigger key, camera distance and smoothing, mouse sensitivity and
recentring, turn rate, gravity, and how hard time slows down.

With SKSE Menu Framework installed, the same settings are on the **STB Projectile Control** pages
of its menu. Changes apply on the next frame and are written back to the INI (its
comments are kept).

## Slow motion

The ride uses the game's own magic slowdown — the one the Slow Time shout uses. A Slow
Time already running when you loose the shot is never made weaker, and carries on after
the ride; one that ends mid-flight is not brought back.

## Building

Requirements:
- Visual Studio 2026 (the `default` preset uses the `Visual Studio 18 2026` generator and
  the `v145` toolset; change them in `CMakePresets.json` for an older Visual Studio)
- [vcpkg](https://github.com/microsoft/vcpkg), with `VCPKG_ROOT` set
- A checkout of [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG), with
  `CommonLibSSEPath_NG` pointing at it. The vcpkg port is not used: the plugin needs a
  CommonLibSSE-NG that knows Address Library format 5 and treats 1.7.x as AE.

```powershell
cmake --preset default
cmake --build build --config Release
.\package.ps1          # assembles the mod archive into package/
```

`gen.ps1` configures the same way and can set a folder the DLL is copied into after
every build (`STB_PROJECTILECONTROL_DEPLOY_DIR`).

## Credits

The projectile-steering
approach — bending `linearVelocity` and letting the engine integrate — is the same one
NewProjectiles uses for homing.

Licensed under GPL-3.0.
