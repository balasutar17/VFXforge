// VFX Forge app: where the artist's own library of effects is kept.
#pragma once

#include <filesystem>

#include <QDir>
#include <QStandardPaths>
#include <QString>

// A "Library" folder in the app's own data folder, one folder per effect.
// The VFX_USER_LIBRARY environment variable names another place (tests).
inline std::filesystem::path userLibraryRoot() {
    const QByteArray chosen = qgetenv("VFX_USER_LIBRARY");
    const QString base = chosen.isEmpty()
                             ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Library")
                             : QString::fromLocal8Bit(chosen);
    return std::filesystem::path(QDir::toNativeSeparators(base).toStdWString());
}
