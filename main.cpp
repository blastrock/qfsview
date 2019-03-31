/*****************************************************
 * FSView, a simple TreeMap application
 *
 * (C) 2002, Josef Weidendorfer
 */

#include "fsview.h"
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.process(app);

    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("+[folder]"), QApplication::translate("main", "View filesystem starting from this folder")));

    QString path = QStringLiteral(".");

    if (parser.positionalArguments().count() > 0) {
        path = parser.positionalArguments().at(0);
    }

    // TreeMap Widget as toplevel window
    FSView w(new Inode());

    QObject::connect(&w, SIGNAL(clicked(TreeMapItem*)),
                     &w, SLOT(selected(TreeMapItem*)));
    QObject::connect(&w, SIGNAL(returnPressed(TreeMapItem*)),
                     &w, SLOT(selected(TreeMapItem*)));
    QObject::connect(&w, SIGNAL(contextMenuRequested(TreeMapItem*,QPoint)),
                     &w, SLOT(contextMenu(TreeMapItem*,QPoint)));

    w.setPath(path);
    w.show();

    return app.exec();
}
