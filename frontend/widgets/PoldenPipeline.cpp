#include "PoldenPanel.hpp"

#include <OBSApp.hpp>
#include <QComboBox>
#include <QPushButton>
#include <utility/PoldenMarkers.hpp>
#include <utility/PoldenFileTransfer.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QProcess>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <memory>

using namespace PoldenFileTransfer;

namespace {
QString expand(QString text, const QString &root, const QString &footage, const QString &user, const QDateTime &time)
{
	text.replace(QStringLiteral("{root}"), root);
	text.replace(QStringLiteral("{footage}"), footage);
	text.replace(QStringLiteral("{user}"), user);
	text.replace(QStringLiteral("{date}"), time.toString(QStringLiteral("yyyy-MM-dd")));
	text.replace(QStringLiteral("{timestamp}"), time.toString(QStringLiteral("yyyy-MM-dd-HH-mm-ss")));
	return QDir::toNativeSeparators(text);
}

} // namespace

QString PoldenPanel::jobsPath() const
{
	char path[1024];
	if (GetAppConfigPath(path, sizeof(path), "obs-studio/polden-jobs.json") <= 0) {
		return {};
	}
	return QString::fromUtf8(path);
}

void PoldenPanel::loadJobs()
{
	QFile file(jobsPath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
	for (const QJsonValue &value : array) {
		QJsonObject object = value.toObject();
		Job job;
		job.id = object.value(QStringLiteral("id")).toString();
		job.projectId = object.value(QStringLiteral("projectId")).toString();
		job.sourcePath = object.value(QStringLiteral("sourcePath")).toString();
		job.localPath = object.value(QStringLiteral("localPath")).toString();
		job.destinationPath = object.value(QStringLiteral("destinationPath")).toString();
		job.recordedDate = object.value(QStringLiteral("recordedDate")).toString();
		job.recordedTimestamp = object.value(QStringLiteral("recordedTimestamp")).toString();
		job.fileName = object.value(QStringLiteral("fileName")).toString();
		job.error = object.value(QStringLiteral("error")).toString();
		// Errors after copying belonged to the removed import step in 0.0.1.
		if (object.value(QStringLiteral("targetLevel")).toInt() >= 3 &&
		    object.value(QStringLiteral("copied")).toBool()) {
			job.error.clear();
		}
		job.targetLevel = qBound(0, object.value(QStringLiteral("targetLevel")).toInt(), 2);
		job.converted = object.value(QStringLiteral("converted")).toBool();
		job.copied = object.value(QStringLiteral("copied")).toBool();
		job.cancelled = object.value(QStringLiteral("cancelled")).toBool();
		if (!job.id.isEmpty() && !job.sourcePath.isEmpty()) {
			jobs.append(job);
		}
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
					 {QStringLiteral("recordedDate"), job.recordedDate},
					 {QStringLiteral("recordedTimestamp"), job.recordedTimestamp},
					 {QStringLiteral("fileName"), job.fileName},
					 {QStringLiteral("error"), job.error},
					 {QStringLiteral("targetLevel"), job.targetLevel},
					 {QStringLiteral("converted"), job.converted},
					 {QStringLiteral("copied"), job.copied},
					 {QStringLiteral("cancelled"), job.cancelled}});
	}
	QString path = jobsPath();
	if (path.isEmpty()) {
		return;
	}
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
		file.commit();
	}
}

PoldenPanel::Job *PoldenPanel::jobById(const QString &id)
{
	for (Job &job : jobs) {
		if (job.id == id) {
			return &job;
		}
	}
	return nullptr;
}

PoldenPanel::Job *PoldenPanel::lastJobForSelectedProject()
{
	for (auto it = jobs.rbegin(); it != jobs.rend(); ++it) {
		if (it->projectId == selectedProjectId) {
			return &*it;
		}
	}
	return nullptr;
}

PoldenPanel::Job *PoldenPanel::retryableJobForSelectedProject()
{
	for (auto it = jobs.rbegin(); it != jobs.rend(); ++it) {
		if (it->projectId == selectedProjectId && (it->cancelled || !it->error.isEmpty())) {
			return &*it;
		}
	}
	return nullptr;
}

void PoldenPanel::recordingStarted()
{
	if (markers) {
		markers->recordingStarted();
	}
	markerRecording = markers != nullptr;
	refreshMarkerButtons();
	projectCombo->setEnabled(false);
	const Project *project = selectedProject();
	recordingProjectId = project ? project->id : QString();
	if (project) {
		recordingProjectSnapshot = *project;
	}
	recordingSettingsSnapshot = settings;
	recordingAutomation = project ? project->automation : 0;
	recordingStartedAt = QDateTime::currentDateTime();
	recordingFiles.clear();
	setStatus(project ? tr("Recording for %1; automation level %2.").arg(project->name).arg(recordingAutomation)
			  : tr("Recording without a Polden project."));
}

void PoldenPanel::recordingFileChanged(const QString &path)
{
	if (!path.isEmpty() && !recordingFiles.contains(path)) {
		recordingFiles.append(path);
	}
}

