#ifndef SAILRSS_IMAGECACHE_H
#define SAILRSS_IMAGECACHE_H

#include <QQmlNetworkAccessManagerFactory>
#include <QString>

// Network access for QML (article images) with a disk cache. Cached images
// are used without asking the server again, so images of articles opened
// before still show offline.
class ImageCacheFactory : public QQmlNetworkAccessManagerFactory
{
public:
    static QString directory();
    static void clear();

    QNetworkAccessManager *create(QObject *parent) override;
};

#endif  // SAILRSS_IMAGECACHE_H
