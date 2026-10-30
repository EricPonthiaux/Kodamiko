# Kodamiko brand

The mark is a geometric K on a 64-unit grid. An upright editor caret and two
diagonal strokes form the letter, separated by matching four-unit cuts. The
caret's outer corners are slightly rounded; the diagonal ends stay crisp.
The upper stroke reaches a little farther to balance the upright. On the app
icon's tile, the monogram is centred and fills 64 % of its height.

| File | Use |
|---|---|
| `kodamiko-icon.svg`, `kodamiko-<size>.png` | App icon: the mark on its dark rounded tile |
| `kodamiko-mark.svg` | The mark alone, on dark backgrounds |
| `kodamiko-mark-light.svg` | The mark alone, on light backgrounds |
| `kodamiko-mark-mono.svg` | One color (`currentColor`) |

Colors: caret `#e7e9ef` (or `#1b1f2a` on light), diagonal strokes `#4f7cf0`,
tile `#1f232c`. All shapes use solid fills. The monochrome mark preserves the
same cuts and silhouette in a single color.

The application draws the same shapes itself (`src/ui/BrandMark.cpp`); keep
both in step, including the visible bounds in `inkRect` and the icon's placement
in `centredArea`. The SVG copies in `packaging/linux/kodamiko.svg` and
`site/assets/kodamiko-icon.svg` must match `kodamiko-icon.svg` byte for byte.
`packaging/windows/kodamiko.ico` and `packaging/macos/kodamiko.icns` contain the
same app icon. From this directory, regenerate the PNGs and Windows icon with:

    for s in 16 32 48 64 128 256 512 1024; do rsvg-convert -w $s -h $s kodamiko-icon.svg -o kodamiko-$s.png; done
    magick kodamiko-16.png kodamiko-32.png kodamiko-48.png kodamiko-64.png kodamiko-128.png kodamiko-256.png ../windows/kodamiko.ico

On macOS, package the same PNGs with:

    mkdir kodamiko.iconset
    for s in 16 32 128 256 512; do cp kodamiko-$s.png kodamiko.iconset/icon_${s}x$s.png; cp kodamiko-$((s * 2)).png kodamiko.iconset/icon_${s}x$s@2x.png; done
    iconutil -c icns kodamiko.iconset -o ../macos/kodamiko.icns && rm -r kodamiko.iconset
