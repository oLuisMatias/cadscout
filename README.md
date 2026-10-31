# CADscout

**CAD & 3D print file viewer for Windows.** Open a model, look around, see what is in it. No CAD program needed.

Two editions are built from the same code:

- **CADscout Lite** – a small, fast window for a quick look at a model. Available now.
- **CADscout Full** – under development.

## Download

**CADscout Lite** is free in the Microsoft Store: [get it here](https://apps.microsoft.com/detail/9NM96BXSNB6V).
If a file does not open correctly, or you have an idea, please leave a review in the Store or open an issue here.
See [Releases](../../releases) for what changed in each version.

## What CADscout Lite does

- **Look around:** orbit, pan and zoom with CAD-style mouse controls (changeable in Settings), fit, isometric view, perspective on/off, edges on/off, light and dark theme.
- **Fast:** STEP files are repaired on all processor cores, and models that took a while to load are kept in a cache,
  so opening them again is almost instant.
- **Parts:** click a part to see its name and size; right-click to hide it, show only it or make it transparent.
- **Sizes:** the model's size (W × D × H) in the status bar, in mm or inches.
- **Snapshot:** Ctrl+C copies the view as a sharp picture with a transparent background.
- **Follows your work:** the file reloads by itself when it is saved again (F5 to reload by hand), keeping the camera
  and the hidden or transparent parts.
- **Browse:** the Left/Right arrow keys open the previous/next model in the folder; recent files; open a second window
  (Ctrl+N); drag and drop.
- **Explorer:** right-click a model file and choose "View with CADscout".
- **3D printing projects:** for 3MF files a side panel (key I) shows the printer, print profile, filaments, plates,
  objects and the slicing result.
- **Languages:** English, Portuguese, German, Spanish and Slovak.

## What it opens

| Format | Extensions |
|---|---|
| STEP | `.stp` `.step` |
| IGES | `.igs` `.iges` |
| BREP | `.brep` |
| STL | `.stl` |
| OBJ (with `.mtl` colours) | `.obj` |
| PLY | `.ply` |
| VRML | `.wrl` `.vrml` |
| glTF | `.gltf` `.glb` |
| 3MF, including Bambu Studio / Orca Slicer projects | `.3mf` |
| DXF (ASCII; 3D faces and 2D lines) | `.dxf` |

CADscout Lite also opens these files inside a `.zip` archive without unpacking it first (it asks which one when the zip
holds several).

For Bambu Studio / Orca Slicer projects it shows every plate with its printer bed, the objects and parts with their
filament colours, and a click on a plate or part shows the plate name and print time, or the object and its filament.

SolidWorks files (`.sldprt`, `.sldasm`) use a closed format that cannot be read directly: export them to STEP first.

## Build

Windows, Visual Studio 2022 (C++), CMake 3.21+, and [vcpkg](https://github.com/microsoft/vcpkg) (Qt 6 and OpenCASCADE are
installed from `vcpkg.json`; the first build compiles OpenCASCADE and takes a while).

```
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
build_all.bat
```

`build_all.bat` builds both editions and stages `build\package\Lite` and `build\package\Full`.
`build_msix.bat` then makes `build\CADscoutLite.msix`, the Microsoft Store package of CADscout Lite (needs the Windows SDK).

The version number lives only in `vcpkg.json` (`version-string`). Test models are in `samples/`.

## License

CADscout is free software under the [MIT license](LICENSE).

It is built with [Qt](https://www.qt.io/) (LGPL v3), [Open CASCADE Technology](https://dev.opencascade.org/)
(LGPL 2.1 with the Open CASCADE exception) and other open-source libraries (zlib, RapidJSON, Tabler Icons, ...).
Their licenses are listed in [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt), which is also shown in the app
(About window, button in the toolbar or F1) and shipped next to the programs.

## Support

CADscout is free, and nothing is unlocked by paying. If it is useful to you, you can support its development with a
donation on [Buy Me a Coffee](https://www.buymeacoffee.com/luismatias) or [Ko-fi](https://ko-fi.com/oluismatias)
(also linked in the app: Settings > Support and the About window).

## Status

Early version: CADscout Lite is released in the Microsoft Store and has not been tested with many files yet.
CADscout Full is under development.
