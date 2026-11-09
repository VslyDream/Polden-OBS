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
#include <utility/PoldenTemplates.hpp>
#include <QButtonGroup>
#include <QActionGroup>
#include <QFrame>
#include <QMenu>
#include <QProgressBar>
#include <QTimer>
#include <QStyle>
#include <QScrollArea>

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

QString durationText(qint64 ms)
{
	return QStringLiteral("%1:%2:%3")
		.arg(ms / 3600000)
		.arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
		.arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'));
}

const char *poldenStyle = R"(
#poldenDock, QDialog#poldenDialog { background: #272a33; color: #fff; }
#poldenDock QLabel, #poldenDialog QLabel { background: transparent; }
#poldenDock QPushButton, #poldenDock QToolButton, #poldenDialog QPushButton { background: #3c404b; color: white; border: 1px solid transparent; border-radius: 4px; padding: 5px 8px; min-height: 20px; }
#poldenDock QPushButton:hover, #poldenDock QToolButton:hover, #poldenDialog QPushButton:hover { background: #484e5c; }
#poldenDock QPushButton:disabled { color: #858994; background: #30333c; }
#poldenDock QPushButton:checked, #poldenDialog QPushButton:checked { background: #3a57c9; border-color: #5a7be0; }
#poldenDock QFrame#poldenTakeCard, #poldenDock QFrame#poldenEmptyGame { background: #1d1f26; border: 1px solid #33363f; border-radius: 6px; }
#poldenDock QLabel[poldenMuted="true"], #poldenDialog QLabel[poldenMuted="true"] { color: #9a9fab; font-size: 12px; }
#poldenDock QProgressBar { border: 0; background: #33363f; border-radius: 3px; max-height: 6px; min-height: 6px; }
#poldenDock QProgressBar::chunk { background: #72c89a; border-radius: 3px; }
#poldenDock QToolButton#poldenUpdate { background: #294438; border: 1px solid #4d8267; color: #9ee0bb; border-radius: 12px; font-size: 12px; }
#poldenDialog QLineEdit, #poldenDialog QComboBox { background: #1d1f26; border: 1px solid #3c404b; border-radius: 4px; padding: 6px; color: white; }
#poldenDialog QPushButton#poldenDeleteGame, #poldenDialog QPushButton#poldenConfirmDelete { color: #ff8a8a; border-color: #c4343d; }
)";

QString friendlyWindow(const QString &value)
{
	const auto parts = value.split(QLatin1Char(':'));
	return parts.size() >= 3 ? parts.front() + QStringLiteral(" / ") + parts.back() : value;
}
} // namespace

