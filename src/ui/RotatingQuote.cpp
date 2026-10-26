// SPDX-License-Identifier: GPL-3.0-only

#include "RotatingQuote.h"

#include "Motion.h"

#include <QCoreApplication>
#include <QLabel>
#include <QRandomGenerator>
#include <QTimer>
#include <QVBoxLayout>

#include <array>
#include <chrono>

using namespace std::chrono_literals;

namespace {

struct Quote {
    const char *text;
    const char *author;
};

constexpr std::chrono::milliseconds quoteDuration = 8s;

constexpr std::array quotes{
    Quote{QT_TRANSLATE_NOOP("RotatingQuote",
                            "Programs must be written for people to read, and only incidentally "
                            "for machines to execute."),
          "Harold Abelson"},
    Quote{QT_TRANSLATE_NOOP("RotatingQuote",
                            "Any fool can write code that a computer can understand. Good "
                            "programmers write code that humans can understand."),
          "Martin Fowler"},
    Quote{QT_TRANSLATE_NOOP("RotatingQuote", "Simplicity is prerequisite for reliability."), "Edsger W. Dijkstra"},
    Quote{QT_TRANSLATE_NOOP("RotatingQuote", "Controlling complexity is the essence of computer programming."),
          "Brian Kernighan"},
    Quote{QT_TRANSLATE_NOOP("RotatingQuote", "Premature optimization is the root of all evil."), "Donald Knuth"},
    Quote{QT_TRANSLATE_NOOP("RotatingQuote", "Make it work, make it right, make it fast."), "Kent Beck"},
    Quote{QT_TRANSLATE_NOOP("RotatingQuote", "Talk is cheap. Show me the code."), "Linus Torvalds"},
    Quote{QT_TRANSLATE_NOOP("RotatingQuote", "The best way to predict the future is to invent it."), "Alan Kay"},
    Quote{QT_TRANSLATE_NOOP("RotatingQuote",
                            "Perfection is achieved not when there is nothing more to add, but "
                            "when there is nothing left to take away."),
          "Antoine de Saint-Exupéry"},
    Quote{QT_TRANSLATE_NOOP("RotatingQuote", "Alone we can do so little; together we can do so much."), "Helen Keller"},
};

} // namespace

RotatingQuote::RotatingQuote(QWidget *parent)
    : QWidget(parent)
    , text_(new QLabel(this))
    , author_(new QLabel(this))
    , rotation_(new QTimer(this))
{
    text_->setWordWrap(true);
    author_->setObjectName(QStringLiteral("settingsDescription"));
    author_->setAlignment(Qt::AlignRight);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(text_);
    layout->addWidget(author_);

    rotation_->setInterval(quoteDuration);
    connect(rotation_, &QTimer::timeout, this, &RotatingQuote::showNextQuote);
    showQuote(0);
}

void RotatingQuote::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    showQuote(QRandomGenerator::global()->bounded(int(quotes.size())));
    rotation_->start();
}

void RotatingQuote::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    rotation_->stop();
}

void RotatingQuote::showQuote(qsizetype index)
{
    index_ = index;
    const Quote &quote = quotes.at(std::size_t(index));
    text_->setText(QStringLiteral("“%1”").arg(QCoreApplication::translate("RotatingQuote", quote.text)));
    author_->setText(QStringLiteral("— %1").arg(QString::fromUtf8(quote.author)));
}

void RotatingQuote::showNextQuote()
{
    showQuote((index_ + 1) % qsizetype(quotes.size()));
    Motion::fadeIn(this);
}
