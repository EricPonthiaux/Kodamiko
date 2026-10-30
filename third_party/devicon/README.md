# Project template logos

Original color logos from [Devicon v2.17.0](https://github.com/devicons/devicon/tree/v2.17.0),
distributed under the [MIT license](LICENSE). SVG sources are unchanged apart
from their filenames.

| Local name | Upstream file relative to `icons/` |
| --- | --- |
| `angular.svg` | `angular/angular-original.svg` |
| `cpp.svg` | `cplusplus/cplusplus-original.svg` |
| `java.svg` | `java/java-original.svg` |
| `python.svg` | `python/python-original.svg` |
| `qt.svg` | `qt/qt-original.svg` |
| `typescript.svg` | `typescript/typescript-original.svg` |

The 128 × 128 transparent PNG exports are embedded in the application. They
provide enough resolution for the 24-pixel template icons at high display
scales without requiring Qt's optional SVG plugin. Regenerate an export with:

```sh
rsvg-convert --width 128 --height 128 angular.svg --output angular.png
```

Names and logos belong to their respective owners and identify the available
project templates; their use does not imply endorsement.
