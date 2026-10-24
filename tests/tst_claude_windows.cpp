// SPDX-License-Identifier: GPL-3.0-only

#include "agent/vendor/claude/ClaudeConfig.h"

#include <QtTest>

// Claude Code's folders named after Windows paths: the drive's colon and the separators become dashes.
class TestClaudeWindows : public QObject {
    Q_OBJECT

private slots:

    void autoMemoryFollowsClaudeCodeNaming()
    {
        qputenv("CLAUDE_CONFIG_DIR", "/c");

        const QString memory = ClaudeConfig::autoMemoryDir("C:/Users/me/my_app.v2");

        qunsetenv("CLAUDE_CONFIG_DIR");
        QCOMPARE(memory, QStringLiteral("/c/projects/C--Users-me-my-app-v2/memory"));
    }
};

QTEST_GUILESS_MAIN(TestClaudeWindows)
#include "tst_claude_windows.moc"
