# Noto fonts for the engine's last-resort fallback

The engine draws text with its own FreeType (one-color glyph outlines, no text
shaping). When a character is missing from the current font it tries backup
fonts, and `assets/notoemoji.ttf` is the last one for every character. That
asset used to be Noto Emoji 1.05 (2014), which left blank cells in the emoji
picker (185 of the 1033 code points in its iOS 10 list were missing) and in
the kaomoji board (117 of 144 entries used a character no font had).

`make_asset.py` rebuilds the asset reproducibly:
- `NotoEmoji[wght].ttf` (kept here) as a static weight-600 instance: the
  current monochrome Noto Emoji, covering all emoji in the picker;
- plus glyphs from Noto text fonts (Noto Sans, the script fonts for Arabic,
  Kannada, Tibetan, Canadian syllabics and others, Noto Sans JP, Symbols,
  Symbols 2, Math) for every kaomoji character and for the kana, CJK
  punctuation, fullwidth/halfwidth forms, box drawing and geometric shapes
  blocks that neither Noto Emoji nor the engine's `andikar` font has.

The text fonts are downloaded from google/fonts at a pinned commit into
`sources/` (not version-controlled). `NotoEmoji[wght].ttf`, `OFL.txt` and
`METADATA.pb` come from https://github.com/google/fonts/tree/main/ofl/notoemoji.
All of these fonts are licensed under the SIL Open Font License 1.1
(`OFL.txt`). A color font such as Android's system emoji can't be used: the
engine only draws one-color outlines.
