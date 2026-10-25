#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QPainter>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

#include "Icons.h"
#include "SupportButtons.h"

namespace {

// Square, dark checkbox with a thin border and a check mark, drawn by hand so it needs no image plugins.
class FlatCheckBox : public QCheckBox {
public:
    explicit FlatCheckBox(const QString& text) : QCheckBox(text) {
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
    }

    QSize sizeHint() const override {
        const QFontMetrics fm(font());
        return {kBox + kGap + fm.horizontalAdvance(text()) + 2, std::max(kBox + 4, fm.height() + 4)};
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF box(0.5, (height() - kBox) / 2.0 + 0.5, kBox - 1, kBox - 1);
        const QColor win = palette().color(QPalette::Window);
        p.setBrush(win.lightness() < 128 ? win.lighter(135) : win.darker(104));
        QColor border = palette().color(QPalette::WindowText);
        border.setAlpha(110);
        p.setPen(QPen(underMouse() || hasFocus() ? palette().color(QPalette::Highlight) : border, 1));
        p.drawRoundedRect(box, 3, 3);
        if (isChecked()) {
            const int icon = kBox - 4;
            const QPixmap pm = renderTablerIcon("check", palette().color(QPalette::WindowText), icon, devicePixelRatioF());
            p.drawPixmap(QPointF(box.center().x() - icon / 2.0, box.center().y() - icon / 2.0), pm);
        }
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(QRect(kBox + kGap, 0, width() - kBox - kGap, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }

private:
    static constexpr int kBox = 18, kGap = 10;
};

// A table in the style of a code editor's keyboard-shortcuts list:
// pencil | Command | Keybinding (drawn as key caps) | When | Source.
class BindingTable : public QTableWidget {
public:
    static constexpr int kRowHeight = 30, kHeaderHeight = 36;

    BindingTable() : QTableWidget(0, 5) {
        setHorizontalHeaderLabels({"", "Command", "Keybinding", "When", "Source"});
        verticalHeader()->hide();
        verticalHeader()->setDefaultSectionSize(kRowHeight);
        horizontalHeader()->setFixedHeight(kHeaderHeight);
        horizontalHeader()->setHighlightSections(false);
        horizontalHeader()->setSectionsClickable(false);
        horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
        horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
        horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
        horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
        setColumnWidth(0, 36);
        setColumnWidth(1, 195);
        setColumnWidth(2, 215);
        setColumnWidth(4, 84);
        setWordWrap(false);
        setTextElideMode(Qt::ElideRight);
        setShowGrid(false);
        setFrameShape(QFrame::NoFrame);
        setSelectionBehavior(QAbstractItemView::SelectRows);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setEditTriggers(QAbstractItemView::NoEditTriggers);
        setFocusPolicy(Qt::NoFocus);
        setAlternatingRowColors(true);
        setMouseTracking(true);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setStyleSheet(
            "QTableWidget{background:transparent;alternate-background-color:rgba(128,128,128,22);border:none;"
            "outline:0;font-size:13px;}"
            "QTableWidget::item{padding:0 8px;border:none;}"
            "QTableWidget::item:hover{background:rgba(128,128,128,50);}"
            "QTableWidget::item:selected{background:rgba(128,128,128,60);color:palette(window-text);}"
            "QHeaderView{background:transparent;}"
            "QHeaderView::section{background:transparent;color:palette(window-text);border:none;"
            "border-bottom:1px solid palette(mid);padding:0 8px;font-weight:bold;text-align:left;}");
        connect(this, &QTableWidget::cellClicked, this, [this](int row, int col) {
            if (col == 0 && isEditable(row) && onEdit) onEdit(row);
        });
        connect(this, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
            if (isEditable(row) && onEdit) onEdit(row);
        });
    }

    std::function<void(int)> onEdit;  // called with the row when its pencil is clicked

    int addRow(const QString& command, const QStringList& keys, const QString& when, const QString& source,
               bool editable) {
        const int row = rowCount();
        insertRow(row);
        auto* pencil = new QTableWidgetItem;
        if (editable) {
            const QColor fg = palette().color(QPalette::WindowText);
            pencil->setIcon(tablerIcon("pencil", fg, fg, 14, devicePixelRatioF()));
        }
        setItem(row, 0, pencil);
        setItem(row, 1, new QTableWidgetItem(command));
        auto* whenItem = new QTableWidgetItem(when);
        QFont mono(whenItem->font());
        mono.setFamily("Consolas");
        whenItem->setFont(mono);
        setItem(row, 3, whenItem);
        setItem(row, 4, new QTableWidgetItem(source));
        editable_.push_back(editable);
        text_.push_back(QString());
        setKeys(row, keys);
        text_[size_t(row)] = (command + " " + keys.join(" ") + " " + when + " " + source).toLower();
        fitHeight();
        return row;
    }

    void setKeys(int row, const QStringList& keys) {
        auto* w = new QWidget;
        auto* l = new QHBoxLayout(w);
        l->setContentsMargins(8, 0, 8, 0);
        l->setSpacing(4);
        l->setAlignment(Qt::AlignVCenter);
        for (int i = 0; i < keys.size(); ++i) {
            if (i > 0) {
                auto* plus = new QLabel("+");
                plus->setFixedHeight(22);
                plus->setStyleSheet("QLabel{color:palette(placeholder-text);}");
                l->addWidget(plus);
            }
            auto* cap = new QLabel(keys[i]);
            cap->setFixedHeight(22);
            cap->setStyleSheet(
                "QLabel{border:1px solid palette(mid);border-bottom:2px solid palette(mid);border-radius:4px;"
                "padding:1px 6px;background:palette(base);font-family:Consolas;font-size:12px;}");
            l->addWidget(cap);
        }
        l->addStretch(1);
        setCellWidget(row, 2, w);
    }

    // Shows only the rows that contain every search word; returns how many are shown.
    int filter(const QStringList& words) {
        int shown = 0;
        for (int r = 0; r < rowCount(); ++r) {
            bool ok = true;
            for (const QString& w : words) ok = ok && text_[size_t(r)].contains(w);
            setRowHidden(r, !ok);
            shown += ok ? 1 : 0;
        }
        fitHeight();
        return shown;
    }

private:
    bool isEditable(int row) const { return row >= 0 && size_t(row) < editable_.size() && editable_[size_t(row)]; }
    void fitHeight() {
        int visible = 0;
        for (int r = 0; r < rowCount(); ++r) visible += isRowHidden(r) ? 0 : 1;
        setFixedHeight(kHeaderHeight + visible * kRowHeight + 2);
    }
    std::vector<bool> editable_;
    std::vector<QString> text_;
};

const Qt::MouseButton kButtons[] = {Qt::LeftButton, Qt::MiddleButton, Qt::RightButton};
const Qt::KeyboardModifier kMods[] = {Qt::NoModifier, Qt::ShiftModifier, Qt::ControlModifier, Qt::AltModifier};
}  // namespace

SettingsDialog::SettingsDialog(const Controls& initial, int meshQuality, const PlateSettings& plate, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Settings");
    resize(900, 600);
    setStyleSheet(
        "QDialog{background:palette(window);}"
        "QLabel,QCheckBox{font-size:13px;}"
        "QLineEdit{border:1px solid palette(mid);border-radius:6px;padding:7px 10px;font-size:13px;"
        "background:palette(base);}"
        "QLineEdit:focus{border-color:palette(highlight);}"
        "QPushButton{border:1px solid palette(mid);border-radius:6px;padding:7px 14px;"
        "background:transparent;color:palette(window-text);}"
        "QPushButton:hover{background:palette(midlight);}"
        "QPushButton:pressed{background:palette(mid);}"
        "QPushButton:default{background:palette(highlight);color:palette(highlighted-text);"
        "border-color:palette(highlight);}"
        "QComboBox{min-height:28px;border:1px solid palette(mid);border-radius:6px;padding:0 10px;"
        "background:palette(base);}"
        "QComboBox:hover{border-color:palette(highlight);}"
        "QDoubleSpinBox{min-height:28px;border:1px solid palette(mid);border-radius:6px;padding:0 8px;"
        "background:palette(base);}"
        "QDoubleSpinBox:focus{border-color:palette(highlight);}"
        "QDoubleSpinBox:disabled{color:palette(placeholder-text);}"
        "QComboBox QAbstractItemView{border:1px solid palette(mid);background:palette(base);outline:0;"
        "selection-background-color:palette(highlight);selection-color:palette(highlighted-text);}"
        "QScrollBar:vertical{background:transparent;width:10px;margin:0;}"
        "QScrollBar::handle:vertical{background:palette(mid);border-radius:5px;min-height:30px;}"
        "QScrollBar::handle:vertical:hover{background:palette(dark);}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}"
        "QListWidget{border:none;background:transparent;outline:0;font-size:13px;}"
        "QListWidget::item{padding:5px 10px;border-left:2px solid transparent;}"
        "QListWidget::item:hover{background:palette(midlight);}"
        "QListWidget::item:selected{background:transparent;color:palette(text);font-weight:bold;"
        "border-left:2px solid palette(highlight);}"
        "QScrollArea{background:transparent;border:none;}"
        "QScrollArea > QWidget > QWidget{background:transparent;}");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(14);

    search_ = new QLineEdit;
    search_->setPlaceholderText("Search settings");
    search_->setClearButtonEnabled(true);
    root->addWidget(search_);

    auto* body = new QHBoxLayout;
    body->setSpacing(24);
    categories_ = new QListWidget;
    categories_->setFixedWidth(170);
    body->addWidget(categories_);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* page = new QWidget;
    itemsLayout_ = new QVBoxLayout(page);
    itemsLayout_->setContentsMargins(0, 0, 14, 0);
    itemsLayout_->setSpacing(26);
    heading_ = new QLabel;
    heading_->setStyleSheet("QLabel{font-size:24px;font-weight:bold;}");
    itemsLayout_->addWidget(heading_);
    scroll->setWidget(page);
    body->addWidget(scroll, 1);
    root->addLayout(body, 1);

    const int supportCat = addCategory("Support");  // first category = the page Settings opens on
    const int controlsCat = addCategory("Controls");
    const int shortcutsCat = addCategory("Shortcuts");
    const int meshCat = addCategory("Mesh quality");
    const int plateCat = addCategory("Print plate");
    buildSupportItems(supportCat);
    buildControlsItems(controlsCat);
    buildShortcutItems(shortcutsCat);
    buildMeshItems(meshCat, meshQuality);
    buildPlateItems(plateCat, plate);

    empty_ = new QLabel("No settings found.");
    empty_->setStyleSheet("QLabel{color:palette(placeholder-text);}");
    itemsLayout_->addWidget(empty_);
    itemsLayout_->addStretch(1);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    // Footer: app name and version on the left, Save / Cancel on the right.
#ifndef CADSCOUT_VERSION
#define CADSCOUT_VERSION "dev"
#endif
#ifdef CADSCOUT_LITE
    auto* version = new QLabel(QString("CADscout Lite %1").arg(CADSCOUT_VERSION));
#else
    auto* version = new QLabel(QString("CADscout Full %1").arg(CADSCOUT_VERSION));
#endif
    version->setStyleSheet("QLabel{color:palette(placeholder-text);}");
    auto* footer = new QHBoxLayout;
    footer->addWidget(version);
    footer->addStretch(1);
    footer->addWidget(box);
    root->addLayout(footer);

    connect(categories_, &QListWidget::currentRowChanged, this, [this] { refresh(); });
    connect(search_, &QLineEdit::textChanged, this, [this] { refresh(); });
    categories_->setCurrentRow(0);
    apply(initial);
    refresh();
}

int SettingsDialog::addCategory(const QString& name) {
    categoryNames_ << name;
    categories_->addItem(name);
    return categoryNames_.size() - 1;
}

// One setting: dim "Category:" prefix with a bold name, a dim description, then the control.
void SettingsDialog::addItem(int category, const QString& title, const QString& description, QWidget* control) {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    QColor dimColor = palette().color(QPalette::WindowText);
    dimColor.setAlpha(140);
    const QString dim = QString("rgba(%1,%2,%3,%4)")
                            .arg(dimColor.red()).arg(dimColor.green()).arg(dimColor.blue()).arg(dimColor.alpha());
    auto* t = new QLabel(QString("<span style=\"color:%1\">%2: </span><b>%3</b>")
                             .arg(dim, categoryNames_[category].toHtmlEscaped(), title.toHtmlEscaped()));
    v->addWidget(t);
    if (!description.isEmpty()) {
        auto* d = new QLabel(description);
        d->setWordWrap(true);
        d->setStyleSheet("QLabel{color:palette(placeholder-text);}");
        v->addWidget(d);
    }
    v->addSpacing(4);
    v->addWidget(control, 0, Qt::AlignLeft);

    itemsLayout_->addWidget(w);
    items_.push_back({w, category, (categoryNames_[category] + " " + title + " " + description).toLower()});
}

void SettingsDialog::addWide(int category, const QString& description, QWidget* control, const QString& keywords,
                             QTableWidget* table) {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);
    if (!description.isEmpty()) {
        auto* d = new QLabel(description);
        d->setWordWrap(true);
        d->setStyleSheet("QLabel{color:palette(placeholder-text);}");
        v->addWidget(d);
    }
    v->addWidget(control);
    itemsLayout_->addWidget(w);
    items_.push_back({w, category, (categoryNames_[category] + " " + keywords).toLower(), table});
}

