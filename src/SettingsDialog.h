#pragma once
#include <QDialog>
#include <QStringList>
#include <vector>
#include "Controls.h"
#include "PlateSettings.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTableWidget;
class QVBoxLayout;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(const Controls& initial, int meshQuality, const PlateSettings& plate, QWidget* parent = nullptr);
    Controls controls() const;
    int meshQuality() const;  // 0 fast, 1 balanced, 2 high
    bool browseWithArrows() const;  // Lite only
    bool autoReload() const;  // Lite only
    bool useInches() const;   // Lite only
    int languageSetting() const;  // Lite only: 0 = like Windows, 1..5 = English, Português, Deutsch, Español, Slovenčina
    bool cacheEnabled() const;
    int cacheLimitMegabytes() const;
    double cacheMinSeconds() const;
    PlateSettings plateSettings() const;

private:
    struct Item {
        QWidget* widget;
        int category;
        QString keywords;  // lower-case text matched by the search box
        QTableWidget* table = nullptr;  // set for key/mouse binding tables: the search also filters their rows
    };

    int addCategory(const QString& name);
    void addItem(int category, const QString& title, const QString& description, QWidget* control);
    void addWide(int category, const QString& description, QWidget* control, const QString& keywords,
                 QTableWidget* table = nullptr);  // full-width item without a "Category: Title" line
    void buildSupportItems(int category);
    void buildGeneralItems(int category);  // Lite only
    void buildControlsItems(int category);
    void buildShortcutItems(int category);
    void refreshControlsTable();
    void editBinding(int index);
    void buildMeshItems(int category, int quality);
    void buildPlateItems(int category, const PlateSettings& plate);
    void buildCacheItems(int category);
    void refresh();
    void apply(const Controls& c);
    void accept() override;

    QLineEdit* search_;
    QListWidget* categories_;
    QLabel* heading_;
    QLabel* empty_;
    QVBoxLayout* itemsLayout_;
    std::vector<Item> items_;
    QStringList categoryNames_;

    QTableWidget* controlsTable_ = nullptr;
    QComboBox* button_[3];
    QComboBox* modifier_[3];
    QCheckBox* invert_;
    QCheckBox* arrows_ = nullptr;  // Lite: Left/Right arrow keys open the previous/next file
    QCheckBox* reload_ = nullptr;  // Lite: reload the file when it changes on disk
    QComboBox* units_ = nullptr;   // Lite: sizes in millimetres or inches
    QComboBox* language_ = nullptr;  // Lite: interface language
    QComboBox* quality_;
    QCheckBox* cacheOn_ = nullptr;
    QComboBox* cacheLimit_ = nullptr;
    QComboBox* cacheMin_ = nullptr;
    QComboBox* plateMode_;
    QDoubleSpinBox* plateWidth_;
    QDoubleSpinBox* plateDepth_;
};
