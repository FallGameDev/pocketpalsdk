# Pocket Pal

A 3D digital pet for Mac by **Here Studios**. Adopt a cat, dog, human or your own uploaded character, keep them happy, fed and healthy, and watch their mood faces in your Mac menu bar.

## Always running

Installing the `.pkg` adds a login agent (`~/Library/LaunchAgents/com.herestudios.pocketpal.plist`) that starts Pocket Pal in the menu bar when you log in and restarts it if it crashes. Closing the window or pressing Cmd+Q only hides the game: the pets keep living in the menu bar, their stats keep changing, and macOS notifications go off when a pet is hungry, sad or sick.

To remove it completely: `launchctl bootout gui/$(id -u)/com.herestudios.pocketpal`, then delete the plist above and `/Applications/Pocket Pal.app`.

## How updates work

The Mac app checks this repo every time it opens:

1. It asks GitHub for the newest commit on `main`.
2. It reads `game/version.json` from that commit.
3. If that number is higher than the version it is running, it downloads `game/pet.html`, saves it in `~/Library/Application Support/PocketPal/game/`, and reloads into it. Your pets are kept.

So to ship an update to everyone, you only need to push a new `game/pet.html` with a higher number in `game/version.json`. Players can turn this off, or check by hand, in **Settings → Auto updates**.

Changes to the Mac app itself (`native/`) still need a new `.pkg`, since only the game file updates itself.

## Releasing a game update

```bash
# edit src/pet.src.html, then:
python3 src/build.py --bump      # version + 1 and rebuild game/pet.html
git add -A && git commit -m "Game update" && git push
```

## Building the installer

```bash
bash packaging/build.sh          # makes build/Pocket Pal Installer.pkg
```

Works on Mac or Linux. It needs Python 3 (with Pillow only if you remake the menu bar faces) and, for the Mac app binary, `clang` and `ld64.lld` from LLVM 18.

## What is in here

| Folder | What it holds |
| --- | --- |
| `game/` | The built game (`pet.html`) and its version number. This is what the app downloads. |
| `src/` | The game source (`pet.src.html`), the build script, three.js r147 and the built-in cat and dog models. |
| `native/` | The Mac app: a small C program that shows the game, runs the menu bar faces, saves files and installs updates. |
| `packaging/` | The app bundle template, icon, menu bar face art and the `.pkg` builder. |

## Credits

3D rendering by [three.js](https://threejs.org) (MIT license, see `src/libs/three-LICENSE.txt`). The cat and dog models are third-party assets used in the built-in pets.