void SettingsDialog::buildSupportItems(int cat) {
    addWide(cat,
            "CADscout is free software. If it is useful to you, you can support its development with a donation.\n\n"
            "You pay on the donation website, in your browser. CADscout never sees your payment details, and "
            "nothing is unlocked in return.",
            new SupportButtons, "support donate donation coffee buy me a coffee ko-fi kofi tip thanks free");
}

void SettingsDialog::buildControlsItems(int cat) {
    auto* presets = new QWidget;
    auto* row = new QHBoxLayout(presets);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    auto* cad = new QPushButton("CAD (middle = orbit)");
    auto* classic = new QPushButton("Classic (left = orbit)");
    row->addWidget(cad);
    row->addWidget(classic);
    connect(cad, &QPushButton::clicked, this, [this] { apply(Controls::cad()); });
    connect(classic, &QPushButton::clicked, this, [this] { apply(Controls::classic()); });
    addItem(cat, "Preset", "Start from a ready-made mouse layout, then change single bindings in the table below.", presets);

    // The bindings are kept in these (hidden) boxes; the table shows them and the pencil edits them.
    for (int i = 0; i < 3; ++i) {
        button_[i] = new QComboBox(this);
        button_[i]->addItems({"Left", "Middle", "Right"});
        button_[i]->hide();
        modifier_[i] = new QComboBox(this);
        modifier_[i]->addItems({"None", "Shift", "Ctrl", "Alt"});
        modifier_[i]->hide();
    }

    auto* table = new BindingTable;
    controlsTable_ = table;
    const char* names[3] = {"Orbit", "Pan", "Zoom (drag)"};
    for (int i = 0; i < 3; ++i) table->addRow(names[i], {}, "3D view", "Default", true);
    table->addRow("Zoom (wheel)", {"Mouse Wheel"}, "3D view", "Built-in", false);
    table->addRow("Select part / plate", {"Left Button"}, "3D view", "Built-in", false);
    table->onEdit = [this](int r) {
        if (r < 3) editBinding(r);
    };
    addWide(cat, "Mouse controls of the 3D view. Click the pencil to change a binding.", table,
            "orbit pan zoom wheel mouse select part plate bindings", table);

    invert_ = new FlatCheckBox("Reverse the direction of mouse wheel zoom");
    addItem(cat, "Invert wheel", QString(), invert_);
}

