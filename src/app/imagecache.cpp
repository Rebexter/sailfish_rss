#include "imagecache.h"

#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkRequest>
#include <QStandardPaths>

namespace {

const qint64 MaximumCacheSize = 100 * 1024 * 1024;

class CachingNetworkAccessManager : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *data) override
    {
        QNetworkRequest cached(request);
        if (operation == GetOperation)
            cached.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        return QNetworkAccessManager::createRequest(operation, cached, data);
    }
};

}  // namespace

QString ImageCacheFactory::directory()
{
    // Inside the per-app cache directory that Sailjail allows
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/images");
}

void ImageCacheFactory::clear()
{
    QDir(directory()).removeRecursively();
}

// Called by the QML engine, possibly from its image loading thread.
QNetworkAccessManager *ImageCacheFactory::create(QObject *parent)
{
    auto *manager = new CachingNetworkAccessManager(parent);
    auto *cache = new QNetworkDiskCache(manager);
    cache->setCacheDirectory(directory());
    cache->setMaximumCacheSize(MaximumCacheSize);
    manager->setCache(cache);
    return manager;
}
