#pragma once
#include <array>
#include <string>

#include "InfoSection.h"
#include <utility>
#include <vector>

// Readers for formats OpenCASCADE cannot open (3MF, DXF). They return plain geometry that StepLoader turns
// into the same model structure as every other format.
using Vec3 = std::array<double, 3>;

struct SceneMesh {
    std::string name;
    std::vector<Vec3> points;
    std::vector<std::array<int, 3>> triangles;  // indices into points
    bool hasColor = false;
    std::array<float, 3> color{0.72f, 0.74f, 0.78f};  // sRGB, 0..1
    // Bambu/Orca projects: which object this belongs to and the filament it is printed with.
    std::string objectName;
    int filament = 0;  // filament number, 1-based; 0 = unknown
    std::string filamentType;  // e.g. "PLA"
    std::string filamentVendor;  // e.g. "Bambu Lab"
};

struct SceneLines {
    std::string name;
    std::vector<std::pair<Vec3, Vec3>> segments;
};

// A print plate (bed) outline in world coordinates, millimetres, at Z = 0.
struct ScenePlate {
    std::vector<std::array<double, 2>> outline;
    std::string name;          // e.g. "Plate 1"
    double printSeconds = -1;  // from a sliced file, -1 when not sliced
};

// Optional hierarchy (e.g. plate -> object -> part). A leaf points at a mesh; a group has children.
struct SceneNode {
    std::string name;
    std::vector<SceneNode> children;
    int mesh = -1;  // index into Scene::meshes, -1 for a group
};

struct Scene {
    std::vector<SceneMesh> meshes;
    std::vector<SceneLines> lines;
    std::vector<SceneNode> tree;  // empty: show meshes and lines as a flat list
    std::vector<ScenePlate> plates;  // print plates (Bambu/Orca projects)
    std::string info;             // short summary for the status bar (plates, print time, ...)
    std::vector<InfoSection> sections;  // everything the file says besides geometry (info panel)
};

// Both throw std::runtime_error with a readable message when the file cannot be used.
Scene read3mf(const std::string& utf8Path);
Scene readDxf(const std::string& utf8Path);

// Zip archives that hold model files (Lite opens them). Names inside the archive use '/'.
// The entries whose extension (lower case, no dot) is in `extensions`, in archive order.
std::vector<std::string> zipModelEntries(const std::string& zipPath, const std::vector<std::string>& extensions);
// Writes `entry` into `destDir` and returns the path of the written file. For OBJ/glTF/VRML the other files in the same
// archive folder (materials, textures, buffers; not other models) are written too, so the model finds them. Such a side
// file that cannot be written is skipped and counted in *skipped.
std::string extractZipModel(const std::string& zipPath, const std::string& entry, const std::string& destDir,
                            const std::vector<std::string>& modelExtensions, int* skipped = nullptr);
