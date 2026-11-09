#include "PoldenPanel.hpp"

#include "OBSBasic.hpp"

#include <OBSApp.hpp>
#include <obs.hpp>
#include <obs-frontend-api.h>
#include <util/platform.h>

#include "ui-config.h"
#include <utility/PoldenUpdater.hpp>
#include <utility/PoldenMarkers.hpp>
#include <components/FlowLayout.hpp>

#include <QDesktopServices>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QIcon>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <cstring>

namespace {
struct GameSource {
	QString name;
	QString uuid;
	QString window;
};

bool collectGameSource(void *param, obs_source_t *source)
{
	if (std::strcmp(obs_source_get_unversioned_id(source), "game_capture") != 0) {
		return true;
	}
	OBSDataAutoRelease data = obs_source_get_settings(source);
	auto *sources = static_cast<QVector<GameSource> *>(param);
	sources->append({QString::fromUtf8(obs_source_get_name(source)), QString::fromUtf8(obs_source_get_uuid(source)),
			 QString::fromUtf8(obs_data_get_string(data, "window"))});
	return true;
}

QVector<GameSource> gameSources()
{
	QVector<GameSource> sources;
	obs_enum_sources(collectGameSource, &sources);
	return sources;
}

QStringList gameWindows(const QString &uuid)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid.toUtf8().constData());
	if (!source) {
		return {};
	}
	obs_properties_t *properties = obs_source_properties(source);
	if (!properties) {
		return {};
	}
	QStringList windows;
	obs_property_t *property = obs_properties_get(properties, "window");
	if (property) {
		const size_t count = obs_property_list_item_count(property);
		for (size_t index = 0; index < count; ++index) {
			QString value = QString::fromUtf8(obs_property_list_item_string(property, index));
			if (!value.isEmpty()) {
				windows.append(value);
			}
		}
	}
	obs_properties_destroy(properties);
	return windows;
}

QString previewPath(QString text, const QString &root, const QString &footage, const QString &user,
		    const QDateTime &time)
{
	text.replace(QStringLiteral("{root}"), root);
	text.replace(QStringLiteral("{footage}"), footage);
	text.replace(QStringLiteral("{user}"), user);
	text.replace(QStringLiteral("{date}"), time.toString(QStringLiteral("yyyy-MM-dd")));
	text.replace(QStringLiteral("{timestamp}"), time.toString(QStringLiteral("yyyy-MM-dd-HH-mm-ss")));
	return QDir::toNativeSeparators(text);
}
} // namespace

