#pragma once
#include <QString>
#include <QStringList>

// The model file types the viewer's own screens know about (Open dialog, browsing a folder with the arrow keys, units in
// the status bar). The reader itself picks its code by extension in `formatOf` (StepLoader.cpp): add a new format there too.
inline const QStringList& modelExtensions() {
    static const QStringList list = {"stp", "step", "igs", "iges", "brep", "stl", "obj", "ply",
                                     "wrl", "vrml", "gltf", "glb", "3mf", "dxf"};
    return list;
}

// "*.stp *.step ..." for file dialogs.
inline QString modelFilterPatterns() {
    QStringList patterns;
    for (const QString& e : modelExtensions()) patterns << "*." + e;
    return patterns.join(' ');
}

// Formats whose coordinates are in millimetres (defined by the format, or the 3D-printing convention).
inline bool extensionIsMillimetres(const QString& extension) {
    static const QStringList mm = {"stp", "step", "igs", "iges", "brep", "3mf", "stl"};
    return mm.contains(extension.toLower());
}
