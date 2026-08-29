import QtQuick
import QtTest

TestCase {
    id: testCase
    name: "UiLogic"
    when: windowShown
    width: 1100
    height: 700

    function load(relativePath, properties, parentObject) {
        const component = Qt.createComponent(
            Qt.resolvedUrl("../../qml/" + relativePath));
        compare(component.status, Component.Ready, component.errorString());
        const object = component.createObject(parentObject ?? testCase,
                                              properties ?? {});
        verify(object !== null, component.errorString());
        return object;
    }

    function child(object, name) {
        const result = findChild(object, name);
        verify(result !== null, "Missing UI test hook: " + name);
        return result;
    }

    function init() {
        uiTestHarness.reset();
    }

    function test_loginRejectsInvalidInputAndNormalizesUsername() {
        const screen = load("screens/LoginScreen.qml", {
            width: 700,
            height: 500
        });
        const username = child(screen, "usernameField");
        const password = child(screen, "passwordField");
        const submit = child(screen, "authSubmitButton");

        compare(submit.enabled, false);
        username.text = "   ";
        password.text = "secret";
        compare(submit.enabled, false);

        username.text = "  alice  ";
        password.text = "x";
        tryCompare(submit, "enabled", true);
        password.text = "secret";
        tryCompare(submit, "enabled", true);

        authManager.authPending = true;
        tryCompare(submit, "enabled", false);
        compare(submit.text, "Please wait…");
        authManager.authPending = false;
        tryCompare(submit, "enabled", true);
        submit.clicked();

        compare(authManager.loginCalls, 1);
        compare(authManager.lastUsername, "alice");
        compare(authManager.lastPassword, "secret");
        compare(authManager.registrationCalls, 0);
        screen.destroy();
    }

    function test_registrationValidatesEmailAndBcryptByteLimit() {
        const screen = load("screens/LoginScreen.qml", {
            width: 700,
            height: 500
        });
        const tabs = child(screen, "authModeTabs");
        const username = child(screen, "usernameField");
        const email = child(screen, "emailField");
        const password = child(screen, "passwordField");
        const submit = child(screen, "authSubmitButton");

        tabs.currentIndex = 1;
        tryCompare(screen, "isLogin", false);

        username.text = "new-user";
        password.text = "secret";
        email.text = "not-an-email";
        compare(submit.enabled, false);

        email.text = "  user@example.test  ";
        tryCompare(submit, "enabled", true);

        password.text = "😀".repeat(19);
        compare(submit.enabled, false);

        password.text = "secret";
        tryCompare(submit, "enabled", true);
        submit.clicked();

        compare(authManager.registrationCalls, 1);
        compare(authManager.lastUsername, "new-user");
        compare(authManager.lastEmail, "user@example.test");
        compare(authManager.lastPassword, "secret");
        screen.destroy();
    }

    function test_createServerTrimsAndClearsFields() {
        const dialog = load("dialogs/CreateServerDialog.qml");
        const name = child(dialog, "serverNameField");
        const description = child(dialog, "serverDescriptionField");
        const create = child(dialog, "createServerButton");

        compare(name.maximumLength, 64);
        compare(description.maximumLength, 256);
        name.text = "   ";
        compare(create.enabled, false);

        name.text = "  Team  ";
        description.text = "  Nightly build users  ";
        tryCompare(create, "enabled", true);
        dialog.accept();

        compare(appState.createServerCalls, 1);
        compare(appState.lastName, "Team");
        compare(appState.lastDescription, "Nightly build users");
        compare(name.text, "");
        compare(description.text, "");

        name.text = "stale draft";
        dialog.reject();
        compare(name.text, "");
        compare(appState.createServerCalls, 1);
        dialog.destroy();
    }

    function test_createChannelPreservesSelectedKind() {
        const dialog = load("dialogs/CreateChannelDialog.qml");
        const name = child(dialog, "channelNameField");
        const kind = child(dialog, "channelKindBox");
        const create = child(dialog, "createChannelButton");

        name.text = "  planning  ";
        kind.currentIndex = 1;
        tryCompare(create, "enabled", true);
        dialog.accept();

        compare(appState.createChannelCalls, 1);
        compare(appState.lastName, "planning");
        compare(appState.lastKind, "text");
        compare(name.text, "");
        dialog.destroy();
    }

    function test_inviteCodeCannotBeBlankAndIsCleared() {
        const dialog = load("dialogs/JoinByInviteDialog.qml");
        const code = child(dialog, "inviteCodeField");
        const join = child(dialog, "joinByInviteButton");

        code.text = "   ";
        compare(join.enabled, false);
        code.text = "  abc-123  ";
        tryCompare(join, "enabled", true);
        dialog.accept();

        compare(appState.acceptInviteCalls, 1);
        compare(appState.lastInviteCode, "abc-123");
        compare(code.text, "");
        dialog.destroy();
    }

    function test_shareDialogStaysOpenWhenCaptureFails() {
        const dialog = load("dialogs/ShareDialog.qml");
        dialog.open();
        tryCompare(dialog, "opened", true);
        compare(dialog.targets.length, 1);
        dialog.selectedIndex = 0;

        appState.sharingSucceeds = false;
        dialog.goLive();
        compare(appState.startSharingCalls, 1);
        compare(dialog.opened, true);

        appState.sharingSucceeds = true;
        dialog.goLive();
        compare(appState.startSharingCalls, 2);
        tryCompare(dialog, "opened", false);
        dialog.destroy();
    }

    function test_mainWindowRestoreFailureAndErrorPriority() {
        const window = load("Main.qml", {}, null);
        const loader = child(window, "screenLoader");
        const banner = child(window, "errorBanner");
        const bannerText = child(window, "errorBannerText");
        const dismiss = child(window, "errorBannerDismissArea");

        compare(authManager.restoreCalls, 1);
        compare(loader.state, "restoring");
        compare(banner.visible, false);

        authManager.failSessionRestore();
        tryCompare(loader, "state", "login");
        tryVerify(function() {
            return findChild(loader, "usernameField") !== null;
        });

        authManager.failLogin("Wrong password");
        tryCompare(banner, "visible", true);
        compare(bannerText.text, "Wrong password");

        appState.apiError = "API unavailable";
        tryCompare(bannerText, "text", "API unavailable");
        mouseClick(dismiss, dismiss.width / 2, dismiss.height / 2);
        tryCompare(banner, "visible", false);
        compare(appState.apiError, "");

        window.close();
        window.destroy();
    }
}
