#include "Snippet.h"

#include <QHash>
#include <QMap>

#include <climits>

namespace Snippet {
namespace {

// Recursive descent over the LSP snippet grammar, writing the plain text
// and remembering where the lowest tab stop lands.
class Parser {
public:
    explicit Parser(const QString &snippet)
        : s_(snippet)
    {
    }

    ExpandedSnippet run()
    {
        parse(false);
        ExpandedSnippet result;
        result.text = out_;
        if (bestStop_ != INT_MAX) {
            result.selectionStart = bestStart_;
            result.selectionLength = bestLength_;
        } else if (finalStop_ >= 0) {
            result.selectionStart = finalStop_;
        } else {
            result.selectionStart = int(out_.size());
        }
        for (auto it = stops_.cbegin(); it != stops_.cend(); ++it) {
            if (it.key() > 0)
                result.stops.append({it.key(), it.value()});
        }
        if (stops_.contains(0))
            result.stops.append({0, stops_.value(0)});
        return result;
    }

private:
    // Reads until the end, or until the "}" closing a placeholder.
    void parse(bool inPlaceholder)
    {
        while (i_ < s_.size()) {
            const QChar c = s_[i_];
            if (c == u'\\' && i_ + 1 < s_.size()) {
                out_ += s_[i_ + 1];
                i_ += 2;
            } else if (c == u'}' && inPlaceholder) {
                ++i_;
                return;
            } else if (c == u'$') {
                dollar();
            } else {
                out_ += c;
                ++i_;
            }
        }
    }

    int number()
    {
        int value = 0;
        while (i_ < s_.size() && s_[i_].isDigit())
            value = value * 10 + s_[i_++].digitValue();
        return value;
    }

    void stop(int index, int start)
    {
        const int length = int(out_.size()) - start;
        stops_[index].append({start, length});
        if (length > 0 && !placeholders_.contains(index))
            placeholders_.insert(index, out_.mid(start, length));
        if (index == 0) {
            if (finalStop_ < 0)
                finalStop_ = start;
        } else if (index < bestStop_) {
            bestStop_ = index;
            bestStart_ = start;
            bestLength_ = length;
        }
    }

    void dollar()
    {
        ++i_; // "$"
        if (i_ < s_.size() && s_[i_].isDigit()) { // $1
            const int index = number();
            const int start = int(out_.size());
            out_ += placeholders_.value(index);
            stop(index, start);
            return;
        }
        if (i_ < s_.size() && s_[i_] == u'{') {
            ++i_;
            if (i_ < s_.size() && s_[i_].isDigit()) {
                const int index = number();
                const int start = int(out_.size());
                if (i_ < s_.size() && s_[i_] == u':') { // ${1:text}
                    ++i_;
                    parse(true);
                } else if (i_ < s_.size() && s_[i_] == u'|') { // ${1|one,two|}
                    ++i_;
                    QString choice;
                    bool first = true;
                    while (i_ < s_.size() && s_[i_] != u'|') {
                        if (s_[i_] == u',')
                            first = false;
                        else if (first)
                            choice += s_[i_];
                        ++i_;
                    }
                    i_ += 2; // "|}"
                    out_ += choice;
                } else if (i_ < s_.size() && s_[i_] == u'}') { // ${1}
                    ++i_;
                    out_ += placeholders_.value(index);
                }
                stop(index, start);
                return;
            }
            // ${NAME} or ${NAME:default}: the default only.
            while (i_ < s_.size() && (s_[i_].isLetterOrNumber() || s_[i_] == u'_'))
                ++i_;
            if (i_ < s_.size() && s_[i_] == u':') {
                ++i_;
                parse(true);
            } else {
                while (i_ < s_.size() && s_[i_] != u'}')
                    ++i_;
                ++i_;
            }
            return;
        }
        // $NAME: a variable, nothing; a lone "$": itself.
        if (i_ < s_.size() && (s_[i_].isLetter() || s_[i_] == u'_')) {
            while (i_ < s_.size() && (s_[i_].isLetterOrNumber() || s_[i_] == u'_'))
                ++i_;
            return;
        }
        out_ += u'$';
    }

    const QString &s_;
    qsizetype i_ = 0;
    QString out_;
    int bestStop_ = INT_MAX;
    int bestStart_ = 0;
    int bestLength_ = 0;
    int finalStop_ = -1;
    QMap<int, QList<ExpandedSnippet::Range>> stops_; // by index
    QHash<int, QString> placeholders_; // the first text of each tab stop
};

} // namespace

ExpandedSnippet expand(const QString &snippet)
{
    return Parser(snippet).run();
}

QString escape(QString text)
{
    text.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    text.replace(QLatin1Char('$'), QLatin1String("\\$"));
    text.replace(QLatin1Char('}'), QLatin1String("\\}"));
    return text;
}

} // namespace Snippet
