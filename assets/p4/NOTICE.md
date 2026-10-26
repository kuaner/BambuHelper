# P4 dashboard artwork

The transparent PNGs in `printers/` are unchanged Bambu Studio 02.08.02.61
product images, also used in the owner's BambuDroid app. Original files are in
`/Applications/BambuStudio.app/Contents/Resources/images/` and upstream
https://github.com/bambulab/BambuStudio/tree/master/resources/images.

Mapping: x2d → printer_thumbnail_N6_png.png; p2s → printer_thumbnail_N7_png.png;
p1s → printer_thumbnail_p1s_png.png; p1p → printer_thumbnail_p1p_png.png;
x1 → printer_thumbnail_png.png; x1c → printer_thumbnail_x1c_png.png;
x1e → printer_thumbnail_x1e_png.png; a1 → printer_thumbnail_n2s_png.png;
a1mini → printer_thumbnail_n1_png.png; h2d → printer_thumbnail_h2d_png.png;
h2dpro → printer_thumbnail_o1e_png.png; h2s → printer_thumbnail_o1s_png.png;
h2c → printer_thumbnail_o1c_png.png.

These assets retain Bambu Lab's ownership and AGPL-3.0 license, reproduced in
`docs/licenses/BambuStudio-AGPL-3.0.txt`; they are not relicensed under this
repository's MIT license. `scripts/generate_p4_artwork.py` embeds the original
bytes without modification. Only P4 firmware links them.

The spool visual follows the ISC-licensed ha-bambulab-cards v0.6.54 spool
(`cards/shared-components/spool/spool.ts` and `spool.styles.ts`), with its notice
in `docs/licenses/ha-bambulab-cards-ISC.txt`. Layout follows the owner's approved
BambuDroid dashboard; the C++ renderer is implemented for this project.

## P4 Sans bitmap fonts

`p4_sans_20.vlw`, `p4_sans_24.vlw` and the small duration face are converted from
Google Fonts' Noto Sans SC variable font at weight 400. They cover GB2312 plus
UI punctuation (7,543 requested glyphs), use grayscale antialiasing, and are
loaded from flash with glyph lookup tables in PSRAM. Rare characters outside
this subset are not guaranteed. Only P4 firmware embeds these fonts.

Source: https://github.com/google/fonts/tree/main/ofl/notosanssc
Original font SHA-256: `a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da`.
SIL Open Font License: `docs/licenses/NotoSansSC-OFL.txt`.
Regenerate with `python scripts/generate_p4_font.py path/to/NotoSansSC.ttf`
(requires freetype-py). Converted bitmap faces are named P4 Sans.
