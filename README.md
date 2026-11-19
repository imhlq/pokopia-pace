# Pokopia Pace · 生活加速

[简体中文](README.zh-CN.md)

Pokopia Pace adds configurable movement, crafting, collection, wait times,
food power-ups, and Magnet Rise options to Pokémon Pokopia.

**Compatibility** · Windows x64, Pokémon Pokopia 2.0.1 (update 655360),
Modtopia 0.7.x. Tested with Recompia 0.14.1. This is a gameplay mod and makes no
network requests. For Link Play, everyone needs the same mod version and
settings.

## Install or update

1. Close the game.
2. Put `builder_boost-1.6.4.pkmod` in your Recompia game's `mods` folder.
   When updating, replace the previous package. Older releases were called
   Builder Boost and used the same mod ID.
3. Start the game from the Recompia launcher. Approve the mod code if prompted
   and let the launcher rebuild the game with the mod's hooks.
4. Open **F1 → Mods → Pokopia Pace · 生活加速** to change settings.
   If you use **Run Speed** or **Gathering Boost**, disable them to avoid
   overlapping effects.

You can disable Pokopia Pace from the Mods menu or remove its package while the
game is closed. The loader keeps your settings for later use.

[Modtopia installation guide](https://pokopia-native.vercel.app/mods/add)

## Settings

| Setting | Default | What it changes |
| --- | --- | --- |
| Stick-click sprint speed | 1.5× | Sprint speed, independently of regular movement. Range 1–3×. |
| Full-stick movement speed | 1.5× | Regular movement at full stick tilt. Range 1–3×. |
| Common crafting output | 3× | Selected common materials and construction pieces. Range 1–5×. |
| Collection yield | 3× | Selected fresh material drops, ores and harvested crops. Range 1–5×. |
| Gameplay wait duration | 0.5× | Construction, timed processing and Pokémon appearance timers. Range 0.1–1×. |
| Cooked-food power-up duration | 2× | Power-up duration from newly eaten meals. Range 1–5×. |
| Unlock Magnet Rise early | On | Adds Magnet Rise directly to the move wheel before it is learned. |
| Magnet Rise PP cost | 50% | Scales ongoing drain and action costs. Range 10–100%. |

One iron ingot and one stone produce **30 railway tracks** at the default
multiplier. The crafting multiplier applies to output. Ingredient costs, quest
items, recipe unlocks and rare crafting outputs keep their normal values.
Collection applies when the game generates eligible materials or harvested
produce.

At the default wait setting, a 15-minute build takes 7 minutes 30 seconds and
a 120-second processing step takes 60 seconds. Habitat checks normally run
every 180 seconds and run every 90 seconds at the default. Habitat conditions
and encounter requirements still apply.

Open the move wheel to select Magnet Rise. It fills the first empty slot or
temporarily occupies the last slot when the wheel is full. Turn early access
off to restore the saved wheel and normal unlock requirement. Its PP setting
works independently.

## Build from source

On Windows x64 with Python 3.11 or newer and Zig, run this from the source folder.

```powershell
python build.py --zig C:\path\to\zig.exe
```

The script compiles the DLL, runs the test host against it and writes the package
and SHA-256 to `build/`. When the source is in `mod_sources/builder_boost` inside
a Recompia installation, the script uses the bundled Zig by default.

Pokopia Pace and the bundled Modtopia SDK are MIT-licensed. The SDK is pinned
to Recompia v0.13.9. See `vendor/modtopia/UPSTREAM.json` in the source archive.