void SettingsDialog::buildShortcutItems(int cat) {
    auto* table = new BindingTable;
    table->addRow("Open model", {"Ctrl", "O"}, "Anywhere", "Built-in", false);
    table->addRow("Fit to view", {"F"}, "Anywhere", "Built-in", false);
#ifndef CADSCOUT_LITE
    table->addRow("Show / hide edges", {"E"}, "Anywhere", "Built-in", false);
    table->addRow("Home view", {"H"}, "Anywhere", "Built-in", false);
    table->addRow("Show / hide assembly tree", {"T"}, "Anywhere", "Built-in", false);
#endif
    table->addRow("About CADscout", {"F1"}, "Anywhere", "Built-in", false);
    table->addRow("Close the info popup", {"Esc"}, "Anywhere", "Built-in", false);
    addWide(cat, "Keyboard shortcuts of the viewer. They are fixed and cannot be changed.", table,
            "keyboard keys shortcuts open fit edges home tree popup", table);
}

// Shows the current bindings (modifier + mouse button) in the table; marks rows that differ from the CAD defaults.
void SettingsDialog::refreshControlsTable() {
    if (!controlsTable_) return;
    auto* table = static_cast<BindingTable*>(controlsTable_);
    static const char* buttonNames[3] = {"Left Button", "Middle Button", "Right Button"};
    static const char* modifierNames[4] = {"", "Shift", "Ctrl", "Alt"};
    const Controls def = Controls::cad();
    const Binding* defs[3] = {&def.orbit, &def.pan, &def.zoom};
    for (int i = 0; i < 3; ++i) {
        const int b = button_[i]->currentIndex(), m = modifier_[i]->currentIndex();
        QStringList keys;
        if (m > 0) keys << modifierNames[m];
        keys << buttonNames[b];
        table->setKeys(i, keys);
        const bool isDefault = kButtons[b] == defs[i]->button && kMods[m] == defs[i]->modifier;
        table->item(i, 4)->setText(isDefault ? "Default" : "Custom");
    }
}

