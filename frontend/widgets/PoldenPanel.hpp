#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QPointer>
#include <QVector>
#include <QWidget>

class OBSBasic;
class QComboBox;
class QLabel;
class QPushButton;
class QProcess;
class PoldenUpdater;

class PoldenPanel : public QWidget {
public:
	explicit PoldenPanel(OBSBasic *main);

	void recordingStarted();
	void recordingFileChanged(const QString &path);
	void recordingStopped(const QString &path, bool success);
	void activateSelectedCapture();
	void checkForUpdates(bool manual = false);
	bool processingFiles() const;

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
		QString filenameTemplate = QStringLiteral("{user}_{timestamp}.mp4");
		QString ffmpegPath;
	};
	struct Job {
		QString id;
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
	};

	OBSBasic *main;
	QComboBox *projectCombo = nullptr;
	QComboBox *automationCombo = nullptr;
	QLabel *statusLabel = nullptr;
	QPushButton *editButton = nullptr;
	QPushButton *retryButton = nullptr;
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
	QProcess *converter = nullptr;
	bool copying = false;
	PoldenUpdater *updater = nullptr;

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
	Job *jobById(const QString &id);
	void pump();
	void startConversion(Job &job);
	void startCopy(Job &job);
};