PoldenPanel::PoldenPanel(OBSBasic *main) : QWidget(main), main(main)
{
	setObjectName(QStringLiteral("poldenDock"));
	setStyleSheet(QString::fromUtf8(poldenStyle));
	setMinimumWidth(300);
	settings.ffmpegPath = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
	load();
	loadJobs();
	auto *outerLayout = new QVBoxLayout(this);
	outerLayout->setContentsMargins(0, 0, 0, 0);
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	auto *contents = new QWidget(scroll);
	contents->setObjectName(QStringLiteral("poldenDockContents"));
	scroll->setWidget(contents);
	outerLayout->addWidget(scroll);
	auto *layout = new QVBoxLayout(contents);
	layout->setContentsMargins(12, 12, 12, 8);
	layout->setSpacing(12);
	auto *projectRow = new QHBoxLayout;
	projectButton = new QToolButton(this);
	projectButton->setObjectName(QStringLiteral("poldenGame"));
	projectButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	projectButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	projectButton->setAccessibleName(tr("Game"));
	projectButton->setPopupMode(QToolButton::InstantPopup);
	projectMenu = new QMenu(projectButton);
	projectButton->setMenu(projectMenu);
	auto *settingsButton = new QToolButton(this);
	settingsButton->setText(QString::fromUtf8("⚙"));
	settingsButton->setObjectName(QStringLiteral("poldenSettings"));
	settingsButton->setToolTip(tr("Polden settings"));
	settingsButton->setAccessibleName(settingsButton->toolTip());
	settingsButton->setFixedSize(32, 32);
	projectRow->addWidget(projectButton, 1);
	projectRow->addWidget(settingsButton);
	layout->addLayout(projectRow);
	emptyGame = new QFrame(this);
	emptyGame->setObjectName(QStringLiteral("poldenEmptyGame"));
	auto *emptyLayout = new QVBoxLayout(emptyGame);
	auto *invitation = new QLabel(tr("Add your first game"), emptyGame);
	auto *addButton = new QPushButton(tr("Add game…"), emptyGame);
	emptyLayout->addWidget(invitation);
	emptyLayout->addWidget(addButton);
	layout->addWidget(emptyGame);
	automationLabel = new QLabel(tr("After recording"), this);
	automationLabel->setProperty("poldenMuted", true);
	layout->addWidget(automationLabel);
	auto *automationRow = new QHBoxLayout;
	automationRow->setSpacing(3);
	automationButtons = new QButtonGroup(this);
	const QStringList modes = {tr("Keep file"), tr("Convert MP4"), tr("MP4 → Lucid")};
	for (int level = 0; level < 3; ++level) {
		auto *button = new QPushButton(modes[level], this);
		button->setObjectName(QStringLiteral("poldenMode%1").arg(level));
		button->setCheckable(true);
		button->setStyleSheet(QStringLiteral("font-size: 12px; padding: 4px 3px;"));
		button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		automationButtons->addButton(button, level);
		automationRow->addWidget(button, 1);
	}
	layout->addLayout(automationRow);
	markerButtonsWidget = new QWidget(this);
	markerButtonsWidget->setObjectName(QStringLiteral("poldenMarkerButtons"));
	new FlowLayout(markerButtonsWidget, 0, 6, 6);
	layout->addWidget(markerButtonsWidget);
	auto *card = new QFrame(this);
	card->setObjectName(QStringLiteral("poldenTakeCard"));
	auto *cardLayout = new QVBoxLayout(card);
	cardLayout->setContentsMargins(12, 12, 12, 12);
	cardLayout->setSpacing(8);
	auto *head = new QHBoxLayout;
	takeTitle = new QLabel(card);
	takeTitle->setObjectName(QStringLiteral("poldenTakeTitle"));
	takeTitle->setWordWrap(true);
	takeTitle->setStyleSheet(QStringLiteral("font-weight: 600;"));
	deleteButton = new QToolButton(card);
	deleteButton->setObjectName(QStringLiteral("poldenDeleteLastTake"));
	deleteButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
	deleteButton->setToolTip(tr("Delete last take…"));
	deleteButton->setAccessibleName(deleteButton->toolTip());
	head->addWidget(takeTitle, 1);
	head->addWidget(deleteButton, 0, Qt::AlignTop);
	cardLayout->addLayout(head);
	takeFile = new QLabel(card);
	takeFile->setObjectName(QStringLiteral("poldenTakeFile"));
	takeFile->setWordWrap(false);
	takeFile->setTextFormat(Qt::PlainText);
	takeFile->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	cardLayout->addWidget(takeFile);
	takeDetails = new QLabel(card);
	takeDetails->setProperty("poldenMuted", true);
	takeDetails->setTextFormat(Qt::PlainText);
	takeDetails->setWordWrap(true);
	cardLayout->addWidget(takeDetails);
	takeProgress = new QProgressBar(card);
	takeProgress->setTextVisible(false);
	cardLayout->addWidget(takeProgress);
	auto *actions = new QHBoxLayout;
	retryButton = new QPushButton(tr("Retry"), card);
	retryButton->setObjectName(QStringLiteral("poldenRetry"));
	cancelButton = new QPushButton(tr("Cancel"), card);
	cancelButton->setObjectName(QStringLiteral("poldenCancelProcessing"));
	openFolderButton = new QPushButton(tr("Open folder"), card);
	openFolderButton->setObjectName(QStringLiteral("poldenOpenFilesFolder"));
	actions->addWidget(retryButton);
	actions->addWidget(cancelButton);
	actions->addWidget(openFolderButton, 1);
	cardLayout->addLayout(actions);
	layout->addWidget(card);
	backgroundProcessing = new QWidget(this);
	auto *backgroundRow = new QHBoxLayout(backgroundProcessing);
	backgroundRow->setContentsMargins(0, 0, 0, 0);
	backgroundLabel = new QLabel(backgroundProcessing);
	backgroundLabel->setProperty("poldenMuted", true);
	backgroundLabel->setWordWrap(true);
	auto *backgroundCancel = new QToolButton(backgroundProcessing);
	backgroundCancel->setText(QStringLiteral("×"));
	backgroundCancel->setToolTip(tr("Cancel processing"));
	backgroundRow->addWidget(backgroundLabel, 1);
	backgroundRow->addWidget(backgroundCancel);
	layout->addWidget(backgroundProcessing);
	statusLabel = new QLabel(this);
	statusLabel->setWordWrap(true);
	statusLabel->setTextFormat(Qt::PlainText);
	statusLabel->hide();
	layout->addWidget(statusLabel);
	layout->addStretch();
	auto *footer = new QHBoxLayout;
	auto *version = new QLabel(QStringLiteral("Polden " POLDEN_VERSION), this);
	version->setProperty("poldenMuted", true);
	footer->addWidget(version);
	footer->addStretch();
	auto *updateButton = new QToolButton(this);
	updateButton->setObjectName(QStringLiteral("poldenUpdate"));
	updateButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	updateButton->setAccessibleName(tr("Update Polden"));
	footer->addWidget(updateButton);
	updater = new PoldenUpdater(this, updateButton);
	auto *bugReportButton = new QToolButton(this);
	bugReportButton->setObjectName(QStringLiteral("poldenBugReport"));
	bugReportButton->setIcon(QIcon(QStringLiteral(":/res/images/bug-report.svg")));
	bugReportButton->setToolTip(tr("Report a bug — @Vsly_Dream"));
	bugReportButton->setAccessibleName(tr("Report a bug"));
	footer->addWidget(bugReportButton);
	layout->addLayout(footer);
	connect(bugReportButton, &QToolButton::clicked, this,
		[]() { QDesktopServices::openUrl(QUrl(QStringLiteral("https://t.me/Vsly_Dream"))); });
	connect(addButton, &QPushButton::clicked, this, [this]() { showProjectDialog(false); });
	connect(settingsButton, &QToolButton::clicked, this, [this]() { showSettingsDialog(); });
	connect(openFolderButton, &QPushButton::clicked, this, &PoldenPanel::openFilesFolder);
	connect(deleteButton, &QToolButton::clicked, this, &PoldenPanel::deleteLastTake);
	connect(cancelButton, &QPushButton::clicked, this, &PoldenPanel::cancelProcessing);
	connect(backgroundCancel, &QToolButton::clicked, this, &PoldenPanel::cancelProcessing);
	connect(retryButton, &QPushButton::clicked, this, [this]() {
		if (Job *job = retryableJobForSelectedProject()) {
			job->error.clear();
			saveJobs();
			refresh();
			pump();
		}
	});
	connect(automationButtons, &QButtonGroup::idClicked, this, [this](int level) {
		if (Project *project = selectedProject()) {
			project->automation = level;
			save();
			refresh();
		}
	});
	auto *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &PoldenPanel::refreshTake);
	timer->start(500);
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
			auto *button = new QPushButton(PoldenMarkers::colorIcon(color), QString::number(color + 1),
						       markerButtonsWidget);
			button->setObjectName(QStringLiteral("poldenMarkerButton%1").arg(color));
			button->setToolTip(tr("Add marker: %1\n%2")
						   .arg(PoldenMarkers::colorName(color), markers->shortcutText(color)));
			button->setFixedSize(48, 32);
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
	settings.videoSubfolder = options.value(QStringLiteral("videoSubfolder")).toString(settings.videoSubfolder);
	settings.gameNameTemplate =
		options.value(QStringLiteral("gameNameTemplate")).toString(settings.gameNameTemplate);
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
			    {QStringLiteral("ffmpegPath"), settings.ffmpegPath},
			    {QStringLiteral("videoSubfolder"), settings.videoSubfolder},
			    {QStringLiteral("gameNameTemplate"), settings.gameNameTemplate}};
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
	if (!selectedProject() && !projects.isEmpty()) {
		selectedProjectId = projects.front().id;
	}
	const Project *project = selectedProject();
	const QStringList modes = {tr("Keep file"), tr("Convert MP4"), tr("MP4 → Lucid")};
	// Rebuild only on user/config changes, never on progress/timer updates while the menu is open.
	projectMenu->clear();
	for (const Project &item : projects) {
		auto *action = projectMenu->addAction(item.name + QStringLiteral(" · ") + modes[item.automation]);
		action->setCheckable(true);
		action->setChecked(item.id == selectedProjectId);
		connect(action, &QAction::triggered, this, [this, id = item.id]() {
			selectedProjectId = id;
			if (const Project *selected = selectedProject(); selected && !recording) {
				applyCaptureWindow(*selected);
			}
			applyFilenameTemplate();
			save();
			// An action is still dispatching; let its menu close before rebuilding it.
			QTimer::singleShot(0, this, [this]() { refresh(); });
		});
	}
	projectMenu->addSeparator();
	projectMenu->addAction(tr("Add game…"), this, [this]() { showProjectDialog(false); });
	auto *edit = projectMenu->addAction(project ? tr("Edit “%1”…").arg(project->name) : tr("Edit game…"), this,
					    [this]() { showProjectDialog(true); });
	edit->setEnabled(project != nullptr);
	auto *folder = projectMenu->addAction(tr("Open video folder"), this, [this]() {
		const Project *selected = selectedProject();
		if (selected && QDir(selected->footageDirectory).exists()) {
			QDesktopServices::openUrl(QUrl::fromLocalFile(selected->footageDirectory));
		} else {
			setStatus(tr("Video folder is unavailable."));
		}
	});
	folder->setEnabled(project != nullptr);
	projectButton->setText(project ? project->name + QStringLiteral(" ▾") : tr("Choose game ▾"));
	emptyGame->setVisible(!project);
	for (auto *button : automationButtons->buttons()) {
		button->setEnabled(project != nullptr);
		button->setChecked(automationButtons->id(button) == (project ? project->automation : 0));
	}
	refreshTake();
}

