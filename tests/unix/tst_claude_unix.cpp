// SPDX-License-Identifier: GPL-3.0-only

#include "agent/vendor/claude/ClaudeConfig.h"

#include <QtTest>

// Claude Code's folders named after Unix paths: the separators become dashes.
class TestClaudeUnix : public QObject {
    Q_OBJECT

private slots:

    void autoMemoryFollowsClaudeCodeNaming()
    {
        qputenv("CLAUDE_CONFIG_DIR", "/c");

        const QString memory = ClaudeConfig::autoMemoryDir("/home/me/my_app.v2");

        qunsetenv("CLAUDE_CONFIG_DIR");
        QCOMPARE(memory, QStringLiteral("/c/projects/-home-me-my-app-v2/memory"));
    }
};

QTEST_GUILESS_MAIN(TestClaudeUnix)
#include "tst_claude_unix.moc"
