Medal of Honor: Rising Sun - XBE Recomp @VERSION@ (preview)
=============================================================

An unofficial native PC port of the original Xbox version of Medal of Honor: Rising Sun. Not affiliated with
Electronic Arts. This download contains no game data and no game code: you need your own copy of the Xbox game.


Requirements
------------
- Windows 10 or 11, 64-bit
- A graphics card with Direct3D 12
- Room for your disc image (about 4.5 GB for a game-partition image or 7.8 GB for a full Redump) in assets\,
  and about 1 GB more for the first start's build
- A controller (XInput) or keyboard and mouse


Installing
----------
1. Unzip this folder anywhere you can write to (not Program Files).
2. Put your Xbox disc image (.iso) into the assets folder. An image of the game partition (xiso) or a full redump
   both work. The original modified USA dump and the USA and Europe (En,Es,It,Nl,Sv) Redump releases are recognized.
   Other XBE hashes trigger a warning before starting. Keep only the copy you want to use in assets.
3. Start MOHRS.exe. The first start translates the game's code from your disc image and builds it into MOHRS.dll and
   MOHRSCoop.dll behind a progress window, which takes a few minutes. Later starts go straight to the game. If the
   build is interrupted, the next start carries on where it stopped.

Two Player Campaign (co-op) in the main menu switches to MOHRSCoop.exe and back by itself; keep both executables
together.


Controls
--------
Controllers work as on the Xbox. In singleplayer, keyboard/mouse and the first controller both control player 1;
switch between them at any time. In co-op or split-screen multiplayer setup they become separate inputs. At
"press A to join", press Enter on KBM and A on the controller. Join with KBM first to make it player 1.
They stay separate during the match and pause menus, and share player 1 again on returning to singleplayer.
KBM has its own Xbox port, leaving three ports for physical controllers. No settings change is needed.

Keyboard and mouse controls:

  Move                  W A S D          Walk (hold)           Shift
  Fire                  Left mouse       Aim mode              Right mouse
  Lean (hold)           Alt
  Action                E, F             Change weapon         Q, Mouse 4
  Primary weapon        1                Pistol                2
  Special weapon        3
  Grenade               G                Jump                  Space
  Reload                R                Crouch                C, Ctrl
  Melee                 V, Middle mouse  Center view           Z
  Next/previous weapon  Wheel            D-pad left/right      4, 5
  Sniper zoom           Wheel (scoped)
  Back (objectives)     Tab              Pause                 Escape
  Quick save            F5               Quick load            F9

1 cycles rifles, shotguns and automatic weapons, 2 pistols, 3 everything else you carry (bazooka first). The
wheel steps through all your weapons, down for the next, up for the previous; looking through a sniper scope it
zooms instead.

Quick save and load are for testing, in single player during play. There is one slot, also written to
quicksave\MOHRS.qsv: after a restart, start or continue any mission and press F9. A new version of the game
cannot load an older version's quick save.

In menus, hover and left click to select an entry; right click goes back. The wheel moves through entries. On
settings screens, click a label or displayed value to select its row, then click the arrows or use the wheel to
change the value. Click the select/confirm hint to save or
the back hint to cancel. Pause headings are clickable; right click on a pause page resumes.
The arrow keys also move, Enter confirms, Backspace goes back, and so does Escape in the main menus and on the
save screens (in the pause menu Escape resumes). Gameplay bindings can be changed in PC Options (below) or in
the [keys] table of settings.toml.

Exit to Desktop, the last entry of the main menu and of the pause screen's Exit page, closes the game at once,
as the window's close button does. Progress since the last save is lost.


Settings
--------
Options > PC Options in the main menu, and Preferences > PC Options in the pause screen: the Category row on top
picks the page. Display has the display mode, render scale, frame rate (30, 60 or unlocked), FPS limit, VSync,
field of view, widescreen and keep bodies; Mouse the horizontal and vertical sensitivity and inversion; Move,
Combat, Weapon, Look and Other Keys the key bindings (and the walk speed). Left and right change a row. To bind a
key, select its row (Enter or a click), then press the new key or mouse button: it replaces the action's keys and
is taken off any other action. Escape cancels, Backspace leaves the action unbound. Confirm saves, back cancels
everything changed.

