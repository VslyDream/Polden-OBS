#include "PoldenPanel.hpp"

#include <OBSApp.hpp>
#include <QComboBox>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <memory>

namespace {
QString expand(QString text, const QString &root, const QString &footage, const QString &user,
	       const QDateTime &time)
{
	text.replace(QStringLiteral("{root}"), root);
	text.replace(QStringLiteral("{footage}"), footage);
	text.replace(QStringLiteral("{user}"), user);
	text.replace(QStringLiteral("{date}"), time.toString(QStringLiteral("yyyy-MM-dd")));
	text.replace(QStringLiteral("{timestamp}"), time.toString(QStringLiteral("yyyy-MM-dd-HH-mm-ss")));
	return QDir::toNativeSeparators(text);
}

QByteArray fileHash(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	QCryptographicHash hash(QCryptographicHash::Sha256);
	while (!file.atEnd()) {
		QByteArray block = file.read(4 * 1024 * 1024);
		if (block.isEmpty() && file.error() != QFileDevice::NoError)
			return {};
		hash.addData(block);
	}
	return hash.result();
}

QString copyVerified(const QString &source, const QString &target, const QString &jobId)
{
	QFileInfo sourceInfo(source);
	if (!sourceInfo.isFile() || sourceInfo.size() == 0)
		return QStringLiteral("Source file is missing or empty: %1").arg(source);
	if (!QDir().mkpath(QFileInfo(target).absolutePath()))
		return QStringLiteral("Could not create LucidLink folder: %1").arg(QFileInfo(target).absolutePath());
	const QByteArray sourceHash = fileHash(source);
	if (sourceHash.isEmpty())
		return QStringLiteral("Could not read source file: %1").arg(source);
	if (QFile::exists(target)) {
		if (fileHash(target) == sourceHash)
			return {};
		return QStringLiteral("Target already exists with different contents: %1").arg(target);
	}
	QString temporary = target + QStringLiteral(".polden-part-") + jobId;
	if (QFile::exists(temporary))
		QFile::remove(temporary);
	if (!QFile::copy(source, temporary))
		return QStringLiteral("Could not copy to LucidLink: %1").arg(target);
	if (fileHash(temporary) != sourceHash) {
		QFile::remove(temporary);
		return QStringLiteral("LucidLink copy did not pass checksum verification: %1").arg(target);
	}
	if (!QFile::rename(temporary, target))
		return QStringLiteral("Could not finalize LucidLink file: %1").arg(target);
	return {};
}

QString copyVideoAndInfoWriter(const QString &sourceVideo, const QString &targetVideo, const QString &originalVideo,
			       const QString &jobId)
{
	QString error = copyVerified(sourceVideo, targetVideo, jobId);
	if (!error.isEmpty())
		return error;
	const QFileInfo original(originalVideo);
	const QFileInfo target(targetVideo);
	for (const QString &extension : {QStringLiteral("csv"), QStringLiteral("txt")}) {
		QString sidecar = original.absolutePath() + QDir::separator() + original.completeBaseName() +
				  QStringLiteral(".") + extension;
		if (!QFile::exists(sidecar))
			continue;
		QString destination = target.absolutePath() + QDir::separator() + target.completeBaseName() +
				      QStringLiteral(".") + extension;
		error = copyVerified(sidecar, destination, jobId + QStringLiteral("-") + extension);
		if (!error.isEmpty())
			return error;
	}
	return {};
}
} // namespace

QString PoldenPanel::jobsPath() const
{
	char path[1024];
	if (GetAppConfigPath(path, sizeof(path), "obs-studio/polden-jobs.json") <= 0)
		return {};
	return QString::fromUtf8(path);
}

