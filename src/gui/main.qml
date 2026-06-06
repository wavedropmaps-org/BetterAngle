import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15

Window {
    id: mainWindow
    width: 650
    height: 480
    minimumWidth: 500
    minimumHeight: 350
    maximumWidth: 1200
    maximumHeight: 800
    // X and Y are set dynamically on completion to prevent monitor snapping bugs
    visible: false
    title: qsTr("BetterAngle Pro Angle HUD")
    color: "#0a0a0f"
    
    property bool isBooting: true

    Timer {
        id: bootTimer
        interval: 2500
        running: true
        repeat: false
        onTriggered: {
            mainWindow.isBooting = false
        }
    }

    // Frameless window style for a custom sleek look
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowSystemMenuHint | Qt.WindowMinimizeButtonHint

    onVisibleChanged: {
        // No longer forcing crosshair state here to allow user preference to persist
    }

    // True only while we apply the saved position on startup. Setting x/y here
    // fires onXChanged/onYChanged; without this guard the restore would call
    // syncHudToWindow and yank the HUD onto the dashboard's monitor, overriding
    // the startup detection that already placed it on Fortnite's monitor.
    property bool _initialRestore: true

    Component.onCompleted: {
        // Restore last saved position; fall back to screen centre on first run.
        var sx = backend ? backend.savedDashX : -2147483648
        var sy = backend ? backend.savedDashY : -2147483648
        if (sx !== -2147483648 && sy !== -2147483648) {
            x = sx
            y = sy
        } else {
            x = Screen.width / 2 - width / 2
            y = Screen.height / 2 - height / 2
        }
        _initialRestore = false
    }

    // When the dashboard is dragged, keep the HUD angle readout attached and
    // persist the new window position so it restores on next launch. The HUD
    // sync is skipped during the initial restore (see _initialRestore above).
    onXChanged: {
        if (backend) {
            if (!_initialRestore) backend.syncHudToWindow(x, y, width, height)
            backend.saveDashboardPosition(x, y)
        }
    }
    onYChanged: {
        if (backend) {
            if (!_initialRestore) backend.syncHudToWindow(x, y, width, height)
            backend.saveDashboardPosition(x, y)
        }
    }

    // Restore + focus when the user clicks the taskbar icon (frameless windows
    // need this because the shell cannot manage them natively).
    onVisibilityChanged: {
        if (visibility === Window.Windowed || visibility === Window.Maximized) {
            raise()
            requestActivate()
        }
    }

    Connections {
        target: backend
        onShowControlPanelRequested: {
            // If the window is currently the active foreground window, minimise it.
            // If it's in the background or minimised, restore and focus it.
            if (mainWindow.active) {
                mainWindow.showMinimized()
            } else {
                mainWindow.showNormal()
                mainWindow.raise()
                mainWindow.requestActivate()
            }
        }
    }



    Rectangle {
        id: titleBar
        width: parent.width
        height: 40
        color: "#181824"
        
        Image {
            id: logo
            source: "qrc:/assets/logo.png"
            width: 24
            height: 24
            mipmap: true
            smooth: true
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 12
        }

        Text {
            anchors.centerIn: parent
            text: "BetterAngle Pro"
            color: "#ffffff"
            font.bold: true
            font.pixelSize: 16
            horizontalAlignment: Text.AlignHCenter
        }

        MouseArea {
            anchors.fill: parent
            onPressed: mainWindow.startSystemMove()
        }

        // Window Controls
        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            Button {
                text: "—"
                width: 40
                height: 40
                background: Rectangle { color: parent.hovered ? "#303040" : "transparent" }
                contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: mainWindow.showMinimized()
            }
            Button {
                text: "✕"
                width: 40
                height: 40
                background: Rectangle { color: parent.hovered ? "#c42b1c" : "transparent" }
                contentItem: Text { text: parent.text; color: "white"; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: Qt.quit()
            }
        }
    }

    // Resize handles for frameless window
    Item {
        anchors.fill: parent
        z: 1 // above background but below titleBar? Actually titleBar is at z 0, we want resize areas behind titleBar? We'll keep above but exclude titleBar area.
        // We'll make the resize areas only at edges, excluding central area where titleBar and dashboard are.

        // Left edge
        MouseArea {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 5
            cursorShape: Qt.SizeHorCursor
            drag { target: null; axis: Drag.XAxis; threshold: 0 }
            onPressed: mainWindow.startSystemResize(Qt.LeftEdge)
        }
        // Right edge
        MouseArea {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 5
            cursorShape: Qt.SizeHorCursor
            drag { target: null; axis: Drag.XAxis; threshold: 0 }
            onPressed: mainWindow.startSystemResize(Qt.RightEdge)
        }
        // Top edge (excluding titleBar area? We'll allow top edge resize but titleBar will also capture mouse)
        MouseArea {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 5
            cursorShape: Qt.SizeVerCursor
            drag { target: null; axis: Drag.YAxis; threshold: 0 }
            onPressed: mainWindow.startSystemResize(Qt.TopEdge)
        }
        // Bottom edge
        MouseArea {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 5
            cursorShape: Qt.SizeVerCursor
            drag { target: null; axis: Drag.YAxis; threshold: 0 }
            onPressed: mainWindow.startSystemResize(Qt.BottomEdge)
        }
        // Top-left corner
        MouseArea {
            anchors.left: parent.left
            anchors.top: parent.top
            width: 10
            height: 10
            cursorShape: Qt.SizeFDiagCursor
            drag { target: null; axis: Drag.XAndYAxis; threshold: 0 }
            onPressed: mainWindow.startSystemResize(Qt.LeftEdge | Qt.TopEdge)
        }
        // Top-right corner
        MouseArea {
            anchors.right: parent.right
            anchors.top: parent.top
            width: 10
            height: 10
            cursorShape: Qt.SizeBDiagCursor
            drag { target: null; axis: Drag.XAndYAxis; threshold: 0 }
            onPressed: mainWindow.startSystemResize(Qt.RightEdge | Qt.TopEdge)
        }
        // Bottom-left corner
        MouseArea {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            width: 10
            height: 10
            cursorShape: Qt.SizeBDiagCursor
            drag { target: null; axis: Drag.XAndYAxis; threshold: 0 }
            onPressed: mainWindow.startSystemResize(Qt.LeftEdge | Qt.BottomEdge)
        }
        // Bottom-right corner
        MouseArea {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: 10
            height: 10
            cursorShape: Qt.SizeFDiagCursor
            drag { target: null; axis: Drag.XAndYAxis; threshold: 0 }
            onPressed: mainWindow.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
        }
    }

    Dashboard {
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        enabled: !mainWindow.isBooting
        opacity: mainWindow.isBooting ? 0 : 1
        
        Behavior on opacity { NumberAnimation { duration: 500 } }
    }

    // Splash Screen Overlay
    Rectangle {
        id: splashScreen
        anchors.fill: parent
        color: "#0a0a0f"
        visible: mainWindow.isBooting
        z: 1000

        Image {
            id: splashImage
            property int splashIndex: Math.floor(Math.random() * 3) + 1
            source: "qrc:/assets/loading_" + splashIndex + ".png"
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true
            opacity: 0
            Component.onCompleted: fadeIn.start()
            
            NumberAnimation on opacity {
                id: fadeIn
                to: 1
                duration: 1000
            }
        }

        // Premium Loading Bar (Fire themed)
        Rectangle {
            id: loadingTrack
            width: parent.width * 0.8
            height: 6
            color: "#1a1a24"
            radius: 3
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 40
            anchors.horizontalCenter: parent.horizontalCenter
            clip: true

            // The Progress Fill
            Rectangle {
                id: loadingFill
                height: parent.height
                radius: 3
                
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#ff4d00" } // Deep Fire Red
                    GradientStop { position: 0.5; color: "#ff8c00" } // Fire Orange
                    GradientStop { position: 1.0; color: "#ffcc00" } // Golden Glow
                }

                PropertyAnimation on width {
                    id: progressAnim
                    from: 0
                    to: 520
                    duration: 2500
                    running: mainWindow.isBooting
                }
            }

            // Lead edge glow for "fire" effect
            Rectangle {
                width: 20
                height: parent.height
                anchors.right: loadingFill.right
                visible: loadingFill.width > 0
                
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: "#ffffff" } // Bright spark at edge
                }
                opacity: 0.6
            }
        }
    }
}
