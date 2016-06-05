/********************************************************************************
** Form generated from reading UI file 'AvatarSettingsDialogfn6616.ui'
**
** Created by: Qt User Interface Compiler version 5.5.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef AVATARSETTINGSDIALOGFN6616_H
#define AVATARSETTINGSDIALOGFN6616_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include "../qt/FileSelectWidget.h"
#include "AvatarPreviewWidget.h"

QT_BEGIN_NAMESPACE

class Ui_AvatarSettingsDialog
{
public:
    QVBoxLayout *verticalLayout;
    FileSelectWidget *avatarSelectWidget;
    QWidget *centralWidget;
    QVBoxLayout *verticalLayout_2;
    AvatarPreviewWidget *avatarPreviewGLWidget;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *AvatarSettingsDialog)
    {
        if (AvatarSettingsDialog->objectName().isEmpty())
            AvatarSettingsDialog->setObjectName(QStringLiteral("AvatarSettingsDialog"));
        AvatarSettingsDialog->resize(400, 300);
        verticalLayout = new QVBoxLayout(AvatarSettingsDialog);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        avatarSelectWidget = new FileSelectWidget(AvatarSettingsDialog);
        avatarSelectWidget->setObjectName(QStringLiteral("avatarSelectWidget"));
        avatarSelectWidget->setMaximumSize(QSize(16777215, 20));

        verticalLayout->addWidget(avatarSelectWidget);

        centralWidget = new QWidget(AvatarSettingsDialog);
        centralWidget->setObjectName(QStringLiteral("centralWidget"));
        verticalLayout_2 = new QVBoxLayout(centralWidget);
        verticalLayout_2->setObjectName(QStringLiteral("verticalLayout_2"));
        avatarPreviewGLWidget = new AvatarPreviewWidget(centralWidget);
        avatarPreviewGLWidget->setObjectName(QStringLiteral("avatarPreviewGLWidget"));

        verticalLayout_2->addWidget(avatarPreviewGLWidget);


        verticalLayout->addWidget(centralWidget);

        buttonBox = new QDialogButtonBox(AvatarSettingsDialog);
        buttonBox->setObjectName(QStringLiteral("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);


        retranslateUi(AvatarSettingsDialog);
        QObject::connect(buttonBox, SIGNAL(accepted()), AvatarSettingsDialog, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), AvatarSettingsDialog, SLOT(reject()));

        QMetaObject::connectSlotsByName(AvatarSettingsDialog);
    } // setupUi

    void retranslateUi(QDialog *AvatarSettingsDialog)
    {
        AvatarSettingsDialog->setWindowTitle(QApplication::translate("AvatarSettingsDialog", "Avatar Settings", 0));
    } // retranslateUi

};

namespace Ui {
    class AvatarSettingsDialog: public Ui_AvatarSettingsDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // AVATARSETTINGSDIALOGFN6616_H
