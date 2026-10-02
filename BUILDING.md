# Building Nitoryu

Nitoryu is an [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi) plugin built on
[KenshiLib](https://github.com/KenshiReclaimer/KenshiLib). This repository has the plugin's source and
the mod folder's config and texts. The game data (`Nitoryu.mod`) and the animation track files are not
here; they ship with the mod on the Steam Workshop.

The code is licensed under the GNU GPL v3 (`LICENSE`), as are KenshiLib and RE_Kenshi it is built on.

## What you need

- **The Visual C++ 2010 toolset (v100), x64.** Kenshi and KenshiLib are built with it, so the plugin
  must be too. A newer Visual Studio works as the IDE when VS2010 (or the Windows SDK 7.1 compilers) is
  installed next to it. No range-based `for` and no initializer lists; `auto`, lambdas and `nullptr` work.
- **KenshiLib 0.5.0**, the version RE_Kenshi loads: its `Include` folder and `KenshiLib.lib`.
- **[KenshiLib_Examples_deps](https://github.com/KenshiReclaimer/KenshiLib_Examples_deps)**: the Ogre and
  MyGUI libraries and Boost 1.60.

## Environment

The project reads three environment variables, the same ones the KenshiLib examples use:

| Variable | Points at |
|---|---|
| `KENSHILIB_DIR` | a KenshiLib folder with `Include\` and `Libraries\KenshiLib.lib` |
| `KENSHILIB_DEPS_DIR` | KenshiLib_Examples_deps |
| `BOOST_INCLUDE_PATH` | Boost 1.60 inside the deps (its `stage\lib` holds the built libraries) |

Set them with `setx` and open a new shell afterwards (a running shell does not see them).

If your KenshiLib headers and library sit elsewhere (for example the headers from RE_Kenshi's own
KenshiLib and the `.lib` from its build), pass them as properties instead:

```
msbuild Nitoryu.vcxproj /p:Configuration=Release /p:Platform=x64 ^
    /p:KenshiLibInclude=C:\path\to\KenshiLib\Include ^
    /p:KenshiLibLib=C:\path\to\KenshiLib.lib ^
    /p:KenshiLibLibraries=C:\path\to\KenshiLib_Examples_deps\Libraries
```

## Build

Only `Release|x64` exists:

```
msbuild Nitoryu.vcxproj /p:Configuration=Release /p:Platform=x64
```

The DLL lands in `x64\Release\Nitoryu.dll`.

## Install your build

Install the mod from the Workshop first (it brings `Nitoryu.mod` and the animations), then replace
`Nitoryu.dll` in its folder with yours. The files in `mod/` are that folder's ini, texts, icons and
`fcs.def`; the READMEs and the files in `docs/` are copied next to them.

## Layout

| Path | What |
|---|---|
| `src/Nitoryu.cpp` | the plugin's entry and main loop |
| `src/SettingsDef.h` | every setting: its ini key, its FCS field and its default |
| `src/McmDef.h` | the in-game settings page |
| `src/Moves.h`, `src/ClipsGen.h` | the moves and their animation clips |
| `src/core/` | pure math with no game header |
| `src/kit/` | small helpers: logging, FCS fields, input, RTTI lookup |
| `mod/` | the mod folder: `Nitoryu.ini`, `fcs.def`, `lang/`, icons |
| `docs/` | the modders' manual (`FCS.md`, `FCS_RU.md`), the settings table, the balance |
