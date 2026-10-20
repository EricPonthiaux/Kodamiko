// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Completion.h"

// Documentation comments, as in CLion: Doxygen in C and C++, Javadoc in
// Java, JSDoc in JavaScript and TypeScript.

namespace DocComments {

// Start of the documentation comment ("/**", "/*!", "///", "//!") that
// `position` is in, -1 when it is in none.
int commentStart(const QByteArray &text, int position, const QString &languageId);

// The comment to write for the declaration that follows `position` (just
// after the "/**" typed), as a snippet: a line for the summary, one
// "@param" per parameter, "@tparam" per template parameter, "@return"
// when it returns a value, "@throws" per exception declared. `indent`
// starts each line. With no function below, the summary line alone.
QString stub(const QByteArray &text, int position, const QString &languageId, const QString &indent);

} // namespace DocComments

// Inside a documentation comment: the tags after "@" or "\" ("param",
// "return", "brief"...), and after "@param" the parameters of the
// function documented, those not documented yet.
class DocCommentProvider : public ICompletionProvider {
public:
    bool handles(const CompletionContext &context) const override;
    void complete(const CompletionContext &context, const Callback &done) override;

    bool servesComments() const override { return true; }
};
