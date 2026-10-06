#include "PoldenUpdater.hpp"

#include <widgets/PoldenPanel.hpp>
#include <OBSApp.hpp>
#include <obs-frontend-api.h>
#include <util/base.h>
#include "ui-config.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QVersionNumber>

#include <memory>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
constexpr auto repository = "https://api.github.com/repos/VslyDream/Polden-OBS/releases/latest";

QNetworkRequest request(const QUrl &url)
{
	QNetworkRequest result(url);
	result.setRawHeader("User-Agent", "Polden-OBS/" POLDEN_VERSION);
	result.setRawHeader("Accept", "application/vnd.github+json");
	result.setTransferTimeout(30000);
	result.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	return result;
}
} // namespace

PoldenUpdater::PoldenUpdater(PoldenPanel *panel, QToolButton *button)
	: QObject(panel),
	  panel(panel),
	  button(button),
	  network(new QNetworkAccessManager(this))
{
	button->hide();
	connect(button, &QToolButton::clicked, this, [this]() { install(); });
#ifdef _WIN32
	if (!QCoreApplication::arguments().contains(QStringLiteral("--disable-updater"))) {
		QTimer::singleShot(5000, this, [this]() { check(); });
	}
#endif
}

void PoldenUpdater::fail(const QString &message)
{
	QMessageBox::warning(panel, tr("Polden update"), message);
}

void PoldenUpdater::check(bool manual)
{
#ifdef _WIN32
	if (checking || downloading) {
		return;
	}
	checking = true;
	blog(LOG_INFO, "Polden: checking GitHub Releases for updates (current %s)", POLDEN_VERSION);
	auto *reply = network->get(request(QUrl(QString::fromLatin1(repository))));
	connect(reply, &QNetworkReply::finished, this, [this, reply, manual]() {
		checking = false;
		reply->deleteLater();
		if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404) {
			blog(LOG_INFO, "Polden: no public release is available");
			if (manual) {
				QMessageBox::information(panel, tr("Polden update"),
							 tr("No public releases are available yet."));
			}
			return;
		}
		if (reply->error() != QNetworkReply::NoError) {
			blog(LOG_WARNING, "Polden: update check failed: %s", reply->errorString().toUtf8().constData());
			if (manual) {
				fail(tr("Could not check for updates: %1").arg(reply->errorString()));
			}
			return;
		}
		const QJsonObject candidate = QJsonDocument::fromJson(reply->readAll()).object();
		const QString tag = candidate.value(QStringLiteral("tag_name")).toString();
		static const QRegularExpression tagPattern(QStringLiteral("^polden-v([0-9]+\\.[0-9]+\\.[0-9]+)$"));
		const auto match = tagPattern.match(tag);
		if (!match.hasMatch() || candidate.value(QStringLiteral("draft")).toBool() ||
		    candidate.value(QStringLiteral("prerelease")).toBool()) {
			if (manual) {
				fail(tr("The latest release does not contain a stable Polden version."));
			}
			return;
		}
		const QString version = match.captured(1);
		if (QVersionNumber::fromString(version) <= QVersionNumber::fromString(QStringLiteral(POLDEN_VERSION))) {
			blog(LOG_INFO, "Polden: current version is up to date (latest %s)",
			     version.toUtf8().constData());
			release = {};
			asset = {};
			button->hide();
			if (manual) {
				QMessageBox::information(panel, tr("Polden update"),
							 tr("Polden %1 is up to date.").arg(POLDEN_VERSION));
			}
			return;
		}
		const QString name = QStringLiteral("Polden-OBS-%1-Windows-x64.zip").arg(version);
		const QString expectedUrl =
			QStringLiteral("https://github.com/VslyDream/Polden-OBS/releases/download/%1/%2").arg(tag, name);
		for (const auto &item : candidate.value(QStringLiteral("assets")).toArray()) {
			const auto file = item.toObject();
			static const QRegularExpression digestPattern(QStringLiteral("^sha256:[0-9a-fA-F]{64}$"));
			if (file.value(QStringLiteral("name")).toString() != name ||
			    file.value(QStringLiteral("browser_download_url")).toString() != expectedUrl ||
			    !digestPattern.match(file.value(QStringLiteral("digest")).toString()).hasMatch()) {
				continue;
			}
			release = candidate;
			asset = file;
			blog(LOG_INFO, "Polden: update %s is available", version.toUtf8().constData());
			button->setToolTip(tr("Polden %1 is available — update and restart").arg(version));
			button->show();
			if (manual) {
				install();
			}
			return;
		}
		if (manual) {
			fail(tr("The release is missing a verified Windows x64 archive."));
		}
	});
#else
	Q_UNUSED(manual);
#endif
}

bool PoldenUpdater::canInstall()
{
	if (obs_frontend_recording_active() || obs_frontend_streaming_active() || obs_frontend_replay_buffer_active() ||
	    obs_frontend_virtualcam_active() || panel->processingFiles()) {
		fail(tr("Finish recording, streaming, replay buffer, virtual camera and file processing before updating."));
		return false;
	}
	return true;
}

