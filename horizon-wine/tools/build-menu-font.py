"""Rebuild CorkDeckUI.ttf with fontTools and the pinned NotoSansSC variable font.

Input: google/fonts at 06bfe0d62a98375cb09c942fa18e68bd32cff057,
ofl/notosanssc/NotoSansSC[wght].ttf. Run: python3 build-menu-font.py INPUT.ttf.
The checked-in TTF is used directly by the Switch build.
"""
import hashlib
import sys
from pathlib import Path

from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont

root = Path(__file__).resolve().parents[1]
assets = root / "assets/fonts"
source = Path(sys.argv[1])
if hashlib.sha256(source.read_bytes()).hexdigest() != "a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da":
    sys.exit("Unexpected NotoSansSC font revision")
base = TTFont(assets / "Manrope-SemiBold.ttf", recalcTimestamp=False)
noto = TTFont(source)
cmap = noto.getBestCmap()
glyphs = noto.getGlyphSet(location={"wght": 600})
scale = base["head"].unitsPerEm / noto["head"].unitsPerEm

# Include common Chinese game titles as well as every character in the UI.
chars = {ord(c) for p in (root / "source/shell/shell_strings.inc", root / "source/app/app_settings.c")
         for c in p.read_text(encoding="utf-8") if ord(c) >= 128}
for lead in range(0xa1, 0xf8):
    for trail in range(0xa1, 0xff):
        chars.update(map(ord, bytes((lead, trail)).decode("gb2312", errors="ignore")))

for code in sorted(chars - base.getBestCmap().keys()):
    if code not in cmap:
        continue
    src = glyphs[cmap[code]]
    outline = DecomposingRecordingPen(glyphs)
    src.draw(outline)
    pen = TTGlyphPen(None)
    outline.replay(TransformPen(pen, (scale, 0, 0, scale, 0, 0)))
    name = f"uni{code:04X}.sc"
    glyph = pen.glyph()
    base["glyf"][name] = glyph
    glyph.recalcBounds(base["glyf"])
    base["hmtx"][name] = (round(src.width * scale), getattr(glyph, "xMin", 0))
    for table in base["cmap"].tables:
        if table.isUnicode():
            table.cmap[code] = name

names = base["name"]
updates = {0: names.getDebugName(0) + "; " + noto["name"].getDebugName(0),
           1: "CorkDeck UI", 2: "Regular", 3: "CorkDeckUI-Regular-1",
           4: "CorkDeck UI", 6: "CorkDeckUI-Regular", 16: "CorkDeck UI", 17: "Regular",
           13: (assets / "NotoSansSC-OFL.txt").read_text().split("\n", 1)[1].strip()}
for record in names.names:
    if record.nameID in updates:
        names.setName(updates[record.nameID], record.nameID, record.platformID, record.platEncID, record.langID)
base.save(assets / "CorkDeckUI.ttf")
