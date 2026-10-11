# NitoryuAddonSample

a small RE_Kenshi plugin that shows what Nitoryu's plugin API gives you. it takes Nitoryu's function table, writes a line into `RE_Kenshi_log.txt` for every event (the second blade hit, a parry, a rank change, ...), reads the state of the character the event is about, and adds one pair rule (a heavy weapon pairs with a sabre or a katana) and one move filter (that pair does no twin strikes). no animations or moves of its own, it is a demo of the API and not a finished mod.

the manual is `docs/API.md`, the header is `src/api/NitoryuApi.h`.

## Build

the same toolchain and the same three environment variables as Nitoryu itself (`BUILDING.md` in the repository's root):

```
msbuild NitoryuAddonSample.vcxproj /p:Configuration=Release /p:Platform=x64
```

the DLL lands in `x64\Release\NitoryuAddonSample.dll`.

## Try it

1. Make a folder `Kenshi\mods\NitoryuAddonSample` and put the DLL and `RE_Kenshi.json` into it.
2. The launcher lists a mod only when the folder has a `.mod` of the same name: make an empty `NitoryuAddonSample.mod` in FCS.
3. Tick it in the launcher, above or below Nitoryu, it works both ways.
4. Draw two katanas and fight. The log gets `NitoryuAddonSample:` lines, and Nitoryu's own `api_addon module='NitoryuAddonSample.dll' ... init='ok'`.
5. Put a heavy weapon and a sabre on the back and draw: the stance button shows and the two fight as a pair (`api_pair_rule addon='NitoryuAddonSample' answer=3` in the log).

## Start your own from it

keep `NitoryuAddon_Init` and `startPlugin`, replace `onEvent`, `pairRule` and `moveFilter`, and copy `NitoryuApi.h` next to your source (it is one file and includes nothing).
