# Brand assets

Source art for the SZK identity. These are the masters — they are not loaded by
the application at runtime.

| File | Use |
| --- | --- |
| `szk-icon.png` | 1254×1254, transparent. Source for `src/resources/szk.ico`, which the resource script compiles into the exe. |
| `szk-wordmark.png` | 1254×1254, transparent. For the site, storefront and social. |

The in-app mark is not either of these: it is `icons::id::szk_mark`, drawn as
vector strokes so it stays crisp at 16px, follows the light/dark theme, and
costs no texture upload. Keep the three consistent if the identity changes.

## Regenerating the icon

`szk.ico` holds 256/128/64/48/32/16 as embedded PNGs. Rebuild it from
`szk-icon.png` with any icon tool, or with the script in the commit that
introduced it.

At 16px the gauge ticks in the full art disappear into noise. If the icon ever
needs to read better in the taskbar, draw a simplified 16px variant by hand
rather than downscaling further.
