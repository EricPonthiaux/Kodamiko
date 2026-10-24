// SPDX-License-Identifier: GPL-3.0-only

#include "PreviewSite.h"

#include "system/FilePaths.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMimeDatabase>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>

namespace {

constexpr const char *schemeName = "duet-preview";

struct SiteContent {
    QString root;     // canonical: a symbolic link cannot lead out of it
    QString filePath; // served from `text`
    QByteArray text;  // UTF-8
};

// The living sites, by host. The scheme handler, shared by every web view
// of a profile, finds them here.
QHash<QString, SiteContent> &sites()
{
    static QHash<QString, SiteContent> registry;
    return registry;
}

bool isInside(const QString &path, const QString &folder)
{
    const QString prefix = folder.endsWith(u'/') ? folder : folder + u'/';
    return path.startsWith(prefix);
}

class SiteHandler : public QWebEngineUrlSchemeHandler {
public:
    using QWebEngineUrlSchemeHandler::QWebEngineUrlSchemeHandler;

    void requestStarted(QWebEngineUrlRequestJob *job) override
    {
        const QUrl url = job->requestUrl();
        const auto site = sites().constFind(url.host());
        if (site == sites().cend()) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        const QString path = FilePaths::fromUrlPath(url.path());
        if (path == site->filePath) {
            auto *buffer = new QBuffer(job);
            buffer->setData(site->text);
            buffer->open(QIODevice::ReadOnly);
            job->reply("text/html;charset=utf-8", buffer);
            return;
        }
        replyWithFile(job, path, site->root);
    }

private:
    static void replyWithFile(QWebEngineUrlRequestJob *job, const QString &path, const QString &root)
    {
        const QString canonical = QFileInfo(path).canonicalFilePath();
        if (canonical.isEmpty() || !QFileInfo(canonical).isFile()) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        if (!isInside(canonical, root)) {
            job->fail(QWebEngineUrlRequestJob::RequestDenied);
            return;
        }
        auto *file = new QFile(canonical, job);
        if (!file->open(QIODevice::ReadOnly)) {
            job->fail(QWebEngineUrlRequestJob::RequestFailed);
            return;
        }
        job->reply(QMimeDatabase().mimeTypeForFile(canonical).name().toUtf8(), file);
    }
};

} // namespace

void PreviewSite::registerScheme()
{
    QWebEngineUrlScheme scheme(schemeName);
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    // Secure: ES modules and the APIs restricted to secure contexts work as
    // on localhost. No LocalScheme: the pages cannot load file:// URLs.
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
}

void PreviewSite::serveTo(QWebEngineProfile *profile)
{
    profile->installUrlSchemeHandler(schemeName, new SiteHandler(profile));
}

PreviewSite::PreviewSite(const QString &root)
{
    static int siteCount = 0;
    host_ = QStringLiteral("site%1").arg(++siteCount);
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    sites().insert(host_, {.root = canonicalRoot.isEmpty() ? QDir::cleanPath(root) : canonicalRoot});
}

PreviewSite::~PreviewSite()
{
    sites().remove(host_);
}

QUrl PreviewSite::serve(const QString &filePath, const QString &text)
{
    SiteContent &site = sites()[host_];
    site.filePath = QDir::cleanPath(filePath);
    site.text = text.toUtf8();
    QUrl url;
    url.setScheme(QString::fromLatin1(schemeName));
    url.setHost(host_);
    url.setPath(FilePaths::toUrlPath(site.filePath));
    return url;
}

std::optional<QString> PreviewSite::localPath(const QUrl &url) const
{
    if (url.scheme() != QLatin1String(schemeName) || url.host() != host_)
        return std::nullopt;
    return FilePaths::fromUrlPath(url.path());
}
