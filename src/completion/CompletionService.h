#pragma once

#include "Completion.h"
#include "CompletionHistory.h"

#include <QObject>

#include <memory>
#include <vector>

// Asks every provider that handles a context and merges their candidates:
// one item per label, the richest kept (a language server's method over a
// word of the file). Shared by all the editors.
class CompletionService : public QObject {
    Q_OBJECT

public:
    // `incomplete`: a provider kept only part of its candidates.
    using Update = std::function<void(const QList<CompletionItem> &items, bool incomplete)>;

    explicit CompletionService(QObject *parent = nullptr);
    ~CompletionService() override;

    void addProvider(std::shared_ptr<ICompletionProvider> provider);
    // What the user chose before, to rank the candidates (see
    // CompletionController).
    CompletionHistory *history() { return &history_; }

    // Asks the providers; calls `update` with all the candidates gathered so
    // far each time one answers (right away for the local ones). A new
    // request, or cancel(), makes the older one's late answers ignored.
    void request(const CompletionContext &context, const Update &update);
    void cancel();

    // Whether typing `c` (after `previous`) opens completion in this
    // language: "." for members, "->" and "::" in C++.
    bool isTrigger(const QString &languageId, QChar c, QChar previous) const;

    // Merges `items` into `into`, by label.
    static void merge(QList<CompletionItem> &into, const QList<CompletionItem> &items);

private:
    std::vector<std::shared_ptr<ICompletionProvider>> providers_;
    CompletionHistory history_;
    int generation_ = 0;
};
