// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>
#include <QString>

class AgentProvider;

// Every agent Duet can run, and how settings name them. The providers live
// as long as the application; the pointers and references returned are not
// owned.
namespace AgentProviders {

// In the order the menus list them.
const QList<const AgentProvider *> &all();

// The provider settings store as `key` (see key()); Claude Code for an empty
// or unknown key.
const AgentProvider &find(const QString &key);

// How settings store the choice of `provider` ("claude-code").
QString key(const AgentProvider &provider);

} // namespace AgentProviders
