# Duet brand

The mark is a thin text caret (the editor) next to a block cursor (the
terminal where Claude runs): two rectangles on a 64-unit grid.

| File | Use |
|---|---|
| `duet-icon.svg`, `duet-<size>.png` | App icon: the mark on its dark rounded tile |
| `duet-mark.svg` | The mark alone, on dark backgrounds |
| `duet-mark-light.svg` | The mark alone, on light backgrounds |
| `duet-mark-mono.svg` | One color (`currentColor`) |

Colors: caret `#e7e9ef` (or `#1b1f2a` on light), block `#4f7cf0`, tile `#1f232c`.

The application draws the same shapes itself (`src/ui/BrandMark.cpp`); keep
both in step. `packaging/linux/duet.svg`, `packaging/windows/duet.ico` and
`packaging/macos/duet.icns` are copies of the app icon. The PNGs, the `.ico`
and the `.icns` are made with:

    for s in 16 32 48 64 128 256 512 1024; do rsvg-convert -w $s -h $s duet-icon.svg -o duet-$s.png; done
    magick duet-16.png duet-32.png duet-48.png duet-64.png duet-128.png duet-256.png ../windows/duet.ico
    mkdir duet.iconset
    for s in 16 32 128 256 512; do cp duet-$s.png duet.iconset/icon_${s}x$s.png; cp duet-$((s * 2)).png duet.iconset/icon_${s}x$s@2x.png; done
    iconutil -c icns duet.iconset -o ../macos/duet.icns && rm -r duet.iconset
