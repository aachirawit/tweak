# Thai font (optional)

Drop a `.ttf` or `.otf` here and its Thai glyphs are merged on top of Geist,
which has none of its own. If several files are present the first by name is
used. With the folder empty the app falls back to the system Thai UI font -
Leelawadee UI, or Tahoma on older Windows - and with neither of those present
nothing is merged and Thai renders as boxes.

A bundled face is preferred over the system one because it is the only way every
machine renders the app the same: the system fonts differ by Windows version and
can be replaced.

## What gets used

Only the Thai block (U+0E00–U+0E7F) is taken from this font. Latin, digits and
punctuation stay Geist, so the two scripts keep one voice instead of the Thai
face quietly taking over the whole interface.

Pick a Thai face whose weight and roundness sit near Geist's. A display or
handwriting face will read as a different product the moment a Thai string
appears next to an English one.

## Licence

Whatever you put here ships with the app, so it has to be a font you may
redistribute. The Windows system fonts are not - which is why they are only ever
loaded from the machine, never copied in here.
