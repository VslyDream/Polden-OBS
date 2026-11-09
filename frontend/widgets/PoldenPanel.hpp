#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QPointer>
#include <QVector>
#include <QWidget>

#include <atomic>
#include <memory>

class OBSBasic;
class QComboBox;
class QLabel;
class QPushButton;
class QProcess;
class QToolButton;
class QButtonGroup;
class QMenu;
class QProgressBar;
class PoldenUpdater;
class PoldenMarkers;

class PoldenPanel : public QWidget {
public:
	explicit PoldenPanel(OBSBasic *main);

	void recordingStarted();
	void recordingFileChanged(const QString &path);
	void recordingStopped(const QString &path, bool success);
	void activateSelectedCapture();
	void checkForUpdates(bool manual = false);
	bool processingFiles() const;
	void initializeMarkers();
	void shutdownMarkers();

private:
	struct Project {
		QString id;
		QString name;
		QString root;
		QString footageDirectory;
		QString sourceUuid;
		QString window;
		int automation = 0;
	};

	struct Settings {
		QString operatorName = QStringLiteral("Vasiliy");
		QString directoryTemplate = QStringLiteral("{footage}/{date}");
		QString ffmpegPath;
		QString videoSubfolder = QStringLiteral("02_Assets/01_VIDEO");
		QString gameNameTemplate = QStringLiteral("{folder}");
	};
	struct Job {
		QString id;
		QString takeId;
		QString projectId;
		QString sourcePath;
		QString localPath;
		QString destinationPath;
		QString recordedDate;
		QString recordedTimestamp;
		QString fileName;
		QString error;
		int targetLevel = 0;
		bool converted = false;
		bool copied = false;
		bool cancelled = false;
		qint64 durationMs = 0;
		qint64 segmentDurationMs = 0;
		int markerCount = 0;
		QStringList deliveredFiles;
	};

	OBSBasic *main;
	QToolButton *projectButton = nullptr;
	QMenu *projectMenu = nullptr;
	QButtonGroup *automationButtons = nullptr;
	QLabel *automationLabel = nullptr;
	QWidget *emptyGame = nullptr;
	QLabel *takeTitle = nullptr;
	QLabel *takeFile = nullptr;
	QLabel *takeDetails = nullptr;
	QProgressBar *takeProgress = nullptr;
	QWidget *backgroundProcessing = nullptr;
	QLabel *backgroundLabel = nullptr;
	QToolButton *deleteButton = nullptr;
	QPushButton *openFolderButton = nullptr;
	QLabel *statusLabel = nullptr;
	QPushButton *retryButton = nullptr;
	QPushButton *cancelButton = nullptr;
	QVector<Project> projects;
	QVector<Job> jobs;
	Settings settings;
	QString selectedProjectId;
	QString recordingProjectId;
	Project recordingProjectSnapshot;
	Settings recordingSettingsSnapshot;
	int recordingAutomation = 0;
	QDateTime recordingStartedAt;
	QStringList recordingFiles;
	QVector<qint64> recordingSegmentEnds;
	QProcess *converter = nullptr;
	bool copying = false;
	std::shared_ptr<std::atomic_bool> copyCancellation;
	PoldenUpdater *updater = nullptr;
	PoldenMarkers *markers = nullptr;
	QWidget *markerButtonsWidget = nullptr;
	bool markerRecording = false;
	bool recording = false;
	QString activeJobId;
	int processingProgress = -1;
	QString lastMarker;
	QStringList pendingDeleteIds;
	QStringList pendingDeleteLocal;
	QStringList pendingDeleteRemote;
	void refreshTake();
	void deleteLastTake();
	void finishTakeDeletion();
	void applyFilenameTemplate();
	QString filenameTemplate() const;
	void refreshMarkerButtons();

	QString configPath() const;
	void load();
	void save();
	void refresh();
	void showProjectDialog(bool editing);
	void showSettingsDialog();
	void applyCaptureWindow(const Project &project);
	Project *selectedProject();
	const Project *selectedProject() const;
	void setStatus(const QString &text);
	void openFilesFolder();
	QString jobsPath() const;
	void loadJobs();
	void saveJobs();
	Job *lastJobForSelectedProject();
	Job *retryableJobForSelectedProject();
	Job *jobById(const QString &id);
	void pump();
	void startConversion(Job &job);
	void startCopy(Job &job);
	void cancelProcessing();
};
