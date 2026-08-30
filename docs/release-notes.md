# Localized release notes

GitHub keeps one release body, while Throned can show only the section that
matches the interface language. Wrap each translation in invisible HTML
comments; keep English first and put additional translations in ordinary
GitHub `<details>` blocks:

```md
<!-- throned:lang=en -->
## What's new

- English release notes go here.
<!-- throned:lang=end -->

<details>
<summary>Русский</summary>

<!-- throned:lang=ru -->
## Что нового

- Здесь находится русский текст.
<!-- throned:lang=end -->

</details>
```

The comments are invisible on GitHub. In the updater, a regional locale such
as `ru_RU` first tries `ru-ru`, then `ru`, then English. If none of those
exists, the first translated block is used. Release bodies without markers are
shown unchanged, so old releases remain compatible; malformed marked bodies
are also left intact instead of hiding text.

To render either language with the real update dialog:

```powershell
Throned.exe --update-prompt-preview --notes release.md -lang en --output update-en.png
Throned.exe --update-prompt-preview --notes release.md -lang ru --output update-ru.png
```
