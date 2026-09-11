# Shell background (optional)

Drop **one** image here (`.png`, `.jpg`, or `.bmp`) and it becomes the
background of the main window. Leave the folder empty and the shell keeps its
flat `c_background` fill. If several images are present, the first one is used.

The image is cover-fitted to the window (cropped on the overflowing axis,
centred, never stretched) and rounded to the plate's corners.

## It will look faint, and that is deliberate

A scrim of the ground colour is laid back over the image at 92% opacity. That
number is measured rather than taste: the weakest text in the app
(`c_muted_foreground`) needs a ground no brighter than 0.019 relative luminance
to hold WCAG AA 4.5:1, and a white area of an image only falls to that once 92%
of the ground is back on top. At 90% the same text measures 4.23:1, at 88%
3.96:1 - both below AA.

So the background reads as a texture, not a picture. Choose accordingly:

- **Good**: dark, low-contrast, no large light areas, nothing that needs to be
  read (gradients, grain, subtle geometry, out-of-focus photography).
- **Poor**: bright images, or anything with large display type - the app's own
  text will sit on top of it and the scrim will have flattened it to a smudge
  anyway.

The scrim lives in `src/ui/screens/shell_layout.cpp` as `k_background_scrim`.
Lowering it makes the image louder and takes the UI below AA; that is the whole
trade.
