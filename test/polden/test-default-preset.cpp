#include "../../frontend/utility/PoldenDefaults.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

void require(bool value, const char *message)
{
	if (!value) {
		throw std::runtime_error(message);
	}
}

QByteArray read(const QString &path)
{
	QFile file(path);
	require(file.open(QIODevice::ReadOnly), "Open preset file");
	return file.readAll();
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	try {
		require(app.arguments().size() == 2, "Pass preset resource directory");
		const QString resources = app.arguments()[1];
		QTemporaryDir temporary(QDir::current().filePath(QStringLiteral("polden-defaults-XXXXXX")));
		require(temporary.isValid(), "Temporary directory");
		QDir root(temporary.path());
		const QString profiles = root.filePath("profiles");
		const QString scenes = root.filePath("scenes");
		require(PoldenDefaults::install(resources, profiles, scenes), "Preset installs into empty config");
		const QString ini = QDir(profiles).filePath("PoldenGameplay/basic.ini");
		require(read(ini) == read(QDir(resources).filePath("basic.ini")), "Profile copied verbatim");
		const QByteArray settings = read(ini);
		for (const QByteArray key : {"BaseCX=1920", "BaseCY=1080", "OutputCX=1920", "OutputCY=1080",
					     "FPSInt=60", "RecTracks=1", "RecFormat2=fragmented_mp4"}) {
			require(settings.contains(key), "Full HD 60 profile settings");
		}
		require(!settings.contains("RecFilePath="), "Recording path uses current user default");
		const QJsonObject scene =
			QJsonDocument::fromJson(read(QDir(scenes).filePath("PoldenGameplay.json"))).object();
		require(scene["resolution"].toObject()["x"].toInt() == 1920, "Scene is Full HD");
		require(scene["sources"].toArray().size() == 2, "Exactly one game source and one scene");
		const auto game = scene["sources"].toArray()[0].toObject();
		require(game["settings"].toObject()["capture_audio"].toBool(), "Game sound enabled");
		require(game["settings"].toObject()["window"].toString().isEmpty(), "No personal game window");
		require(game["mixers"].toInt() == 1, "Game uses only first audio track");
		require(scene["DesktopAudioDevice1"].isNull() && scene["AuxAudioDevice1"].isNull(),
			"Global audio disabled");
		require(!PoldenDefaults::install(resources, profiles, scenes), "Second launch preserves settings");
		require(read(ini) == settings, "Existing profile unchanged");
		const QString existingProfiles = root.filePath("existing-profiles");
		require(QDir().mkpath(existingProfiles + "/OtherProfile"), "Create existing profile");
		require(!PoldenDefaults::install(resources, existingProfiles, root.filePath("new-scenes")),
			"Existing profiles prevent installation");
		require(!QFile::exists(root.filePath("new-scenes/PoldenGameplay.json")), "No partial new scene");
		const QString existingScenes = root.filePath("existing-scenes");
		require(QDir().mkpath(existingScenes), "Create scene folder");
		QFile existing(existingScenes + "/Other.json");
		require(existing.open(QIODevice::WriteOnly), "Create existing scene");
		existing.write("{}");
		existing.close();
		require(!PoldenDefaults::install(resources, root.filePath("new-profiles"), existingScenes),
			"Existing scene prevents installation");
		require(!PoldenDefaults::install(root.filePath("missing-resources"), root.filePath("missing-profiles"),
						 root.filePath("missing-scenes")),
			"Missing resources leave configuration untouched");
		std::cout
			<< "PASS: Full HD preset, game audio, clean paths, fresh install, existing config preservation\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
