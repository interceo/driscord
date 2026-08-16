#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QString>
#include <QtQuickTest/quicktest.h>

class MockAuthManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY authChanged)
    Q_PROPERTY(QString username READ username NOTIFY authChanged)
    Q_PROPERTY(int loginCalls READ loginCalls NOTIFY callsChanged)
    Q_PROPERTY(int registrationCalls READ registrationCalls NOTIFY callsChanged)
    Q_PROPERTY(int restoreCalls READ restoreCalls NOTIFY callsChanged)
    Q_PROPERTY(QString lastUsername READ lastUsername NOTIFY callsChanged)
    Q_PROPERTY(QString lastEmail READ lastEmail NOTIFY callsChanged)
    Q_PROPERTY(QString lastPassword READ lastPassword NOTIFY callsChanged)
    Q_PROPERTY(bool authPending READ authPending WRITE setAuthPending NOTIFY authPendingChanged)

public:
    bool loggedIn() const { return loggedIn_; }
    QString username() const { return username_; }
    int loginCalls() const { return loginCalls_; }
    int registrationCalls() const { return registrationCalls_; }
    int restoreCalls() const { return restoreCalls_; }
    QString lastUsername() const { return lastUsername_; }
    QString lastEmail() const { return lastEmail_; }
    QString lastPassword() const { return lastPassword_; }
    bool authPending() const { return authPending_; }

    Q_INVOKABLE void setAuthPending(bool value)
    {
        if (authPending_ == value)
            return;
        authPending_ = value;
        emit authPendingChanged();
    }

    Q_INVOKABLE void login(const QString& username, const QString& password)
    {
        ++loginCalls_;
        lastUsername_ = username;
        lastPassword_ = password;
        emit callsChanged();
    }

    Q_INVOKABLE void registerUser(const QString& username, const QString& email,
        const QString& password)
    {
        ++registrationCalls_;
        lastUsername_ = username;
        lastEmail_ = email;
        lastPassword_ = password;
        emit callsChanged();
    }

    Q_INVOKABLE void tryRestoreSession()
    {
        ++restoreCalls_;
        emit callsChanged();
    }

    Q_INVOKABLE void setLoggedIn(bool value)
    {
        loggedIn_ = value;
        emit authChanged();
    }

    Q_INVOKABLE void failSessionRestore() { emit sessionRestoreFailed(); }
    Q_INVOKABLE void restoreSession() { emit sessionRestored(); }
    Q_INVOKABLE void failLogin(const QString& message) { emit loginError(message); }

    void reset()
    {
        loggedIn_ = false;
        username_ = QStringLiteral("tester");
        loginCalls_ = 0;
        registrationCalls_ = 0;
        restoreCalls_ = 0;
        lastUsername_.clear();
        lastEmail_.clear();
        lastPassword_.clear();
        authPending_ = false;
        emit callsChanged();
        emit authChanged();
        emit authPendingChanged();
    }

signals:
    void authChanged();
    void loginError(const QString& message);
    void sessionRestored();
    void sessionRestoreFailed();
    void callsChanged();
    void authPendingChanged();

private:
    bool loggedIn_ = false;
    QString username_ = QStringLiteral("tester");
    int loginCalls_ = 0;
    int registrationCalls_ = 0;
    int restoreCalls_ = 0;
    QString lastUsername_;
    QString lastEmail_;
    QString lastPassword_;
    bool authPending_ = false;
};

class MockAppState final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString apiError READ apiError WRITE setApiError NOTIFY apiErrorChanged)
    Q_PROPERTY(int createServerCalls READ createServerCalls NOTIFY callsChanged)
    Q_PROPERTY(int createChannelCalls READ createChannelCalls NOTIFY callsChanged)
    Q_PROPERTY(int acceptInviteCalls READ acceptInviteCalls NOTIFY callsChanged)
    Q_PROPERTY(int startSharingCalls READ startSharingCalls NOTIFY callsChanged)
    Q_PROPERTY(bool sharingSucceeds READ sharingSucceeds WRITE setSharingSucceeds NOTIFY callsChanged)
    Q_PROPERTY(QString lastName READ lastName NOTIFY callsChanged)
    Q_PROPERTY(QString lastDescription READ lastDescription NOTIFY callsChanged)
    Q_PROPERTY(QString lastKind READ lastKind NOTIFY callsChanged)
    Q_PROPERTY(QString lastInviteCode READ lastInviteCode NOTIFY callsChanged)

