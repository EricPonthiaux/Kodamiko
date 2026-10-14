#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Enlumine"));
    QApplication::setOrganizationName(QStringLiteral("Enlumine"));
    QApplication::setApplicationVersion(QStringLiteral(ENLUMINE_VERSION));
    // Fusion suit la palette de l'application sur tous les systèmes, ce qui
    // permet le thème sombre partout.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Éditeur de sources avec terminal Claude intégré"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("dossier"), QStringLiteral("Dossier du projet à ouvrir"));
    parser.process(app);

    const QStringList arguments = parser.positionalArguments();
    MainWindow window(arguments.isEmpty() ? QString() : arguments.first());
    window.show();
    return app.exec();
}
