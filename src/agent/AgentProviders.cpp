// SPDX-License-Identifier: GPL-3.0-only

#include "AgentProviders.h"

#include "agent/vendor/claude/ClaudeCodeProvider.h"

namespace AgentProviders {
namespace {

const ClaudeCodeProvider &claudeCode()
{
    static const ClaudeCodeProvider provider;
    return provider;
}

} // namespace

const QList<const AgentProvider *> &all()
{
    static const QList<const AgentProvider *> providers{&claudeCode()};
    return providers;
}

const AgentProvider &find(const QString &key)
{
    for (const AgentProvider *provider : all()) {
        if (AgentProviders::key(*provider) == key)
            return *provider;
    }
    return claudeCode();
}

QString key(const AgentProvider &provider)
{
    // NOLINTNEXTLINE(readability-trivial-switch): a case per provider, no default: the compiler names a missing one
    switch (provider.id()) {
    case AgentProviderId::ClaudeCode:
        return QStringLiteral("claude-code");
    }
    return {};
}

} // namespace AgentProviders
