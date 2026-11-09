#include "PoldenFileTransfer.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace PoldenFileTransfer {
namespace {
bool cancelled(const Cancellation &cancel)
{
	return cancel && cancel->load();
}

QString cancellationError()
{
	return QStringLiteral("Processing cancelled.");
}

QByteArray fileHash(const QString &path, const Cancellation &cancel)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	QCryptographicHash hash(QCryptographicHash::Sha256);
	while (!file.atEnd()) {
		if (cancelled(cancel)) {
			return {};
		}
		QByteArray block = file.read(4 * 1024 * 1024);
		if (block.isEmpty() && file.error() != QFileDevice::NoError) {
			return {};
		}
		hash.addData(block);
	}
	return hash.result();
}
} // namespace

QString copyVerified(const QString &source, const QString &target, const QString &jobId, const Cancellation &cancel)
{
	if (cancelled(cancel)) {
		return cancellationError();
	}
	QFileInfo sourceInfo(source);
	if (!sourceInfo.isFile() || sourceInfo.size() == 0) {
		return QStringLiteral("Source file is missing or empty: %1").arg(source);
	}
	if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
		return QStringLiteral("Could not create destination folder: %1").arg(QFileInfo(target).absolutePath());
	}
	const QByteArray sourceHash = fileHash(source, cancel);
	if (cancelled(cancel)) {
		return cancellationError();
	}
	if (sourceHash.isEmpty()) {
		return QStringLiteral("Could not read source file: %1").arg(source);
	}
	if (QFile::exists(target)) {
		const QByteArray targetHash = fileHash(target, cancel);
		if (cancelled(cancel)) {
			return cancellationError();
		}
		if (targetHash == sourceHash) {
			return {};
		}
		return QStringLiteral("Target already exists with different contents: %1").arg(target);
	}
	const QString temporary = target + QStringLiteral(".polden-part-") + jobId;
	QFile input(source);
	QFile output(temporary);
	if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return QStringLiteral("Could not copy file to: %1").arg(target);
	}
	auto discard = [&]() {
		output.close();
		QFile::remove(temporary);
	};
	while (!input.atEnd()) {
		if (cancelled(cancel)) {
			discard();
			return cancellationError();
		}
		const QByteArray block = input.read(4 * 1024 * 1024);
		if ((block.isEmpty() && input.error() != QFileDevice::NoError) || output.write(block) != block.size()) {
			discard();
			return QStringLiteral("Could not copy file to: %1").arg(target);
		}
	}
	if (!output.flush()) {
		discard();
		return QStringLiteral("Could not flush copied file: %1").arg(target);
	}
	output.close();
	const QByteArray targetHash = fileHash(temporary, cancel);
	if (cancelled(cancel)) {
		QFile::remove(temporary);
		return cancellationError();
	}
	if (targetHash != sourceHash) {
		QFile::remove(temporary);
		return QStringLiteral("Copy did not pass checksum verification: %1").arg(target);
	}
	if (!QFile::rename(temporary, target)) {
		QFile::remove(temporary);
		return QStringLiteral("Could not finalize copied file: %1").arg(target);
	}
	return {};
}

QString copyRecordingSidecars(const QString &originalVideo, const QString &targetVideo, const QString &jobId,
			      const Cancellation &cancel)
{
	const QFileInfo original(originalVideo);
	const QFileInfo target(targetVideo);
	for (const QString &extension : {QStringLiteral("csv"), QStringLiteral("txt")}) {
		const QString sidecar =
			original.dir().filePath(original.completeBaseName() + QStringLiteral(".") + extension);
		if (!QFile::exists(sidecar)) {
			continue;
		}
		const QString destination =
			target.dir().filePath(target.completeBaseName() + QStringLiteral(".") + extension);
		const QString error =
			copyVerified(sidecar, destination, jobId + QStringLiteral("-") + extension, cancel);
		if (!error.isEmpty()) {
			return error;
		}
	}
	return cancelled(cancel) ? cancellationError() : QString();
}

QString copyVideoAndMarkers(const QString &sourceVideo, const QString &targetVideo, const QString &originalVideo,
			    const QString &jobId, const Cancellation &cancel)
{
	QString error = copyVerified(sourceVideo, targetVideo, jobId, cancel);
	return error.isEmpty() ? copyRecordingSidecars(originalVideo, targetVideo, jobId, cancel) : error;
}
} // namespace PoldenFileTransfer
