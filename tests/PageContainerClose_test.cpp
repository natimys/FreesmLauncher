// SPDX-License-Identifier: GPL-3.0-only
#include <QApplication>
#include <QTest>
#include <QWidget>

#include "ui/pages/BasePage.h"
#include "ui/pages/BasePageProvider.h"
#include "ui/widgets/PageContainer.h"

class CloseGuardPage final : public QWidget, public BasePage {
   public:
    QString id() const override { return QStringLiteral("guard"); }
    QString displayName() const override { return QStringLiteral("Guard"); }
    QIcon icon() const override { return {}; }
    bool apply() override
    {
        ++applyCalls;
        return true;
    }
    bool prepareToClose() override
    {
        ++closeCalls;
        return allowClose;
    }

    bool allowClose = false;
    int closeCalls = 0;
    int applyCalls = 0;
};

class CloseGuardProvider final : public BasePageProvider {
   public:
    explicit CloseGuardProvider(CloseGuardPage* page) : page(page) {}
    QList<BasePage*> getPages() override { return {page}; }
    QString dialogTitle() override { return QStringLiteral("Close guard test"); }

    CloseGuardPage* page;
};

class PageContainerCloseTest final : public QObject {
    Q_OBJECT

   private slots:
    void closeGuardRunsWithoutSavingPages();
};

void PageContainerCloseTest::closeGuardRunsWithoutSavingPages()
{
    auto* page = new CloseGuardPage();
    CloseGuardProvider provider(page);
    PageContainer container(&provider, QStringLiteral("guard"));

    QVERIFY(!container.prepareToClose(false));
    QCOMPARE(page->closeCalls, 1);
    QCOMPARE(page->applyCalls, 0);

    page->allowClose = true;
    QVERIFY(container.prepareToClose(false));
    QCOMPARE(page->closeCalls, 2);
    QCOMPARE(page->applyCalls, 0);
}

QTEST_MAIN(PageContainerCloseTest)
#include "PageContainerClose_test.moc"
