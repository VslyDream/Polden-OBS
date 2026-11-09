#include "PoldenDefaults.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <array>

namespace PoldenDefaults {
bool install(const QString &resources, const QString &profiles, const QString &scenes)
{
	const auto entries = QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot;
	if (!QDir(profiles).entryList(entries).isEmpty() || !QDir(scenes).entryList(entries).isEmpty()) {
		return false;
	}
	const QString profile = QDir(profiles).filePath(QStringLiteral("PoldenGameplay"));
	const std::array<QString, 3> names = {QStringLiteral("basic.ini"), QStringLiteral("recordEncoder.json"),
					      QStringLiteral("PoldenGameplay.json")};
	const std::array<QString, 3> targets = {QDir(profile).filePath(names[0]), QDir(profile).filePath(names[1]),
						QDir(scenes).filePath(names[2])};
	for (const QString &name : names) {
		if (!QFileInfo(QDir(resources).filePath(name)).isFile()) {
			return false;
		}
	}
	if (!QDir().mkpath(profile) || !QDir().mkpath(scenes)) {
		QDir().rmdir(profile);
		return false;
	}
	QStringList copied;
	for (size_t index = 0; index < names.size(); ++index) {
		if (!QFile::copy(QDir(resources).filePath(names[index]), targets[index])) {
			for (const QString &path : copied) {
				QFile::remove(path);
			}
			QDir().rmdir(profile);
			return false;
		}
		copied.append(targets[index]);
	}
	return true;
}
} // namespace PoldenDefaults