PoldenPanel::PoldenPanel(OBSBasic *main) : QWidget(main), main(main)
{
	settings.ffmpegPath = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
	load();
	loadJobs();

	auto *layout = new QVBoxLayout(this);
	auto *projectRow = new QHBoxLayout;
	projectCombo = new QComboBox(this);
	projectCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	projectCombo->setAccessibleName(tr("Polden project"));
	auto *addButton = new QPushButton(tr("+"), this);
	addButton->setToolTip(tr("Add game project"));
	editButton = new QPushButton(tr("Edit"), this);
	projectRow->addWidget(projectCombo, 1);
	projectRow->addWidget(addButton);
	projectRow->addWidget(editButton);
	layout->addLayout(projectRow);

	auto *automationRow = new QHBoxLayout;
	automationRow->addWidget(new QLabel(tr("Automation"), this));
	automationCombo = new QComboBox(this);
	automationCombo->addItem(tr("0 · OBS only"), 0);
	automationCombo->addItem(tr("1 · Convert MP4"), 1);
	automationCombo->addItem(tr("2 · LucidLink"), 2);
	automationRow->addWidget(automationCombo, 1);
	layout->addLayout(automationRow);

	markerButtonsWidget = new QWidget(this);
	markerButtonsWidget->setObjectName(QStringLiteral("poldenMarkerButtons"));
	new FlowLayout(markerButtonsWidget, 0, 6, 6);
	markerButtonsWidget->hide();
	layout->addWidget(markerButtonsWidget);

	auto *openFolderButton = new QPushButton(tr("Open files folder"), this);
	openFolderButton->setObjectName(QStringLiteral("poldenOpenFilesFolder"));
	layout->addWidget(openFolderButton);
	statusLabel = new QLabel(this);
	statusLabel->setWordWrap(true);
	layout->addWidget(statusLabel);
	retryButton = new QPushButton(tr("Retry last failed step"), this);
	retryButton->setObjectName(QStringLiteral("poldenRetry"));
	layout->addWidget(retryButton);
	cancelButton = new QPushButton(tr("Cancel processing"), this);
	cancelButton->setObjectName(QStringLiteral("poldenCancelProcessing"));
	cancelButton->setToolTip(tr("Cancel conversion, LucidLink copying and queued jobs. Keep original recordings."));
	layout->addWidget(cancelButton);
	auto *settingsButton = new QPushButton(tr("Polden settings…"), this);
	settingsButton->setObjectName(QStringLiteral("poldenSettings"));
	layout->addWidget(settingsButton);
	layout->addStretch();
	auto *footer = new QHBoxLayout;
	footer->addWidget(new QLabel(QStringLiteral("Polden " POLDEN_VERSION), this));
	footer->addStretch();
	auto *updateButton = new QToolButton(this);
	updateButton->setObjectName(QStringLiteral("poldenUpdate"));
	updateButton->setIcon(QIcon(QStringLiteral(":/res/images/polden-update.svg")));
	updateButton->setIconSize(QSize(16, 16));
	updateButton->setAutoRaise(true);
	updateButton->setAccessibleName(tr("Update Polden"));
	footer->addWidget(updateButton);
	updater = new PoldenUpdater(this, updateButton);
	auto *bugReportButton = new QToolButton(this);
	bugReportButton->setObjectName(QStringLiteral("poldenBugReport"));
	bugReportButton->setIcon(QIcon(QStringLiteral(":/res/images/bug-report.svg")));
	bugReportButton->setIconSize(QSize(16, 16));
	bugReportButton->setAutoRaise(true);
	bugReportButton->setToolTip(tr("Report a bug — @Vsly_Dream"));
	bugReportButton->setAccessibleName(tr("Report a bug"));
	footer->addWidget(bugReportButton);
	layout->addLayout(footer);
	connect(bugReportButton, &QToolButton::clicked, this, [this]() {
		if (!QDesktopServices::openUrl(QUrl(QStringLiteral("https://t.me/Vsly_Dream")))) {
			setStatus(tr("Could not open Telegram: https://t.me/Vsly_Dream"));
		}
	});

	connect(addButton, &QPushButton::clicked, this, [this]() { showProjectDialog(false); });
	connect(editButton, &QPushButton::clicked, this, [this]() { showProjectDialog(true); });
	connect(settingsButton, &QPushButton::clicked, this, [this]() { showSettingsDialog(); });
	connect(openFolderButton, &QPushButton::clicked, this, &PoldenPanel::openFilesFolder);
	connect(cancelButton, &QPushButton::clicked, this, &PoldenPanel::cancelProcessing);
	connect(retryButton, &QPushButton::clicked, this, [this]() {
		if (Job *job = retryableJobForSelectedProject()) {
			job->error.clear();
			job->cancelled = false;
			saveJobs();
			refresh();
			pump();
		}
	});
	connect(projectCombo, &QComboBox::currentIndexChanged, this, [this]() {
		selectedProjectId = projectCombo->currentData().toString();
		if (const Project *project = selectedProject()) {
			applyCaptureWindow(*project);
		}
		save();
		refresh();
	});
	connect(automationCombo, &QComboBox::currentIndexChanged, this, [this]() {
		if (Project *project = selectedProject()) {
			project->automation = automationCombo->currentData().toInt();
			save();
		}
	});
	refresh();
	pump();
}

void PoldenPanel::checkForUpdates(bool manual)
{
	updater->check(manual);
}

void PoldenPanel::initializeMarkers()
{
	markers = new PoldenMarkers(this, [this](const QString &text) { setStatus(text); });
	refreshMarkerButtons();
}

void PoldenPanel::shutdownMarkers()
{
	delete markers;
	markers = nullptr;
	markerButtonsWidget->hide();
}

void PoldenPanel::refreshMarkerButtons()
{
	auto *layout = markerButtonsWidget->layout();
	while (auto *item = layout->takeAt(0)) {
		delete item->widget();
		delete item;
	}
	if (markers) {
		for (int color = 0; color < 8; ++color) {
			if (!markers->buttonVisible(color)) {
				continue;
			}
			auto *button = new QPushButton(PoldenMarkers::colorIcon(color), PoldenMarkers::colorName(color),
						       markerButtonsWidget);
			button->setObjectName(QStringLiteral("poldenMarkerButton%1").arg(color));
			button->setToolTip(tr("Add marker: %1").arg(PoldenMarkers::colorName(color)));
			button->setAccessibleName(button->toolTip());
			button->setEnabled(markerRecording);
			connect(button, &QPushButton::clicked, this, [this, color]() { markers->addMarker(color); });
			layout->addWidget(button);
		}
	}
	markerButtonsWidget->setVisible(layout->count() > 0);
}