Widescreen fills 16:10, 16:9, ultrawide 21:9 and 32:9 screens: the view is widened to the screen's shape (set
by the resolution, or the desktop's), the HUD and menus keep their shape in the middle, and the mission movies are
shown as large as they fit without being stretched.

Display > Keep Bodies: dead enemies and allies stay where they fell for the rest of the level instead of fading out
a few seconds later. Up to 128 bodies a level (64 in co-op); past that the oldest one goes. Bodies stay as they
fell: shots and explosions pass through them.

Preferences > Cheats in the pause screen (single player and co-op) switches Give All Weapons, Infinite Ammo, God
Mode, One Shot Kills, Bullet Shield, Achilles Head, Invisible Soldiers and Rubber Grenades, and sets Quick Reload
(Off, 2x, 3x or 4x faster reloads) and Custom Movement (Off or Quake: on foot you run, stop, jump and steer in the
air as in Quake, bunny hops and air strafing included; hold jump to hop on every landing, your speed is shown under
the crosshair; your own grenades and rockets do you no harm and push you away, so you can rocket jump). They last until the game is closed. Give All Weapons gives every weapon the current level
has, and in single player the campaign's other weapons too. One Shot Kills and Achilles Head also apply to your allies.

Controller Options > Modern Aiming (in the main menu's options and the pause screen's preferences): while you aim
you walk, at half speed, and can jump, instead of leaning to the side. Hold Alt (Lean) to lean as on the console:
it aims too, and with Modern Aiming on, on the ground, A and D lean out while it is held; let go and you walk on
without leaving the aim. Off is the console's aiming.

The end-of-level stats also show the mission time: how long the level took, pauses not counted. HUD Options >
Mission Timer (in the main menu's options and the pause screen's preferences) shows it in the top right corner
while you play, and HUD Options > FPS Counter the frames a second in the top left corner.

Everything is also in settings.toml beside the executable, written on the first start. Some settings are only there:

  skip_intros = true            no EA logo and opening movie at start
  skip_cutscenes = true         no level intro and outro movies
  width / height                output resolution, 0 for the desktop's
  anisotropic_filtering         0 as the game asks, else a level such as 16
  check_game_hash = true        false starts a release the build does not recognize without asking each time

Unlocked frame rates above 60 FPS are new; if something behaves differently from the console, compare with
Frame Rate at 60 FPS and please report it.


Files
-----
  assets\        your disc image
  hdd\           the Xbox hard disk: profiles and saved games (hdd\E\UDATA\4541003f)
  settings.toml  settings
  logs\          logs of the last runs
  CrashDumps\    written if the game crashes
  compiler\      the tools of the first start's build

To move to a new version, keep hdd\ and settings.toml and replace the rest.


Known limits of this preview
----------------------------
- The whole campaign has not been played through on this port yet; the later levels (1_5, 1_7, 1_8, 1_9) have
  not been checked.
- Multiplayer over a network and Xbox Live are not supported. Split-screen and co-op work.
- The new PC Options and Cheats screens, the mission time rows and Exit to Desktop are in English in every
  language.


Reporting problems
------------------
Please include the log from logs\ (and the crash dump from CrashDumps\ if there is one), the mission and what you
were doing.


Credits
-------
Built with Xbox XBE Recomp by TGP482 (https://github.com/TGP482/XboxXBE-Recomp), modelled on BLACK-XBERecomp.
This port is under the MIT License (LICENSE.txt), the SDK under its own (LICENSE-XboxXBE-Recomp.txt); third party
licenses are in THIRD_PARTY.txt and compiler\LLVM-LICENSE.txt. The game and its data belong to Electronic Arts.
