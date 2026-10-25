# Windows installer

The installer offers a Light-only choice and a Full choice. Full also installs
Light, so the Light app's **Open in Full** button can launch the Full app from
the same installation directory. If Full is not installed, the button explains
that it is unavailable.

Both editions open STEP files and therefore include the Qt viewport runtime and
OpenCascade STEP reader. Each edition is staged separately; future dependencies
needed only by Full should be linked only to the Full target.

From the repository root, build and stage both editions with a Visual Studio
CMake build:

```powershell
cmake --build build --config Release --target StepViewer StepViewerLight
cmake --install build --config Release --prefix .\build\package\Light --component Light
cmake --install build --config Release --prefix .\build\package\Full --component Light
cmake --install build --config Release --prefix .\build\package\Full --component Full
```

Install Inno Setup 6, then compile the installer:

```powershell
iscc .\installer\StepViewer.iss
```

The installer is written to `build\StepViewerSetup.exe`.
