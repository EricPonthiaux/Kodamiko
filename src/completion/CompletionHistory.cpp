#include "CompletionHistory.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr int saveDelayMs = 2000;
constexpr int maxEntries = 3000;
// A choice counts half as much after this many days without being made again.
constexpr double halfLifeDays = 14.0;
constexpr int maxBoost = 14;
constexpr int penaltyPerRejection = 3;
constexpr int maxRejections = 3;

} // namespace

CompletionHistory::CompletionHistory(QObject *parent)
    : QObject(parent)
{
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(saveDelayMs);
    connect(&saveTimer_, &QTimer::timeout, this, &CompletionHistory::save);
}

CompletionHistory::~CompletionHistory()
{
    if (saveTimer_.isActive())
        save();
}

QString CompletionHistory::fileFor(const QString &folder)
{
    const QByteArray id = QCryptographicHash::hash(QDir::cleanPath(folder).toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/completion/") +
           QString::fromLatin1(id.left(16)) + QStringLiteral(".json");
}

void CompletionHistory::setFile(const QString &path)
{
    if (path == file_)
        return;
    if (saveTimer_.isActive())
        save(); // the previous project's
    file_ = path;
    uses_.clear();
    load();
}

QString CompletionHistory::keyOf(const QString &languageId, const QString &label)
{
    return languageId + QLatin1Char('\n') + label;
}

void CompletionHistory::recordAccepted(const QString &languageId, const QString &label, const QDateTime &when)
{
    Use &use = uses_[keyOf(languageId, label)];
    ++use.accepted;
    // A choice made again is forgiven a past rejection.
    use.rejected = std::max(use.rejected - 1, 0);
    use.lastUsed = when.toSecsSinceEpoch();
    changed();
}

void CompletionHistory::recordRejected(const QString &languageId, const QString &label)
{
    Use &use = uses_[keyOf(languageId, label)];
    // Its acceptance did not count.
    use.accepted = std::max(use.accepted - 1, 0);
    use.rejected = std::min(use.rejected + 1, maxRejections);
    changed();
}

int CompletionHistory::boost(const QString &languageId, const QString &label, const QDateTime &now) const
{
    const auto it = uses_.constFind(keyOf(languageId, label));
    if (it == uses_.cend())
        return 0;
    // Frequency on a log scale (1 choice: 4, 3: 8, 7: 12), weighed by how
    // recent the last one is.
    const double days = std::max<double>(0, now.toSecsSinceEpoch() - it->lastUsed) / 86400.0;
    const double recency = std::pow(0.5, days / halfLifeDays);
    const double frequency = 4.0 * std::log2(1.0 + it->accepted);
    const int bonus = std::min(maxBoost, int(std::lround(frequency * (0.5 + 0.5 * recency))));
    return bonus - it->rejected * penaltyPerRejection;
}

void CompletionHistory::changed()
{
    if (uses_.size() > maxEntries) {
        // The least recently used go first.
        std::vector<std::pair<qint64, QString>> byAge;
        byAge.reserve(size_t(uses_.size()));
        for (auto it = uses_.cbegin(); it != uses_.cend(); ++it)
            byAge.emplace_back(it->lastUsed, it.key());
        std::sort(byAge.begin(), byAge.end());
        for (size_t i = 0; i < byAge.size() && uses_.size() > maxEntries; ++i)
            uses_.remove(byAge[i].second);
    }
    if (!file_.isEmpty())
        saveTimer_.start();
}

void CompletionHistory::load()
{
    if (file_.isEmpty())
        return;
    QFile file(file_);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject entries = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        uses_.insert(it.key(), {entry.value("accepted").toInt(), entry.value("rejected").toInt(),
                                entry.value("lastUsed").toInteger()});
    }
}

void CompletionHistory::save()
{
    saveTimer_.stop();
    if (file_.isEmpty())
        return;
    QJsonObject entries;
    for (auto it = uses_.cbegin(); it != uses_.cend(); ++it)
        entries.insert(it.key(),
                       QJsonObject{{"accepted", it->accepted}, {"rejected", it->rejected}, {"lastUsed", it->lastUsed}});
    QDir().mkpath(QFileInfo(file_).absolutePath());
    QSaveFile file(file_);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(entries).toJson(QJsonDocument::Compact));
        file.commit();
    }
}
