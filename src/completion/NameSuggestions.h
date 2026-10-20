// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Completion.h"

#include <QStringList>

// Names for the variable or parameter being declared, from its type, as
// CLion suggests them: "QListWidget |" offers "listWidget" and "widget",
// "std::vector<Item> |" offers "items", "int |" offers "i". For C, C++
// and Java.
class NameSuggestionProvider : public ICompletionProvider {
public:
    bool handles(const CompletionContext &context) const override;
    void complete(const CompletionContext &context, const Callback &done) override;

    // The type declared before `position` ("const Widget *" in "const
    // Widget *|"), empty when `position` is no declared name.
    static QString declaredTypeAt(const QByteArray &text, int position, const QString &languageId);
    // Names for a value of `type`, the likeliest first.
    static QStringList namesFor(const QString &type, const QString &languageId);
};