void PoldenPanel::refreshTake()
{
	automationLabel->setText(recording ? tr("After recording · applies to the next take") : tr("After recording"));
	Job *last = lastJobForSelectedProject();
	takeFile->setVisible(recording || last);
	takeDetails->setVisible(recording ? !lastMarker.isEmpty() : last != nullptr);
	Job *active = jobById(activeJobId);
	const bool busy = active && processingFiles();
	backgroundProcessing->setVisible(recording && busy);
	const QString step = copying ? tr("Copying to LucidLink") : tr("Converting to MP4");
	const QString percent = processingProgress < 0 ? QString() : QStringLiteral(" · %1%").arg(processingProgress);
	backgroundLabel->setText(step + percent);
	takeProgress->setVisible(false);
	retryButton->setVisible(false);
	cancelButton->setVisible(false);
	deleteButton->setVisible(!recording && last);
	deleteButton->setEnabled(pendingDeleteIds.isEmpty());
	openFolderButton->setVisible(!recording);
	if (recording) {
		takeTitle->setText(
			tr("● Recording · %1")
				.arg(durationText(markers ? markers->elapsedMs()
							  : recordingStartedAt.msecsTo(QDateTime::currentDateTime()))));
		BPtr<char> currentPath = obs_frontend_get_last_recording();
		const QString currentFile = currentPath ? QFileInfo(QString::fromUtf8(currentPath.Get())).fileName()
							: QString();
		takeFile->setToolTip(currentFile);
		takeFile->setText(takeFile->fontMetrics().elidedText(
			currentFile.isEmpty() ? tr("Recording…") : currentFile, Qt::ElideRight, takeFile->width()));
		takeDetails->setText(lastMarker);
		return;
	}
	if (!last) {
		takeTitle->setText(tr("No recordings yet"));
		takeFile->clear();
		takeDetails->clear();
		return;
	}

	auto sameTake = [](const Job &a, const Job &b) {
		return a.projectId == b.projectId && a.takeId == b.takeId;
	};
	Job *shown = last;
	int queued = 0;
	for (Job &job : jobs) {
		if (sameTake(job, *last) && (job.cancelled || !job.error.isEmpty())) {
			shown = &job;
		}
		if (!job.cancelled && job.error.isEmpty() && job.targetLevel > 0 &&
		    (!job.converted || (job.targetLevel >= 2 && !job.copied)) && job.id != activeJobId) {
			++queued;
		}
	}
	if (busy && sameTake(*active, *last)) {
		shown = active;
	}
	const QString fileName = QFileInfo(shown->targetLevel == 0 ? shown->sourcePath : shown->localPath).fileName();
	takeFile->setToolTip(fileName);
	takeFile->setText(takeFile->fontMetrics().elidedText(fileName, Qt::ElideRight, takeFile->width()));
	if (!pendingDeleteIds.isEmpty() && pendingDeleteIds.contains(last->id)) {
		takeTitle->setText(tr("Deleting take…"));
		takeDetails->setText(tr("Waiting for processing to stop."));
	} else if (shown->cancelled) {
		takeTitle->setText(tr("⊘ Processing cancelled"));
		takeDetails->setText(tr("Original recording kept"));
	} else if (!shown->error.isEmpty()) {
		takeTitle->setText(shown->converted ? tr("⚠ Copy failed") : tr("⚠ Conversion failed"));
		takeDetails->setText(shown->error);
		retryButton->setVisible(true);
		retryButton->setEnabled(!processingFiles());
	} else if (busy && sameTake(*active, *last)) {
		takeTitle->setText(step + percent);
		takeDetails->setText(tr("Step %1 of %2 · %3 more in queue")
					     .arg(copying ? 2 : 1)
					     .arg(active->targetLevel)
					     .arg(queued));
		takeProgress->setVisible(true);
		takeProgress->setRange(0, processingProgress < 0 ? 0 : 100);
		if (processingProgress >= 0) {
			takeProgress->setValue(processingProgress);
		}
		cancelButton->setVisible(true);
	} else if (last->targetLevel > 0 && (!last->converted || (last->targetLevel >= 2 && !last->copied))) {
		takeTitle->setText(tr("Waiting for processing"));
		takeDetails->setText(tr("%1 in queue").arg(queued));
		cancelButton->setVisible(true);
	} else {
		takeTitle->setText(last->copied      ? tr("✓ Copied to LucidLink")
				   : last->converted ? tr("✓ Converted")
						     : tr("✓ Saved"));
		takeDetails->setText(tr("%1 · %2 markers").arg(durationText(last->durationMs)).arg(last->markerCount));
	}
}

