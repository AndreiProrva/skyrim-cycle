# C3 Skyrim Cycle 0.1.25 — installation

1. Check SkyrimSE.exe → Properties → Details: **Steam 1.7.104.0 x64**.
2. Install **SKSE 2.3.1** from https://skse.silverlock.org/. Follow its own
   instructions: loader/DLLs beside SkyrimSE.exe, scripts in Data or an MO2 mod.
3. Install **Address Library for SKSE Plugins, All-in-one AE v13**:
   https://www.nexusmods.com/skyrimspecialedition/mods/32444.
4. If missing, install the Microsoft Visual C++ v14 x64 runtime:
   https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist.
5. Back up your save. Prefer a separate MO2 profile for the initial test.
6. Install `C3-Skyrim-Cycle-0.1.25.zip` through MO2 or Vortex. Its root is the
   Data directory contents; do not introduce an extra nested Data folder.
   Enable the mod and `C3SkyrimCycle.esp`. Deploy in Vortex if applicable.
7. Start the game with the matching SKSE loader through your mod manager.

## Obtain the bicycle

Load a save and stand outside on reasonably flat ground. Open the console
with the tilde key and enter:

    help "C3 Cycle Bike" 4

Locate the **NPC_ C3 Cycle Bike** entry and copy its eight-digit FormID.
If it is **07000800**, for example, enter:

    player.placeatme 07000800 1

Replace **07** with the actual prefix shown in your console. The prefix is
your plugin load index; it is not always 07. Do not use a saved FF-prefixed
reference ID. Spawn one bicycle, close the console, approach it and press E.
The mod does not automatically place a bicycle in the world.

To skip the cart introduction for a disposable test, use `coc Riverwood`
from the main-menu console, then spawn the bicycle. Existing saves can also
be used; retain a pre-mod backup.

## Controls

Default Skyrim movement/activation bindings: W/S forward/back, A/D turn,
E mount/dismount, Shift accelerate. Top-row 1–8 select a gear; L toggles
the amber headlamp. These extra keys operate while riding the bicycle,
with blocking menus closed. Russian layout uses the same physical keys.

Normal forward gear targets: **14, 18, 22, 26, 30, 34, 38, 42 km/h**.
HUD labels are Russian in this version. RPM is in-game animation cadence;
speed is estimated from game-world displacement. There is no BLE input or
measured exercise power in this release. Shift, slopes and collisions can
change actual speed relative to the normal gear target.

## Configuration

Edit INIs with the game closed. In `SKSE/Plugins/C3CycleAudio.ini`, `[Audio]`:
WheelSound=1 enables bicycle audio; WheelVolumePercent=20 sets volume (0–100);
WheelReferenceRPM=60 is the pitch reference; MuteBikeHorseSounds=1 enables
scoped suppression of bicycle horse sounds. RememberedPlayerBikeRef=0 is
the clean initial state and is updated automatically for your own bicycle.
Do not copy another player's saved reference ID.
`C3CycleLight.ini`, `[Light]`, ConeEnabled=1 enables the forward cone;
0 keeps the supplementary point light. L switches the lamp on/off.

## Troubleshooting and compatibility

The binary rejects any game/SKSE version except 1.7.104 / 2.3.1. No GOG,
VR, console, older SE/AE support is claimed. Python, Blender, FNIS and
Nemesis are not needed. Custom bodies, high heels and other mount, camera,
animation, input or skeleton mods may require separate compatibility work.

Missing bike: check ESP and console spawning. Missing HUD/controls: check
SKSE, Address Library, DLL and C3CyclePose.bin. Pink mesh: check the included
textures. For crashes, compare against a separate minimal profile first.
Report game/SKSE versions, mod list, reproduction steps and C3KeyboardMotion.log
from your Documents/My Games/Skyrim Special Edition/SKSE directory (Documents
may be redirected). Remove private paths before sharing logs.

Update by replacing the old mod directory completely; preserve your custom
INI settings separately if needed. Do not install global horse/rider animation
overrides from old experimental builds. The native DLL still reports 0.1.24;
the 0.1.25 package changes only the rider pose table.

To uninstall, dismount, turn the lamp off, save separately and exit; then disable
the mod/ESP. For a complete rollback use a save from before installation.
Removing an ESP cannot guarantee cleanup of all references in an existing save.
