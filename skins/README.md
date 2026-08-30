# Skins

Bundled skins are compiled into Throned, so release and installed folders do not
contain theme assets. To add a custom skin without rebuilding, create `skins/`
next to the binary or in Throned's app-data directory and drop a skin folder in it.

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
