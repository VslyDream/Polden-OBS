#pragma once

#include <QDateTime>
#include <QString>

namespace PoldenTemplates {
QString toObs(QString text, const QString &name, const QString &game);
QString fromObs(QString text);
QString folder(QString text, const QString &root, const QString &video, const QString &name, const QDateTime &time);
QString gameName(QString text, const QString &root);
bool validVideoSubfolder(const QString &text);
} // namespace PoldenTemplates