public:
    QString apiError() const { return apiError_; }
    int createServerCalls() const { return createServerCalls_; }
    int createChannelCalls() const { return createChannelCalls_; }
    int acceptInviteCalls() const { return acceptInviteCalls_; }
    int startSharingCalls() const { return startSharingCalls_; }
    bool sharingSucceeds() const { return sharingSucceeds_; }
    QString lastName() const { return lastName_; }
    QString lastDescription() const { return lastDescription_; }
    QString lastKind() const { return lastKind_; }
    QString lastInviteCode() const { return lastInviteCode_; }

    void setSharingSucceeds(bool value)
    {
        sharingSucceeds_ = value;
        emit callsChanged();
    }

    void setApiError(const QString& value)
    {
        if (apiError_ == value)
            return;
        apiError_ = value;
        emit apiErrorChanged();
    }

    Q_INVOKABLE void createServer(const QString& name, const QString& description)
    {
        ++createServerCalls_;
        lastName_ = name;
        lastDescription_ = description;
        emit callsChanged();
    }

    Q_INVOKABLE void createChannel(const QString& name, const QString& kind)
    {
        ++createChannelCalls_;
        lastName_ = name;
        lastKind_ = kind;
        emit callsChanged();
    }

    Q_INVOKABLE void acceptInvite(const QString& code)
    {
        ++acceptInviteCalls_;
        lastInviteCode_ = code;
        emit callsChanged();
    }

    Q_INVOKABLE QString captureVideoTargetsJson() const
    {
        return QStringLiteral(
            R"([{"type":0,"id":"screen-1","name":"Screen 1"}])");
    }

    Q_INVOKABLE bool startSharing(const QString&, int, int, int, bool,
        const QString&)
    {
        ++startSharingCalls_;
        emit callsChanged();
        return sharingSucceeds_;
    }

    void reset()
    {
        apiError_.clear();
        createServerCalls_ = 0;
        createChannelCalls_ = 0;
        acceptInviteCalls_ = 0;
        startSharingCalls_ = 0;
        sharingSucceeds_ = true;
        lastName_.clear();
        lastDescription_.clear();
        lastKind_.clear();
        lastInviteCode_.clear();
        emit apiErrorChanged();
        emit callsChanged();
    }

signals:
    void apiErrorChanged();
    void callsChanged();

private:
    QString apiError_;
    int createServerCalls_ = 0;
    int createChannelCalls_ = 0;
    int acceptInviteCalls_ = 0;
    int startSharingCalls_ = 0;
    bool sharingSucceeds_ = true;
    QString lastName_;
    QString lastDescription_;
    QString lastKind_;
    QString lastInviteCode_;
};

class MockBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int previewCalls READ previewCalls NOTIFY previewChanged)
    Q_PROPERTY(bool previewEnabled READ previewEnabled NOTIFY previewChanged)

public:
    int previewCalls() const { return previewCalls_; }
    bool previewEnabled() const { return previewEnabled_; }

    Q_INVOKABLE void setLocalPreviewEnabled(bool enabled)
    {
        ++previewCalls_;
        previewEnabled_ = enabled;
        emit previewChanged();
    }

    Q_INVOKABLE QString captureAudioTargetsJson() const
    {
        return QStringLiteral("[]");
    }

    Q_INVOKABLE void requestThumbnail(const QString&, int, int) { }

    void reset()
    {
        previewCalls_ = 0;
        previewEnabled_ = false;
        emit previewChanged();
    }

signals:
    void previewChanged();
    void thumbnailReady(const QString& targetJson, const QString& url);

private:
    int previewCalls_ = 0;
    bool previewEnabled_ = false;
};

class UiTestHarness final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* auth READ auth CONSTANT)
    Q_PROPERTY(QObject* appState READ appState CONSTANT)
    Q_PROPERTY(QObject* bridge READ bridge CONSTANT)

public:
    QObject* auth() { return &auth_; }
    QObject* appState() { return &appState_; }
    QObject* bridge() { return &bridge_; }

    Q_INVOKABLE void reset()
    {
        auth_.reset();
        appState_.reset();
        bridge_.reset();
    }

private:
    MockAuthManager auth_;
    MockAppState appState_;
    MockBridge bridge_;
};

class QuickTestSetup final : public QObject {
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine* engine)
    {
        auto* context = engine->rootContext();
        context->setContextProperty(QStringLiteral("authManager"), harness_.auth());
        context->setContextProperty(QStringLiteral("appState"), harness_.appState());
        context->setContextProperty(QStringLiteral("bridge"), harness_.bridge());
        context->setContextProperty(QStringLiteral("uiTestHarness"), &harness_);
        context->setContextProperty(QStringLiteral("defaultScreenFps"), 30);
    }

private:
    UiTestHarness harness_;
};

QUICK_TEST_MAIN_WITH_SETUP(qml_components, QuickTestSetup)

#include "tst_qml_components.moc"