void PoldenPanel::setStatus(const QString &text)
{
	if (recording && text.startsWith(tr("Marker "))) {
		lastMarker = text;
		refreshTake();
	}
	// Operational state belongs to the card; this label is for actionable notices.
	statusLabel->setText(text);
	statusLabel->setVisible(text.contains(tr("Could not")) || text.contains(tr("unavailable")));
}

QString PoldenPanel::filenameTemplate() const
{
	const QString stored = QString::fromUtf8(config_get_string(main->Config(), "Polden", "FilenameTemplate"));
	const QString applied =
		QString::fromUtf8(config_get_string(main->Config(), "Polden", "AppliedFilenameFormatting"));
	const QString current = QString::fromUtf8(config_get_string(main->Config(), "Output", "FilenameFormatting"));
	// An explicit edit through OBS Output settings takes precedence over our saved template.
	return !stored.isEmpty() && current == applied ? stored : PoldenTemplates::fromObs(current);
}

void PoldenPanel::applyFilenameTemplate()
{
	if (recording || obs_frontend_recording_active()) {
		return;
	}
	const QString text = filenameTemplate();
	const QString format = PoldenTemplates::toObs(text, settings.operatorName,
						      selectedProject() ? selectedProject()->name : QString());
	config_set_string(main->Config(), "Polden", "FilenameTemplate", text.toUtf8().constData());
	config_set_string(main->Config(), "Polden", "AppliedFilenameFormatting", format.toUtf8().constData());
	config_set_string(main->Config(), "Output", "FilenameFormatting", format.toUtf8().constData());
	config_save_safe(main->Config(), "tmp", nullptr);
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
	applyFilenameTemplate();
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
	const Project *selected = editing ? selectedProject() : nullptr;
	if (editing && !selected) {
		return;
	}
	// Keep a value copy while the nested event loop can complete pipeline jobs.
	const Project original = selected ? *selected : Project{};
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("poldenDialog"));
	dialog.setStyleSheet(QString::fromUtf8(poldenStyle));
	dialog.setWindowTitle(editing ? tr("Edit game") : tr("Add game"));
	dialog.resize(600, 380);
	auto *layout = new QVBoxLayout(&dialog);
	auto *form = new QFormLayout;
	auto *name = new QLineEdit(original.name, &dialog);
	auto *root = new QLineEdit(original.root, &dialog);
	auto *footage = new QLineEdit(original.footageDirectory, &dialog);
	root->setObjectName(QStringLiteral("poldenGameFolder"));
	name->setObjectName(QStringLiteral("poldenGameName"));
	footage->setObjectName(QStringLiteral("poldenVideoFolder"));
	auto *source = new QComboBox(&dialog);
	auto *window = new QComboBox(&dialog);
	const auto sources = gameSources();
	for (const GameSource &item : sources) {
		source->addItem(item.name, item.uuid);
	}
	if (editing) {
		source->setCurrentIndex(source->findData(original.sourceUuid));
	}
	auto populateWindows = [source, window, original]() {
		QString current = source->currentData().toString() == original.sourceUuid ? original.window : QString();
		const QString uuid = source->currentData().toString();
		for (const GameSource &item : gameSources()) {
			if (item.uuid == uuid && current.isEmpty()) {
				current = item.window;
			}
		}
		window->clear();
		for (const QString &raw : gameWindows(uuid)) {
			window->addItem(friendlyWindow(raw), raw);
		}
		if (!current.isEmpty() && window->findData(current) < 0) {
			window->addItem(friendlyWindow(current), current);
		}
		window->setCurrentIndex(window->findData(current));
	};
	populateWindows();
	connect(source, &QComboBox::currentIndexChanged, &dialog, populateWindows);
	auto *rootRow = new QHBoxLayout;
	rootRow->addWidget(root);
	auto *browseRoot = new QPushButton(tr("Browse…"), &dialog);
	rootRow->addWidget(browseRoot);
	auto detectPaths = [this, name, root, footage](bool force, bool rename = false) {
		const QString path = QDir::cleanPath(QDir::fromNativeSeparators(root->text().trimmed()));
		if (path.isEmpty() || path == QStringLiteral(".")) {
			return;
		}
		if (rename || name->text().trimmed().isEmpty()) {
			name->setText(PoldenTemplates::gameName(settings.gameNameTemplate, path));
		}
		if (force || footage->text().trimmed().isEmpty()) {
			footage->setText(QDir::toNativeSeparators(
				QDir(path).filePath(QDir::fromNativeSeparators(settings.videoSubfolder))));
		}
	};
	connect(browseRoot, &QPushButton::clicked, &dialog, [root, &dialog, detectPaths]() {
		const QString path = QFileDialog::getExistingDirectory(&dialog, QObject::tr("Game folder on LucidLink"),
								       root->text());
		if (!path.isEmpty()) {
			root->setText(QDir::toNativeSeparators(path));
			detectPaths(true, true);
		}
	});
	auto *footageRow = new QHBoxLayout;
	footageRow->addWidget(footage);
	auto *detect = new QPushButton(QStringLiteral("↺"), &dialog);
	detect->setToolTip(tr("Recalculate from game folder layout"));
	footageRow->addWidget(detect);
	connect(root, &QLineEdit::editingFinished, &dialog, [detectPaths]() { detectPaths(false); });
	connect(detect, &QPushButton::clicked, &dialog, [detectPaths]() { detectPaths(true); });
	form->addRow(tr("Game folder on LucidLink"), rootRow);
	form->addRow(tr("Game name"), name);
	form->addRow(tr("Video folder"), footageRow);
	form->addRow(tr("Game window"), window);
	if (sources.size() > 1) {
		form->addRow(tr("Source"), source);
	} else {
		source->hide();
		form->addRow(tr("Source"),
			     new QLabel(sources.isEmpty() ? tr("No Game Capture source — add one in OBS")
							  : tr("Game Capture · %1").arg(sources.front().name),
					&dialog));
	}
	layout->addLayout(form);
	layout->addStretch();
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
	buttons->button(QDialogButtonBox::Save)->setText(editing ? tr("Save") : tr("Add game"));
	if (editing) {
		auto *remove = new QPushButton(tr("Delete game…"), &dialog);
		remove->setObjectName(QStringLiteral("poldenDeleteGame"));
		buttons->addButton(remove, QDialogButtonBox::DestructiveRole);
		connect(remove, &QPushButton::clicked, &dialog, [this, &dialog, original]() {
			QMessageBox question(QMessageBox::Question, tr("Delete game"),
					     tr("Delete “%1” from the game list? Files and existing jobs will be kept.")
						     .arg(original.name),
					     QMessageBox::Yes | QMessageBox::Cancel, &dialog);
			question.setDefaultButton(QMessageBox::Cancel);
			question.setEscapeButton(QMessageBox::Cancel);
			if (question.exec() != QMessageBox::Yes) {
				return;
			}
			projects.removeIf([&](const Project &item) { return item.id == original.id; });
			selectedProjectId = projects.isEmpty() ? QString() : projects.front().id;
			save();
			refresh();
			applyFilenameTemplate();
			dialog.reject();
		});
	}
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
		if (name->text().trimmed().isEmpty() || root->text().trimmed().isEmpty() ||
		    footage->text().trimmed().isEmpty()) {
			QMessageBox::warning(&dialog, tr("Game"),
					     tr("Enter a game name, game folder and video folder."));
			return;
		}
		if (source->currentData().toString().isEmpty() || window->currentData().toString().isEmpty()) {
			QMessageBox::warning(&dialog, tr("Game"), tr("Choose a Game Capture source and window."));
			return;
		}
		dialog.accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	Project project = original;
	if (project.id.isEmpty()) {
		project.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	}
	project.name = name->text().trimmed();
	project.root = QDir::toNativeSeparators(root->text().trimmed());
	project.footageDirectory = QDir::toNativeSeparators(footage->text().trimmed());
	project.sourceUuid = source->currentData().toString();
	project.window = window->currentData().toString();
	bool replaced = false;
	for (Project &item : projects) {
		if (item.id == project.id) {
			item = project;
			replaced = true;
			break;
		}
	}
	if (!replaced) {
		projects.append(project);
	}
	selectedProjectId = project.id;
	save();
	refresh();
	if (!recording) {
		applyCaptureWindow(project);
	}
	applyFilenameTemplate();
}

