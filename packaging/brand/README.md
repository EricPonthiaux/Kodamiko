# Kodamiko brand

The mark is a thin text caret (the editor) next to a block cursor (the
terminal where Claude runs): two rectangles on a 64-unit grid.

| File | Use |
|---|---|
| `kodamiko-icon.svg`, `kodamiko-<size>.png` | App icon: the mark on its dark rounded tile |
| `kodamiko-mark.svg` | The mark alone, on dark backgrounds |
| `kodamiko-mark-light.svg` | The mark alone, on light backgrounds |
| `kodamiko-mark-mono.svg` | One color (`currentColor`) |

Colors: caret `#e7e9ef` (or `#1b1f2a` on light), block `#4f7cf0`, tile `#1f232c`.

The application draws the same shapes itself (`src/ui/BrandMark.cpp`); keep
both in step. `packaging/linux/kodamiko.svg`, `packaging/windows/kodamiko.ico` and
`packaging/macos/kodamiko.icns` are copies of the app icon. The PNGs, the `.ico`
and the `.icns` are made with:

    for s in 16 32 48 64 128 256 512 1024; do rsvg-convert -w $s -h $s kodamiko-icon.svg -o kodamiko-$s.png; done
    magick kodamiko-16.png kodamiko-32.png kodamiko-48.png kodamiko-64.png kodamiko-128.png kodamiko-256.png ../windows/kodamiko.ico
    mkdir kodamiko.iconset
    for s in 16 32 128 256 512; do cp kodamiko-$s.png kodamiko.iconset/icon_${s}x$s.png; cp kodamiko-$((s * 2)).png kodamiko.iconset/icon_${s}x$s@2x.png; done
    iconutil -c icns kodamiko.iconset -o ../macos/kodamiko.icns && rm -r kodamiko.iconset
