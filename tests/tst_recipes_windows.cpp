// SPDX-License-Identifier: GPL-3.0-only

#include "recipes/VisualStudio.h"
#include "terminal/ProcessSpec.h"

#include <QDir>
#include <QStandardPaths>
#include <QtTest>

// Visual Studio's environment, which the recipes of MSVC kits use.
class TestRecipesWindows : public QObject {
    Q_OBJECT

private slots:

    // On a machine with Visual Studio's C++ tools only.
    void findsVisualStudiosCompilerAndCMake()
    {
        QString error;

        const std::optional<QMap<QString, QString>> variables = VisualStudio::environment(&error);

        if (!variables)
            QSKIP(qPrintable(error));
        QVERIFY(variables->contains("INCLUDE"));
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        addTo(environment, {.variables = *variables, .pathFolders = {}});
        const QStringList path = environment.value("PATH").split(QDir::listSeparator());
        QVERIFY(!QStandardPaths::findExecutable("cl", path).isEmpty());
        QVERIFY(!QStandardPaths::findExecutable("cmake", path).isEmpty());
        QVERIFY(!QStandardPaths::findExecutable("ninja", path).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRecipesWindows)
#include "tst_recipes_windows.moc"
