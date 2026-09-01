# NMRiH2 offline inspector overlay

Version-specific internal debug overlay for:

- Game: **No More Room in Hell 2**
- Build: **5.6.1-484414+++Kosovo+Mangle.4**
- Engine layout: UE 5.6.1 SDK generated with Dumper-7

The project uses the supplied SDK and its confirmed offsets:

| Symbol | Offset |
| --- | ---: |
| `GWorld` | `0xA3880F0` |
| `GObjects` | `0xA69C800` |
| `GNames` | `0xA580080` |
| `FName::AppendString` | `0x1358660` |
| `ProcessEvent` | `0x1571450` |
| `ProcessEvent` vtable index | `0x4C` |
| `UWorld::PersistentLevel` | `0x30` |
| `ULevel::Actors` | `0xA0` |

## What it displays

- Blue boxes, names, distance, and health for other player actors
- Red boxes, distance, and health for living zombie actors
- Optional socket-based skeleton ESP, bottom-centre snaplines, head markers, and
  adult/child plus shambler/walker/runner/crawler zombie-type labels
- A player-up radar/minimap for loaded zombies, loot, objectives, and
  extractions, with separate category filters, size, and range controls
- Directional incoming-hostile arrows and a nearest-threat readout
- An equipped-weapon panel showing loaded/capacity ammo, reserve ammo, fire
  mode, and disposable-weapon durability when those SDK components exist
- A nearest-first objective tracker showing active, waiting, and completed
  states with distance
- Every concrete pickup family found in the supplied SDK: 15 firearms, 19 melee
  weapons, ten ammo-box classes across eight calibres, and all medical,
  explosive, attachment, equipment, supply, deployable, key-item, and fallback
  pickup classes (104 concrete pickup classes in total)
- Curated world markers for loot containers and racks, objectives, extraction
  points, powered interactables, doors, and vehicles
- An optional `Other SDK actors` filter for inspecting otherwise-unclassified
  actors; it is off by default to prevent excessive marker noise
- All currently loaded `UWorld::Levels`, rather than only the persistent level
- An adjustable scan radius from 25 to 5,000 metres in 25-metre steps
- Nearest-first marker sorting and an adjustable 25-500 marker limit
- An optional outgoing player-damage multiplier covering every firearm hit
  zone and every melee weapon, with a separate stability/stagger switch
- A zombie-only aim page with an adjustable screen-space FOV, FOV circle,
  line-of-sight filter, aim smoothing, hipfire activation, and pitch-weighted
  recoil recovery while locked
- Offline combat actions: one-hit melee, double melee/shove range, tele-kill
  the zombie nearest the crosshair, or tele-kill all currently loaded zombies
- Reversible fullbright, game-native ghost/fly plus AI-ignore mode, and local
  player time dilation for movement and attack speed
- Infinite-reserve or bottomless-magazine training modes, infinite stamina,
  automatic flashlight recharging, god mode, and once-per-second health
  regeneration
- Reversible equipped-weapon controls for recoil, spread, reload speed, fire
  rate, and melee attack playback rate
- Zombie-only freeze/slow/fast time and guarded global time dilation
- Game-native offline debug actions for destroying all AI, spawning an item by
  SDK identifier, granting an ammo type, and granting supplies
- Teleport to the nearest loaded objective or extraction, teleport to the
  pickup nearest the crosshair, and one-shot pickup vacuum with an adjustable
  25-5,000 metre radius
- A searchable copied-snapshot SDK actor inspector, an in-menu diagnostic
  console, and an optional last-lines HUD console
- A standalone photo mode that uses the game-owned debug camera together with
  adjustable slow motion, restoring both camera and time state when disabled
- Combat, Loot Search, and Exploration visual presets
- Six JSON configuration slots and immediate hotkey rebinding

Every character, alert, pickup, and world category has an independent RGBA
editor. The on-screen legend dims a category when that filter is disabled.

## ImGui control panel

