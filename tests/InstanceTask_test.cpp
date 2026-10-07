// SPDX-License-Identifier: GPL-3.0-only
#include <QTest>

#include "InstanceTask.h"

class StagingPolicyTask : public InstanceTask {
   private:
    void executeTask() override {}
};

class InstanceTaskTest : public QObject {
    Q_OBJECT

   private slots:
    void stagingIsPreservedOnlyWhenExplicitlyRequested()
    {
        StagingPolicyTask task;
        QVERIFY(task.shouldDestroyStagingOnFailure());
        QVERIFY(task.shouldDestroyStagingOnAbort());
        task.setPreserveStagingOnFailure(true);
        QVERIFY(!task.shouldDestroyStagingOnFailure());
        QVERIFY(task.shouldDestroyStagingOnAbort());
    }
};

QTEST_GUILESS_MAIN(InstanceTaskTest)
#include "InstanceTask_test.moc"
