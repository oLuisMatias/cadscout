# CADscout

**CAD & 3D print file viewer for Windows.** Open a model, look around, see what is in it. No CAD program needed.

Two editions are built from the same code:

- **CADscout Light** – a small, minimal window for a quick look (orbit, pan, zoom, perspective on/off).
- **CADscout Full** – toolbar, assembly tree, edges, view cube, home/fit, plus room for more tools (measure, section view, ...).

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

`build_all.bat` builds both editions, stages `build\package\Light` and `build\package\Full`, and builds
`build\CADscoutSetup.exe` when [Inno Setup 6](https://jrsoftware.org/isinfo.php) is installed.

The version number lives only in `vcpkg.json` (`version-string`). Test models are in `samples/`.

## License

CADscout is free software under the [MIT license](LICENSE).

It is built with [Qt](https://www.qt.io/) (LGPL v3), [Open CASCADE Technology](https://dev.opencascade.org/)
(LGPL 2.1 with the Open CASCADE exception) and other open-source libraries (zlib, RapidJSON, Tabler Icons, ...).
Their licenses are listed in [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt), which is also shown in the app
(Settings > About) and shipped next to the programs.

## Status

Early development (version 0.1.0).