bool PoldenPanel::processingFiles() const
{
	return copying || converter != nullptr;
}

QString PoldenPanel::configPath() const
{
	char path[1024];
	if (GetAppConfigPath(path, sizeof(path), "obs-studio/polden.json") <= 0) {
		return {};
	}
	return QString::fromUtf8(path);
}

void PoldenPanel::load()
{
	QFile file(configPath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
	const QJsonObject options = root.value(QStringLiteral("settings")).toObject();
	settings.operatorName = options.value(QStringLiteral("operatorName")).toString(settings.operatorName);
	settings.directoryTemplate =
		options.value(QStringLiteral("directoryTemplate")).toString(settings.directoryTemplate);
	settings.ffmpegPath = options.value(QStringLiteral("ffmpegPath")).toString(settings.ffmpegPath);
	selectedProjectId = root.value(QStringLiteral("selectedProjectId")).toString();
	for (const QJsonValue &value : root.value(QStringLiteral("projects")).toArray()) {
		const QJsonObject object = value.toObject();
		Project project;
		project.id = object.value(QStringLiteral("id")).toString();
		project.name = object.value(QStringLiteral("name")).toString();
		project.root = object.value(QStringLiteral("root")).toString();
		project.footageDirectory = object.value(QStringLiteral("footageDirectory")).toString();
		project.sourceUuid = object.value(QStringLiteral("sourceUuid")).toString();
		project.window = object.value(QStringLiteral("window")).toString();
		project.automation = qBound(0, object.value(QStringLiteral("automation")).toInt(), 2);
		if (!project.id.isEmpty() && !project.name.isEmpty()) {
			projects.append(project);
		}
	}
}

void PoldenPanel::save()
{
	QJsonArray array;
	for (const Project &project : projects) {
		array.append(QJsonObject{{QStringLiteral("id"), project.id},
					 {QStringLiteral("name"), project.name},
					 {QStringLiteral("root"), project.root},
					 {QStringLiteral("footageDirectory"), project.footageDirectory},
					 {QStringLiteral("sourceUuid"), project.sourceUuid},
					 {QStringLiteral("window"), project.window},
					 {QStringLiteral("automation"), project.automation}});
	}
	QJsonObject options{{QStringLiteral("operatorName"), settings.operatorName},
			    {QStringLiteral("directoryTemplate"), settings.directoryTemplate},
			    {QStringLiteral("ffmpegPath"), settings.ffmpegPath}};
	QJsonObject root{{QStringLiteral("schemaVersion"), 1},
			 {QStringLiteral("selectedProjectId"), selectedProjectId},
			 {QStringLiteral("settings"), options},
			 {QStringLiteral("projects"), array}};
	QString path = configPath();
	if (path.isEmpty()) {
		return;
	}
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
		file.commit();
	}
}

PoldenPanel::Project *PoldenPanel::selectedProject()
{
	for (Project &project : projects) {
		if (project.id == selectedProjectId) {
			return &project;
		}
	}
	return nullptr;
}

const PoldenPanel::Project *PoldenPanel::selectedProject() const
{
	for (const Project &project : projects) {
		if (project.id == selectedProjectId) {
			return &project;
		}
	}
	return nullptr;
}

void PoldenPanel::refresh()
{
	QSignalBlocker projectBlocker(projectCombo);
	QSignalBlocker automationBlocker(automationCombo);
	projectCombo->clear();
	for (const Project &project : projects) {
		projectCombo->addItem(project.name, project.id);
	}
	int index = projectCombo->findData(selectedProjectId);
	if (index < 0 && !projects.isEmpty()) {
		index = 0;
		selectedProjectId = projects.front().id;
	}
	projectCombo->setCurrentIndex(index);
	const Project *project = selectedProject();
	editButton->setEnabled(project != nullptr);
	automationCombo->setEnabled(project != nullptr);
	automationCombo->setCurrentIndex(project ? project->automation : 0);
	const Job *last = lastJobForSelectedProject();
	const Job *retryable = retryableJobForSelectedProject();
	retryButton->setVisible(retryable != nullptr);
	retryButton->setText(retryable && retryable->cancelled ? tr("Resume cancelled job")
							       : tr("Retry last failed step"));
	retryButton->setEnabled(!processingFiles());
	bool pending = false;
	for (const Job &job : jobs) {
		if (!job.cancelled && job.targetLevel > 0 &&
		    (!job.converted || (job.targetLevel >= 2 && !job.copied))) {
			pending = true;
			break;
		}
	}
	cancelButton->setEnabled(pending);
	if (!project) {
		setStatus(tr("Add a game project to begin."));
	} else if (last && last->cancelled) {
		setStatus(processingFiles() ? tr("Cancelling processing…")
					    : tr("Processing cancelled. Original recordings kept."));
	} else if (last && !last->error.isEmpty()) {
		setStatus(tr("Polden: %1").arg(last->error));
	}
}