void PoldenPanel::loadJobs()
{
	QFile file(jobsPath());
	if (!file.open(QIODevice::ReadOnly))
		return;
	QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
	for (const QJsonValue &value : array) {
		QJsonObject object = value.toObject();
		Job job;
		job.id = object.value(QStringLiteral("id")).toString();
		job.projectId = object.value(QStringLiteral("projectId")).toString();
		job.sourcePath = object.value(QStringLiteral("sourcePath")).toString();
		job.localPath = object.value(QStringLiteral("localPath")).toString();
		job.destinationPath = object.value(QStringLiteral("destinationPath")).toString();
		job.premiereProjectPath = object.value(QStringLiteral("premiereProjectPath")).toString();
		job.binPath = object.value(QStringLiteral("binPath")).toString();
		job.recordedDate = object.value(QStringLiteral("recordedDate")).toString();
		job.recordedTimestamp = object.value(QStringLiteral("recordedTimestamp")).toString();
		job.fileName = object.value(QStringLiteral("fileName")).toString();
		job.error = object.value(QStringLiteral("error")).toString();
		job.targetLevel = qBound(0, object.value(QStringLiteral("targetLevel")).toInt(), 3);
		job.converted = object.value(QStringLiteral("converted")).toBool();
		job.copied = object.value(QStringLiteral("copied")).toBool();
		job.imported = object.value(QStringLiteral("imported")).toBool();
		job.timelineRequested = object.value(QStringLiteral("timelineRequested")).toBool();
		job.timelineInserted = object.value(QStringLiteral("timelineInserted")).toBool();
		if (!job.id.isEmpty() && !job.sourcePath.isEmpty())
			jobs.append(job);
	}
}

void PoldenPanel::saveJobs()
{
	QJsonArray array;
	for (const Job &job : jobs) {
		array.append(QJsonObject{{QStringLiteral("id"), job.id},
					 {QStringLiteral("projectId"), job.projectId},
					 {QStringLiteral("sourcePath"), job.sourcePath},
					 {QStringLiteral("localPath"), job.localPath},
					 {QStringLiteral("destinationPath"), job.destinationPath},
					 {QStringLiteral("premiereProjectPath"), job.premiereProjectPath},
					 {QStringLiteral("binPath"), job.binPath},
					 {QStringLiteral("recordedDate"), job.recordedDate},
					 {QStringLiteral("recordedTimestamp"), job.recordedTimestamp},
					 {QStringLiteral("fileName"), job.fileName},
					 {QStringLiteral("error"), job.error},
					 {QStringLiteral("targetLevel"), job.targetLevel},
					 {QStringLiteral("converted"), job.converted},
					 {QStringLiteral("copied"), job.copied},
					 {QStringLiteral("imported"), job.imported},
					 {QStringLiteral("timelineRequested"), job.timelineRequested},
					 {QStringLiteral("timelineInserted"), job.timelineInserted}});
	}
	QString path = jobsPath();
	if (path.isEmpty())
		return;
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
		file.commit();
	}
}

PoldenPanel::Job *PoldenPanel::jobById(const QString &id)
{
	for (Job &job : jobs)
		if (job.id == id)
			return &job;
	return nullptr;
}

PoldenPanel::Job *PoldenPanel::lastJobForSelectedProject()
{
	for (auto it = jobs.rbegin(); it != jobs.rend(); ++it)
		if (it->projectId == selectedProjectId)
			return &*it;
	return nullptr;
}

const PoldenPanel::Project *PoldenPanel::projectById(const QString &id) const
{
	for (const Project &project : projects)
		if (project.id == id)
			return &project;
	return nullptr;
}

void PoldenPanel::recordingStarted()
{
	projectCombo->setEnabled(false);
	const Project *project = selectedProject();
	recordingProjectId = project ? project->id : QString();
	if (project)
		recordingProjectSnapshot = *project;
	recordingSettingsSnapshot = settings;
	recordingAutomation = project ? project->automation : 0;
	recordingStartedAt = QDateTime::currentDateTime();
	recordingFiles.clear();
	setStatus(project ? tr("Recording for %1; automation level %2.").arg(project->name).arg(recordingAutomation)
			  : tr("Recording without a Polden project."));
}

void PoldenPanel::recordingFileChanged(const QString &path)
{
	if (!path.isEmpty() && !recordingFiles.contains(path))
		recordingFiles.append(path);
}

