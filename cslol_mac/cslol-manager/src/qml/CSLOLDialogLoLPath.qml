import QtQuick 2.15
import QtQuick.Layouts 1.12
import QtQuick.Controls 2.15
import Qt.labs.platform 1.0

FileDialog {
    id: lolPathDialog
    visible: false
    title: qsTr("Select League of Legends folder")
    fileMode: FileDialog.OpenFile
    nameFilters: ""
    folder: ""
    onAccepted: {
        // Normalize the path through checkGamePath
        let normalizedPath = CSLOLUtils.checkGamePath(CSLOLUtils.fromFile(file))
        if (normalizedPath !== "") {
            accepted(normalizedPath)
        }
    }
}