void PoldenPanel::setStatus(const QString &text)
{
	statusLabel->setText(text);
}

void PoldenPanel::applyCaptureWindow(const Project &project)
{
	if (project.sourceUuid.isEmpty() || project.window.isEmpty()) {
		return;
	}
	OBSSourceAutoRelease source = obs_get_source_by_uuid(project.sourceUuid.toUtf8().constData());
	if (!source) {
		setStatus(tr("Game Capture source is missing for %1.").arg(project.name));
		return;
	}
	OBSDataAutoRelease data = obs_source_get_settings(source);
	if (project.window == QString::fromUtf8(obs_data_get_string(data, "window")) &&
	    std::strcmp(obs_data_get_string(data, "capture_mode"), "window") == 0) {
		return;
	}
	obs_data_set_string(data, "capture_mode", "window");
	obs_data_set_string(data, "window", project.window.toUtf8().constData());
	obs_source_update(source, data);
	main->SaveProjectDeferred();
	setStatus(tr("Game Capture window selected for %1.").arg(project.name));
}

void PoldenPanel::activateSelectedCapture()
{
	if (const Project *project = selectedProject()) {
		applyCaptureWindow(*project);
	}
}

void PoldenPanel::openFilesFolder()
{
	QString directory;
	if (const Job *job = lastJobForSelectedProject()) {
		for (const QString &path : {job->copied ? job->destinationPath : QString(),
					    job->converted ? job->localPath : QString(), job->sourcePath}) {
			if (!path.isEmpty() && QFileInfo(path).isFile()) {
				directory = QFileInfo(path).absolutePath();
				break;
			}
		}
	}
	if (directory.isEmpty()) {
		const Project *project = selectedProject();
		if (project && project->automation >= 2 && !project->footageDirectory.isEmpty() &&
		    QDir(project->footageDirectory).exists()) {
			directory = project->footageDirectory;
		}
	}
	if (directory.isEmpty()) {
		auto *config = main->Config();
		const char *mode = config_get_string(config, "Output", "Mode");
		const char *type = config_get_string(config, "AdvOut", "RecType");
		const char *section = mode && std::strcmp(mode, "Advanced") == 0 ? "AdvOut" : "SimpleOutput";
		const char *key = std::strcmp(section, "AdvOut") == 0
					  ? (type && std::strcmp(type, "Standard") != 0 ? "FFFilePath" : "RecFilePath")
					  : "FilePath";
		const char *path = config_get_string(config, section, key);
		if (path) {
			directory = QString::fromUtf8(path);
		}
	}
	if (directory.isEmpty() || !QDir(directory).exists()) {
		setStatus(tr("Files folder is unavailable. Check the recording path or LucidLink connection."));
		return;
	}
	if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(directory).absoluteFilePath()))) {
		setStatus(tr("Could not open files folder: %1").arg(QDir::toNativeSeparators(directory)));
	}
}

