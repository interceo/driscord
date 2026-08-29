import QtQuick
import QtTest

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