void PoldenUpdater::install()
{
#ifdef _WIN32
	if (downloading || release.isEmpty() || !canInstall()) {
		return;
	}
	const QString root = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../.."));
	if (!QFile::exists(root + QStringLiteral("/polden-install.json"))) {
		fail(tr("This is a development build. Install the portable ZIP from GitHub Releases to use automatic updates."));
		return;
	}
	const QString version = release.value(QStringLiteral("tag_name")).toString().mid(8);
	QMessageBox question(
		QMessageBox::Question, tr("Polden update"),
		tr("Download Polden %1 (%2 MB) and restart?\n\nYour settings, scenes, projects and custom plugins will be kept. A backup will be saved before installation.")
			.arg(version)
			.arg(asset.value(QStringLiteral("size")).toDouble() / 1048576.0, 0, 'f', 0),
		QMessageBox::Yes | QMessageBox::Cancel, panel);
	question.setDefaultButton(QMessageBox::Cancel);
	question.setDetailedText(release.value(QStringLiteral("body")).toString());
	if (question.exec() != QMessageBox::Yes) {
		return;
	}
	auto temporary = std::make_shared<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/Polden-update-XXXXXX"));
	if (!temporary->isValid()) {
		fail(tr("Could not create the update download folder."));
		return;
	}
	auto file = std::make_shared<QSaveFile>(temporary->filePath(QStringLiteral("update.zip")));
	if (!file->open(QIODevice::WriteOnly)) {
		fail(tr("Could not save the update archive."));
		return;
	}
	auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
	auto *progress = new QProgressDialog(tr("Downloading Polden %1…").arg(version), tr("Cancel"), 0, 100, panel);
	progress->setWindowModality(Qt::WindowModal);
	progress->setAutoClose(false);
	progress->setMinimumDuration(0);
	button->setEnabled(false);
	downloading = true;
	auto *reply = network->get(request(QUrl(asset.value(QStringLiteral("browser_download_url")).toString())));
	connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
	connect(reply, &QNetworkReply::downloadProgress, progress, [progress](qint64 received, qint64 total) {
		if (total > 0) {
			progress->setValue(static_cast<int>(100.0 * received / total));
		}
	});
	auto consume = [reply, file, hash]() {
		const QByteArray bytes = reply->readAll();
		hash->addData(bytes);
		if (file->write(bytes) != bytes.size()) {
			reply->abort();
		}
	};
	connect(reply, &QNetworkReply::readyRead, this, consume);
	connect(reply, &QNetworkReply::finished, this,
		[this, reply, progress, temporary, file, hash, consume, root, version]() {
			consume();
			const bool cancelled = progress->wasCanceled();
			progress->deleteLater();
			reply->deleteLater();
			downloading = false;
			button->setEnabled(true);
			if (cancelled) {
				return;
			}
			if (reply->error() != QNetworkReply::NoError) {
				fail(tr("Download failed: %1").arg(reply->errorString()));
				return;
			}
			if (hash->result().toHex() !=
				    asset.value(QStringLiteral("digest")).toString().mid(7).toLatin1().toLower() ||
			    !file->commit()) {
				fail(tr("The downloaded archive failed verification. No files were changed."));
				return;
			}
			if (!canInstall()) {
				return;
			}
			QFile helperResource(QStringLiteral(":/res/polden/Update-Polden.ps1"));
			QFile helper(temporary->filePath(QStringLiteral("Update-Polden.ps1")));
			QFile parameters(temporary->filePath(QStringLiteral("request.json")));
			char settingsPath[4096] = {};
			GetAppConfigPath(settingsPath, sizeof(settingsPath), "obs-studio");
			QJsonObject data{
				{QStringLiteral("root"), QDir::cleanPath(root)},
				{QStringLiteral("pid"), QCoreApplication::applicationPid()},
				{QStringLiteral("version"), version},
				{QStringLiteral("sha256"), asset.value(QStringLiteral("digest")).toString().mid(7)},
				{QStringLiteral("settings"),
				 QDir::cleanPath(QDir::current().absoluteFilePath(QString::fromUtf8(settingsPath)))},
				{QStringLiteral("portable"), App()->IsPortableMode()}};
			if (!helperResource.open(QIODevice::ReadOnly) || !helper.open(QIODevice::WriteOnly) ||
			    !parameters.open(QIODevice::WriteOnly)) {
				fail(tr("Could not prepare the update helper."));
				return;
			}
			const auto script = helperResource.readAll();
			const auto json = QJsonDocument(data).toJson();
			if (helper.write(script) != script.size() || parameters.write(json) != json.size() ||
			    !helper.flush() || !parameters.flush()) {
				fail(tr("Could not write the update helper."));
				return;
			}
			helper.close();
			parameters.close();
			QProcess process;
			process.setProgram(qEnvironmentVariable("SystemRoot") +
					   QStringLiteral("/System32/WindowsPowerShell/v1.0/powershell.exe"));
			process.setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
					      QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
					      QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
					      QStringLiteral("-File"), helper.fileName(),
					      QStringLiteral("-RequestPath"), parameters.fileName()});
			process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
				arguments->flags |= CREATE_NO_WINDOW;
			});
			if (!process.startDetached()) {
				fail(tr("Could not start the update helper."));
				return;
			}
			temporary->setAutoRemove(false);
			panel->window()->close();
		});
#endif
}
