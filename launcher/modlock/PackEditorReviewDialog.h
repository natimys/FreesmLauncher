// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDialog>
#include <QJsonObject>

class QLineEdit;

class PackEditorReviewDialog final : public QDialog {
    Q_OBJECT
   public:
    explicit PackEditorReviewDialog(const QJsonObject& preview, QWidget* parent = nullptr);
    QString commitMessage() const;

   private:
    QLineEdit* m_commitMessage = nullptr;
};