void PoldenPanel::recordingStopped(const QString &path, bool success)
{
	projectCombo->setEnabled(true);
	if (!success) {
		setStatus(tr("Recording failed; Polden did not process a file."));
		return;
	}
	recordingFileChanged(path);
	const Project *project = recordingProjectId.isEmpty() ? nullptr : &recordingProjectSnapshot;
	if (!project) {
		setStatus(tr("Recording saved; no Polden project was selected."));
		return;
	}
	const int segmentCount = recordingFiles.size();
	for (int index = 0; index < segmentCount; ++index) {
		const QString &source = recordingFiles[index];
		if (source.isEmpty())
			continue;
		Job job;
		job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		job.projectId = project->id;
		job.sourcePath = source;
		job.recordedDate = recordingStartedAt.toString(QStringLiteral("yyyy-MM-dd"));
		job.recordedTimestamp = recordingStartedAt.toString(QStringLiteral("yyyy-MM-dd-HH-mm-ss"));
		job.targetLevel = recordingAutomation;
		QString fileName = expand(recordingSettingsSnapshot.filenameTemplate, project->root,
					  project->footageDirectory, recordingSettingsSnapshot.operatorName,
					  recordingStartedAt);
		fileName = QFileInfo(fileName).fileName();
		if (segmentCount > 1) {
			QString suffix = QStringLiteral("-seg%1").arg(index + 1, 2, 10, QLatin1Char('0'));
			fileName = QFileInfo(fileName).completeBaseName() + suffix + QStringLiteral(".mp4");
		}
		QString destinationDirectory = expand(recordingSettingsSnapshot.directoryTemplate, project->root,
							 project->footageDirectory, recordingSettingsSnapshot.operatorName,
							 recordingStartedAt);
		const QString originalName = QFileInfo(fileName).completeBaseName();
		const QString extension = QFileInfo(fileName).suffix();
		for (int collision = 2;; ++collision) {
			const QString candidate = QDir(destinationDirectory).filePath(fileName);
			const bool usedByJob = std::any_of(jobs.cbegin(), jobs.cend(), [&candidate](const Job &other) {
				return other.destinationPath.compare(candidate, Qt::CaseInsensitive) == 0;
			});
			if (!usedByJob && !QFileInfo::exists(candidate))
				break;
			fileName = originalName + QStringLiteral("-%1").arg(collision, 2, 10, QLatin1Char('0')) +
				   QStringLiteral(".") + extension;
		}
		job.fileName = fileName;
		job.destinationPath = QDir(destinationDirectory).filePath(fileName);
		job.premiereProjectPath = project->premiereProject;
		job.binPath = expand(recordingSettingsSnapshot.binTemplate, project->root,
				     project->footageDirectory, recordingSettingsSnapshot.operatorName,
				     recordingStartedAt);
		job.localPath = QDir(QFileInfo(source).absolutePath())
					.filePath(QStringLiteral("Polden Converted/%1/%2").arg(project->id, fileName));
		jobs.append(job);
	}
	saveJobs();
	refresh();
	setStatus(tr("Recording saved; %1 Polden job(s) created.").arg(segmentCount));
	QTimer::singleShot(1500, this, [this]() { pump(); });
}

void PoldenPanel::importLastRecording(bool timeline)
{
	Job *job = lastJobForSelectedProject();
	if (!job)
		return;
	job->targetLevel = 3;
	job->timelineRequested |= timeline;
	job->error.clear();
	saveJobs();
	pump();
}

void PoldenPanel::pump()
{
	if (converter || copying || premiereBusy)
		return;
	for (Job &job : jobs) {
		if (!job.error.isEmpty() || job.targetLevel == 0)
			continue;
		if (job.targetLevel >= 1 && !job.converted) {
			startConversion(job);
			return;
		}
		if (job.targetLevel >= 2 && !job.copied) {
			startCopy(job);
			return;
		}
		if (job.targetLevel >= 3 && !job.imported) {
			startPremiere(job, false);
			if (premiereBusy)
				return;
			continue;
		}
		if (job.timelineRequested && !job.timelineInserted) {
			startPremiere(job, true);
			if (premiereBusy)
				return;
		}
	}
}

