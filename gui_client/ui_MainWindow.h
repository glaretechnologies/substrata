/********************************************************************************
** Form generated from reading UI file 'MainWindowa11236.ui'
**
** Created by: Qt User Interface Compiler version 5.5.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef MAINWINDOWA11236_H
#define MAINWINDOWA11236_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include "GlWidget.h"

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QAction *actionAvatarSettings;
    QAction *actionAddObject;
    QWidget *centralwidget;
    QVBoxLayout *verticalLayout;
    GlWidget *glWidget;
    QMenuBar *menubar;
    QStatusBar *statusbar;
    QToolBar *toolBar;
    QDockWidget *chatDockWidget;
    QWidget *dockWidgetContents;
    QVBoxLayout *verticalLayout_3;
    QWidget *chatWidget;
    QVBoxLayout *verticalLayout_2;
    QTextEdit *chatMessagesTextEdit;
    QWidget *widget;
    QHBoxLayout *horizontalLayout;
    QLineEdit *chatMessageLineEdit;
    QPushButton *chatPushButton;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName(QStringLiteral("MainWindow"));
        MainWindow->resize(1090, 639);
        actionAvatarSettings = new QAction(MainWindow);
        actionAvatarSettings->setObjectName(QStringLiteral("actionAvatarSettings"));
        actionAddObject = new QAction(MainWindow);
        actionAddObject->setObjectName(QStringLiteral("actionAddObject"));
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName(QStringLiteral("centralwidget"));
        verticalLayout = new QVBoxLayout(centralwidget);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        glWidget = new GlWidget(centralwidget);
        glWidget->setObjectName(QStringLiteral("glWidget"));

        verticalLayout->addWidget(glWidget);

        MainWindow->setCentralWidget(centralwidget);
        menubar = new QMenuBar(MainWindow);
        menubar->setObjectName(QStringLiteral("menubar"));
        menubar->setGeometry(QRect(0, 0, 1090, 21));
        MainWindow->setMenuBar(menubar);
        statusbar = new QStatusBar(MainWindow);
        statusbar->setObjectName(QStringLiteral("statusbar"));
        MainWindow->setStatusBar(statusbar);
        toolBar = new QToolBar(MainWindow);
        toolBar->setObjectName(QStringLiteral("toolBar"));
        MainWindow->addToolBar(Qt::TopToolBarArea, toolBar);
        chatDockWidget = new QDockWidget(MainWindow);
        chatDockWidget->setObjectName(QStringLiteral("chatDockWidget"));
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(chatDockWidget->sizePolicy().hasHeightForWidth());
        chatDockWidget->setSizePolicy(sizePolicy);
        dockWidgetContents = new QWidget();
        dockWidgetContents->setObjectName(QStringLiteral("dockWidgetContents"));
        verticalLayout_3 = new QVBoxLayout(dockWidgetContents);
        verticalLayout_3->setObjectName(QStringLiteral("verticalLayout_3"));
        chatWidget = new QWidget(dockWidgetContents);
        chatWidget->setObjectName(QStringLiteral("chatWidget"));
        verticalLayout_2 = new QVBoxLayout(chatWidget);
        verticalLayout_2->setObjectName(QStringLiteral("verticalLayout_2"));
        chatMessagesTextEdit = new QTextEdit(chatWidget);
        chatMessagesTextEdit->setObjectName(QStringLiteral("chatMessagesTextEdit"));
        chatMessagesTextEdit->setReadOnly(true);

        verticalLayout_2->addWidget(chatMessagesTextEdit);

        widget = new QWidget(chatWidget);
        widget->setObjectName(QStringLiteral("widget"));
        QSizePolicy sizePolicy1(QSizePolicy::Preferred, QSizePolicy::Fixed);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(widget->sizePolicy().hasHeightForWidth());
        widget->setSizePolicy(sizePolicy1);
        horizontalLayout = new QHBoxLayout(widget);
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        chatMessageLineEdit = new QLineEdit(widget);
        chatMessageLineEdit->setObjectName(QStringLiteral("chatMessageLineEdit"));

        horizontalLayout->addWidget(chatMessageLineEdit);

        chatPushButton = new QPushButton(widget);
        chatPushButton->setObjectName(QStringLiteral("chatPushButton"));

        horizontalLayout->addWidget(chatPushButton);


        verticalLayout_2->addWidget(widget);


        verticalLayout_3->addWidget(chatWidget);

        chatDockWidget->setWidget(dockWidgetContents);
        MainWindow->addDockWidget(static_cast<Qt::DockWidgetArea>(2), chatDockWidget);

        toolBar->addAction(actionAvatarSettings);
        toolBar->addAction(actionAddObject);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QApplication::translate("MainWindow", "MainWindow", 0));
        actionAvatarSettings->setText(QApplication::translate("MainWindow", "Avatar Settings", 0));
        actionAddObject->setText(QApplication::translate("MainWindow", "Add Object", 0));
        toolBar->setWindowTitle(QApplication::translate("MainWindow", "toolBar", 0));
        chatDockWidget->setWindowTitle(QApplication::translate("MainWindow", "Chat", 0));
        chatPushButton->setText(QApplication::translate("MainWindow", "Chat", 0));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // MAINWINDOWA11236_H
