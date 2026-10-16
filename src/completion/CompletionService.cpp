#include "CompletionService.h"

#include <QHash>
#include <QPointer>

CompletionService::CompletionService(QObject *parent)
    : QObject(parent)
{
}

CompletionService::~CompletionService() = default;

void CompletionService::addProvider(std::shared_ptr<ICompletionProvider> provider)
{
    providers_.push_back(std::move(provider));
}

void CompletionService::merge(QList<CompletionItem> &into, const QList<CompletionItem> &items)
{
    QHash<QString, qsizetype> index;
    index.reserve(into.size() + items.size());
    for (qsizetype i = 0; i < into.size(); ++i)
        index.insert(into[i].label, i);
    for (const CompletionItem &item : items) {
        const auto it = index.constFind(item.label);
        if (it == index.constEnd()) {
            index.insert(item.label, into.size());
            into.append(item);
            continue;
        }
        // The same name from two sources: the one that knows more.
        CompletionItem &existing = into[it.value()];
        const bool richer = item.priority > existing.priority
                         || (existing.kind == CompletionItem::Kind::Text && item.kind != CompletionItem::Kind::Text);
        if (richer)
            existing = item;
    }
}

void CompletionService::request(const CompletionContext &context, const Update &update)
{
    const int generation = ++generation_;
    // Shared by the answers of this request.
    auto gathered = std::make_shared<QList<CompletionItem>>();
    auto incomplete = std::make_shared<bool>(false);
    QPointer<CompletionService> self(this);
    for (const std::shared_ptr<ICompletionProvider> &provider : providers_) {
        if (!provider->handles(context))
            continue;
        provider->complete(context, [self, generation, gathered, incomplete,
                                     update](const QList<CompletionItem> &items, bool partial) {
            if (!self || self->generation_ != generation)
                return; // outdated
            merge(*gathered, items);
            *incomplete = *incomplete || partial;
            update(*gathered, *incomplete);
        });
    }
}

void CompletionService::cancel()
{
    ++generation_;
}

bool CompletionService::isTrigger(const QString &languageId, QChar c, QChar previous) const
{
    for (const std::shared_ptr<ICompletionProvider> &provider : providers_) {
        const QString triggers = provider->triggerCharacters(languageId);
        if (!triggers.contains(c))
            continue;
        // ">" only after "-", ":" only after ":" (not "a > b", "case x:").
        if (c == QLatin1Char('>'))
            return previous == QLatin1Char('-');
        if (c == QLatin1Char(':'))
            return previous == QLatin1Char(':');
        return true;
    }
    return false;
}
