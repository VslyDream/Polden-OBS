#include "PoldenPanel.hpp"

#include <OBSApp.hpp>
#include <utility/PoldenTemplates.hpp>
#include <utility/PoldenTakeFiles.hpp>
#include <obs-frontend-api.h>
#include <QRegularExpression>
#include <QMessageBox>
#include <QToolButton>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>
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
		job.takeId = object.value(QStringLiteral("takeId")).toString();
		if (job.takeId.isEmpty()) {
			job.takeId = object.value(QStringLiteral("recordedTimestamp")).toString(job.id);
			if (job.takeId.isEmpty()) {
				job.takeId = job.id;
			}
		}
		job.durationMs = qint64(object.value(QStringLiteral("durationMs")).toDouble());
		job.segmentDurationMs = qint64(object.value(QStringLiteral("segmentDurationMs")).toDouble());
		job.markerCount = object.value(QStringLiteral("markerCount")).toInt();
		for (const auto &path : object.value(QStringLiteral("deliveredFiles")).toArray()) {
			job.deliveredFiles.append(path.toString());
		}
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
		array.append(
			QJsonObject{{QStringLiteral("id"), job.id},
				    {QStringLiteral("takeId"), job.takeId},
				    {QStringLiteral("durationMs"), double(job.durationMs)},
				    {QStringLiteral("segmentDurationMs"), double(job.segmentDurationMs)},
				    {QStringLiteral("markerCount"), job.markerCount},
				    {QStringLiteral("deliveredFiles"), QJsonArray::fromStringList(job.deliveredFiles)},
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
	Job *last = lastJobForSelectedProject();
	if (!last) {
		return nullptr;
	}
	for (auto it = jobs.rbegin(); it != jobs.rend(); ++it) {
		if (it->projectId == selectedProjectId && it->takeId == last->takeId && !it->cancelled &&
		    !it->error.isEmpty()) {
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
	recording = true;
	lastMarker.clear();
	const Project *project = selectedProject();
	recordingProjectId = project ? project->id : QString();
	if (project) {
		recordingProjectSnapshot = *project;
	}
	recordingSettingsSnapshot = settings;
	recordingAutomation = project ? project->automation : 0;
	recordingStartedAt = QDateTime::currentDateTime();
	recordingFiles.clear();
	recordingSegmentEnds.clear();
	refreshTake();
	setStatus(project ? tr("Recording for %1; automation level %2.").arg(project->name).arg(recordingAutomation)
			  : tr("Recording without a Polden project."));
}

void PoldenPanel::recordingFileChanged(const QString &path)
{
	if (!path.isEmpty() && !recordingFiles.contains(path)) {
		recordingFiles.append(path);
		recordingSegmentEnds.append(markers ? markers->elapsedMs()
						    : recordingStartedAt.msecsTo(QDateTime::currentDateTime()));
	}
}

void PoldenPanel::recordingStopped(const QString &path, bool success)
{
	const qint64 duration = markers ? markers->elapsedMs()
					: recordingStartedAt.msecsTo(QDateTime::currentDateTime());
	const int markerCount = markers ? markers->markerCount() : 0;
	recordingFileChanged(path);
	if (markers) {
		markers->recordingStopped();
	}
	markerRecording = false;
	refreshMarkerButtons();
	recording = false;
	applyFilenameTemplate();
	refresh();
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
	const QString takeId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	const int segmentCount = recordingFiles.size();
	for (int index = 0; index < segmentCount; ++index) {
		const QString &source = recordingFiles[index];
		if (source.isEmpty()) {
			continue;
		}
		Job job;
		job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		job.projectId = project->id;
		job.takeId = takeId;
		job.durationMs = duration;
		job.segmentDurationMs =
			qMax(qint64(0), recordingSegmentEnds[index] - (index ? recordingSegmentEnds[index - 1] : 0));
		job.markerCount = markerCount;
		job.sourcePath = source;
		job.recordedDate = recordingStartedAt.toString(QStringLiteral("yyyy-MM-dd"));
		job.recordedTimestamp = recordingStartedAt.toString(QStringLiteral("yyyy-MM-dd-HH-mm-ss"));
		job.targetLevel = recordingAutomation;
		const QString fileName = QFileInfo(source).completeBaseName() + QStringLiteral(".mp4");
		QString destinationDirectory = PoldenTemplates::folder(recordingSettingsSnapshot.directoryTemplate,
								       project->root, project->footageDirectory,
								       recordingSettingsSnapshot.operatorName,
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
	if (!pendingDeleteIds.isEmpty()) {
		finishTakeDeletion();
		return;
	}
	if (converter || copying) {
		return;
	}
	activeJobId.clear();
	processingProgress = -1;
	refreshTake();
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
	activeJobId = job.id;
	processingProgress = -1;
	const QString id = job.id;
	if (settings.ffmpegPath.isEmpty() || !QFileInfo(settings.ffmpegPath).isFile()) {
		job.error = tr("Set the FFmpeg executable in Polden settings.");
		saveJobs();
		refreshTake();
		QTimer::singleShot(0, this, [this]() { pump(); });
		return;
	}
	if (!QFileInfo(job.sourcePath).isFile()) {
		job.error = tr("Recording file is missing: %1").arg(job.sourcePath);
		saveJobs();
		refreshTake();
		QTimer::singleShot(0, this, [this]() { pump(); });
		return;
	}
	if (QFileInfo(job.localPath).isFile() && QFileInfo(job.localPath).size() > 0) {
		job.error = copyRecordingSidecars(job.sourcePath, job.localPath, id);
		job.converted = job.error.isEmpty();
		saveJobs();
		refreshTake();
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
			       QStringLiteral("error"), QStringLiteral("-progress"), QStringLiteral("pipe:1"),
			       QStringLiteral("-i"), job.sourcePath, QStringLiteral("-map"), QStringLiteral("0"),
			       QStringLiteral("-c"), QStringLiteral("copy"), QStringLiteral("-movflags"),
			       QStringLiteral("+faststart"), QStringLiteral("-f"), QStringLiteral("mp4"), temporary});
	setStatus(tr("Converting %1 to MP4…").arg(QFileInfo(job.sourcePath).fileName()));
	refreshTake();
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
				refreshTake();
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
			refreshTake();
		}
		pump();
	});
	auto progressBuffer = std::make_shared<QByteArray>();
	connect(process, &QProcess::readyReadStandardOutput, this, [this, process, id, progressBuffer]() {
		progressBuffer->append(process->readAllStandardOutput());
		int end;
		while ((end = progressBuffer->indexOf('\n')) >= 0) {
			const QByteArray line = progressBuffer->left(end).trimmed();
			progressBuffer->remove(0, end + 1);
			if (Job *item = jobById(id);
			    item && item->segmentDurationMs > 0 &&
			    (line.startsWith("out_time_us=") || line.startsWith("out_time_ms="))) {
				bool ok = false;
				const qint64 time = line.mid(12).toLongLong(&ok);
				if (ok) {
					processingProgress =
						qBound(0, int(100.0 * time / (item->segmentDurationMs * 1000.0)), 99);
				}
			}
		}
		refreshTake();
	});
	process->start();
}

void PoldenPanel::startCopy(Job &job)
{
	activeJobId = job.id;
	processingProgress = -1;
	copying = true;
	copyCancellation = std::make_shared<std::atomic_bool>(false);
	const auto cancel = copyCancellation;
	const QString id = job.id;
	const QString source = job.localPath;
	const QString destination = job.destinationPath;
	const QString original = job.sourcePath;
	auto result = std::make_shared<QString>();
	auto delivered = std::make_shared<QStringList>();
	setStatus(tr("Copying %1 to LucidLink…").arg(job.fileName));
	refreshTake();
	QThread *thread = QThread::create([=]() {
		int previous = -1;
		*result = copyVideoAndMarkers(
			source, destination, original, id, cancel,
			[this, id, &previous](qint64 bytes, qint64 total) {
				const int percent = total > 0 ? qBound(0, int(100.0 * bytes / total), 99) : -1;
				if (percent == previous) {
					return;
				}
				previous = percent;
				QMetaObject::invokeMethod(
					this,
					[this, id, percent]() {
						if (activeJobId == id) {
							processingProgress = percent;
							refreshTake();
						}
					},
					Qt::QueuedConnection);
			},
			delivered.get());
	});
	connect(thread, &QThread::finished, this, [this, id, result, delivered, thread]() {
		copying = false;
		copyCancellation.reset();
		thread->deleteLater();
		if (Job *item = jobById(id)) {
			for (const auto &path : *delivered) {
				if (!item->deliveredFiles.contains(path)) {
					item->deliveredFiles.append(path);
				}
			}
			if (item->cancelled) {
				setStatus(tr("Processing cancelled. Original recordings kept."));
			} else if (result->isEmpty()) {
				item->copied = true;
				setStatus(tr("Copied %1 to LucidLink.").arg(item->fileName));
			} else {
				item->error = *result;
			}
			saveJobs();
			refreshTake();
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

void PoldenPanel::deleteLastTake()
{
	if (recording || !pendingDeleteIds.isEmpty()) {
		return;
	}
	const Job *last = lastJobForSelectedProject();
	if (!last) {
		return;
	}
	const QString take = last->takeId;
	const QString project = last->projectId;
	const auto plan = PoldenTakeFiles::plan(jobs, project, take);
	const auto &ids = plan.ids;
	const auto &local = plan.local;
	const auto &remote = plan.remote;
	QStringList description;
	for (const QString &path : local) {
		if (QFile::exists(path)) {
			description.append(tr("Recycle Bin: %1").arg(QDir::toNativeSeparators(path)));
		}
	}
	for (const QString &path : remote) {
		description.append(tr("LucidLink (permanent, if present): %1").arg(QDir::toNativeSeparators(path)));
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("poldenDialog"));
	dialog.setWindowTitle(tr("Delete last take?"));
	dialog.resize(640, 430);
	auto *layout = new QVBoxLayout(&dialog);
	auto *text = new QLabel(
		tr("Delete all %1 segment(s) of this take? Processing for this take will be cancelled. Local files go to the Recycle Bin; LucidLink copies are deleted permanently.")
			.arg(ids.size()),
		&dialog);
	text->setWordWrap(true);
	layout->addWidget(text);
	auto *list = new QPlainTextEdit(description.isEmpty()
						? tr("No files remain. Remove this take from the job history?")
						: description.join(QLatin1Char('\n')),
					&dialog);
	list->setReadOnly(true);
	layout->addWidget(list);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
	auto *remove = buttons->addButton(tr("Delete take"), QDialogButtonBox::DestructiveRole);
	remove->setObjectName(QStringLiteral("poldenConfirmDelete"));
	remove->setStyleSheet(QStringLiteral("background: #c4343d; color: white;"));
	remove->setAutoDefault(false);
	buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
	connect(remove, &QPushButton::clicked, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	// A new recording could have been started by a global hotkey while confirmation was open.
	if (recording) {
		setStatus(tr("Could not delete a take while recording. Stop recording and try again."));
		return;
	}
	pendingDeleteIds = ids;
	pendingDeleteLocal = local;
	pendingDeleteRemote = remote;
	for (Job &job : jobs) {
		if (ids.contains(job.id)) {
			job.cancelled = true;
		}
	}
	if (ids.contains(activeJobId)) {
		if (copyCancellation) {
			copyCancellation->store(true);
		}
		if (converter) {
			converter->kill();
		}
	}
	saveJobs();
	refreshTake();
	finishTakeDeletion();
}

void PoldenPanel::finishTakeDeletion()
{
	if (pendingDeleteIds.isEmpty() || processingFiles()) {
		return;
	}
	QStringList failures;
	const Job *selected = jobById(pendingDeleteIds.front());
	if (!selected) {
		pendingDeleteIds.clear();
		return;
	}
	const auto safe = PoldenTakeFiles::plan(jobs, selected->projectId, selected->takeId);
	for (const QString &path : pendingDeleteLocal) {
		if (safe.local.contains(path) && QFile::exists(path) && !QFile::moveToTrash(path)) {
			failures.append(QDir::toNativeSeparators(path));
		}
	}
	for (const QString &path : pendingDeleteRemote) {
		bool owned = false;
		for (const Job &job : jobs) {
			if (pendingDeleteIds.contains(job.id) &&
			    (job.deliveredFiles.contains(path) ||
			     (job.copied && PoldenTakeFiles::paths(job.destinationPath).contains(path)))) {
				owned = true;
			}
		}
		if (owned && safe.remote.contains(path) && QFile::exists(path) && !QFile::remove(path)) {
			failures.append(QDir::toNativeSeparators(path));
		}
	}
	if (failures.isEmpty()) {
		jobs.removeIf([this](const Job &job) { return pendingDeleteIds.contains(job.id); });
	}
	pendingDeleteIds.clear();
	pendingDeleteLocal.clear();
	pendingDeleteRemote.clear();
	saveJobs();
	refreshTake();
	if (!failures.isEmpty()) {
		QMessageBox::warning(this, tr("Delete take"),
				     tr("Could not delete these files. The take remains in history; try again.\n%1")
					     .arg(failures.join(QLatin1Char('\n'))));
	}
	QTimer::singleShot(0, this, [this]() { pump(); });
}