void PoldenPanel::startConversion(Job &job)
{
	const QString id = job.id;
	if (settings.ffmpegPath.isEmpty() || !QFileInfo(settings.ffmpegPath).isFile()) {
		job.error = tr("Set the FFmpeg executable in Polden settings.");
		saveJobs();
		refresh();
		QTimer::singleShot(0, this, [this]() { pump(); });
		return;
	}
	if (!QFileInfo(job.sourcePath).isFile()) {
		job.error = tr("Recording file is missing: %1").arg(job.sourcePath);
		saveJobs();
		refresh();
		QTimer::singleShot(0, this, [this]() { pump(); });
		return;
	}
	if (QFileInfo(job.localPath).isFile() && QFileInfo(job.localPath).size() > 0) {
		job.converted = true;
		saveJobs();
		QTimer::singleShot(0, this, [this]() { pump(); });
		return;
	}
	QDir().mkpath(QFileInfo(job.localPath).absolutePath());
	QString temporary = job.localPath + QStringLiteral(".polden-part-") + id + QStringLiteral(".mp4");
	QFile::remove(temporary);
	converter = new QProcess(this);
	QProcess *process = converter;
	process->setProgram(settings.ffmpegPath);
	process->setArguments({QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"),
			       QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-i"),
			       job.sourcePath, QStringLiteral("-map"), QStringLiteral("0"), QStringLiteral("-c"),
			       QStringLiteral("copy"), QStringLiteral("-movflags"), QStringLiteral("+faststart"),
			       QStringLiteral("-f"), QStringLiteral("mp4"), temporary});
	setStatus(tr("Converting %1 to MP4…").arg(QFileInfo(job.sourcePath).fileName()));
	connect(process, &QProcess::finished, this, [this, process, id, temporary](int exitCode) {
		if (converter != process)
			return;
		QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
		converter = nullptr;
		process->deleteLater();
		if (Job *item = jobById(id)) {
			if (exitCode == 0 && QFileInfo(temporary).size() > 0 && QFile::rename(temporary, item->localPath)) {
				item->converted = true;
				setStatus(tr("Converted %1.").arg(item->fileName));
			} else {
				item->error = tr("FFmpeg conversion failed: %1").arg(error.isEmpty() ? temporary : error);
				QFile::remove(temporary);
			}
			saveJobs();
			refresh();
		}
		pump();
	});
	connect(process, &QProcess::errorOccurred, this, [this, process, id](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart || converter != process)
			return;
		converter = nullptr;
		process->deleteLater();
		if (Job *item = jobById(id)) {
			item->error = tr("Could not start FFmpeg: %1").arg(process->errorString());
			saveJobs();
			refresh();
		}
		pump();
	});
	process->start();
}

void PoldenPanel::startCopy(Job &job)
{
	copying = true;
	const QString id = job.id;
	const QString source = job.localPath;
	const QString destination = job.destinationPath;
	const QString original = job.sourcePath;
	auto result = std::make_shared<QString>();
	setStatus(tr("Copying %1 to LucidLink…").arg(job.fileName));
	QThread *thread = QThread::create([=]() {
		*result = copyVideoAndInfoWriter(source, destination, original, id);
	});
	connect(thread, &QThread::finished, this, [this, id, result, thread]() {
		copying = false;
		thread->deleteLater();
		if (Job *item = jobById(id)) {
			if (result->isEmpty()) {
				item->copied = true;
				setStatus(tr("Copied %1 to LucidLink.").arg(item->fileName));
			} else {
				item->error = *result;
			}
			saveJobs();
			refresh();
		}
		pump();
	});
	thread->start();
}

void PoldenPanel::startPremiere(Job &job, bool timeline)
{
	if (job.premiereProjectPath.isEmpty() || !QFileInfo(job.premiereProjectPath).isFile()) {
		job.error = tr("Choose an existing Footage .prproj for this game.");
	} else if (!QFileInfo(job.destinationPath).isFile()) {
		job.error = tr("LucidLink video is missing: %1").arg(job.destinationPath);
	} else if (job.binPath.isEmpty()) {
		job.error = tr("Premiere bin path is empty.");
	}
	if (!job.error.isEmpty()) {
		saveJobs();
		refresh();
		return;
	}
	const bool online = premiereLastSeen.isValid() &&
			    premiereLastSeen.secsTo(QDateTime::currentDateTimeUtc()) < 10;
	if (!online) {
		if (!settings.queueWhenPremiereUnavailable) {
			job.error = tr("Premiere bridge is unavailable. Open the Polden bridge panel, then retry.");
			saveJobs();
			refresh();
		} else {
			setStatus(tr("Waiting for the Polden bridge in Premiere…"));
		}
		return;
	}
	premiereBusy = true;
	premiereJobId = job.id;
	premiereAction = timeline ? QStringLiteral("timeline") : QStringLiteral("import");
	setStatus(timeline ? tr("Adding the last recording to the active Premiere sequence…")
			   : tr("Importing %1 into Premiere…").arg(job.fileName));
}

