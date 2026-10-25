#pragma once
#include <QSettings>
#include <Qt>

enum class Action { Orbit = 0, Pan = 1, Zoom = 2 };

struct Binding {
    Qt::MouseButton button = Qt::MiddleButton;
    Qt::KeyboardModifier modifier = Qt::NoModifier;
};

struct Controls {
    Binding orbit{Qt::MiddleButton, Qt::NoModifier};
    Binding pan{Qt::MiddleButton, Qt::ShiftModifier};
    Binding zoom{Qt::RightButton, Qt::NoModifier};  // drag-zoom; the wheel always zooms
    bool invertWheel = false;

    static Controls cad() { return Controls{}; }
    static Controls classic() {
        Controls c;
        c.orbit = {Qt::LeftButton, Qt::NoModifier};
        c.pan = {Qt::RightButton, Qt::NoModifier};
        c.zoom = {Qt::MiddleButton, Qt::NoModifier};
        return c;
    }

    void save() const {
        QSettings s;
        s.beginGroup("controls");
        const Binding* all[3] = {&orbit, &pan, &zoom};
        const char* names[3] = {"orbit", "pan", "zoom"};
        for (int i = 0; i < 3; ++i) {
            s.setValue(QString("%1_button").arg(names[i]), int(all[i]->button));
            s.setValue(QString("%1_modifier").arg(names[i]), int(all[i]->modifier));
        }
        s.setValue("invertWheel", invertWheel);
    }

    static Controls load() {
        QSettings s;
        s.beginGroup("controls");
        Controls c = cad();
        Binding* all[3] = {&c.orbit, &c.pan, &c.zoom};
        const char* names[3] = {"orbit", "pan", "zoom"};
        for (int i = 0; i < 3; ++i) {
            all[i]->button = Qt::MouseButton(
                s.value(QString("%1_button").arg(names[i]), int(all[i]->button)).toInt());
            all[i]->modifier = Qt::KeyboardModifier(
                s.value(QString("%1_modifier").arg(names[i]), int(all[i]->modifier)).toInt());
        }
        c.invertWheel = s.value("invertWheel", false).toBool();
        return c;
    }
};