void SettingsDialog::editBinding(int i) {
    static const char* names[3] = {"Orbit", "Pan", "Zoom (drag)"};
    QDialog dlg(this);
    dlg.setWindowTitle(QString("Change binding: %1").arg(names[i]));
    auto* v = new QVBoxLayout(&dlg);
    v->setContentsMargins(18, 16, 18, 16);
    v->setSpacing(14);
    auto* form = new QFormLayout;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    auto* b = new QComboBox;
    b->addItems({"Left", "Middle", "Right"});
    b->setCurrentIndex(button_[i]->currentIndex());
    b->setFixedWidth(200);
    auto* m = new QComboBox;
    m->addItems({"None", "Shift", "Ctrl", "Alt"});
    m->setCurrentIndex(modifier_[i]->currentIndex());
    m->setFixedWidth(200);
    form->addRow("Mouse button", b);
    form->addRow("Modifier key", m);
    v->addLayout(form);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(box);
    if (dlg.exec() == QDialog::Accepted) {
        button_[i]->setCurrentIndex(b->currentIndex());
        modifier_[i]->setCurrentIndex(m->currentIndex());
        refreshControlsTable();
    }
}

void SettingsDialog::buildMeshItems(int cat, int quality) {
    quality_ = new QComboBox;
    quality_->addItems({"Fast", "Balanced", "High quality"});
    quality_->setFixedWidth(220);
    quality_->setCurrentIndex(std::clamp(quality, 0, 2));
    addItem(cat, "Quality",
            "Fast: coarser curves, lowest memory and quickest loading. Balanced: good looking curves at a reasonable "
            "load time. High quality: very smooth curves, slower loading and more memory. "
            "Applies to files opened after saving.",
            quality_);
}

