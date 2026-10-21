#include "app/appcontroller.h"
#include "app/articlelistmodel.h"
#include "app/streamlistmodel.h"

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QScopedPointer>
#include <QtQml>
#include <sailfishapp.h>

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    // Must match OrganizationName and ApplicationName in the .desktop file:
    // the sandbox only allows the data directories derived from them.
    app->setOrganizationName(QStringLiteral("io.github.rebexter"));
    app->setApplicationName(QStringLiteral("harbour-sailrss"));

    qmlRegisterUncreatableType<AppController>("harbour.sailrss", 1, 0, "AppController",
                                              QStringLiteral("Use the \"app\" context property"));
    qmlRegisterType<StreamListModel>("harbour.sailrss", 1, 0, "StreamListModel");
    qmlRegisterType<ArticleListModel>("harbour.sailrss", 1, 0, "ArticleListModel");

    AppController controller;
    if (!controller.open())
        return 1;

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    view->rootContext()->setContextProperty(QStringLiteral("app"), &controller);
    view->setSource(SailfishApp::pathToMainQml());
    view->show();
    return app->exec();
}