The settings panel is a centred Dear ImGui interface rendered in a transparent
companion window with its own private DirectX 11 device. It does not submit
command lists to the game's DirectX 12 queues. It has mouse input, a custom dark
customizable theme, grouped cards, sliders, tab buttons, an offline-status badge,
reset controls, and buttons to open or copy the Discord invite. Press `Page Up`
while the menu is open to cycle through the same tabs without the mouse:

| Page | Options |
| --- | --- |
| General | Radius, players, zombies, health, skeletons, snaplines, head/type labels, radar, threat arrows, weapon panel, objective tracker, marker style, and limits |
| Aim | Zombie aimbot, FOV circle, visibility check, hipfire mode, FOV radius, aim speed, simple RCS, RCS strength |
| Loot | Master loot switch, firearms, melee, ammo, medical, explosives, attachments, equipment, supplies, deployables, key items, other pickups |
| Ammo | Marker filters plus disabled/infinite-reserve/bottomless-magazine mode, infinite stamina, and infinite flashlight battery |
| World | Master world switch, containers, objectives, extractions, interactables, doors, vehicles, and other SDK actors |
| Damage | Outgoing damage, recoil, spread, reload/fire-rate controls, god mode, automatic healing, stamina, flashlight, and live safety status |
| Misc | One-hit melee, double melee range, target/all-zombie tele-kill, fullbright, ghost/fly, AI ignore, and movement/attack speed |
| Tools | Zombie/global time, photo/free-camera mode, destroy-all-AI, objective/extraction/pickup teleport, pickup vacuum, item spawn, ammo grant, and supply grant |
| Visuals | Menu theme plus independent player, zombie, skeleton, head, snapline, threat, loot, and world-category RGBA editors |
| Inspector | Searchable copied actor snapshot, live feature state, scrollable diagnostic console, and optional HUD console |
| Config | Combat/Loot Search/Exploration presets, six JSON profile slots, and hotkey rebinding |

Doors and other SDK actors start disabled because large maps may contain many
instances. Enable raw SDK class names on the General page when identifying an
unrecognised object.

## Controls

- `Insert` toggles the overlay (default)
- `F1` opens or closes the settings panel (default)
- `Page Up` changes the settings page (default)
- Hold `Right Mouse` to engage zombie aim; enable Hipfire mode to also engage
  while holding `Left Mouse`
- `F6` tele-kills the living zombie nearest the crosshair (default)
- `F7` tele-kills all currently loaded living zombies (default; 1,024-actor safety cap)
- Mouse changes toggles, sliders, tabs, and menu actions
- Drag the panel background to move it
- Drag any edge or corner to resize it; content scrolls at smaller sizes
- `End` closes the companion window, restores the HUD vtable, and unloads the DLL (default)

The Config page can rebind all six default hotkeys. Choose distinct keys so one
press does not trigger multiple actions. Rebindings are saved with each JSON
profile. Profiles are written as human-readable files in
`NMRiH2_Offline_DebugOverlay_profiles` beside the DLL; save a slot before first
loading it.

While the panel is open, the companion window explicitly owns focus and mouse
capture and also polls cursor/button state directly for ImGui. This prevents the
game viewport from consuming clicks intended for the menu. Pressing `F1` hides
the panel, releases capture, unlocks the cursor, and returns focus to the game.
Window bounds and input ownership are cached: the companion surface only moves
or resizes when the actual game client rectangle changes, and it acquires focus
once per menu-open transition instead of reasserting both every frame.

The panel opens centred, then keeps the size and position you choose for the
current session. It includes the community invite:
<https://discord.gg/nKDN7vCFE7>. Borderless or windowed display mode is
recommended so Windows can composite the companion panel above the game. If the
companion window cannot initialize, a keyboard-driven Unreal HUD panel is used
as a fail-safe fallback.

## Crash diagnostics

This diagnostic build writes `NMRiH2_Offline_DebugOverlay.log` beside the DLL.
If that directory is not writable, the same filename is written under
`%TEMP%`. The file is append-only, flushed after every entry, and contains no
network upload or telemetry.

