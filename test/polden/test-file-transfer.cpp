#include "../../frontend/utility/PoldenFileTransfer.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace PoldenFileTransfer;

void require(bool condition, const char *message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void write(const QString &path, const QByteArray &data)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		throw std::runtime_error((path + ": " + file.errorString()).toStdString());
	}
	require(file.write(data) == data.size(), "Write fixture");
}

QByteArray read(const QString &path)
{
	QFile file(path);
	require(file.open(QIODevice::ReadOnly), "Read fixture");
	return file.readAll();
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	try {
		QTemporaryDir directory(QDir::current().filePath(QStringLiteral("polden-transfer-XXXXXX")));
		require(directory.isValid(), "Temporary directory");
		QDir root(directory.path());
		const QString source = root.filePath(QStringLiteral("Исходное 2026-11-02-18-28-52.mkv"));
		const QString target = root.filePath(QStringLiteral("Lucid/Исходное 2026-11-02-18-28-52.mp4"));
		write(source, "video bytes");
		qint64 previousBytes = -1;
		int progressEvents = 0;
		require(copyVerified(source, target, "test", {},
				     [&](qint64 bytes, qint64 total) {
					     require(bytes >= previousBytes && bytes <= total && total == 11,
						     "Monotonic byte progress");
					     previousBytes = bytes;
					     ++progressEvents;
				     })
				.isEmpty(),
			"Copy succeeded");
		require(progressEvents >= 2 && previousBytes == 11, "Copy reports start and end bytes");
		require(read(source) == read(target), "Copied bytes match");
		require(copyVerified(source, target, "retry").isEmpty(), "Identical delivery is idempotent");
		write(source, "different video");
		require(!copyVerified(source, target, "conflict").isEmpty(), "Different target is rejected");
		require(read(target) == "video bytes", "Existing target is preserved");
		const QString originalTxt = source.left(source.size() - 4) + ".txt";
		const QString targetTxt = target.left(target.size() - 4) + ".txt";
		write(originalTxt, "0:08:08,1\r\n0:11:54,8\r\n");
		require(copyRecordingSidecars(source, target, "txt").isEmpty(), "TXT delivered");
		require(read(originalTxt) == read(targetTxt), "TXT format preserved");
		write(source.left(source.size() - 4) + ".csv", "00:00:12.350,0\n");
		require(copyRecordingSidecars(source, target, "legacy").isEmpty(), "Legacy CSV delivered");
		require(read(target.left(target.size() - 4) + ".csv") == "00:00:12.350,0\n", "CSV preserved");
		const QString ownedTarget = root.filePath("Owned/video.mp4");
		QStringList delivered;
		require(copyVideoAndMarkers(source, ownedTarget, source, "owned", {}, {}, &delivered).isEmpty(),
			"Track delivered files");
		require(delivered.size() == 3 && delivered.contains(ownedTarget),
			"Delivery tracks video and both sidecars");
		const QString partialTarget = root.filePath("Partial/video.mp4");
		require(QDir().mkpath(root.filePath("Partial")), "Create partial delivery folder");
		write(root.filePath("Partial/video.csv"), "belongs to another recording");
		delivered.clear();
		require(!copyVideoAndMarkers(source, partialTarget, source, "partial", {}, {}, &delivered).isEmpty(),
			"Conflicting sidecar rejects the delivery");
		require(delivered == QStringList{partialTarget},
			"Committed video remains tracked after sidecar failure");
		require(read(root.filePath("Partial/video.csv")) == "belongs to another recording",
			"Conflicting sidecar remains untouched and unowned");

		auto cancel = std::make_shared<std::atomic_bool>(true);
		const QString cancelledTarget = root.filePath("cancelled.mp4");
		require(!copyVerified(source, cancelledTarget, "cancelled", cancel).isEmpty(), "Early cancel reported");
		require(!QFile::exists(cancelledTarget), "Cancelled file is not delivered");

		const QString largeSource = root.filePath("large.mkv");
		const QString largeTarget = root.filePath("large.mp4");
		{
			QFile file(largeSource);
			require(file.open(QIODevice::WriteOnly), "Open large fixture");
			const QByteArray block(4 * 1024 * 1024, 'x');
			for (int index = 0; index < 64; ++index) {
				require(file.write(block) == block.size(), "Write large fixture");
			}
		}
		cancel->store(false);
		auto result = std::async(std::launch::async,
					 [&]() { return copyVerified(largeSource, largeTarget, "active", cancel); });
		const QString partial = largeTarget + ".polden-part-active";
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
		while (!QFile::exists(partial) && std::chrono::steady_clock::now() < deadline &&
		       result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		const bool copying = QFile::exists(partial);
		cancel->store(true);
		const QString error = result.get();
		require(copying, "Cancellation reached an active chunked copy");
		require(!error.isEmpty(), "Active cancellation reported");
		require(!QFile::exists(partial) && !QFile::exists(largeTarget), "Partial copy cleaned up");
		require(QFile::exists(largeSource), "Original recording preserved");
		cancel->store(false);
		require(copyVerified(largeSource, largeTarget, "resume", cancel).isEmpty(),
			"Cancelled copy can resume");
		std::cout << "PASS: verified copy, conflicts, TXT/CSV, active cancellation, cleanup, resume\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