void PoldenPanel::startBridge()
{
	bridge = new QTcpServer(this);
	if (!bridge->listen(QHostAddress::LocalHost, 37941)) {
		setStatus(tr("Premiere bridge could not listen on localhost port 37941: %1").arg(bridge->errorString()));
		return;
	}
	connect(bridge, &QTcpServer::newConnection, this, [this]() {
		while (QTcpSocket *socket = bridge->nextPendingConnection()) {
			connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { readBridgeRequest(socket); });
			connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
		}
	});
	auto *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this]() { pump(); });
	timer->start(3000);
}

void PoldenPanel::readBridgeRequest(QTcpSocket *socket)
{
	QByteArray request = socket->property("poldenRequest").toByteArray() + socket->readAll();
	if (request.size() > 1024 * 1024) {
		socket->disconnectFromHost();
		return;
	}
	const int end = request.indexOf("\r\n\r\n");
	if (end < 0) {
		socket->setProperty("poldenRequest", request);
		return;
	}
	QList<QByteArray> lines = request.left(end).split('\n');
	const QByteArray first = lines.takeFirst().trimmed();
	int length = 0;
	QByteArray key;
	for (const QByteArray &line : lines) {
		const int colon = line.indexOf(':');
		if (colon < 0)
			continue;
		QByteArray name = line.left(colon).trimmed().toLower();
		QByteArray value = line.mid(colon + 1).trimmed();
		if (name == "content-length")
			length = value.toInt();
		else if (name == "x-polden-key")
			key = value;
	}
	if (length < 0 || length > 1024 * 1024) {
		socket->disconnectFromHost();
		return;
	}
	if (request.size() < end + 4 + length) {
		socket->setProperty("poldenRequest", request);
		return;
	}
	auto respond = [socket](int status, const QJsonObject &object) {
		QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);
		QByteArray header = "HTTP/1.1 " + QByteArray::number(status) +
				    (status == 200 ? " OK\r\n" : " Error\r\n") +
				    "Content-Type: application/json\r\n"
				    "Access-Control-Allow-Origin: *\r\n"
				    "Access-Control-Allow-Headers: X-Polden-Key, Content-Type\r\n"
				    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
				    "Connection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
		socket->write(header + body);
		socket->disconnectFromHost();
	};
	if (first.startsWith("OPTIONS ")) {
		respond(200, {});
		return;
	}
	if (key != settings.bridgeKey.toUtf8()) {
		respond(403, {{QStringLiteral("error"), QStringLiteral("Invalid Polden bridge key")}});
		return;
	}
	premiereLastSeen = QDateTime::currentDateTimeUtc();
	if (first.startsWith("GET /next ")) {
		pump();
		QJsonObject command;
		if (premiereBusy) {
			if (Job *job = jobById(premiereJobId)) {
				command = {{QStringLiteral("id"), job->id},
					   {QStringLiteral("action"), premiereAction},
					   {QStringLiteral("filePath"), job->destinationPath},
					   {QStringLiteral("projectPath"), job->premiereProjectPath},
					   {QStringLiteral("binPath"), job->binPath}};
			}
		}
		respond(200, command);
	} else if (first.startsWith("POST /result ")) {
		QJsonObject result = QJsonDocument::fromJson(request.mid(end + 4, length)).object();
		if (premiereBusy && result.value(QStringLiteral("id")).toString() == premiereJobId &&
		    result.value(QStringLiteral("action")).toString() == premiereAction) {
			if (Job *job = jobById(premiereJobId)) {
				if (result.value(QStringLiteral("ok")).toBool()) {
					if (premiereAction == QStringLiteral("timeline"))
						job->timelineInserted = true;
					else
						job->imported = true;
					setStatus(tr("Premiere completed: %1").arg(job->fileName));
				} else {
					job->error = tr("Premiere: %1").arg(result.value(QStringLiteral("error")).toString());
				}
				saveJobs();
				refresh();
			}
			premiereBusy = false;
			premiereJobId.clear();
			premiereAction.clear();
			respond(200, {{QStringLiteral("accepted"), true}});
			QTimer::singleShot(0, this, [this]() { pump(); });
		} else {
			respond(409, {{QStringLiteral("error"), QStringLiteral("No matching Premiere command")}});
		}
	} else {
		respond(404, {{QStringLiteral("error"), QStringLiteral("Unknown bridge request")}});
	}
}
