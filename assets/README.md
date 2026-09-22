# Application icon

`usage-tracker.png` is the original artwork generated with the built-in image generation tool. `usage-tracker.ico` contains 16, 24, 32, 48, 64, 128 and 256 pixel variants, embedded in the executable and used for window and tray icons.

Prompt: Use case: logo-brand. Create one polished Windows application icon for UsageTracker, a compact desktop app tracking AI usage quotas. A dark midnight rounded-square tile with two bold horizontal rounded usage meters, upper teal filled about 75%, lower warm amber filled about 45%, subtle dark slate unfilled tracks. Clean minimal flat graphic, no letters, no numbers, no logos, no extra decoration, legible at 16 pixels, centered square composition with small transparent margin outside tile. True transparent background. Save the generated asset for use in the project.

To regenerate the ICO from the source artwork using ImageMagick:

```bat
magick assets/usage-tracker.png -define icon:auto-resize=256,128,64,48,32,24,16 assets/usage-tracker.ico
```

ImageMagick is only needed when updating the icon, not for normal builds.
