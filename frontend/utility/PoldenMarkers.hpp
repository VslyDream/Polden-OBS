#pragma once

#include <obs.hpp>

#include <QIcon>
#include <QMutex>
#include <QObject>
#include <QWidget>

#include <array>
#include <functional>

class OBSHotkeyWidget;
class QPushButton;
class QLabel;
class PoldenMarkerSettings;

// Recording markers are part of the frontend; no source or plugin is required.
class PoldenMarkers : public QObject {
public:
	PoldenMarkers(QObject *parent, std::function<void(const QString &)> report);
	~PoldenMarkers() override;

	static QString colorName(int color);
	static QIcon colorIcon(int color);
	bool buttonVisible(int color) const { return visibleButtons.at(color); }
	void recordingStarted();
	void recordingStopped();
	void addMarker(int color);
	QString shortcutText(int color) const;
	qint64 elapsedMs();
	int markerCount() const { return totalMarkerCount; }

private:
	friend class PoldenMarkerSettings;
	std::function<void(const QString &)> report;
	std::array<obs_hotkey_id, 8> hotkeys;
	OBSSignal bindingsChanged;
	OBSSignal fileChanged;
	OBSOutput output;
	QMutex recordingMutex;
	QString videoPath;
	int segmentStartFrame = 0;
	std::array<bool, 8> visibleButtons{};
	bool editingHotkeys = false;
	int totalMarkerCount = 0;

	QString configPath() const;
	void save();
	static void hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed);
	static void splitFile(void *data, calldata_t *params);
};

class PoldenMarkerSettings : public QWidget {
public:
	PoldenMarkerSettings(PoldenMarkers *markers, QWidget *parent);
	~PoldenMarkerSettings() override;
	bool validate();
	void apply();
	void setValidityChanged(std::function<void(bool)> callback);

private:
	PoldenMarkers *markers;
	std::array<OBSHotkeyWidget *, 8> editors;
	std::array<QPushButton *, 8> buttons;
	std::function<void(bool)> validityChanged;
	QLabel *errorLabel;
};
