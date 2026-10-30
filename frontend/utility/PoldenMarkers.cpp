#include "PoldenMarkers.hpp"

#include <OBSApp.hpp>
#include <obs-frontend-api.h>
#include <settings/OBSHotkeyWidget.hpp>

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMutexLocker>
#include <QPixmap>
#include <QSaveFile>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace {
constexpr std::array<const char *, 8> markerColors = {"#718637", "#D22C36", "#AF8BB1", "#E96F24",
						      "#D0A12B", "#FFFFFF", "#482DFC", "#19F4D6"};

QString hotkeyName(int color)
{
	return QStringLiteral("Polden.Marker%1").arg(color);
}

QString timestamp(qint64 milliseconds)
{
	return QStringLiteral("%1:%2:%3.%4")
		.arg(milliseconds / 3600000, 2, 10, QLatin1Char('0'))
		.arg(milliseconds / 60000 % 60, 2, 10, QLatin1Char('0'))
		.arg(milliseconds / 1000 % 60, 2, 10, QLatin1Char('0'))
		.arg(milliseconds % 1000, 3, 10, QLatin1Char('0'));
}
} // namespace

QString PoldenMarkers::colorName(int color)
{
	const QStringList names = {tr("Green"),  tr("Red"),   tr("Purple"), tr("Orange"),
				   tr("Yellow"), tr("White"), tr("Blue"),   tr("Cyan")};
	return QStringLiteral("%1 · %2").arg(color).arg(names.value(color));
}

QIcon PoldenMarkers::colorIcon(int color)
{
	QPixmap swatch(16, 16);
	swatch.fill(QColor(QString::fromLatin1(markerColors.at(color))));
	return QIcon(swatch);
}

QString PoldenMarkers::configPath() const
{
	char path[1024];
	if (GetAppConfigPath(path, sizeof(path), "obs-studio/polden-markers.json") <= 0) {
		return {};
	}
	return QString::fromUtf8(path);
}

PoldenMarkers::PoldenMarkers(QObject *parent, std::function<void(const QString &)> report)
	: QObject(parent),
	  report(std::move(report))
{
	QFile file(configPath());
	QJsonObject settings;
	if (file.open(QIODevice::ReadOnly)) {
		settings = QJsonDocument::fromJson(file.readAll()).object();
	}
	for (int color = 0; color < 8; ++color) {
		const QByteArray name = hotkeyName(color).toUtf8();
		const QByteArray description = tr("Polden marker: %1").arg(colorName(color)).toUtf8();
		hotkeys[color] = obs_hotkey_register_frontend(name.constData(), description.constData(), hotkey, this);
		const QJsonObject options = settings.value(QString::fromUtf8(name)).toObject();
		visibleButtons[color] = options.value(QStringLiteral("showButton")).toBool(false);
		const QByteArray json = QJsonDocument(options).toJson();
		OBSDataAutoRelease data = obs_data_create_from_json(json.constData());
		OBSDataArrayAutoRelease bindings = obs_data_get_array(data, "bindings");
		obs_hotkey_load(hotkeys[color], bindings);
	}
	bindingsChanged.Connect(
		obs_get_signal_handler(), "hotkey_bindings_changed",
		[](void *data, calldata_t *) {
			auto *markers = static_cast<PoldenMarkers *>(data);
			QMetaObject::invokeMethod(markers, [markers]() { markers->save(); }, Qt::QueuedConnection);
		},
		this);
}

PoldenMarkers::~PoldenMarkers()
{
	recordingStopped();
	bindingsChanged.Disconnect();
	save();
	for (obs_hotkey_id id : hotkeys) {
		obs_hotkey_unregister(id);
	}
}

void PoldenMarkers::save()
{
	QJsonObject settings{{QStringLiteral("schemaVersion"), 2}};
	for (int color = 0; color < 8; ++color) {
		OBSDataArrayAutoRelease bindings = obs_hotkey_save(hotkeys[color]);
		OBSDataAutoRelease data = obs_data_create();
		obs_data_set_array(data, "bindings", bindings);
		QJsonObject options = QJsonDocument::fromJson(obs_data_get_json(data)).object();
		options.insert(QStringLiteral("showButton"), visibleButtons[color]);
		settings.insert(hotkeyName(color), options);
	}
	const QString path = configPath();
	if (path.isEmpty()) {
		return;
	}
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(settings).toJson()) < 0 || !file.commit()) {
		report(tr("Could not save marker shortcuts: %1").arg(file.errorString()));
	}
}

void PoldenMarkers::hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	if (!pressed) {
		return;
	}
	auto *markers = static_cast<PoldenMarkers *>(data);
	const auto it = std::find(markers->hotkeys.begin(), markers->hotkeys.end(), id);
	if (it == markers->hotkeys.end()) {
		return;
	}
	const int color = int(it - markers->hotkeys.begin());
	QMetaObject::invokeMethod(markers, [markers, color]() { markers->addMarker(color); }, Qt::AutoConnection);
}