Actor traversal is held back for the first 10 seconds after the offline HUD is
first rendered. During that window the menu can initialize, move, and resize,
but no level actor array is enumerated. This makes a repeatable crash easier to
separate:

- A crash during the 10-second warmup, with a final `ExternalUi...` stage,
  points to the isolated companion-window path. The logger also records fatal
  exceptions raised on other game or graphics-driver threads.
- A crash only after `Actor traversal warmup complete`, with a final `Actor...`
  stage, points to Unreal SDK traversal or a particular actor/class.
- `External ImGui Present failed` records a companion graphics failure even if
  Windows does not raise a normal access-violation exception.

After a crash, keep the log and share its last 30-50 lines. Exception entries
include the exception code and address, active overlay stage, level and actor
indices, and the current object/class pointers. The exception logger is
observational: it records the event and then lets Windows continue its normal
crash handling.

The Inspector page mirrors the same log in a bounded 400-line console. Enabling
`Show last lines on HUD` draws only a configurable 3-15 line tail and does not
change the append-only disk log.

The entity overlay and every gameplay feature deliberately fail closed whenever
`UWorld::NetDriver` is non-null. In that case the project restores its captured
offline values, clears pending action requests, shows only a warning, and
does not enumerate or render actors. All gameplay features are disabled by
default. There is no online support, anti-cheat bypass, or injector in this
project.

The damage modifier uses the SDK's `UKAttributeSet_Global` outgoing player
attributes rather than rewriting individual weapon data. It scales melee,
melee-limb, and firearm head/body/limb damage, so newly encountered weapons use
the same setting automatically. Stability/stagger damage can be included or
left unchanged. The feature is disabled by default, captures the original
attribute values before applying, and restores them when the switch is turned
off, a network driver appears, the player/attribute set changes, or the DLL is
unloaded. If a valid local player and initialized attributes are not available,
the modifier waits without writing anything.

One-hit melee uses the same global attribute set but changes only the two melee
outgoing multipliers. Firearm values stay untouched unless the regular damage
multiplier is enabled. Double melee range is applied only when the equipped
main-hand `AKItem` reports itself as a melee weapon; it snapshots and doubles
`Range`, `ShoveRange`, and `HeavyShoveRange`, then conditionally restores them
without overwriting values populated by a newly equipped item.

Tele-kill uses the supplied SDK's `AKDamageManager::InstantKill`. Target mode
selects the living zombie nearest the crosshair inside the configured scan
radius. All mode considers every loaded, living zombie found during the guarded
level scan, independent of marker visibility. The zombie aimbot writes only the
local controller rotation and runs only while an activation mouse button is
held; the visibility option uses `AController::LineOfSightTo`.

Fullbright captures and restores `r.EyeAdaptationQuality`, `r.ExposureOffset`,
and `r.TonemapperGamma`. Ghost mode uses the game's own `UKCheatManager`
`DebugToggleGhost` and AI-ignore functions. Speed mode snapshots the local
pawn's `CustomTimeDilation`, so both movement and actor-driven attack playback
are affected and the original value can be restored.

Weapon handling operates on the equipped character's generated
`UKAttributeSet_Item`. The overlay snapshots all touched recoil, spread,
reload, fire-rate, and melee play-rate attributes and conditionally restores
only values it still owns when the item changes or a feature is disabled.
Infinite stamina similarly snapshots the local stamina attribute and refills it
to the current maximum. God mode temporarily clears the local pawn's
`bCanBeDamaged` flag. Global and per-zombie time dilation each have separate
restore state; global time is clamped to `0.05x` so the HUD game-thread path can
continue processing a request to restore normal time.

Photo mode calls the inherited `UCheatManager::ToggleDebugCamera` only after the
same standalone-world check and shares the reversible global-time snapshot. It
does not create or retain a custom camera UObject. Close the ImGui panel to give
movement input back to the game while framing a shot, then disable photo mode to
return to the player camera.

