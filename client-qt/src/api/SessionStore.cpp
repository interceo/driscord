#include "SessionStore.h"
#include <QSettings>

static const char* kGroup = "session";

SessionStore::SessionStore(QObject* parent)
    : QObject(parent)
{
}

void SessionStore::save(const SessionData& s)
{
    QSettings cfg;
    cfg.beginGroup(kGroup);
    cfg.setValue("refresh_token", s.refreshToken);
    cfg.setValue("username", s.username);
    cfg.setValue("user_id", s.userId);
    cfg.endGroup();
}

std::optional<SessionData> SessionStore::load() const
{
    QSettings cfg;
    cfg.beginGroup(kGroup);
    auto token = cfg.value("refresh_token").toString();
    auto user = cfg.value("username").toString();
    auto uid = cfg.value("user_id", 0).toInt();
    cfg.endGroup();
    if (token.isEmpty())
        return std::nullopt;
    return SessionData { token, user, uid };
}

void SessionStore::clear()
{
    QSettings cfg;
    cfg.remove(kGroup);
}
