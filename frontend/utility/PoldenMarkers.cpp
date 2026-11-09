#include "PoldenMarkers.hpp"

#include <OBSApp.hpp>
#include <obs-frontend-api.h>
#include <util/dstr.hpp>
#include <settings/OBSHotkeyWidget.hpp>

#include <QPushButton>
#include <QHBoxLayout>
#include <components/FlowLayout.hpp>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMutexLocker>
#include <QPixmap>
#include <QPainter>
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
	return QStringLiteral("%1:%2:%3")
		.arg(milliseconds / 3600000)
		.arg(milliseconds / 60000 % 60, 2, 10, QLatin1Char('0'))
		.arg(milliseconds / 1000 % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

QString PoldenMarkers::colorName(int color)
{
	const QStringList names = {tr("Green"),  tr("Red"),   tr("Purple"), tr("Orange"),
				   tr("Yellow"), tr("White"), tr("Blue"),   tr("Cyan")};
	return QStringLiteral("%1 · %2").arg(color + 1).arg(names.value(color));
}

QIcon PoldenMarkers::colorIcon(int color)
{
	QPixmap swatch(16, 16);
	swatch.fill(Qt::transparent);
	QPainter painter(&swatch);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(QString::fromLatin1(markerColors.at(color))));
	painter.drawEllipse(QRectF(2, 2, 12, 12));
	painter.end();
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

QString PoldenMarkers::shortcutText(int color) const
{
	OBSDataArrayAutoRelease bindings = obs_hotkey_save(hotkeys[color]);
	QStringList result;
	for (size_t i = 0; i < obs_data_array_count(bindings); ++i) {
		OBSDataAutoRelease data = obs_data_array_item(bindings, i);
		obs_key_combination_t combo{};
		combo.key = obs_key_from_name(obs_data_get_string(data, "key"));
		if (obs_data_get_bool(data, "control")) {
			combo.modifiers |= INTERACT_CONTROL_KEY;
		}
		if (obs_data_get_bool(data, "shift")) {
			combo.modifiers |= INTERACT_SHIFT_KEY;
		}
		if (obs_data_get_bool(data, "alt")) {
			combo.modifiers |= INTERACT_ALT_KEY;
		}
		if (obs_data_get_bool(data, "command")) {
			combo.modifiers |= INTERACT_COMMAND_KEY;
		}
		DStr text;
		obs_key_combination_to_str(combo, text);
		result.append(QString::fromUtf8(static_cast<const char *>(text)));
	}
	return result.isEmpty() ? tr("No shortcut") : result.join(QStringLiteral(" / "));
}

qint64 PoldenMarkers::elapsedMs()
{
	QMutexLocker lock(&recordingMutex);
	if (!output) {
		return 0;
	}
	video_t *video = obs_output_video(output);
	const video_output_info *info = video ? video_output_get_info(video) : nullptr;
	obs_encoder_t *encoder = obs_output_get_video_encoder(output);
	const uint32_t divisor = encoder ? obs_encoder_get_frame_rate_divisor(encoder) : 1;
	return info && info->fps_num
		       ? qint64(obs_output_get_total_frames(output)) * 1000 * info->fps_den * divisor / info->fps_num
		       : 0;
}

PoldenMarkerSettings::PoldenMarkerSettings(PoldenMarkers *markers, QWidget *parent) : QWidget(parent), markers(markers)
{
	setObjectName(QStringLiteral("poldenMarkerSettings"));
	auto *layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel(tr("Buttons on the dock"), this));
	auto *chips = new QWidget(this);
	auto *chipLayout = new FlowLayout(chips, 0, 6, 6);
	layout->addWidget(chips);
	auto *help = new QLabel(tr("Click a color to show or hide its dock button."), this);
	help->setWordWrap(true);
	layout->addWidget(help);
	layout->addWidget(new QLabel(tr("Shortcuts"), this));
	auto *form = new QGridLayout;
	form->setColumnStretch(0, 1);
	form->setColumnStretch(1, 1);
	form->setHorizontalSpacing(20);
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
		buttons[color] = new QPushButton(PoldenMarkers::colorIcon(color), QString::number(color + 1), chips);
		buttons[color]->setObjectName(QStringLiteral("poldenMarkerShowButton%1").arg(color));
		buttons[color]->setAccessibleName(tr("Show %1 button on dock").arg(PoldenMarkers::colorName(color)));
		buttons[color]->setToolTip(PoldenMarkers::colorName(color));
		buttons[color]->setFixedSize(52, 32);
		buttons[color]->setCheckable(true);
		buttons[color]->setChecked(markers->buttonVisible(color));
		chipLayout->addWidget(buttons[color]);
		auto *cell = new QWidget(this);
		auto *cellLayout = new QVBoxLayout(cell);
		cellLayout->setContentsMargins(0, 4, 0, 4);
		auto *label = new QLabel(PoldenMarkers::colorName(color), cell);
		label->setStyleSheet(QStringLiteral("border-left: 10px solid %1; padding-left: 6px;")
					     .arg(QString::fromLatin1(markerColors[color])));
		cellLayout->addWidget(label);
		editors[color] = new OBSHotkeyWidget(cell, markers->hotkeys[color], hotkeyName(color).toStdString(),
						     nullptr, bindings.combos, true);
		editors[color]->setObjectName(QStringLiteral("poldenMarkerShortcut%1").arg(color));
		cellLayout->addWidget(editors[color]);
		form->addWidget(cell, color % 4, color / 4);
		connect(editors[color], &OBSHotkeyWidget::KeyChanged, this, [this]() { validate(); });
	}
	layout->addLayout(form);
	errorLabel = new QLabel(this);
	errorLabel->setWordWrap(true);
	errorLabel->setObjectName(QStringLiteral("poldenMarkerSettingsError"));
	layout->addWidget(errorLabel);
	auto *hint = new QLabel(
		tr("More shortcuts per color: OBS Settings → Hotkeys. Markers are saved beside each video as TXT (H:MM:SS,1–8) and copied with the MP4."),
		this);
	hint->setWordWrap(true);
	layout->addWidget(hint);
	layout->addStretch();
	markers->editingHotkeys = true;
}

