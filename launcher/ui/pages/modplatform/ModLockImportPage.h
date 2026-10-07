// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QWidget>

#include "ui/pages/BasePage.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QGroupBox;
class NewInstanceDialog;
class ModLockBridge;
class SequentialTask;

class ModLockImportPage final : public QWidget, public BasePage {
    Q_OBJECT

   public:
    explicit ModLockImportPage(NewInstanceDialog* dialog, QWidget* parent = nullptr);
    QString displayName() const override { return tr("ModLock"); }
    QIcon icon() const override { return QIcon::fromTheme("package"); }
    QString id() const override { return "modlock"; }
    QString helpPage() const override { return "ModLock"; }
    bool shouldDisplay() const override { return true; }
    void retranslate() override {}

    void setRepository(const QString& repository);
    void preview();
    void invalidatePreview();

   private:
    NewInstanceDialog* m_dialog;
    QLineEdit* m_repository;
    QLineEdit* m_branch;
    QLineEdit* m_lockPath;
    QGroupBox* m_advanced;
    QLabel* m_status;
    QPushButton* m_preview;
    ModLockBridge* m_bridge = nullptr;
    quint64 m_generation = 0;
};
