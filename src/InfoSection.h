#pragma once
#include <string>
#include <vector>

// Information a file carries besides its geometry (for a Bambu Studio / Orca Slicer 3MF project: file, printer, print
// profile, filaments, plates, objects, slicing result). The info panel shows the sections as they are.
struct InfoRow {
    std::string label;
    std::string value;   // may contain line breaks
    std::string swatch;  // optional colour "#RRGGBB" drawn before the value (a filament colour)
};

struct InfoSection {
    std::string title;
    std::vector<InfoRow> rows;
};
