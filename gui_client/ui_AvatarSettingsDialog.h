/********************************************************************************
** Form generated from reading UI file 'AvatarSettingsDialogh11236.ui'
**
** Created by: Qt User Interface Compiler version 5.5.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef AVATARSETTINGSDIALOGH11236_H
#define AVATARSETTINGSDIALOGH11236_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include "../qt/FileSelectWidget.h"
#include "AvatarPreviewWidget.h"

QT_BEGIN_NAMESPACE

class Ui_AvatarSettingsDialog
{
public:
    QVBoxLayout *verticalLayout;
    QWidget *widget;
    QHBoxLayout *horizontalLayout;
    QLabel *label;
    QLineEdit *usernameLineEdit;
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
        widget = new QWidget(AvatarSettingsDialog);
        widget->setObjectName(QStringLiteral("widget"));
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(widget->sizePolicy().hasHeightForWidth());
        widget->setSizePolicy(sizePolicy);
        horizontalLayout = new QHBoxLayout(widget);
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        label = new QLabel(widget);
        label->setObjectName(QStringLiteral("label"));

        horizontalLayout->addWidget(label);

        usernameLineEdit = new QLineEdit(widget);
        usernameLineEdit->setObjectName(QStringLiteral("usernameLineEdit"));

        horizontalLayout->addWidget(usernameLineEdit);


        verticalLayout->addWidget(widget);

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
        label->setText(QApplication::translate("AvatarSettingsDialog", "User name:", 0));
    } // retranslateUi

};

namespace Ui {
    class AvatarSettingsDialog: public Ui_AvatarSettingsDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // AVATARSETTINGSDIALOGH11236_H
