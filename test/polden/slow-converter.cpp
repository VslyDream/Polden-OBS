// Deterministic subprocess fixture for checking the panel's cancellation path.
#include <QCoreApplication>
#include <QFile>
#include <QThread>

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QFile output(app.arguments().last());
	if (!output.open(QIODevice::WriteOnly)) {
		return 1;
	}
	output.write("partial conversion");
	output.flush();
	QThread::sleep(120);
	return 1;
}
