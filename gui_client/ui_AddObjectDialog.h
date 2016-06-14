/********************************************************************************
** Form generated from reading UI file 'AddObjectDialogmU9760.ui'
**
** Created by: Qt User Interface Compiler version 5.5.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef ADDOBJECTDIALOGMU9760_H
#define ADDOBJECTDIALOGMU9760_H

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
#include "AddObjectPreviewWidget.h"

QT_BEGIN_NAMESPACE

class Ui_AddObjectDialog
{
public:
    QVBoxLayout *verticalLayout;
    FileSelectWidget *avatarSelectWidget;
    QWidget *centralWidget;
    QVBoxLayout *verticalLayout_2;
    AddObjectPreviewWidget *avatarPreviewGLWidget;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *AddObjectDialog)
    {
        if (AddObjectDialog->objectName().isEmpty())
            AddObjectDialog->setObjectName(QStringLiteral("AddObjectDialog"));
        AddObjectDialog->resize(400, 300);
        verticalLayout = new QVBoxLayout(AddObjectDialog);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        avatarSelectWidget = new FileSelectWidget(AddObjectDialog);
        avatarSelectWidget->setObjectName(QStringLiteral("avatarSelectWidget"));
        avatarSelectWidget->setMaximumSize(QSize(16777215, 20));

        verticalLayout->addWidget(avatarSelectWidget);

        centralWidget = new QWidget(AddObjectDialog);
        centralWidget->setObjectName(QStringLiteral("centralWidget"));
        verticalLayout_2 = new QVBoxLayout(centralWidget);
        verticalLayout_2->setObjectName(QStringLiteral("verticalLayout_2"));
        avatarPreviewGLWidget = new AddObjectPreviewWidget(centralWidget);
        avatarPreviewGLWidget->setObjectName(QStringLiteral("avatarPreviewGLWidget"));

        verticalLayout_2->addWidget(avatarPreviewGLWidget);


        verticalLayout->addWidget(centralWidget);

        buttonBox = new QDialogButtonBox(AddObjectDialog);
        buttonBox->setObjectName(QStringLiteral("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);


        retranslateUi(AddObjectDialog);
        QObject::connect(buttonBox, SIGNAL(accepted()), AddObjectDialog, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), AddObjectDialog, SLOT(reject()));

        QMetaObject::connectSlotsByName(AddObjectDialog);
    } // setupUi

    void retranslateUi(QDialog *AddObjectDialog)
    {
        AddObjectDialog->setWindowTitle(QApplication::translate("AddObjectDialog", "Add Object", 0));
    } // retranslateUi

};

namespace Ui {
    class AddObjectDialog: public Ui_AddObjectDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // ADDOBJECTDIALOGMU9760_H