void PoldenPanel::recordingStopped(const QString &path, bool success)
{
	if (markers) {
		markers->recordingStopped();
	}
	markerRecording = false;
	refreshMarkerButtons();
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
		if (source.isEmpty()) {
			continue;
		}
		Job job;
		job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		job.projectId = project->id;
		job.sourcePath = source;
		job.recordedDate = recordingStartedAt.toString(QStringLiteral("yyyy-MM-dd"));
		job.recordedTimestamp = recordingStartedAt.toString(QStringLiteral("yyyy-MM-dd-HH-mm-ss"));
		job.targetLevel = recordingAutomation;
		const QString fileName = QFileInfo(source).completeBaseName() + QStringLiteral(".mp4");
		QString destinationDirectory = expand(recordingSettingsSnapshot.directoryTemplate, project->root,
						      project->footageDirectory, recordingSettingsSnapshot.operatorName,
						      recordingStartedAt);
		job.fileName = fileName;
		job.destinationPath = QDir(destinationDirectory).filePath(fileName);
		job.localPath =
			QDir(QFileInfo(source).absolutePath())
				.filePath(
					QStringLiteral("Polden Converted/%1/%2/%3").arg(project->id, job.id, fileName));
		jobs.append(job);
	}
	saveJobs();
	refresh();
	setStatus(tr("Recording saved; %1 Polden job(s) created.").arg(segmentCount));
	QTimer::singleShot(1500, this, [this]() { pump(); });
}

void PoldenPanel::pump()
{
	if (converter || copying) {
		return;
	}
	for (Job &job : jobs) {
		if (job.cancelled || !job.error.isEmpty() || job.targetLevel == 0) {
			continue;
		}
		if (job.targetLevel >= 1 && !job.converted) {
			startConversion(job);
			return;
		}
		if (job.targetLevel >= 2 && !job.copied) {
			startCopy(job);
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
		job.error = copyRecordingSidecars(job.sourcePath, job.localPath, id);
		job.converted = job.error.isEmpty();
		saveJobs();
		refresh();
		QTimer::singleShot(0, this, [this]() { pump(); });
		return;
	}
	QDir().mkpath(QFileInfo(job.localPath).absolutePath());
	QString temporary = job.localPath + QStringLiteral(".polden-part-") + id + QStringLiteral(".mp4");
	QFile::remove(temporary);
	converter = new QProcess(this);
	QProcess *process = converter;
	process->setProgram(settings.ffmpegPath);
	process->setArguments({QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
			       QStringLiteral("error"), QStringLiteral("-i"), job.sourcePath, QStringLiteral("-map"),
			       QStringLiteral("0"), QStringLiteral("-c"), QStringLiteral("copy"),
			       QStringLiteral("-movflags"), QStringLiteral("+faststart"), QStringLiteral("-f"),
			       QStringLiteral("mp4"), temporary});
	setStatus(tr("Converting %1 to MP4…").arg(QFileInfo(job.sourcePath).fileName()));
	refresh();
	connect(process, &QProcess::finished, this,
		[this, process, id, temporary](int exitCode, QProcess::ExitStatus exitStatus) {
			if (converter != process) {
				return;
			}
			QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
			converter = nullptr;
			process->deleteLater();
			if (Job *item = jobById(id)) {
				if (item->cancelled) {
					QFile::remove(temporary);
					setStatus(tr("Processing cancelled. Original recordings kept."));
				} else if (exitStatus == QProcess::NormalExit && exitCode == 0 &&
					   QFileInfo(temporary).size() > 0 &&
					   QFile::rename(temporary, item->localPath)) {
					item->error = copyRecordingSidecars(item->sourcePath, item->localPath, id);
					item->converted = item->error.isEmpty();
					if (item->converted) {
						setStatus(tr("Converted %1.").arg(item->fileName));
					}
				} else {
					item->error = tr("FFmpeg conversion failed: %1")
							      .arg(error.isEmpty() ? temporary : error);
					QFile::remove(temporary);
				}
				saveJobs();
				refresh();
			}
			pump();
		});
	connect(process, &QProcess::errorOccurred, this, [this, process, id, temporary](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart || converter != process) {
			return;
		}
		converter = nullptr;
		process->deleteLater();
		QFile::remove(temporary);
		if (Job *item = jobById(id)) {
			if (!item->cancelled) {
				item->error = tr("Could not start FFmpeg: %1").arg(process->errorString());
			}
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
	copyCancellation = std::make_shared<std::atomic_bool>(false);
	const auto cancel = copyCancellation;
	const QString id = job.id;
	const QString source = job.localPath;
	const QString destination = job.destinationPath;
	const QString original = job.sourcePath;
	auto result = std::make_shared<QString>();
	setStatus(tr("Copying %1 to LucidLink…").arg(job.fileName));
	refresh();
	QThread *thread =
		QThread::create([=]() { *result = copyVideoAndMarkers(source, destination, original, id, cancel); });
	connect(thread, &QThread::finished, this, [this, id, result, thread]() {
		copying = false;
		copyCancellation.reset();
		thread->deleteLater();
		if (Job *item = jobById(id)) {
			if (item->cancelled) {
				setStatus(tr("Processing cancelled. Original recordings kept."));
			} else if (result->isEmpty()) {
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

void PoldenPanel::cancelProcessing()
{
	for (Job &job : jobs) {
		if (job.targetLevel > 0 && (!job.converted || (job.targetLevel >= 2 && !job.copied))) {
			job.cancelled = true;
		}
	}
	if (copyCancellation) {
		copyCancellation->store(true);
	}
	if (converter) {
		converter->kill();
	}
	saveJobs();
	refresh();
	setStatus(processingFiles() ? tr("Cancelling processing…")
				    : tr("Processing cancelled. Original recordings kept."));
}
