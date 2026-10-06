#pragma once

#include <QObject>
#include <QJsonObject>

class PoldenPanel;
class QNetworkAccessManager;
class QToolButton;

class PoldenUpdater : public QObject {
public:
	PoldenUpdater(PoldenPanel *panel, QToolButton *button);
	void check(bool manual = false);

private:
	PoldenPanel *panel;
	QToolButton *button;
	QNetworkAccessManager *network;
	QJsonObject release;
	QJsonObject asset;
	bool checking = false;
	bool downloading = false;
	void install();
	bool canInstall();
	void fail(const QString &message);
};