Infinite ammo modes and grants call only the non-`Server_` functions exposed by
the game's `UKCheatManager`, and only after a standalone-world check. No
replication or server-validation path is patched. Item and ammo grant text must
match identifiers accepted by this game build; invalid or empty identifiers are
logged and otherwise left to the game's own debug handler. Pickup vacuum moves
at most 256 valid pickup actors per request and logs how many moves succeeded.

## Build

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 with **Desktop development with C++**
- CMake 3.24 or newer

Pinned official Dear ImGui 1.92.9b and MinHook 1.3.4 sources are included under
`third_party`, so configuration does not need to download dependencies.

From an **x64 Native Tools Command Prompt for VS 2022**:

```powershell
cmake -S . -B build-vs2 -G "Visual Studio 17 2022" -A x64
cmake --build build-vs2 --config Release --parallel
```

The result is normally written to:

```text
build-vs2\Release\NMRiH2_Offline_DebugOverlay.dll
```

The configure command must be run before the build command. If CMake reports
that it cannot find Visual Studio, open **Visual Studio Installer**, install
**Desktop development with C++** plus a Windows 10/11 SDK, then open a fresh
**x64 Native Tools Command Prompt for VS 2022** and run both commands again.

The project includes WinUser only after the generated SDK declares
`AHUD::DrawText`, then undefines WinUser's `DrawText` macro before making SDK
calls. `Basic.cpp` carries the same guard. This prevents both the `C2039`
compile error and the `LNK2019` mismatch involving `AHUD::DrawTextW`.

Load it only through an authorized loader while the game is running in an
official offline/no-anti-cheat mode. If the game does not provide such a mode,
do not load the DLL. The code is intentionally limited to the exact build above;
after a game update, regenerate the SDK and review every offset before rebuilding.

## Implementation notes

The worker creates a transparent companion window and a private DirectX 11
device for ImGui, then waits for the local `APlayerController` and `AHUD`. It
does not install DirectX hooks or submit work to the game's swap chain. Entity
markers still render after `ReceiveDrawHUD` using the game's own
`ProjectWorldLocationToScreen` and HUD drawing functions. Only the settings UI
uses ImGui. Companion-window positioning, resize cleanup, Win32 input, and
thread teardown are handled explicitly.

The companion panel and Unreal actor renderer share settings through a mutex.
The menu accepts mouse and keyboard input only while it is visible in a verified
offline world; closing it hides the companion window and returns focus to the
game. Because the UI owns its device and swap chain, DLSS/FSR frame-generation
queues and the game's HDR back buffers are never touched. Actor traversal also
rejects objects marked as being destroyed, finished destroying, or garbage
before calling SDK methods.

Gameplay settings are consumed on the Unreal HUD/game thread. Every update first
checks the current world's `NetDriver`, then resolves the local character's
ability-system component and its spawned `UKAttributeSet_Global`. Attribute
writes call the matching rep-notify handler so Gameplay Ability System state is
updated consistently. Range, speed, fullbright, and ghost state have separate
restore paths. Unload requests remain on the HUD/game-thread path until stateful
features are restored before hooks are removed; direct-value fallback is used
only for plain attribute/time-dilation values.

Class descriptors are cached by `UClass*`, so expensive class-name inspection is
performed only once per encountered actor class. Non-character markers are
sorted by distance and capped before drawing. Actor traversal also stops at a
defensive 200,000-actor limit if SDK array data is corrupt or unreasonable.
The searchable inspector never dereferences actor pointers on the companion UI
thread: the HUD/game thread copies address, class, category, location, distance,
and state into a bounded snapshot first. Skeleton names are resolved from each
skeletal mesh only when skeleton ESP is enabled and are then cached; head
markers otherwise use the existing bounds-derived aim point.

If the status line never appears in a genuine offline session, the game's HUD
subclass is probably not dispatching `ReceiveDrawHUD` for that map. The next
fallback would be a `UGameViewportClient::PostRender` hook, whose vtable index
must be confirmed for this exact executable before use.
