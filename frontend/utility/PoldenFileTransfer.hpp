#pragma once

#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>
#include <functional>

namespace PoldenFileTransfer {
using Cancellation = std::shared_ptr<std::atomic_bool>;
using Progress = std::function<void(qint64, qint64)>;

QString copyVerified(const QString &source, const QString &target, const QString &jobId,
		     const Cancellation &cancel = {}, const Progress &progress = {});
QString copyRecordingSidecars(const QString &originalVideo, const QString &targetVideo, const QString &jobId,
			      const Cancellation &cancel = {}, QStringList *delivered = nullptr);
QString copyVideoAndMarkers(const QString &sourceVideo, const QString &targetVideo, const QString &originalVideo,
			    const QString &jobId, const Cancellation &cancel, const Progress &progress = {},
			    QStringList *delivered = nullptr);
} // namespace PoldenFileTransfer