void SettingsDialog::buildPlateItems(int cat, const PlateSettings& plate) {
    plateMode_ = new QComboBox;
    plateMode_->addItems({"Automatic (from the file)", "Custom size", "Hidden"});
    plateMode_->setFixedWidth(220);
    plateMode_->setCurrentIndex(int(plate.mode));
    addItem(cat, "Plate",
            "Automatic: Bambu Studio and Orca Slicer files show the printer bed stored in the file, one per plate; "
            "other files show no plate. Custom size: always show one plate of the size below, centred under the "
            "model. Hidden: never show a plate.",
            plateMode_);

    auto* size = new QWidget;
    auto* row = new QHBoxLayout(size);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    auto makeSpin = [](double value) {
        auto* spin = new QDoubleSpinBox;
        spin->setRange(50.0, 2000.0);
        spin->setDecimals(0);
        spin->setSuffix(" mm");
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setFixedWidth(100);
        spin->setValue(value);
        return spin;
    };
    plateWidth_ = makeSpin(plate.width);
    plateDepth_ = makeSpin(plate.depth);
    row->addWidget(new QLabel("Width"));
    row->addWidget(plateWidth_);
    row->addSpacing(10);
    row->addWidget(new QLabel("Depth"));
    row->addWidget(plateDepth_);
    addItem(cat, "Custom size", "Plate width (X) and depth (Y) in millimetres. Used when Plate is set to Custom size.", size);

    auto updateEnabled = [this] {
        const bool custom = plateMode_->currentIndex() == int(PlateSettings::Mode::Custom);
        plateWidth_->setEnabled(custom);
        plateDepth_->setEnabled(custom);
    };
    connect(plateMode_, &QComboBox::currentIndexChanged, this, updateEnabled);
    updateEnabled();
}

