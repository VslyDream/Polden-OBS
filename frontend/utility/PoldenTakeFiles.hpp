#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace PoldenTakeFiles {
inline QStringList paths(const QString &video)
{
	if (video.isEmpty()) {
		return {};
	}
	const QFileInfo file(video);
	const QString stem = file.dir().filePath(file.completeBaseName());
	return {video, stem + QStringLiteral(".txt"), stem + QStringLiteral(".csv")};
}

inline QString key(const QString &path)
{
	const QFileInfo info(path);
	const QString canonical = info.canonicalFilePath();
	QString result = QDir::fromNativeSeparators(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
#ifdef _WIN32
	result = result.toCaseFolded();
#endif
	return QDir::cleanPath(result);
}

struct Plan {
	QStringList ids;
	QStringList local;
	QStringList remote;
};

// Job metadata, rather than directory scanning, determines the deletion boundary.
template<typename Jobs> Plan plan(const Jobs &jobs, const QString &project, const QString &take)
{
	Plan result;
	QStringList protectedPaths;
	for (const auto &job : jobs) {
		if (job.projectId == project && job.takeId == take) {
			result.ids.append(job.id);
			result.local.append(paths(job.sourcePath));
			result.local.append(paths(job.localPath));
			for (const QString &path : paths(job.destinationPath)) {
				if (job.copied || job.deliveredFiles.contains(path) || !QFile::exists(path)) {
					result.remote.append(path);
				}
			}
		} else {
			for (const QString &video : {job.sourcePath, job.localPath, job.destinationPath}) {
				for (const QString &path : paths(video)) {
					protectedPaths.append(key(path));
				}
			}
		}
	}
	result.local.removeDuplicates();
	result.remote.removeDuplicates();
	result.local.removeIf([&](const QString &path) { return protectedPaths.contains(key(path)); });
	result.remote.removeIf([&](const QString &path) {
		if (protectedPaths.contains(key(path))) {
			return true;
		}
		for (const QString &source : result.local) {
			if (key(source) == key(path)) {
				return true;
			}
		}
		return false;
	});
	return result;
}
} // namespace PoldenTakeFiles