void PoldenPanel::showProjectDialog(bool editing)
{
	Project *current = editing ? selectedProject() : nullptr;
	if (editing && !current) {
		return;
	}
	QDialog dialog(this);
	dialog.setWindowTitle(editing ? tr("Edit Polden project") : tr("Add Polden project"));
	auto *layout = new QVBoxLayout(&dialog);
	auto *form = new QFormLayout;
	auto *name = new QLineEdit(current ? current->name : QString(), &dialog);
	auto *root = new QLineEdit(current ? current->root : QString(), &dialog);
	auto *footage = new QLineEdit(current ? current->footageDirectory : QString(), &dialog);
	auto *source = new QComboBox(&dialog);
	auto *window = new QComboBox(&dialog);
	window->setEditable(true);
	for (const GameSource &item : gameSources()) {
		source->addItem(item.name, item.uuid);
	}
	if (current) {
		source->setCurrentIndex(source->findData(current->sourceUuid));
	}
	auto populateWindows = [source, window, current]() {
		QString selected = current ? current->window : QString();
		const QString uuid = source->currentData().toString();
		for (const GameSource &item : gameSources()) {
			if (item.uuid == uuid && selected.isEmpty()) {
				selected = item.window;
			}
		}
		window->clear();
		window->addItems(gameWindows(uuid));
		if (!selected.isEmpty() && window->findText(selected) < 0) {
			window->addItem(selected);
		}
		window->setCurrentText(selected);
	};
	populateWindows();
	connect(source, &QComboBox::currentIndexChanged, &dialog, populateWindows);

	auto *rootRow = new QHBoxLayout;
	rootRow->addWidget(root);
	auto *browseRoot = new QPushButton(tr("Browse…"), &dialog);
	rootRow->addWidget(browseRoot);
	auto detectPaths = [name, root, footage](bool force) {
		QString gameRoot = QDir::cleanPath(root->text().trimmed());
		if (gameRoot.isEmpty() || gameRoot == QStringLiteral(".")) {
			return;
		}
		if (name->text().trimmed().isEmpty()) {
			name->setText(QFileInfo(gameRoot).fileName());
		}
		if (force || footage->text().trimmed().isEmpty()) {
			footage->setText(QDir::toNativeSeparators(
				QDir(gameRoot).filePath(QStringLiteral("02_Assets/01_VIDEO"))));
		}
	};
	connect(browseRoot, &QPushButton::clicked, &dialog, [root, &dialog, detectPaths]() {
		QString selected =
			QFileDialog::getExistingDirectory(&dialog, QObject::tr("Game root on LucidLink"), root->text());
		if (!selected.isEmpty()) {
			root->setText(QDir::toNativeSeparators(selected));
			detectPaths(false);
		}
	});
	auto *detect = new QPushButton(tr("Detect paths"), &dialog);
	connect(root, &QLineEdit::editingFinished, &dialog, [detectPaths]() { detectPaths(false); });
	connect(detect, &QPushButton::clicked, &dialog, [detectPaths]() { detectPaths(true); });
	form->addRow(tr("Game name"), name);
	form->addRow(tr("Game root"), rootRow);
	form->addRow(QString(), detect);
	form->addRow(tr("Video folder"), footage);
	form->addRow(tr("Game Capture source"), source);
	form->addRow(tr("Capture window"), window);
	layout->addLayout(form);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
		if (name->text().trimmed().isEmpty() || root->text().trimmed().isEmpty()) {
			QMessageBox::warning(&dialog, tr("Polden project"), tr("Enter a game name and root folder."));
			return;
		}
		if (source->currentData().toString().isEmpty() || window->currentText().trimmed().isEmpty()) {
			QMessageBox::warning(&dialog, tr("Polden project"),
					     tr("Choose a Game Capture source and window."));
			return;
		}
		dialog.accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}

	Project project = current ? *current : Project{};
	if (project.id.isEmpty()) {
		project.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	}
	project.name = name->text().trimmed();
	project.root = QDir::toNativeSeparators(root->text().trimmed());
	project.footageDirectory = QDir::toNativeSeparators(footage->text().trimmed());
	project.sourceUuid = source->currentData().toString();
	project.window = window->currentText().trimmed();
	if (current) {
		*current = project;
	} else {
		projects.append(project);
	}
	selectedProjectId = project.id;
	save();
	refresh();
	applyCaptureWindow(project);
}

