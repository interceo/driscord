#pragma once
#include <QObject>
#include <QString>
#include <optional>

struct SessionData {
    QString refreshToken;
    QString username;
    int userId = 0;
};

class SessionStore : public QObject {
    Q_OBJECT
public:
    explicit SessionStore(QObject* parent = nullptr);

    void save(const SessionData& s);
    std::optional<SessionData> load() const;
    void clear();
};
