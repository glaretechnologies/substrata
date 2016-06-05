/********************************************************************************
** Form generated from reading UI file 'FileSelectWidget.ui'
**
** Created by: Qt User Interface Compiler version 5.5.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_FILESELECTWIDGET_H
#define UI_FILESELECTWIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_FileSelectWidget
{
public:
    QHBoxLayout *horizontalLayout;
    QLineEdit *filePath;
    QPushButton *fileSelectButton;

    void setupUi(QWidget *FileSelectWidget)
    {
        if (FileSelectWidget->objectName().isEmpty())
            FileSelectWidget->setObjectName(QStringLiteral("FileSelectWidget"));
        FileSelectWidget->resize(304, 23);
        horizontalLayout = new QHBoxLayout(FileSelectWidget);
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        filePath = new QLineEdit(FileSelectWidget);
        filePath->setObjectName(QStringLiteral("filePath"));

        horizontalLayout->addWidget(filePath);

        fileSelectButton = new QPushButton(FileSelectWidget);
        fileSelectButton->setObjectName(QStringLiteral("fileSelectButton"));
        QSizePolicy sizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(fileSelectButton->sizePolicy().hasHeightForWidth());
        fileSelectButton->setSizePolicy(sizePolicy);

        horizontalLayout->addWidget(fileSelectButton);


        retranslateUi(FileSelectWidget);

        QMetaObject::connectSlotsByName(FileSelectWidget);
    } // setupUi

    void retranslateUi(QWidget *FileSelectWidget)
    {
        FileSelectWidget->setWindowTitle(QApplication::translate("FileSelectWidget", "Form", 0));
        fileSelectButton->setText(QApplication::translate("FileSelectWidget", "Browse", 0));
    } // retranslateUi

};

namespace Ui {
    class FileSelectWidget: public Ui_FileSelectWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_FILESELECTWIDGET_H