PoldenMarkerSettings::PoldenMarkerSettings(PoldenMarkers *markers, QWidget *parent) : QWidget(parent), markers(markers)
{
	setObjectName(QStringLiteral("poldenMarkerSettings"));
	auto *layout = new QVBoxLayout(this);
	auto *help = new QLabel(tr("Assign a key or combination to each Premiere marker color. "
				   "Enable a panel button only for the colors you want to click."),
				this);
	help->setWordWrap(true);
	layout->addWidget(help);
	auto *form = new QGridLayout;
	form->addWidget(new QLabel(tr("Color"), this), 0, 0);
	form->addWidget(new QLabel(tr("Shortcut"), this), 0, 1);
	form->addWidget(new QLabel(tr("Button on panel"), this), 0, 2);
	form->setColumnStretch(1, 1);
	for (int color = 0; color < 8; ++color) {
		struct Bindings {
			obs_hotkey_id id;
			std::vector<obs_key_combination_t> combos;
		} bindings{markers->hotkeys[color], {}};
		obs_enum_hotkey_bindings(
			[](void *data, size_t, obs_hotkey_binding_t *binding) {
				auto &result = *static_cast<Bindings *>(data);
				if (obs_hotkey_binding_get_hotkey_id(binding) == result.id) {
					result.combos.push_back(obs_hotkey_binding_get_key_combination(binding));
				}
				return true;
			},
			&bindings);
		auto *label = new QLabel(PoldenMarkers::colorName(color), this);
		label->setStyleSheet(QStringLiteral("QLabel { border-left: 12px solid %1; padding-left: 6px; }")
					     .arg(QString::fromLatin1(markerColors[color])));
		editors[color] = new OBSHotkeyWidget(this, markers->hotkeys[color], hotkeyName(color).toStdString(),
						     nullptr, bindings.combos);
		editors[color]->setObjectName(QStringLiteral("poldenMarkerShortcut%1").arg(color));
		buttons[color] = new QCheckBox(this);
		buttons[color]->setObjectName(QStringLiteral("poldenMarkerShowButton%1").arg(color));
		buttons[color]->setAccessibleName(tr("Show %1 button on panel").arg(PoldenMarkers::colorName(color)));
		buttons[color]->setChecked(markers->buttonVisible(color));
		form->addWidget(label, color + 1, 0);
		form->addWidget(editors[color], color + 1, 1);
		form->addWidget(buttons[color], color + 1, 2, Qt::AlignCenter);
	}
	layout->addLayout(form);
	errorLabel = new QLabel(this);
	errorLabel->setWordWrap(true);
	errorLabel->setObjectName(QStringLiteral("poldenMarkerSettingsError"));
	layout->addWidget(errorLabel);
	layout->addStretch();
	markers->editingHotkeys = true;
}

PoldenMarkerSettings::~PoldenMarkerSettings()
{
	markers->editingHotkeys = false;
}

bool PoldenMarkerSettings::validate()
{
	std::vector<obs_key_combination_t> used;
	for (auto *editor : editors) {
		std::vector<obs_key_combination_t> combos;
		editor->GetCombinations(combos);
		for (const auto &combo : combos) {
			if (std::find(used.begin(), used.end(), combo) != used.end()) {
				errorLabel->setText(tr("Choose different shortcuts for different marker colors."));
				return false;
			}
			used.push_back(combo);
		}
	}
	errorLabel->clear();
	return true;
}

void PoldenMarkerSettings::apply()
{
	for (int color = 0; color < 8; ++color) {
		editors[color]->Save();
		markers->visibleButtons[color] = buttons[color]->isChecked();
	}
	markers->save();
}

void PoldenMarkers::recordingStarted()
{
	recordingStopped();
	output = OBSOutputAutoRelease(obs_frontend_get_recording_output());
	if (!output) {
		return;
	}
	OBSDataAutoRelease settings = obs_output_get_settings(output);
	videoPath = QString::fromUtf8(obs_data_get_string(settings, "path"));
	if (videoPath.isEmpty()) {
		videoPath = QString::fromUtf8(obs_data_get_string(settings, "url"));
	}
	segmentStartFrame = 0;
	fileChanged.Connect(obs_output_get_signal_handler(output), "file_changed", splitFile, this);
}

void PoldenMarkers::recordingStopped()
{
	fileChanged.Disconnect();
	QMutexLocker lock(&recordingMutex);
	output = nullptr;
	videoPath.clear();
}

void PoldenMarkers::splitFile(void *data, calldata_t *params)
{
	auto *markers = static_cast<PoldenMarkers *>(data);
	QMutexLocker lock(&markers->recordingMutex);
	markers->videoPath = QString::fromUtf8(calldata_string(params, "next_file"));
	markers->segmentStartFrame = obs_output_get_total_frames(markers->output);
}

void PoldenMarkers::addMarker(int color)
{
	if (editingHotkeys || color < 0 || color >= 8) {
		return;
	}
	QString error;
	QString time;
	{
		QMutexLocker lock(&recordingMutex);
		if (!output || !obs_output_active(output) || obs_output_paused(output)) {
			report(tr("Markers are available while recording is active and unpaused."));
			return;
		}
		video_t *video = obs_output_video(output);
		const video_output_info *info = video ? video_output_get_info(video) : nullptr;
		if (videoPath.isEmpty() || !info || !info->fps_num) {
			report(tr("Could not determine the recording path or frame rate for markers."));
			return;
		}
		// Encoded frames exclude pauses and remain continuous across split files.
		const qint64 frames = qMax(0, obs_output_get_total_frames(output) - segmentStartFrame);
		obs_encoder_t *encoder = obs_output_get_video_encoder(output);
		const uint32_t divisor = encoder ? obs_encoder_get_frame_rate_divisor(encoder) : 1;
		time = timestamp(frames * 1000 * info->fps_den * divisor / info->fps_num);
		const QFileInfo videoFile(videoPath);
		QFile file(videoFile.dir().filePath(videoFile.completeBaseName() + QStringLiteral(".csv")));
		const QByteArray line = (time + QStringLiteral(",%1\n").arg(color)).toUtf8();
		if (!file.open(QIODevice::WriteOnly | QIODevice::Append) || file.write(line) != line.size() ||
		    !file.flush()) {
			error = tr("Could not write marker: %1").arg(file.errorString());
		}
	}
	if (error.isEmpty()) {
		report(tr("Marker %1 at %2").arg(colorName(color), time));
	} else {
		report(error);
	}
}
