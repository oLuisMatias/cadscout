#pragma once
#include <QSettings>
#include <algorithm>

// Which print plate (bed) the viewer draws under the model.
struct PlateSettings {
    enum class Mode {
        Auto = 0,    // the bed stored in the file (Bambu Studio / Orca Slicer projects); nothing for other files
        Custom = 1,  // always one plate of width x depth, centred under the model
        Hidden = 2,
    };
    Mode mode = Mode::Auto;
    float width = 256.f, depth = 256.f;  // millimetres, used by Custom

    void save() const {
        QSettings s;
        s.beginGroup("plate");
        s.setValue("mode", int(mode));
        s.setValue("width", double(width));
        s.setValue("depth", double(depth));
    }

    static PlateSettings load() {
        QSettings s;
        s.beginGroup("plate");
        PlateSettings p;
        const int m = s.value("mode", 0).toInt();
        p.mode = m == 1 ? Mode::Custom : m == 2 ? Mode::Hidden : Mode::Auto;
        p.width = std::clamp(s.value("width", 256.0).toFloat(), 50.f, 2000.f);
        p.depth = std::clamp(s.value("depth", 256.0).toFloat(), 50.f, 2000.f);
        return p;
    }
};
