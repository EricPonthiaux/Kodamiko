#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "viewers/ImageView.h"

#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

class TestImageView : public QObject {
    Q_OBJECT

private slots:
    void opensImagesInAViewer()
    {
        QTemporaryDir dir;
        const QString imagePath = dir.filePath(QStringLiteral("logo.png"));
        QImage image(40, 20, QImage::Format_ARGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(imagePath));

        QVERIFY(ImageView::isImageFile(imagePath));
        EditorTabs tabs(Theme::dark());
        auto *view = qobject_cast<ImageView *>(tabs.openFile(imagePath));
        QVERIFY(view);
        QCOMPARE(view->filePath(), QFileInfo(imagePath).absoluteFilePath());
        QVERIFY(view->description().startsWith(QStringLiteral("40 × 20")));
        QCOMPARE(tabs.openFile(imagePath), view); // pas de second onglet
        QCOMPARE(tabs.count(), 1);
    }

    void opensTextFilesInAnEditor()
    {
        QTemporaryDir dir;
        const QString textPath = dir.filePath(QStringLiteral("main.c"));
        QFile file(textPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("int main(void) { return 0; }\n");
        file.close();

        QVERIFY(!ImageView::isImageFile(textPath));
        EditorTabs tabs(Theme::dark());
        QVERIFY(qobject_cast<EditorWidget *>(tabs.openFile(textPath)));
    }
};

QTEST_MAIN(TestImageView)
#include "tst_imageview.moc"
