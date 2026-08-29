#pragma once
#include <QColor>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

class QNetworkAccessManager;

class AvatarTintProvider : public QObject {
    Q_OBJECT
public:
    explicit AvatarTintProvider(QObject* parent = nullptr);

    Q_INVOKABLE QColor colorFor(const QString& url) const;
    Q_INVOKABLE void prefetch(const QString& url);

signals:
    void colorReady(const QString& url, const QColor& color);

private:
    QNetworkAccessManager* m_nam;
    QHash<QString, QColor> m_cache;
    QSet<QString> m_inflight;
};
