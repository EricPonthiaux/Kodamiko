// SPDX-License-Identifier: GPL-3.0-only

#include "ShellCommandLine.h"

namespace ShellCommandLine {

namespace {

using Words = QStringList;

// Reads a shell command line into its simple commands: enough to find the
// git commands in it, not a shell (nothing is expanded).
class CommandLineReader {
public:
    explicit CommandLineReader(const QString &line)
        : line_(line)
    {
    }

    QList<Words> commands()
    {
        while (at_ < line_.size()) {
            const QChar c = line_.at(at_);
            if (c == QLatin1Char('\'') || c == QLatin1Char('"'))
                readQuoted(c);
            else if (c == QLatin1Char('\\'))
                readEscaped();
            else if (!readOperator())
                readPlain(c);
        }
        endCommand();
        return commands_;
    }

private:
    // The character at `index`, or a null one outside the line.
    QChar charAt(qsizetype index) const { return index >= 0 && index < line_.size() ? line_.at(index) : QChar(); }

    void readPlain(QChar c)
    {
        if (c.isSpace()) {
            endWord();
        } else {
            word_ += c;
            inWord_ = true;
        }
        ++at_;
    }

    // Between double quotes, a backslash escapes only $ ` " \ and a newline.
    void readQuoted(QChar quote)
    {
        static const QString escapable = QStringLiteral("$`\"\\\n");
        inWord_ = true;
        for (++at_; at_ < line_.size() && line_.at(at_) != quote; ++at_) {
            const bool isEscape =
                quote == QLatin1Char('"') && line_.at(at_) == QLatin1Char('\\') && escapable.contains(charAt(at_ + 1));
            if (isEscape)
                ++at_;
            word_ += line_.at(at_);
        }
        ++at_; // the closing quote
    }

    void readEscaped()
    {
        ++at_;
        if (at_ < line_.size() && line_.at(at_) != QLatin1Char('\n')) {
            word_ += line_.at(at_);
            inWord_ = true;
        }
        ++at_;
    }

    // ; | & && || and a newline end a command; & in a redirection (2>&1,
    // &>) does not.
    bool readOperator()
    {
        static const QString operators = QStringLiteral(";|&\n");
        const QChar c = line_.at(at_);
        if (!operators.contains(c))
            return false;
        const bool isRedirection =
            c == QLatin1Char('&') && (charAt(at_ - 1) == QLatin1Char('>') || charAt(at_ + 1) == QLatin1Char('>'));
        if (isRedirection)
            return false;
        endCommand();
        const bool isDoubled = (c == QLatin1Char('&') || c == QLatin1Char('|')) && charAt(at_ + 1) == c;
        at_ += isDoubled ? 2 : 1;
        return true;
    }

    void endWord()
    {
        if (!inWord_)
            return;
        words_.append(word_);
        word_.clear();
        inWord_ = false;
    }

    void endCommand()
    {
        endWord();
        if (!words_.isEmpty())
            commands_.append(words_);
        words_.clear();
    }

    const QString &line_;
    qsizetype at_ = 0;
    QString word_;
    bool inWord_ = false;
    Words words_;
    QList<Words> commands_;
};

} // namespace

QList<QStringList> commands(const QString &line)
{
    return CommandLineReader(line).commands();
}

} // namespace ShellCommandLine
