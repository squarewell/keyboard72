#!/usr/bin/env python3
"""Rebuild app/src/main/assets/notoemoji.ttf.

The engine draws text with its own FreeType, one-color glyph outlines and no
text shaping. A character missing from the current font goes to a backup
font (Roen::FreetypeFont::getBackupFontForChar): Hangul and Hebrew to their
own fonts, emoji to "notoemoji"; anything else tries "andikar", then
"notoemoji". So notoemoji.ttf is the last resort for every character.

The original asset was Noto Emoji 1.05 (2014). The emoji picker's iOS 10 list
has 1033 code points, 185 of them missing from it, and 117 of the 144 entries
in the kaomoji board (bonusthings/allkaomoji.strext) use at least one of 125
characters that no font on the chain had. This builds:
- a static instance of the current monochrome Noto Emoji, which covers every
  emoji in the list;
- plus glyphs from Noto text fonts for every kaomoji character, and for the
  kana, CJK punctuation, fullwidth/halfwidth forms, box drawing and
  geometric shapes blocks, that neither Noto Emoji nor andikar has.

NotoEmoji[wght].ttf is kept here; the text fonts are downloaded from a pinned
google/fonts commit into sources/ (not version-controlled). All are SIL OFL
1.1. Needs fontTools. The output is reproducible.
"""
from copy import deepcopy
from pathlib import Path
import urllib.parse
import urllib.request

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.ttLib.scaleUpem import scale_upem
from fontTools.varLib import instancer

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ASSETS = ROOT / 'app/src/main/assets'
SOURCES = HERE / 'sources'
GOOGLE_FONTS = 'https://raw.githubusercontent.com/google/fonts/23e54b51ddffbc7713c583748e3bd86f62b1fa4a/ofl/'

EMOJI_WEIGHT = 600  # the axis runs 300-700; slightly heavier lines read better at key size
TEXT_WEIGHT = 500   # close to the keyboard's Quicksand Medium

# Text fonts for the missing characters, in order of preference: Latin, Greek
# and Cyrillic from Noto Sans, then script fonts, then Japanese (kaomoji come
# from Japanese text, so its fullwidth symbols fit best), then symbols.
FALLBACK = [
    ('notosans', 'NotoSans[wdth,wght].ttf'),
    ('notosansarabic', 'NotoSansArabic[wdth,wght].ttf'),
    ('notosansarmenian', 'NotoSansArmenian[wdth,wght].ttf'),
    ('notosansgeorgian', 'NotoSansGeorgian[wdth,wght].ttf'),
    ('notosanshebrew', 'NotoSansHebrew[wdth,wght].ttf'),
    ('notosansdevanagari', 'NotoSansDevanagari[wdth,wght].ttf'),
    ('notosansoriya', 'NotoSansOriya[wdth,wght].ttf'),
    ('notosanstamil', 'NotoSansTamil[wdth,wght].ttf'),
    ('notosanstelugu', 'NotoSansTelugu[wdth,wght].ttf'),
    ('notosanskannada', 'NotoSansKannada[wdth,wght].ttf'),
    ('notosansthai', 'NotoSansThai[wdth,wght].ttf'),
    ('notosanslao', 'NotoSansLao[wdth,wght].ttf'),
    ('notoseriftibetan', 'NotoSerifTibetan[wght].ttf'),
    ('notosanscanadianaboriginal', 'NotoSansCanadianAboriginal[wght].ttf'),
    ('notosansjp', 'NotoSansJP[wght].ttf'),
    ('notosanssymbols2', 'NotoSansSymbols2-Regular.ttf'),
    ('notosanssymbols', 'NotoSansSymbols[wght].ttf'),
    ('notosansmath', 'NotoSansMath-Regular.ttf'),
]
BLOCKS = [(0x2500, 0x25FF),  # box drawing, block elements, geometric shapes
          (0x3000, 0x30FF),  # CJK symbols and punctuation, hiragana, katakana
          (0xFF00, 0xFFEF)]  # halfwidth and fullwidth forms


def fetch(family, filename):
    path = SOURCES / filename
    if not path.exists():
        SOURCES.mkdir(exist_ok=True)
        url = GOOGLE_FONTS + family + '/' + urllib.parse.quote(filename)
        with urllib.request.urlopen(url) as response:
            path.write_bytes(response.read())
    return TTFont(path)


def static(font, weight):
    if 'fvar' not in font:
        return font
    wanted = {'wght': weight, 'wdth': 100}
    axes = {a.axisTag: wanted[a.axisTag] for a in font['fvar'].axes if a.axisTag in wanted}
    return instancer.instantiateVariableFont(font, axes)


def wanted_codepoints():
    text = (ASSETS / 'bonusthings/allkaomoji.strext').read_text(encoding='utf-8')
    cps = {ord(c) for line in text.splitlines() if not line.startswith('::') for c in line if ord(c) > 0x20}
    for lo, hi in BLOCKS:
        cps.update(range(lo, hi + 1))
    return cps - set(TTFont(ASSETS / 'andikar.ttf').getBestCmap())


def add_glyphs(base, src, cps, prefix):
    """Copy the glyphs for cps (with their components) from src into base."""
    cmap, glyf, hmtx = src.getBestCmap(), src['glyf'], src['hmtx']
    renamed = {}

    def copy(name):
        if name in renamed:
            return renamed[name]
        new = f'{prefix}.{name}'
        renamed[name] = new
        glyph = deepcopy(glyf[name])
        if glyph.isComposite():
            for component in glyph.components:
                component.glyphName = copy(component.glyphName)
        base['glyf'][new] = glyph
        base['hmtx'][new] = hmtx[name]
        return new

    added = {cp: copy(cmap[cp]) for cp in sorted(cps)}
    base.setGlyphOrder(base.getGlyphOrder() + [n for n in renamed.values() if n not in base.getGlyphOrder()])
    for table in base['cmap'].tables:
        if table.isUnicode() and table.format in (4, 12):
            table.cmap.update({cp: name for cp, name in added.items() if table.format != 4 or cp <= 0xFFFF})


def main():
    base = static(TTFont(HERE / 'NotoEmoji[wght].ttf'), EMOJI_WEIGHT)
    for tag in ('vhea', 'vmtx'):  # vertical layout: unused by the engine, and the text glyphs have none
        del base[tag]
    need = wanted_codepoints() - set(base.getBestCmap())
    options = subset.Options()
    options.layout_features = []
    options.hinting = False
    options.notdef_outline = True
    for family, filename in FALLBACK:
        font = fetch(family, filename)
        take = need & set(font.getBestCmap())
        if not take:
            continue
        font = static(font, TEXT_WEIGHT)
        subsetter = subset.Subsetter(options)
        subsetter.populate(unicodes=take)
        subsetter.subset(font)
        scale_upem(font, base['head'].unitsPerEm)
        add_glyphs(base, font, take, family)
        need -= take
        print(f'{family:28s} {len(take):4d} glyphs')
    printable = sorted(cp for cp in need if chr(cp).isprintable())
    if printable:
        print('not in any source:', ' '.join(f'U+{cp:04X}' for cp in printable))
    base.recalcTimestamp = False  # reproducible output
    base.save(ASSETS / 'notoemoji.ttf')


if __name__ == '__main__':
    main()