void PoldenMarkerSettings::setValidityChanged(std::function<void(bool)> callback)
{
	validityChanged = std::move(callback);
	validate();
}

PoldenMarkerSettings::~PoldenMarkerSettings()
{
	markers->editingHotkeys = false;
}

bool PoldenMarkerSettings::validate()
{
	std::array<std::vector<obs_key_combination_t>, 8> combinations;
	std::array<bool, 8> duplicates{};
	bool valid = true;
	for (int i = 0; i < 8; ++i) {
		editors[i]->GetCombinations(combinations[i]);
	}
	for (int i = 0; i < 8; ++i) {
		for (int j = i + 1; j < 8; ++j) {
			for (const auto &combo : combinations[i]) {
				if (std::find(combinations[j].begin(), combinations[j].end(), combo) !=
				    combinations[j].end()) {
					duplicates[i] = duplicates[j] = true;
					valid = false;
				}
			}
		}
	}
	for (int i = 0; i < 8; ++i) {
		editors[i]->setStyleSheet(duplicates[i] ? QStringLiteral("QLineEdit { border: 1px solid #f0555b; }")
							: QString());
	}
	errorLabel->setText(valid ? QString() : tr("Choose different shortcuts for different marker colors."));
	if (validityChanged) {
		validityChanged(valid);
	}
	return valid;
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
	totalMarkerCount = 0;
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
		QFile file(videoFile.dir().filePath(videoFile.completeBaseName() + QStringLiteral(".txt")));
		const QByteArray line = (time + QStringLiteral(",%1\r\n").arg(color + 1)).toUtf8();
		if (!file.open(QIODevice::WriteOnly | QIODevice::Append) || file.write(line) != line.size() ||
		    !file.flush()) {
			error = tr("Could not write marker: %1").arg(file.errorString());
		}
	}
	if (error.isEmpty()) {
		++totalMarkerCount;
		report(tr("Marker %1 at %2").arg(colorName(color), time));
	} else {
		report(error);
	}
}
