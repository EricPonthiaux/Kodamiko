#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QRegularExpression>
#include <QThreadPool>

#include <atomic>
#include <memory>
#include <optional>

// What Find in Files looks for.
struct FindQuery {
    QString text;
    bool matchCase = false;
    bool wholeWords = false;
    bool regularExpression = false;
    // File name patterns separated by commas ("*.cpp, *.h"); "!" in front
    // excludes ("!*.min.js"). Empty: every file.
    QString fileMask;

    // nullopt (with `error`) for an empty text or an invalid expression.
    std::optional<QRegularExpression> compile(QString *error = nullptr) const;
    bool acceptsFile(const QString &fileName) const;
};

// Occurrence found in a file. Lines and columns start at 0, columns and
// lengths are in characters.
struct FindMatch {
    QString filePath;
    int line = 0;
    int column = 0;
    int length = 0;
    QString lineText; // the whole line, without its line ending
};

namespace FindInFiles {

// Occurrences of `pattern` in `text`, at most `limit`. Matches spanning
// several lines are not reported.
QList<FindMatch> search(const QString &text, const QRegularExpression &pattern, const QString &filePath, int limit);

} // namespace FindInFiles

// Search of the working folder's files, off the UI thread. Files open with
// unsaved changes are searched in their editor's text. Starting a new
// search abandons the previous one.
class FindInFilesSearch : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(FindInFilesSearch)

public:
    static constexpr int matchLimit = 1000;

    explicit FindInFilesSearch(QObject *parent = nullptr);
    // Stops the search in progress and waits for it.
    ~FindInFilesSearch() override;

    // `buffers`: text of files open with unsaved changes, by absolute path.
    void start(const QString &folder, const FindQuery &query, const QHash<QString, QString> &buffers = {});
    void cancel();

    bool isRunning() const { return running_ > 0; }

signals:
    // At the end of the search. `truncated`: matchLimit was reached.
    void finished(const QList<FindMatch> &matches, bool truncated, int filesSearched);

private:
    QThreadPool pool_;
    std::shared_ptr<std::atomic_int> generation_;
    int running_ = 0;
};
