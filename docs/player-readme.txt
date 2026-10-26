Medal of Honor: Rising Sun - XBE Recomp @VERSION@ (preview)
=============================================================

An unofficial native PC port of the original Xbox version of Medal of Honor: Rising Sun. Not affiliated with
Electronic Arts. This download contains no game data and no game code: you need your own copy of the Xbox game.


Requirements
------------
- Windows 10 or 11, 64-bit
- A graphics card with Direct3D 12
- Room for your disc image (4.5 GB) in assets\, and about 1 GB more for the first start's build
- A controller (XInput) or keyboard and mouse


Installing
----------
1. Unzip this folder anywhere you can write to (not Program Files).
2. Put your Xbox disc image (.iso) into the assets folder. An image of the game partition (xiso) or a full redump
   both work. The expected default.xbe SHA-1 is 9f5197f8ded3ce1cd3e1c1cae14a756a17c39d56; the game warns before
   starting from a different one.
3. Start MOHRS.exe. The first start translates the game's code from your disc image and builds it into MOHRS.dll and
   MOHRSCoop.dll behind a progress window, which takes a few minutes. Later starts go straight to the game. If the
   build is interrupted, the next start carries on where it stopped.

Two Player Campaign (co-op) in the main menu switches to MOHRSCoop.exe and back by itself; keep both executables
together.


Controls
--------
Controllers work as on the Xbox. Keyboard and mouse share the first player's controller:

  Move                  W A S D          Walk (hold)           Alt
  Fire                  Left mouse       Aim mode              Right mouse
  Action                E, F             Change weapon         Q, Mouse 4
  Grenade               G                Jump                  Space
  Reload                R                Crouch                C, Ctrl
  Melee                 V, Middle mouse  Center view           Z
  Sniper zoom           Wheel, 1 and 2   D-pad left/right      3, 4
  Back (objectives)     Tab              Pause                 Escape

In menus the arrow keys move, Enter confirms and Backspace goes back. Every binding can be changed in the [keys]
table of settings.toml.


Settings
--------
Options > PC Options in the main menu, and Preferences > PC Options in the pause screen, change the display mode,
render scale, frame rate (30, 60 or unlocked), FPS limit, VSync, field of view, widescreen, FPS counter, mouse
sensitivity and mouse inversion. Left and right change a row, confirm saves, back cancels.

Everything is also in settings.toml beside the executable, written on the first start. Some settings are only there:

  skip_intros = true            no EA logo and opening movie at start
  skip_cutscenes = true         no level intro and outro movies
  mouse_vertical_sensitivity    vertical mouse speed, on top of mouse_sensitivity
  invert_mouse_x                mirrored horizontal mouse look
  walk_speed = 50               percent of running speed while Walk is held
  width / height                output resolution, 0 for the desktop's
  anisotropic_filtering         0 as the game asks, else a level such as 16

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
- The new PC Options screen is in English in every language.


Reporting problems
------------------
Please include the log from logs\ (and the crash dump from CrashDumps\ if there is one), the mission and what you
were doing.


Credits
-------
Built with Xbox XBE Recomp by TGP482 (https://github.com/TGP482/XboxXBE-Recomp), modelled on BLACK-XBERecomp.
This port is under the MIT License (LICENSE.txt), the SDK under its own (LICENSE-XboxXBE-Recomp.txt); third party
licenses are in THIRD_PARTY.txt and compiler\LLVM-LICENSE.txt. The game and its data belong to Electronic Arts.
