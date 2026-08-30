# Skins

A skin is a folder. Throned reads `skins/` next to the binary and `skins/` in the
config directory, so a skin can ship with a build or be dropped in by hand.

```
skins/<id>/
  skin.json   required - name, dark flag, optional font, colour overrides
  skin.qss    optional - appended after the resolved base sheet, so it wins
  icons/      optional - overrides the built-in glyphs
```

Every colour left out of `skin.json` keeps its value from the default theme, so a
skin can restyle three tokens and stay coherent everywhere else. `skin.qss` is
resolved through the same token substitution, which means it can name palette
colours and still reach for gradients, images and fonts the palette cannot express.

Token names are the fields of `ThronedThemeColors` in
`include/ui/setting/ThronedPalette.hpp`.
