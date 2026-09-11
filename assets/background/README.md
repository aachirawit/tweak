# Shell background (optional)

Drop **one** image here (`.png`, `.jpg`, or `.bmp`) and it becomes the
background of the main window. Leave the folder empty and the shell keeps its
flat `c_background` fill. If several images are present, the first one is used.

The image is cover-fitted to the window (cropped on the overflowing axis,
centred, never stretched) and rounded to the plate's corners.

## How much of it you see

The surfaces that carry text - content cards and the sidebar - are opaque, so
they keep their own ground and the image cannot reach the text inside them. The
image shows through everywhere else: the content padding, the gaps between
cards, behind the header and footer strips.

A scrim of the ground colour is laid back over the image at 70%
(`k_background_scrim` in `src/ui/screens/shell_layout.cpp`). That value is
chosen for dark artwork. The text it still has to protect is the text drawn
straight onto the plate - the header row and the footer strip - and the limit is
measured: `c_muted_foreground` needs a ground no brighter than 0.019 relative
luminance to hold WCAG AA 4.5:1, which a white area of an image only reaches at
a 0.92 scrim (0.90 measures 4.23:1, 0.88 measures 3.96:1).

So pick the scrim to match the image:

| Image | Scrim |
|-------|-------|
| Dark, low-contrast, no large light areas | 0.70 (default) |
| Mixed, some light areas | 0.80 - 0.85 |
| Bright, or large light areas | 0.92, and it will read as a faint texture |

## Choosing an image

- **Good**: dark, low-contrast, nothing that needs to be read - gradients,
  grain, subtle geometry, out-of-focus photography.
- **Poor**: anything with large display type. The app draws its own text on top,
  and raising the scrim far enough to keep that readable flattens the type you
  wanted to see in the first place.
