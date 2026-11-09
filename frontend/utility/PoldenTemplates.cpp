#include "PoldenTemplates.hpp"

#include <QDir>
#include <QFileInfo>

namespace PoldenTemplates {
namespace {
QString filenameText(QString text)
{
	// Literal user/game names must not introduce path separators or OBS formatting tokens.
	for (qsizetype i = 0; i < text.size(); ++i) {
		const QChar ch = text[i];
		if (ch.unicode() < 32 || QStringLiteral("<>:\"/\\|?*%{}").contains(ch)) {
			text[i] = QLatin1Char('_');
		}
	}
	return text;
}
} // namespace

QString toObs(QString text, const QString &name, const QString &game)
{
	text.replace(QStringLiteral("{date}"), QStringLiteral("%CCYY-%MM-%DD"));
	text.replace(QStringLiteral("{time}"), QStringLiteral("%hh-%mm-%ss"));
	text.replace(QStringLiteral("{name}"), filenameText(name));
	text.replace(QStringLiteral("{game}"), filenameText(game));
	return text;
}

QString fromObs(QString text)
{
	// Do not guess which literal parts are a name or game in an existing OBS profile.
	text.replace(QStringLiteral("%CCYY-%MM-%DD"), QStringLiteral("{date}"));
	text.replace(QStringLiteral("%hh-%mm-%ss"), QStringLiteral("{time}"));
	return text;
}

QString folder(QString text, const QString &root, const QString &video, const QString &name, const QDateTime &time)
{
	text.replace(QStringLiteral("{video folder}"), QStringLiteral("{footage}"));
	text.replace(QStringLiteral("{game folder}"), QStringLiteral("{root}"));
	text.replace(QStringLiteral("{name}"), QStringLiteral("{user}"));
	text.replace(QStringLiteral("{date}"), time.toString(QStringLiteral("yyyy-MM-dd")));
	text.replace(QStringLiteral("{timestamp}"), time.toString(QStringLiteral("yyyy-MM-dd-HH-mm-ss")));
	text.replace(QStringLiteral("{root}"), root);
	text.replace(QStringLiteral("{footage}"), video);
	text.replace(QStringLiteral("{user}"), name);
	return QDir::toNativeSeparators(QDir::cleanPath(QDir::fromNativeSeparators(text)));
}

QString gameName(QString text, const QString &root)
{
	return text.replace(QStringLiteral("{folder}"), QFileInfo(QDir::cleanPath(root)).fileName());
}

bool validVideoSubfolder(const QString &text)
{
	const QString path = QDir::cleanPath(QDir::fromNativeSeparators(text.trimmed()));
	return !path.isEmpty() && path != QStringLiteral(".") && path != QStringLiteral("..") &&
	       !path.startsWith(QStringLiteral("../")) && !QDir::isAbsolutePath(path) &&
	       !path.contains(QLatin1Char(':'));
}
} // namespace PoldenTemplates
