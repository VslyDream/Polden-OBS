#pragma once

#include <QString>

#include <atomic>
#include <memory>

namespace PoldenFileTransfer {
using Cancellation = std::shared_ptr<std::atomic_bool>;

QString copyVerified(const QString &source, const QString &target, const QString &jobId,
		     const Cancellation &cancel = {});
QString copyRecordingSidecars(const QString &originalVideo, const QString &targetVideo, const QString &jobId,
			      const Cancellation &cancel = {});
QString copyVideoAndMarkers(const QString &sourceVideo, const QString &targetVideo, const QString &originalVideo,
			    const QString &jobId, const Cancellation &cancel);
} // namespace PoldenFileTransfer