void SettingsDialog::refresh() {
    const QStringList words = search_->text().trimmed().toLower().split(' ', Qt::SkipEmptyParts);
    const int cat = categories_->currentRow();
    int shown = 0;
    for (const Item& it : items_) {
        bool visible = words.isEmpty() ? it.category == cat : true;
        for (const QString& w : words) visible = visible && it.keywords.contains(w);
        if (it.table) {  // binding tables: a search also narrows the rows
            auto* table = static_cast<BindingTable*>(it.table);
            if (words.isEmpty()) {
                table->filter({});
            } else {
                const int rows = table->filter(words);
                if (rows == 0 && visible) table->filter({});  // words only matched the page title: keep every row
                visible = visible || rows > 0;
            }
        }
        it.widget->setVisible(visible);
        shown += visible ? 1 : 0;
    }
    heading_->setText(words.isEmpty() ? categoryNames_.value(cat) : QString("Search results"));
    empty_->setVisible(shown == 0);
}

int SettingsDialog::meshQuality() const { return quality_->currentIndex(); }

PlateSettings SettingsDialog::plateSettings() const {
    PlateSettings p;
    const int m = plateMode_->currentIndex();
    p.mode = m == 1 ? PlateSettings::Mode::Custom : m == 2 ? PlateSettings::Mode::Hidden : PlateSettings::Mode::Auto;
    p.width = float(plateWidth_->value());
    p.depth = float(plateDepth_->value());
    return p;
}

void SettingsDialog::apply(const Controls& c) {
    const Binding* all[3] = {&c.orbit, &c.pan, &c.zoom};
    for (int i = 0; i < 3; ++i) {
        for (int k = 0; k < 3; ++k)
            if (kButtons[k] == all[i]->button) button_[i]->setCurrentIndex(k);
        for (int k = 0; k < 4; ++k)
            if (kMods[k] == all[i]->modifier) modifier_[i]->setCurrentIndex(k);
    }
    invert_->setChecked(c.invertWheel);
    refreshControlsTable();
}

Controls SettingsDialog::controls() const {
    Controls c;
    Binding* all[3] = {&c.orbit, &c.pan, &c.zoom};
    for (int i = 0; i < 3; ++i) {
        all[i]->button = kButtons[button_[i]->currentIndex()];
        all[i]->modifier = kMods[modifier_[i]->currentIndex()];
    }
    c.invertWheel = invert_->isChecked();
    return c;
}

void SettingsDialog::accept() {
    const Controls c = controls();
    const Binding* all[3] = {&c.orbit, &c.pan, &c.zoom};
    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 3; ++j)
            if (all[i]->button == all[j]->button && all[i]->modifier == all[j]->modifier) {
                QMessageBox::warning(this, "Conflict", "Two actions use the same button and modifier.");
                return;
            }
    QDialog::accept();
}
