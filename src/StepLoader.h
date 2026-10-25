#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct MeshBatch {
    std::array<float, 3> color{0.7f, 0.7f, 0.75f};
    std::vector<float> vertices;     // interleaved: position xyz, normal xyz
    std::vector<uint32_t> indices;
};

struct PartRange {
    int batch = 0;
    uint32_t start = 0;  // first index in the batch's index buffer
    uint32_t count = 0;
};

// A filament a part is printed with (Bambu/Orca projects).
struct FilamentUse {
    int number = 0;  // 1-based
    bool hasColor = false;
    std::array<float, 3> color{0, 0, 0};
    std::string type;  // e.g. "PLA"
};

struct Part {
    std::string name;
    std::vector<PartRange> ranges;
    std::string object;                  // Bambu/Orca: name of the object this part belongs to
    std::vector<FilamentUse> filaments;  // Bambu/Orca: filament(s) the part is printed with
};

struct Node {
    std::string name;
    int parent = -1;
    int part = -1;  // index into Model::parts, -1 for assemblies
};

// A print plate (bed) outline in world coordinates, millimetres, at Z = 0 (Bambu/Orca projects).
struct PlateShape {
    std::vector<std::array<float, 2>> outline;
    std::string name;          // e.g. "Plate 1"
    double printSeconds = -1;  // from a sliced file; -1 when the file is not sliced
};

struct Model {
    std::vector<MeshBatch> batches;
    std::vector<Part> parts;
    std::vector<Node> nodes;  // assembly tree in creation order; parents precede children
    std::vector<float> edges;        // line segment endpoints, xyz each
    std::array<float, 3> boundsMin{0, 0, 0};
    std::array<float, 3> boundsMax{0, 0, 0};
    size_t triangleCount = 0;
    size_t faceCount = 0;
    double loadSeconds = 0;
    std::string formatName;        // e.g. "STL"
    std::vector<PlateShape> plates;  // empty when the file has no printer bed information
    std::string extraInfo;         // e.g. printer, plates and print time from a Bambu/Orca 3MF
    bool singleObject = false;     // the format cannot hold more than one object (STL, PLY, BREP)
};

enum class MeshQuality { Fast = 0, Balanced = 1, High = 2 };

// Loads STEP, IGES, BREP, STL, OBJ, PLY or VRML, chosen by file extension.
// Throws std::runtime_error on failure.
std::shared_ptr<Model> loadModelFile(const std::string& utf8Path, MeshQuality quality = MeshQuality::Balanced);
std::shared_ptr<Model> loadStepFile(const std::string& utf8Path, MeshQuality quality = MeshQuality::Balanced);