void PoldenPanel::showSettingsDialog()
{
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Polden settings"));
	auto *layout = new QVBoxLayout(&dialog);
	auto *tabs = new QTabWidget(&dialog);
	tabs->setObjectName(QStringLiteral("poldenSettingsTabs"));
	auto *general = new QWidget(tabs);
	auto *generalLayout = new QVBoxLayout(general);
	tabs->addTab(general, tr("General"));
	auto *markerSettings = markers ? new PoldenMarkerSettings(markers, tabs) : nullptr;
	if (markerSettings) {
		tabs->addTab(markerSettings, tr("Markers"));
	}
	layout->addWidget(tabs);
	auto *form = new QFormLayout;
	auto *operatorName = new QLineEdit(settings.operatorName, &dialog);
	auto *directoryTemplate = new QLineEdit(settings.directoryTemplate, &dialog);
	auto *filenameFormat = new QLineEdit(
		QString::fromUtf8(config_get_string(main->Config(), "Output", "FilenameFormatting")), &dialog);
	filenameFormat->setObjectName(QStringLiteral("poldenFilenameFormat"));
	filenameFormat->setEnabled(!obs_frontend_recording_active());
	filenameFormat->setToolTip(
		tr("OBS filename format, without extension. Example: Vasiliy_%CCYY-%MM-%DD-%hh-%mm-%ss. "
		   "Applies to new recordings; MP4 and markers keep the same name. Stop recording to edit."));
	auto *ffmpegPath = new QLineEdit(settings.ffmpegPath, &dialog);
	auto *browseFfmpeg = new QPushButton(tr("Browse…"), &dialog);
	auto *ffmpegRow = new QHBoxLayout;
	ffmpegRow->addWidget(ffmpegPath);
	ffmpegRow->addWidget(browseFfmpeg);
	connect(browseFfmpeg, &QPushButton::clicked, &dialog, [ffmpegPath, &dialog]() {
		QString path = QFileDialog::getOpenFileName(&dialog, QObject::tr("FFmpeg executable"),
							    ffmpegPath->text(), QObject::tr("Executables (*.exe)"));
		if (!path.isEmpty()) {
			ffmpegPath->setText(path);
		}
	});
	auto *preview = new QLabel(&dialog);
	preview->setWordWrap(true);
	auto updatePreview = [=]() {
		QString gameRoot = selectedProject() ? selectedProject()->root : QStringLiteral("L:/Meowgic");
		QString footage = selectedProject() ? selectedProject()->footageDirectory
						    : QStringLiteral("L:/Meowgic/02_Assets/01_VIDEO");
		QString path = previewPath(directoryTemplate->text(), gameRoot, footage, operatorName->text(),
					   QDateTime::currentDateTime());
		const bool advanced = qstricmp(config_get_string(main->Config(), "Output", "Mode"), "Advanced") == 0;
		const bool noSpace = config_get_bool(main->Config(), advanced ? "AdvOut" : "SimpleOutput",
						     advanced ? "RecFileNameWithoutSpace" : "FileNameWithoutSpace");
		BPtr<char> formatted =
			os_generate_formatted_filename("mp4", !noSpace, filenameFormat->text().toUtf8().constData());
		QString name = QFileInfo(QString::fromUtf8(formatted.Get())).fileName();
		preview->setText(tr("Example: %1").arg(QDir(path).filePath(name)));
	};
	connect(directoryTemplate, &QLineEdit::textChanged, &dialog, updatePreview);
	connect(operatorName, &QLineEdit::textChanged, &dialog, updatePreview);
	connect(filenameFormat, &QLineEdit::textChanged, &dialog, updatePreview);
	updatePreview();
	form->addRow(tr("Recorder name"), operatorName);
	form->addRow(tr("Video folder template"), directoryTemplate);
	form->addRow(tr("Recording / MP4 filename"), filenameFormat);
	auto *filenameHelp = new QLabel(tr("Without extension. %CCYY-%MM-%DD = date, %hh-%mm-%ss = time. "
					   "Uses the active OBS profile's current format."),
					&dialog);
	filenameHelp->setWordWrap(true);
	form->addRow(QString(), filenameHelp);
	form->addRow(tr("FFmpeg"), ffmpegRow);
	generalLayout->addLayout(form);
	generalLayout->addWidget(preview);
	generalLayout->addStretch();
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
		if (filenameFormat->text().trimmed().isEmpty()) {
			tabs->setCurrentWidget(general);
			filenameFormat->setFocus();
			QMessageBox::warning(&dialog, tr("Filename"), tr("Enter a recording filename format."));
			return;
		}
		if (markerSettings && !markerSettings->validate()) {
			tabs->setCurrentWidget(markerSettings);
			return;
		}
		dialog.accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	settings.operatorName = operatorName->text().trimmed();
	settings.directoryTemplate = directoryTemplate->text().trimmed();
	settings.ffmpegPath = ffmpegPath->text().trimmed();
	if (filenameFormat->isEnabled()) {
		config_set_string(main->Config(), "Output", "FilenameFormatting",
				  filenameFormat->text().trimmed().toUtf8().constData());
		config_save_safe(main->Config(), "tmp", nullptr);
	}
	if (markerSettings) {
		markerSettings->apply();
	}
	save();
	refreshMarkerButtons();
}
