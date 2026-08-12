import QtQuick
import QtTest

// IconBox is a leaf component: QtQuick plus Qt5Compat.GraphicalEffects, no
// application context. That makes it the one place where a QML test can run
// without the driscord module, which qt_add_qml_module builds into the client
// executable rather than into a standalone module.
TestCase {
    id: root
    name: "IconBox"

    function loadIconBox(properties) {
        const component = Qt.createComponent(
            Qt.resolvedUrl("../../qml/components/IconBox.qml"));
        compare(component.status, Component.Ready, component.errorString());
        const box = component.createObject(root, properties);
        verify(box !== null);
        return box;
    }

    function test_squareByDefault() {
        const box = loadIconBox({});
        compare(box.width, box.height);
        compare(box.width, 16);
        box.destroy();
    }

    function test_sizeDrivesBothDimensions() {
        const box = loadIconBox({ size: 24 });
        compare(box.width, 24);
        compare(box.height, 24);
        box.destroy();
    }
}
