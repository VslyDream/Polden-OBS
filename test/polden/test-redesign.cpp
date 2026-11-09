#include "../../frontend/utility/PoldenTemplates.hpp"
#include "../../frontend/utility/PoldenTakeFiles.hpp"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QVector>

#include <iostream>
#include <stdexcept>

void require(bool value, const char *message)
{
	if (!value) {
		throw std::runtime_error(message);
	}
}

struct Job {
	QString id, projectId, takeId, sourcePath, localPath, destinationPath;
	bool copied = false;
	QStringList deliveredFiles;
};

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	try {
		using namespace PoldenTemplates;
		const QString format =
			toObs("{name}_{date}-{time}_{game}", QString::fromUtf8("Василий"), "Iron Frontier");
		require(format == QString::fromUtf8("Василий_%CCYY-%MM-%DD-%hh-%mm-%ss_Iron Frontier"),
			"Expand all filename tokens");
		require(fromObs("literal_%CCYY-%MM-%DD-%hh-%mm-%ss_%fps") == "literal_{date}-{time}_%fps",
			"Preserve literal names and other OBS tokens");
		require(toObs("{name}_{game}", "../Name%hh", "Game:One\\Two") == ".._Name_hh_Game_One_Two",
			"Names cannot inject a path or OBS token");
		const QDateTime time(QDate(2026, 11, 9), QTime(14, 5, 6));
		require(folder("{video folder}/{date}/{name}", "L:/Game", "L:/Game/Video", "Operator", time) ==
				folder("{footage}/{date}/{user}", "L:/Game", "L:/Game/Video", "Operator", time),
			"Legacy folder tokens remain compatible");
		require(folder("{game folder}/{timestamp}", "L:/Game", "", "", time) ==
				QDir::toNativeSeparators("L:/Game/2026-11-09-14-05-06"),
			"Root and timestamp aliases");
		require(gameName("Game {folder}", "L:/Iron Frontier/") == "Game Iron Frontier", "Game name from root");
		require(validVideoSubfolder("02_Assets/01_VIDEO") && validVideoSubfolder("Video"),
			"Accept relative layouts");
		require(!validVideoSubfolder("../Video") && !validVideoSubfolder("L:/Video") &&
				!validVideoSubfolder("/Video") && !validVideoSubfolder(""),
			"Reject escaping layouts");

		QTemporaryDir temp(QDir::current().filePath("polden-takes-XXXXXX"));
		require(temp.isValid(), "Temporary fixture");
		const QDir root(temp.path());
		QVector<Job> jobs;
		jobs.append({"old",
			     "game",
			     "old-take",
			     root.filePath("old.mkv"),
			     root.filePath("old.mp4"),
			     root.filePath("Lucid/old.mp4"),
			     true,
			     {}});
		jobs.append({"first",
			     "game",
			     "new-take",
			     root.filePath("one.mkv"),
			     root.filePath("one.mp4"),
			     root.filePath("Lucid/one.mp4"),
			     true,
			     {}});
		jobs.append({"second",
			     "game",
			     "new-take",
			     root.filePath("two.mkv"),
			     root.filePath("two.mp4"),
			     root.filePath("Lucid/two.mp4"),
			     false,
			     {}});
		jobs.append({"other-game",
			     "other",
			     "new-take",
			     root.filePath("other.mkv"),
			     root.filePath("other.mp4"),
			     root.filePath("Lucid/other.mp4"),
			     true,
			     {}});
		auto plan = PoldenTakeFiles::plan(jobs, "game", "new-take");
		require(plan.ids == QStringList({"first", "second"}), "Delete all segments in exactly one game's take");
		require(plan.local.contains(root.filePath("one.mkv")) && plan.local.contains(root.filePath("two.txt")),
			"Include originals, converted files and marker sidecars");
		require(!plan.local.contains(root.filePath("old.mkv")) &&
				!plan.remote.contains(root.filePath("Lucid/other.mp4")),
			"Preserve previous takes and other games");
		require(QDir().mkpath(root.filePath("Lucid")), "Create delivery fixture");
		QFile conflict(root.filePath("Lucid/two.mp4"));
		require(conflict.open(QIODevice::WriteOnly), "Create conflicting destination");
		conflict.write("belongs to somebody else");
		conflict.close();
		plan = PoldenTakeFiles::plan(jobs, "game", "new-take");
		require(!plan.remote.contains(conflict.fileName()),
			"Never delete an unowned destination after a conflict");
		jobs[2].deliveredFiles.append(conflict.fileName());
		plan = PoldenTakeFiles::plan(jobs, "game", "new-take");
		require(plan.remote.contains(conflict.fileName()),
			"Include a committed video when sidecar delivery failed");
		jobs[3].destinationPath = jobs[1].destinationPath;
		jobs[3].sourcePath = jobs[1].sourcePath;
		plan = PoldenTakeFiles::plan(jobs, "game", "new-take");
		require(!plan.remote.contains(jobs[1].destinationPath) && !plan.local.contains(jobs[1].sourcePath),
			"Shared paths referenced by other jobs are protected");
		jobs[2].destinationPath = jobs[2].sourcePath;
		plan = PoldenTakeFiles::plan(jobs, "game", "new-take");
		require(!plan.remote.contains(jobs[2].sourcePath),
			"Local original is never permanently deleted through a delivery alias");
		std::cout
			<< "PASS: filename and folder tokens, layouts, segmented take manifest, collision and shared-file protection\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
