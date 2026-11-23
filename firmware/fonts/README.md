# Fonts

You only need these to change the clock's fonts. The firmware already contains the rendered glyphs
(`src/graphics/fonts/Fonts.cpp`), so a normal build doesn't need them.

To regenerate the glyphs with `python -I tools/gen_fonts.py`, put these files in this folder:

| File | Font | Licence | Download |
|---|---|---|---|
| `Inter-Variable.ttf` | Inter | SIL OFL 1.1 (`OFL-Inter.txt`) | https://github.com/rsms/inter |
| `Outfit-Variable.ttf` | Outfit | SIL OFL 1.1 (`OFL-Outfit.txt`) | https://github.com/Outfitio/Outfit-Fonts |
| `Ubuntu-L.ttf`, `Ubuntu-R.ttf`, `Ubuntu-M.ttf`, `Ubuntu-B.ttf` | Ubuntu | Ubuntu Font Licence 1.0 | https://design.ubuntu.com/font |
| `SourceCodePro-Medium.ttf`, `SourceCodePro-Semibold.ttf` | Source Code Pro | SIL OFL 1.1 | https://github.com/adobe-fonts/source-code-pro |

The variable fonts must be renamed to the file names above.
