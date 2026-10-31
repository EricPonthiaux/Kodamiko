// SPDX-License-Identifier: GPL-3.0-only

// CodeIntelligence: Call Hierarchy and Type Hierarchy.

#include "CodeIntelligence.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "lsp/LspClient.h"

#include <QFileInfo>
#include <QPointer>

using namespace LspProtocol;

// What differs between the two hierarchies (declared in CodeIntelligence.h).
struct HierarchyKind {
    QString capability;
    QString prepareMethod;
    QString upMethod;
    QString downMethod;
    // In the answers of the up and down methods: the calls' "from" and
    // "to", or the items themselves (empty).
    QString upKey;
    QString downKey;
};

namespace {

const HierarchyKind &calls()
{
    static const HierarchyKind kind{QStringLiteral("callHierarchyProvider"),
                                    QStringLiteral("textDocument/prepareCallHierarchy"),
                                    QStringLiteral("callHierarchy/incomingCalls"),
                                    QStringLiteral("callHierarchy/outgoingCalls"),
                                    QStringLiteral("from"),
                                    QStringLiteral("to")};
    return kind;
}

const HierarchyKind &types()
{
    static const HierarchyKind kind{QStringLiteral("typeHierarchyProvider"),
                                    QStringLiteral("textDocument/prepareTypeHierarchy"),
                                    QStringLiteral("typeHierarchy/supertypes"),
                                    QStringLiteral("typeHierarchy/subtypes"),
                                    {},
                                    {}};
    return kind;
}

// The children of a hierarchy's elements, asked to `server`.
Hierarchy::Children childrenFrom(LspClient *server, const HierarchyKind &kind)
{
    return [server = QPointer<LspClient>(server), kind](Hierarchy::Direction direction, const HierarchyItem &parent,
                                                        const Hierarchy::ChildrenReply &reply) {
        if (!server)
            return;
        const bool isUp = direction == Hierarchy::Direction::Up;
        const QString key = isUp ? kind.upKey : kind.downKey;
        server->request(isUp ? kind.upMethod : kind.downMethod, QJsonObject{{"item", parent.raw}},
                        [reply, key](const QJsonValue &result, const QString &) {
                            reply(key.isEmpty() ? parseHierarchyItems(result) : parseHierarchyCalls(result, key));
                        });
    };
}

} // namespace

void CodeIntelligence::showCallHierarchy()
{
    showHierarchy(calls(), {tr("Calls of “%1”"), tr("Callers"), tr("Callees"), {}, {}});
}

void CodeIntelligence::showTypeHierarchy()
{
    showHierarchy(types(), {tr("Types of “%1”"), tr("Supertypes"), tr("Subtypes"), {}, {}});
}

void CodeIntelligence::showHierarchy(const HierarchyKind &kind, Hierarchy hierarchy)
{
    EditorWidget *editor = tabs_->currentEditor();
    LspClient *client = serverFor(editor);
    if (!client)
        return;
    // Known once the server is ready; until then, the request waits.
    if (client->isReady() && !client->capabilities().contains(kind.capability)) {
        emit statusMessage(tr("%1 does not support this command").arg(QFileInfo(client->program()).fileName()));
        return;
    }
    const int caret = int(editor->send(SCI_GETCURRENTPOS));
    client->request(kind.prepareMethod, positionParams(editor, caret),
                    [this, kind, server = QPointer<LspClient>(client),
                     hierarchy = std::move(hierarchy)](const QJsonValue &result, const QString &error) mutable {
                        hierarchy.roots = parseHierarchyItems(result);
                        if (!error.isEmpty() || hierarchy.roots.isEmpty() || !server) {
                            emit statusMessage(error.isEmpty() ? tr("Place the cursor on a function or a type")
                                                               : tr("No hierarchy: %1").arg(error));
                            return;
                        }
                        hierarchy.title = hierarchy.title.arg(hierarchy.roots.first().name);
                        hierarchy.children = childrenFrom(server, kind);
                        emit hierarchyFound(hierarchy);
                    });
}
