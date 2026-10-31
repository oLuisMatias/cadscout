#pragma once
#include <QString>
#include <memory>
#include "StepLoader.h"

// Remembers big models that took a while to load, so opening the same file again is quick.
//
// The cache holds the finished result (triangles, edge lines, part tree), not a copy of the model file. An entry is only used
// while the file is unchanged (same path, size and modification time), the quality setting is the same and the app version is
// the same. Only slow loads are stored, the total size is limited, and the entries used longest ago are deleted first.
// It lives in the app's own local data folder and never leaves the PC.
namespace ModelCache {

constexpr double kDefaultMinLoadSeconds = 3.0;  // faster loads are not worth storing (Settings > Cache can change it)

bool enabled();         // QSettings cache/enabled, on by default
int limitMegabytes();   // QSettings cache/limitMb, 2048 by default
double minLoadSeconds();  // QSettings cache/minSeconds, 3 by default: only loads at least this slow are stored
void setEnabled(bool on);
void setLimitMegabytes(int mb);
void setMinLoadSeconds(double seconds);

// Returns the stored model for this file, or nullptr when there is none (or it is outdated or damaged).
std::shared_ptr<Model> load(const QString& path, int quality);

// Stores a model that took model.loadSeconds >= minSeconds to load, then deletes old entries above the size limit.
void store(const QString& path, int quality, const Model& model, int limitMb, double minSeconds);

qint64 sizeBytes();  // current size of the cache folder
void clear();        // deletes every entry
// Shows the cache folder in Explorer. In the Store package Windows keeps the app's data in a private place, so the real
// folder is looked up first (Explorer cannot see the path the app itself uses). False when Explorer could not be started.
bool openFolder();

}  // namespace ModelCache
