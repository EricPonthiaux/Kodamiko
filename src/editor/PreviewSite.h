// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>
#include <QUrl>

#include <optional>

class QWebEngineProfile;

// A folder served to a web view as a local web server would, under the
// duet-preview:// scheme: the page's relative paths (scripts, ES modules,
// fetch, style sheets, images) lead to the files next to it, while its
// scripts can read no file outside the folder (file:// URLs stay out of
// their reach). One file is served from the editor's text, saved or not.
// Each site is its own origin; it is served as long as it lives.
class PreviewSite {
public:
    // Registers the scheme with Qt WebEngine: once, before the application
    // exists.
    static void registerScheme();
    // Lets the web views of `profile` load the sites.
    static void serveTo(QWebEngineProfile *profile);

    // The files under `root`, an absolute path.
    explicit PreviewSite(const QString &root);
    ~PreviewSite();
    PreviewSite(const PreviewSite &) = delete;
    PreviewSite &operator=(const PreviewSite &) = delete;

    // The address of `filePath`, served with `text` in place of its saved
    // content.
    QUrl serve(const QString &filePath, const QString &text);
    // The local file a URL of this site leads to, inside the folder or not
    // (a link the user clicks); nullopt for any other URL.
    std::optional<QString> localPath(const QUrl &url) const;

private:
    QString host_;
};
