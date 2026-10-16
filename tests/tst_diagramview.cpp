#include "editor/DiagramView.h"
#include "editor/EditorTabs.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {

const char *const kDiagram = R"({
  "format": "flowboard",
  "version": 1,
  "rankdir": "TB",
  "shapes": [
    {"id": 1, "key": "A", "variant": "stadium", "text": "Début", "x": 0, "y": 0},
    {"id": 2, "key": "B", "variant": "square", "text": "Étape", "x": 0, "y": 120}
  ],
  "links": [
    {"id": 3, "from": 1, "to": 2}
  ]
}
)";

QString writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &content)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(content);
    return path;
}

} // namespace

class TestDiagramView : public QObject {
    Q_OBJECT

private slots:
    void opensFlowFilesInTheDiagramEditor()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, QStringLiteral("process.flow"), kDiagram);
        QVERIFY(DiagramView::isDiagramFile(path));
        QVERIFY(!DiagramView::isDiagramFile(dir.filePath(QStringLiteral("notes.md"))));

        EditorTabs tabs(Theme::dark());
        auto *view = qobject_cast<DiagramView *>(tabs.openFile(path));
        QVERIFY(view);
        QCOMPARE(view->filePath(), QFileInfo(path).absoluteFilePath());
        QVERIFY(!view->isModified());
        QCOMPARE(tabs.openFile(path), view); // no second tab
        QCOMPARE(tabs.count(), 1);
        QCOMPARE(tabs.currentFilePath(), view->filePath());
        QCOMPARE(tabs.openFilePaths(), QStringList{view->filePath()});
    }

    void refusesInvalidDiagrams()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, QStringLiteral("broken.flow"), "{ not json");
        DiagramView view(Theme::dark());
        QString error;
        QVERIFY(!view.load(path, &error));
        QVERIFY(!error.isEmpty());
    }

    void savesAndRecognisesItsOwnWrites()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, QStringLiteral("process.flow"), kDiagram);
        DiagramView view(Theme::dark());
        QString error;
        QVERIFY2(view.load(path, &error), qPrintable(error));
        QVERIFY(view.matchesDisk());

        QVERIFY2(view.save(&error), qPrintable(error));
        QVERIFY(view.matchesDisk());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray saved = file.readAll();
        QVERIFY(saved.contains("\"format\": \"flowboard\""));
        QVERIFY(saved.contains("Étape"));

        // A change made by someone else is not ours.
        writeFile(dir, QStringLiteral("process.flow"), saved + "\n");
        QVERIFY(!view.matchesDisk());
        QVERIFY2(view.reloadFromDisk(&error), qPrintable(error));
        QVERIFY(view.matchesDisk());
        QVERIFY(!view.isModified());
    }

    void opensAnEmptyFileAsABlankDiagram()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, QStringLiteral("new.flow"), QByteArray());
        DiagramView view(Theme::dark());
        QString error;
        QVERIFY2(view.load(path, &error), qPrintable(error));
        QVERIFY(!view.isModified());
        QVERIFY2(view.save(&error), qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("\"format\": \"flowboard\""));
    }

    void severalDiagramsCanBeOpenAtOnce()
    {
        QTemporaryDir dir;
        const QString first = writeFile(dir, QStringLiteral("a.flow"), kDiagram);
        const QString second = writeFile(dir, QStringLiteral("b.flow"), kDiagram);
        EditorTabs tabs(Theme::dark());
        QVERIFY(qobject_cast<DiagramView *>(tabs.openFile(first)));
        QVERIFY(qobject_cast<DiagramView *>(tabs.openFile(second)));
        QCOMPARE(tabs.count(), 2);
        QVERIFY(tabs.saveAll());
        QVERIFY(tabs.closeAll());
    }
};

QTEST_MAIN(TestDiagramView)
#include "tst_diagramview.moc"