void PoldenPanel::showSettingsDialog()
{
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("poldenDialog"));
	dialog.setStyleSheet(QString::fromUtf8(poldenStyle));
	dialog.setWindowTitle(tr("Polden settings"));
	dialog.resize(640, 570);
	auto *layout = new QVBoxLayout(&dialog);
	auto *tabs = new QTabWidget(&dialog);
	tabs->setObjectName(QStringLiteral("poldenSettingsTabs"));
	auto *files = new QWidget(tabs);
	auto *filesLayout = new QVBoxLayout(files);
	auto *scroll = new QScrollArea(tabs);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidget(files);
	tabs->addTab(scroll, tr("Files"));
	auto *markerSettings = markers ? new PoldenMarkerSettings(markers, tabs) : nullptr;
	if (markerSettings) {
		tabs->addTab(markerSettings, tr("Markers"));
	}
	layout->addWidget(tabs);
	auto *form = new QFormLayout;
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	auto *operatorName = new QLineEdit(settings.operatorName, files);
	operatorName->setObjectName(QStringLiteral("poldenOperatorName"));
	auto *directoryTemplate = new QLineEdit(settings.directoryTemplate, files);
	auto *filenameFormat = new QLineEdit(filenameTemplate(), files);
	filenameFormat->setObjectName(QStringLiteral("poldenFilenameFormat"));
	filenameFormat->setEnabled(!obs_frontend_recording_active());
	filenameFormat->setToolTip(tr("Without extension. Changes apply to the next recording."));
	auto *recordingGuard = new QTimer(&dialog);
	connect(recordingGuard, &QTimer::timeout, &dialog,
		[filenameFormat]() { filenameFormat->setEnabled(!obs_frontend_recording_active()); });
	recordingGuard->start(250);
	auto addTokens = [files](QLineEdit *edit, const QStringList &labels, const QStringList &tokens) {
		auto *row = new QHBoxLayout;
		row->setSpacing(4);
		for (int i = 0; i < labels.size(); ++i) {
			auto *button = new QPushButton(labels[i], files);
			button->setEnabled(edit->isEnabled());
			QObject::connect(button, &QPushButton::clicked, files, [edit, token = tokens[i]]() {
				if (!edit->isEnabled()) {
					return;
				}
				edit->insert(token);
				edit->setFocus();
			});
			row->addWidget(button);
		}
		row->addStretch();
		return row;
	};
	form->addRow(tr("Your name"), operatorName);
	form->addRow(tr("File name"), filenameFormat);
	form->addRow(QString(), addTokens(filenameFormat, {tr("Name"), tr("Date"), tr("Time"), tr("Game")},
					  {QStringLiteral("{name}"), QStringLiteral("{date}"), QStringLiteral("{time}"),
					   QStringLiteral("{game}")}));
	form->addRow(tr("Folder on LucidLink"), directoryTemplate);
	form->addRow(QString(),
		     addTokens(directoryTemplate, {tr("Video folder"), tr("Game folder"), tr("Date"), tr("Name")},
			       {QStringLiteral("{video folder}"), QStringLiteral("{game folder}"),
				QStringLiteral("{date}"), QStringLiteral("{name}")}));
	filesLayout->addLayout(form);
	auto *preview = new QLabel(files);
	preview->setObjectName(QStringLiteral("poldenPathPreview"));
	preview->setTextFormat(Qt::PlainText);
	preview->setWordWrap(true);
	preview->setMinimumWidth(100);
	auto updatePreview = [=]() {
		const QString gameRoot = selectedProject() ? selectedProject()->root : QStringLiteral("L:/Game");
		const QString footage = selectedProject() ? selectedProject()->footageDirectory
							  : QStringLiteral("L:/Game/02_Assets/01_VIDEO");
		const QString directory = PoldenTemplates::folder(directoryTemplate->text(), gameRoot, footage,
								  operatorName->text(), QDateTime::currentDateTime());
		const QString format = PoldenTemplates::toObs(filenameFormat->text(), operatorName->text(),
							      selectedProject() ? selectedProject()->name : tr("Game"));
		const bool advanced = qstricmp(config_get_string(main->Config(), "Output", "Mode"), "Advanced") == 0;
		const bool noSpace = config_get_bool(main->Config(), advanced ? "AdvOut" : "SimpleOutput",
						     advanced ? "RecFileNameWithoutSpace" : "FileNameWithoutSpace");
		BPtr<char> formatted = os_generate_formatted_filename("mp4", !noSpace, format.toUtf8().constData());
		const QString name = QFileInfo(QString::fromUtf8(formatted.Get())).fileName();
		preview->setText(tr("Next recording on LucidLink\n%1\n\nMarkers go next to it as %2.txt")
					 .arg(QDir::toNativeSeparators(QDir(directory).filePath(name)),
					      QFileInfo(name).completeBaseName()));
	};
	connect(directoryTemplate, &QLineEdit::textChanged, &dialog, updatePreview);
	connect(operatorName, &QLineEdit::textChanged, &dialog, updatePreview);
	connect(filenameFormat, &QLineEdit::textChanged, &dialog, updatePreview);
	updatePreview();
	filesLayout->addWidget(preview);
	auto *advancedToggle = new QPushButton(tr("▸ Advanced"), files);
	advancedToggle->setCheckable(true);
	filesLayout->addWidget(advancedToggle);
	auto *advanced = new QWidget(files);
	auto *advancedForm = new QFormLayout(advanced);
	auto *subfolder = new QLineEdit(QDir::toNativeSeparators(settings.videoSubfolder), advanced);
	auto *gameName = new QLineEdit(settings.gameNameTemplate, advanced);
	subfolder->setObjectName(QStringLiteral("poldenVideoSubfolder"));
	gameName->setObjectName(QStringLiteral("poldenGameNameTemplate"));
	auto *help = new QLabel(
		tr("Game folder layout. Applied when picking a folder or pressing ↺. Existing games keep their paths. {folder} is the selected folder name."),
		advanced);
	help->setWordWrap(true);
	advancedForm->addRow(help);
	advancedForm->addRow(tr("Video folder"), subfolder);
	advancedForm->addRow(tr("Game name"), gameName);
	auto *reset = new QPushButton(tr("Reset to defaults"), advanced);
	connect(reset, &QPushButton::clicked, &dialog, [subfolder, gameName]() {
		subfolder->setText(QDir::toNativeSeparators(QStringLiteral("02_Assets/01_VIDEO")));
		gameName->setText(QStringLiteral("{folder}"));
	});
	advancedForm->addRow(QString(), reset);
	auto *ffmpegPath = new QLineEdit(settings.ffmpegPath, advanced);
	auto *browseFfmpeg = new QPushButton(tr("Browse…"), advanced);
	auto *ffmpegRow = new QHBoxLayout;
	ffmpegRow->addWidget(ffmpegPath);
	ffmpegRow->addWidget(browseFfmpeg);
	connect(browseFfmpeg, &QPushButton::clicked, &dialog, [ffmpegPath, &dialog]() {
		const QString path =
			QFileDialog::getOpenFileName(&dialog, QObject::tr("FFmpeg executable"), ffmpegPath->text());
		if (!path.isEmpty()) {
			ffmpegPath->setText(path);
		}
	});
	advancedForm->addRow(tr("FFmpeg"), ffmpegRow);
	filesLayout->addWidget(advanced);
	advanced->hide();
	connect(advancedToggle, &QPushButton::toggled, &dialog, [advanced, advancedToggle](bool open) {
		advanced->setVisible(open);
		advancedToggle->setText(open ? QObject::tr("▾ Advanced") : QObject::tr("▸ Advanced"));
	});
	filesLayout->addStretch();
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
	layout->addWidget(buttons);
	if (markerSettings) {
		markerSettings->setValidityChanged(
			[buttons](bool valid) { buttons->button(QDialogButtonBox::Save)->setEnabled(valid); });
	}
	connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
		if (filenameFormat->text().trimmed().isEmpty() || directoryTemplate->text().trimmed().isEmpty() ||
		    gameName->text().trimmed().isEmpty() || !PoldenTemplates::validVideoSubfolder(subfolder->text())) {
			tabs->setCurrentIndex(0);
			QMessageBox::warning(
				&dialog, tr("Files"),
				tr("Enter filename and folder templates. Video folder must be a relative path inside the game folder."));
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
	settings.videoSubfolder = QDir::fromNativeSeparators(subfolder->text().trimmed());
	settings.gameNameTemplate = gameName->text().trimmed();
	if (filenameFormat->isEnabled() && !obs_frontend_recording_active()) {
		const QString text = filenameFormat->text().trimmed();
		const QString format = PoldenTemplates::toObs(text, settings.operatorName,
							      selectedProject() ? selectedProject()->name : QString());
		config_set_string(main->Config(), "Polden", "FilenameTemplate", text.toUtf8().constData());
		config_set_string(main->Config(), "Polden", "AppliedFilenameFormatting", format.toUtf8().constData());
		config_set_string(main->Config(), "Output", "FilenameFormatting", format.toUtf8().constData());
		config_save_safe(main->Config(), "tmp", nullptr);
	}
	if (markerSettings) {
		markerSettings->apply();
	}
	save();
	refreshMarkerButtons();
}
