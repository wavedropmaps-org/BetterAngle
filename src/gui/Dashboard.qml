import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root

    Timer {
        interval: 10
        running: bar.currentIndex === 3 // Debug tab index
        repeat: true
        onTriggered: backend.refreshDebugData()
    }

    TabBar {
        id: bar
        width: parent.width
        background: Rectangle { color: "#0d0d12" }
        onCurrentIndexChanged: {
            if (currentIndex == 4) { // UPDATES tab
                if (!backend.hasCheckedForUpdates) {
                    backend.checkForUpdates()
                }
            }
        }
        
        TabButton {
            text: qsTr("GENERAL")
            contentItem: Text { text: parent.text; color: parent.checked ? "#00ffcc" : "#888"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.checked ? "#1a1a2e" : "transparent" }
        }
        TabButton {
            text: qsTr("CROSSHAIR")
            contentItem: Text { text: parent.text; color: parent.checked ? "#00ffcc" : "#888"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.checked ? "#1a1a2e" : "transparent" }
        }
        TabButton {
            text: qsTr("UPDATES")
            contentItem: Text { text: parent.text; color: parent.checked ? "#00ffcc" : "#888"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.checked ? "#1a1a2e" : "transparent" }
        }
        TabButton {
            text: qsTr("DEBUG")
            contentItem: Text { text: parent.text; color: parent.checked ? "#00ffcc" : "#888"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.checked ? "#1a1a2e" : "transparent" }
        }
    }

    StackLayout {
        width: parent.width
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        currentIndex: bar.currentIndex

        // ─── GENERAL ────────────────────────────────────────────────
        Rectangle {
            color: "#0d0d12"
            Flickable {
                anchors.fill: parent
                contentHeight: genCol.implicitHeight + 40
                clip: true
                Column {
                    id: genCol
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
                    spacing: 14

                    Text { text: "MANUAL SENSITIVITY"; color: "#666"; font.pixelSize: 11; font.bold: true }

                    Button {
                        text: "RESET ANGLE TO 0"
                        width: parent.width
                        height: 44
                        contentItem: Text { text: parent.text; color: "white"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { 
                            color: parent.hovered ? "#00cca3" : "#00a382"
                            radius: 4 
                            border.color: "#00ffa3"
                            border.width: 1
                        }
                        onClicked: backend.setZero()
                    }

                    // Sens X
                    Column {
                        spacing: 4
                        width: parent.width
                        Text { text: "Fortnite Sens X"; color: "#aaa"; font.pixelSize: 12 }
                        TextField {
                            id: sensXField
                            width: parent.width
                            // Re-read when profile changes so value always shows on startup
                            text: backend.sensX.toFixed(1)
                            color: "white"
                            background: Rectangle { color: "#1c1c2e"; radius: 4; border.color: "#333"; border.width: 1 }
                            onEditingFinished: backend.sensX = Number(parseFloat(text).toFixed(1))
                            Connections {
                                target: backend
                                onProfileChanged: sensXField.text = backend.sensX.toFixed(1)
                            }
                        }
                    }

                    // Sens Y
                    Column {
                        spacing: 4
                        width: parent.width
                        Text { text: "Fortnite Sens Y"; color: "#aaa"; font.pixelSize: 12 }
                        TextField {
                            id: sensYField
                            width: parent.width
                            text: backend.sensY.toFixed(1)
                            color: "white"
                            background: Rectangle { color: "#1c1c2e"; radius: 4; border.color: "#333"; border.width: 1 }
                            onEditingFinished: backend.sensY = Number(parseFloat(text).toFixed(1))
                            Connections {
                                target: backend
                                onProfileChanged: sensYField.text = backend.sensY.toFixed(1)
                            }
                        }
                    }

                    Text { text: "DISPLAY & MONITOR"; color: "#666"; font.pixelSize: 11; font.bold: true; topPadding: 10 }
                    Column {
                        spacing: 4
                        width: parent.width
                        Text { text: "Active Game Monitor"; color: "#aaa"; font.pixelSize: 12 }
                        ComboBox {
                            id: monitorCombo
                            width: parent.width
                            model: backend.availableScreens
                            currentIndex: backend.screenIndex
                            onActivated: backend.screenIndex = index
                            Connections {
                                target: backend
                                onProfileChanged: monitorCombo.currentIndex = backend.screenIndex
                            }
                            contentItem: Text {
                                text: monitorCombo.displayText
                                color: "white"
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: 10
                            }
                            background: Rectangle {
                                color: "#1c1c2e"
                                radius: 4
                                border.color: "#333"
                                border.width: 1
                            }
                            delegate: ItemDelegate {
                                width: monitorCombo.width
                                contentItem: Text {
                                    text: modelData
                                    color: "white"
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    color: highlighted ? "#33334d" : "#1c1c2e"
                                }
                            }
                            popup: Popup {
                                y: parent.height
                                width: parent.width
                                implicitHeight: Math.min(contentItem.implicitHeight, 200)
                                padding: 1
                                contentItem: ListView {
                                    clip: true
                                    implicitHeight: contentHeight
                                    model: monitorCombo.delegateModel
                                    currentIndex: monitorCombo.highlightedIndex
                                    ScrollIndicator.vertical: ScrollIndicator { }
                                }
                                background: Rectangle {
                                    color: "#1c1c2e"
                                    radius: 4
                                    border.color: "#333"
                                }
                            }
                        }
                    }

                    Text { text: "TRIGGER CALIBRATION (%)"; color: "#666"; font.pixelSize: 12; topPadding: 10 }
                    RowLayout {
                        Text { text: "Dive to glide threshold match limit %"; color: "white"; Layout.preferredWidth: 230; font.pixelSize: 12 }
                        Slider {
                            Layout.fillWidth: true
                            from: 1; to: 20; value: backend.diveGlideMatch
                            onValueChanged: backend.diveGlideMatch = value
                        }
                        Text { text: Math.round(backend.diveGlideMatch).toString() + "%"; color: "#aaa" }
                    }
                    Text { 
                        text: ""; 
                        color: "#666"; font.pixelSize: 11; font.italic: true 
                    }

                    Text { text: "TARGET COLOR SETTINGS"; color: "#666"; font.pixelSize: 12; topPadding: 15 }
                    RowLayout {
                        spacing: 10
                        Rectangle {
                            width: 30; height: 30; radius: 4
                            color: backend.targetColor
                            border.color: "#333"; border.width: 1
                        }
                        ColumnLayout {
                            Text { text: "Tolerance (color match ±)"; color: "white"; font.pixelSize: 12 }
                            RowLayout {
                                Slider {
                                    Layout.fillWidth: true
                                    from: 0; to: 120; value: backend.tolerance
                                    onValueChanged: backend.tolerance = Math.round(value)
                                }
                                Text { text: backend.tolerance; color: "#aaa"; Layout.preferredWidth: 30 }
                            }
                        }
                    }

                    Text { text: "HOTKEY CONFIGURATION"; color: "#666"; font.pixelSize: 11; font.bold: true; topPadding: 10 }
                    
                    Column {
                        id: hotkeyColumn
                        spacing: 8
                        width: parent.width

                        function formatCapturedHotkey(event) {
                            var parts = []
                            if (event.modifiers & Qt.ControlModifier) parts.push("Ctrl")
                            if (event.modifiers & Qt.ShiftModifier) parts.push("Shift")
                            if (event.modifiers & Qt.AltModifier) parts.push("Alt")
                            if (event.modifiers & Qt.MetaModifier) parts.push("Win")

                            if (event.key === Qt.Key_Control || event.key === Qt.Key_Shift || event.key === Qt.Key_Alt || event.key === Qt.Key_Meta)
                                return ""

                            var key = ""
                            if (event.key >= Qt.Key_F1 && event.key <= Qt.Key_F12)
                                key = "F" + (event.key - Qt.Key_F1 + 1)
                            else if ((event.modifiers & Qt.KeypadModifier) && event.key >= Qt.Key_0 && event.key <= Qt.Key_9)
                                key = "Numpad" + String.fromCharCode(event.key)
                            else if ((event.modifiers & Qt.KeypadModifier) && event.key === Qt.Key_Plus)
                                key = "Add"
                            else if ((event.modifiers & Qt.KeypadModifier) && event.key === Qt.Key_Minus)
                                key = "Subtract"
                            else if ((event.modifiers & Qt.KeypadModifier) && event.key === Qt.Key_Asterisk)
                                key = "Multiply"
                            else if ((event.modifiers & Qt.KeypadModifier) && event.key === Qt.Key_Slash)
                                key = "Divide"
                            else if ((event.modifiers & Qt.KeypadModifier) && (event.key === Qt.Key_Period || event.key === Qt.Key_Comma))
                                key = "Decimal"
                            else if (event.key === Qt.Key_Space)
                                key = "Space"
                            else if (event.key === Qt.Key_Tab)
                                key = "Tab"
                            else if (event.key === Qt.Key_Escape)
                                key = "Esc"
                            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                key = "Enter"
                            else if (event.key === Qt.Key_Backspace)
                                key = "Backspace"
                            else if (event.key === Qt.Key_Insert)
                                key = "Insert"
                            else if (event.key === Qt.Key_Delete)
                                key = "Delete"
                            else if (event.key === Qt.Key_Home)
                                key = "Home"
                            else if (event.key === Qt.Key_End)
                                key = "End"
                            else if (event.key === Qt.Key_PageUp)
                                key = "PageUp"
                            else if (event.key === Qt.Key_PageDown)
                                key = "PageDown"
                            else if (event.key === Qt.Key_Up)
                                key = "Up"
                            else if (event.key === Qt.Key_Down)
                                key = "Down"
                            else if (event.key === Qt.Key_Left)
                                key = "Left"
                            else if (event.key === Qt.Key_Right)
                                key = "Right"
                            else if (event.key === Qt.Key_CapsLock)
                                key = "CapsLock"
                            else if (event.key === Qt.Key_NumLock)
                                key = "NumLock"
                            else if (event.key === Qt.Key_ScrollLock)
                                key = "ScrollLock"
                            else if (event.key === Qt.Key_Print)
                                key = "PrintScreen"
                            else if (event.key === Qt.Key_Pause)
                                key = "Pause"
                            else if (event.key >= Qt.Key_A && event.key <= Qt.Key_Z)
                                key = String.fromCharCode(event.key)
                            else if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9)
                                key = String.fromCharCode(event.key)

                            if (key === "")
                                return ""

                            parts.push(key)
                            return parts.join(" + ")
                        }

                        function formatCapturedMouseButton(mouse) {
                            var parts = []
                            if (mouse.modifiers & Qt.ControlModifier) parts.push("Ctrl")
                            if (mouse.modifiers & Qt.ShiftModifier) parts.push("Shift")
                            if (mouse.modifiers & Qt.AltModifier) parts.push("Alt")
                            if (mouse.modifiers & Qt.MetaModifier) parts.push("Win")

                            var button = ""
                            if (mouse.button === Qt.LeftButton)
                                button = "Mouse1"
                            else if (mouse.button === Qt.RightButton)
                                button = "Mouse2"
                            else if (mouse.button === Qt.MiddleButton)
                                button = "Mouse3"
                            else if (mouse.button === Qt.BackButton)
                                button = "Mouse4"
                            else if (mouse.button === Qt.ForwardButton)
                                button = "Mouse5"

                            if (button === "")
                                return ""

                            parts.push(button)
                            return parts.join(" + ")
                        }

                        function captureHotkey(event, applyBinding) {
                            if (event.key === Qt.Key_Escape) {
                                root.forceActiveFocus()
                                event.accepted = true
                                return
                            }

                            var bind = formatCapturedHotkey(event)
                            if (bind === "") {
                                event.accepted = true
                                return
                            }

                            applyBinding(bind)
                            backend.saveKeybinds()
                            root.forceActiveFocus()
                            event.accepted = true
                        }

                        function captureMouseHotkey(mouse, applyBinding) {
                            var bind = formatCapturedMouseButton(mouse)
                            if (bind === "")
                                return

                            applyBinding(bind)
                            backend.saveKeybinds()
                            root.forceActiveFocus()
                        }
                        
                        RowLayout {
                            Text { text: "Toggle Dashboard:"; color: "white"; Layout.preferredWidth: 150 }
                            TextField {
                                id: keyToggleField
                                Layout.fillWidth: true
                                readOnly: true
                                selectByMouse: false
                                activeFocusOnTab: true
                                text: activeFocus ? "Listening for keys..." : backend.keyToggle
                                color: activeFocus ? "#00ffa3" : "white"
                                font.bold: activeFocus
                                background: Rectangle { 
                                    color: "#1c1c2e"; radius: 4 
                                    border.color: keyToggleField.activeFocus ? "#00cca3" : "#333"
                                    border.width: keyToggleField.activeFocus ? 2 : 1 
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.AllButtons
                                    cursorShape: Qt.PointingHandCursor
                                    onPressed: function(mouse) {
                                        if (keyToggleField.activeFocus)
                                            hotkeyColumn.captureMouseHotkey(mouse, function(bind) { backend.keyToggle = bind })
                                        else
                                            keyToggleField.forceActiveFocus()
                                    }
                                }
                                Keys.priority: Keys.BeforeItem
                                Keys.onPressed: function(event) {
                                    hotkeyColumn.captureHotkey(event, function(bind) { backend.keyToggle = bind })
                                }
                                onActiveFocusChanged: {
                                    if (activeFocus) {
                                        backend.startKeybindAssignment()
                                    } else {
                                        backend.endKeybindAssignment()
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Text { text: "Selection Overlay:"; color: "white"; Layout.preferredWidth: 150 }
                            TextField {
                                id: keyRoiField
                                Layout.fillWidth: true
                                readOnly: true
                                selectByMouse: false
                                activeFocusOnTab: true
                                text: activeFocus ? "Listening for keys..." : backend.keyRoi
                                color: activeFocus ? "#00ffa3" : "white"
                                font.bold: activeFocus
                                background: Rectangle { 
                                    color: "#1c1c2e"; radius: 4 
                                    border.color: keyRoiField.activeFocus ? "#00cca3" : "#333"
                                    border.width: keyRoiField.activeFocus ? 2 : 1 
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.AllButtons
                                    cursorShape: Qt.PointingHandCursor
                                    onPressed: function(mouse) {
                                        if (keyRoiField.activeFocus)
                                            hotkeyColumn.captureMouseHotkey(mouse, function(bind) { backend.keyRoi = bind })
                                        else
                                            keyRoiField.forceActiveFocus()
                                    }
                                }
                                Keys.priority: Keys.BeforeItem
                                Keys.onPressed: function(event) {
                                    hotkeyColumn.captureHotkey(event, function(bind) { backend.keyRoi = bind })
                                }
                                onActiveFocusChanged: {
                                    if (activeFocus) {
                                        backend.startKeybindAssignment()
                                    } else {
                                        backend.endKeybindAssignment()
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Text { text: "Toggle Crosshair:"; color: "white"; Layout.preferredWidth: 150 }
                            TextField {
                                id: keyCrossField
                                Layout.fillWidth: true
                                readOnly: true
                                selectByMouse: false
                                activeFocusOnTab: true
                                text: activeFocus ? "Listening for keys..." : backend.keyCross
                                color: activeFocus ? "#00ffa3" : "white"
                                font.bold: activeFocus
                                background: Rectangle { 
                                    color: "#1c1c2e"; radius: 4 
                                    border.color: keyCrossField.activeFocus ? "#00cca3" : "#333"
                                    border.width: keyCrossField.activeFocus ? 2 : 1 
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.AllButtons
                                    cursorShape: Qt.PointingHandCursor
                                    onPressed: function(mouse) {
                                        if (keyCrossField.activeFocus)
                                            hotkeyColumn.captureMouseHotkey(mouse, function(bind) { backend.keyCross = bind })
                                        else
                                            keyCrossField.forceActiveFocus()
                                    }
                                }
                                Keys.priority: Keys.BeforeItem
                                Keys.onPressed: function(event) {
                                    hotkeyColumn.captureHotkey(event, function(bind) { backend.keyCross = bind })
                                }
                                onActiveFocusChanged: {
                                    if (activeFocus) {
                                        backend.startKeybindAssignment()
                                    } else {
                                        backend.endKeybindAssignment()
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Text { text: "Zero Counter:"; color: "white"; Layout.preferredWidth: 150 }
                            TextField {
                                id: keyZeroField
                                Layout.fillWidth: true
                                readOnly: true
                                selectByMouse: false
                                activeFocusOnTab: true
                                text: activeFocus ? "Listening for keys..." : backend.keyZero
                                color: activeFocus ? "#00ffa3" : "white"
                                font.bold: activeFocus
                                background: Rectangle { 
                                    color: "#1c1c2e"; radius: 4 
                                    border.color: keyZeroField.activeFocus ? "#00cca3" : "#333"
                                    border.width: keyZeroField.activeFocus ? 2 : 1 
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.AllButtons
                                    cursorShape: Qt.PointingHandCursor
                                    onPressed: function(mouse) {
                                        if (keyZeroField.activeFocus)
                                            hotkeyColumn.captureMouseHotkey(mouse, function(bind) { backend.keyZero = bind })
                                        else
                                            keyZeroField.forceActiveFocus()
                                    }
                                }
                                Keys.priority: Keys.BeforeItem
                                Keys.onPressed: function(event) {
                                    hotkeyColumn.captureHotkey(event, function(bind) { backend.keyZero = bind })
                                }
                                onActiveFocusChanged: {
                                    if (activeFocus) {
                                        backend.startKeybindAssignment()
                                    } else {
                                        backend.endKeybindAssignment()
                                    }
                                }
                            }
                        }


                        Text {
                            text: "Click a bind, then press the combo. Changes apply immediately."
                            color: "#888"
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            width: parent.width
                        }
                    }


                    Button {
                        text: "QUIT APP"
                        width: parent.width
                        height: 40
                        contentItem: Text { text: parent.text; color: "white"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { color: parent.hovered ? "#ff4c4c" : "#e63939"; radius: 4 }
                        onClicked: backend.terminateApp()
                    }
                }
            }
        }

        // ─── CROSSHAIR ──────────────────────────────────────────────
        Rectangle {
            color: "#0d0d12"
            Flickable {
                anchors.fill: parent
                contentHeight: crossCol.implicitHeight + 40
                clip: true
                Column {
                    id: crossCol
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                    spacing: 12

                    // Toggle
                    Button {
                        id: crosshairToggleBtn
                        text: backend.crosshairOn ? "CROSSHAIR: ON" : "CROSSHAIR: OFF"
                        width: parent.width
                        height: 38
                        contentItem: Text { text: parent.text; color: "white"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { 
                            color: crosshairToggleBtn.pressed ? (backend.crosshairOn ? "#008a6e" : "#222") : (backend.crosshairOn ? (parent.hovered ? "#00b38f" : "#00cca3") : (parent.hovered ? "#444" : "#333"))
                            radius: 4 
                            border.color: crosshairToggleBtn.activeFocus ? "#00ffa3" : "transparent"
                            border.width: 1
                        }
                        onClicked: backend.crosshairOn = !backend.crosshairOn
                    }

                    // Thickness
                    Column { spacing: 4; width: parent.width
                        Text { text: "Line Thickness: " + Math.round(backend.crossThickness) + " px"; color: "white"; font.pixelSize: 12 }
                        Slider {
                            width: parent.width
                            from: 1; to: 10; stepSize: 1
                            value: backend.crossThickness
                            onMoved: backend.crossThickness = Math.round(value)
                        }
                    }

                    Button {
                        id: pulseToggleBtn
                        text: backend.crossPulse ? "PULSE ANIMATION: ON" : "PULSE ANIMATION: OFF"
                        width: parent.width
                        height: 38
                        enabled: backend.crosshairOn
                        opacity: enabled ? 1.0 : 0.4
                        contentItem: Text { 
                            text: parent.text; 
                            color: "white"; 
                            font.bold: true; 
                            horizontalAlignment: Text.AlignHCenter; 
                            verticalAlignment: Text.AlignVCenter 
                        }
                        background: Rectangle { 
                            color: !pulseToggleBtn.enabled ? "#222" : (backend.crossPulse ? "#4a3080" : "#333"); 
                            radius: 4; 
                            border.color: !pulseToggleBtn.enabled ? "#333" : (backend.crossPulse ? "#6644aa" : "#444"); 
                            border.width: 1 
                        }
                        onClicked: backend.crossPulse = !backend.crossPulse
                    }

                    // ── HSV Spectrum Color Picker ──────────────────────
                    Text { text: "CROSSHAIR COLOUR"; color: "#666"; font.pixelSize: 11; font.bold: true }

                    Item {
                        id: colorPicker
                        width: parent.width
                        height: svCanvas.height + hueStrip.height + hexRow.height + 16

                        // Internal HSV state
                        property real hue: 0.0
                        property real sat: 1.0
                        property real val: 1.0

                        // Initialise from backend color when it changes
                        function initFromBackend() {
                            var c = backend.crossColor
                            var hsv = rgbToHsv(c.r, c.g, c.b)
                            hue = hsv[0]; sat = hsv[1]; val = hsv[2]
                        }

                        function rgbToHsv(r, g, b) {
                            var max = Math.max(r,g,b), min = Math.min(r,g,b)
                            var d = max - min, h = 0, s = max === 0 ? 0 : d/max, v = max
                            if (d !== 0) {
                                if (max === r) h = ((g-b)/d + (g < b ? 6 : 0)) / 6
                                else if (max === g) h = ((b-r)/d + 2) / 6
                                else h = ((r-g)/d + 4) / 6
                            }
                            return [h, s, v]
                        }

                        function hsvToRgb(h, s, v) {
                            var i = Math.floor(h*6), f = h*6-i
                            var p=v*(1-s), q=v*(1-f*s), t=v*(1-(1-f)*s)
                            switch(i%6){
                                case 0: return [v,t,p]
                                case 1: return [q,v,p]
                                case 2: return [p,v,t]
                                case 3: return [p,q,v]
                                case 4: return [t,p,v]
                                case 5: return [v,p,q]
                            }
                            return [0,0,0]
                        }

                        function toHex2(x) {
                            var s = Math.round(x*255).toString(16)
                            return s.length===1 ? "0"+s : s
                        }

                        function applyColor() {
                            var rgb = hsvToRgb(hue, sat, val)
                            backend.crossColor = Qt.rgba(rgb[0], rgb[1], rgb[2], 1)
                            hexField.text = toHex2(rgb[0]) + toHex2(rgb[1]) + toHex2(rgb[2])
                        }

                        Component.onCompleted: initFromBackend()
                        Connections {
                            target: backend
                            function onCrosshairChanged() { colorPicker.initFromBackend() }
                        }

                        // Pure hue color for the SV gradient base
                        property color hueColor: Qt.hsva(hue, 1.0, 1.0, 1.0)

                        // ── SV Square ──────────────────────────────────
                        Item {
                            id: svCanvas
                            x: 0; y: 0
                            width: parent.width; height: Math.min(parent.width * 0.55, 160)

                            // White → HueColor (left to right)
                            Rectangle {
                                anchors.fill: parent; radius: 6
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: "white" }
                                    GradientStop { position: 1.0; color: colorPicker.hueColor }
                                }
                            }
                            // Transparent → Black (top to bottom, overlaid)
                            Rectangle {
                                anchors.fill: parent; radius: 6
                                gradient: Gradient {
                                    orientation: Gradient.Vertical
                                    GradientStop { position: 0.0; color: "transparent" }
                                    GradientStop { position: 1.0; color: "black" }
                                }
                            }

                            // Picker cursor circle
                            Rectangle {
                                x: colorPicker.sat * parent.width - width/2
                                y: (1 - colorPicker.val) * parent.height - height/2
                                width: 12; height: 12; radius: 6
                                color: "transparent"
                                border.color: "white"; border.width: 2
                            }

                            MouseArea {
                                anchors.fill: parent
                                function pick(mx, my) {
                                    colorPicker.sat = Math.max(0, Math.min(1, mx / svCanvas.width))
                                    colorPicker.val = Math.max(0, Math.min(1, 1 - my / svCanvas.height))
                                    colorPicker.applyColor()
                                }
                                onPressed: { pick(mouse.x, mouse.y) }
                                onPositionChanged: { if (pressed) pick(mouse.x, mouse.y) }
                            }
                        }

                        // ── Rainbow Hue Strip ──────────────────────────
                        Item {
                            id: hueStrip
                            x: 0; anchors.top: svCanvas.bottom; anchors.topMargin: 8
                            width: parent.width; height: 18

                            Rectangle {
                                anchors.fill: parent; radius: 9
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.000; color: "#ff0000" }
                                    GradientStop { position: 0.167; color: "#ffff00" }
                                    GradientStop { position: 0.333; color: "#00ff00" }
                                    GradientStop { position: 0.500; color: "#00ffff" }
                                    GradientStop { position: 0.667; color: "#0000ff" }
                                    GradientStop { position: 0.833; color: "#ff00ff" }
                                    GradientStop { position: 1.000; color: "#ff0000" }
                                }
                            }

                            // Hue cursor thumb
                            Rectangle {
                                x: colorPicker.hue * parent.width - width/2
                                y: (parent.height - height)/2
                                width: 10; height: 22; radius: 5
                                color: "white"
                            }

                            MouseArea {
                                anchors.fill: parent
                                function pick(mx) {
                                    colorPicker.hue = Math.max(0, Math.min(1, mx / hueStrip.width))
                                    colorPicker.applyColor()
                                }
                                onPressed: { pick(mouse.x) }
                                onPositionChanged: { if (pressed) pick(mouse.x) }
                            }
                        }

                        // ── Swatch + Hex field ─────────────────────────
                        Row {
                            id: hexRow
                            anchors.top: hueStrip.bottom; anchors.topMargin: 8
                            width: parent.width; spacing: 10

                            Rectangle {
                                width: 32; height: 32; radius: 4
                                color: backend.crossColor
                                border.color: "#555"; border.width: 1
                            }

                            Rectangle {
                                width: parent.width - 42; height: 32; radius: 4
                                color: "#1c1c2e"; border.color: "#4466ff"; border.width: 1
                                Row {
                                    anchors { fill: parent; leftMargin: 10 }
                                    spacing: 4
                                    Text { text: "#"; color: "#888"; verticalAlignment: Text.AlignVCenter; height: parent.height }
                                    TextInput {
                                        id: hexField
                                        width: parent.width - 24
                                        height: parent.height
                                        color: "white"
                                        font.pixelSize: 14
                                        text: colorPicker.toHex2(backend.crossColor.r) + colorPicker.toHex2(backend.crossColor.g) + colorPicker.toHex2(backend.crossColor.b)
                                        verticalAlignment: Text.AlignVCenter
                                        onEditingFinished: {
                                            var hex = text.replace("#","")
                                            if (hex.length === 6) {
                                                var r = parseInt(hex.substr(0,2),16)/255
                                                var g = parseInt(hex.substr(2,2),16)/255
                                                var b = parseInt(hex.substr(4,2),16)/255
                                                backend.crossColor = Qt.rgba(r,g,b,1)
                                                colorPicker.initFromBackend()
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }




                    // Fine Position  
                    Text { text: "FINE POSITION"; color: "#666"; font.pixelSize: 11; font.bold: true; verticalAlignment: Text.AlignVCenter; height: 32 }


                    Row {
                        spacing: 6; width: parent.width
                        Text { text: "X: " + backend.crossOffsetX.toFixed(1); color: "white"; verticalAlignment: Text.AlignVCenter; width: 80 }
                        Button { text: "← X −0.5"; width: 70; height: 30
                            contentItem: Text { text: parent.text; color: "white"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { color: parent.hovered ? "#555" : "#333"; radius: 4 }
                            onClicked: backend.crossOffsetX = backend.crossOffsetX - 0.5 }
                        Button { text: "X +0.5 →"; width: 70; height: 30
                            contentItem: Text { text: parent.text; color: "white"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { color: parent.hovered ? "#555" : "#333"; radius: 4 }
                            onClicked: backend.crossOffsetX = backend.crossOffsetX + 0.5 }
                    }
                    Row {
                        spacing: 6; width: parent.width
                        Text { text: "Y: " + backend.crossOffsetY.toFixed(1); color: "white"; verticalAlignment: Text.AlignVCenter; width: 80 }
                        Button { text: "↑ Y −0.5"; width: 70; height: 30
                            contentItem: Text { text: parent.text; color: "white"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { color: parent.hovered ? "#555" : "#333"; radius: 4 }
                            onClicked: backend.crossOffsetY = backend.crossOffsetY - 0.5 }
                        Button { text: "↓ Y +0.5"; width: 70; height: 30
                            contentItem: Text { text: parent.text; color: "white"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { color: parent.hovered ? "#555" : "#333"; radius: 4 }
                            onClicked: backend.crossOffsetY = backend.crossOffsetY + 0.5 }
                    }

                    // Snap to Center
                    Button {
                        text: "SNAP TO CENTER"
                        width: parent.width
                        height: 36
                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            font.pixelSize: 12
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: parent.hovered ? "#3375ee" : "#224ecc"
                            radius: 6
                            border.color: "#4488ff"
                            border.width: 1
                        }
                        onClicked: {
                            backend.crossOffsetX = 0;
                            backend.crossOffsetY = 0;
                        }
                    }

                    // Reset to Defaults
                    Button {
                        text: "RESET TO DEFAULTS"
                        width: parent.width
                        height: 36
                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            font.pixelSize: 12
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: parent.hovered ? "#cc3333" : "#aa2222"
                            radius: 6
                            border.color: "#ff6666"
                            border.width: 1
                        }
                        onClicked: backend.resetCrosshairToDefaults()
                    }

                    // Saved Config
                    Text { text: "SAVED CONFIG"; color: "#666"; font.pixelSize: 11; font.bold: true }

                    Row {
                        spacing: 8; width: parent.width
                        TextField {
                            id: presetNameField
                            width: parent.width - 110
                            placeholderText: "Config name..."
                            color: "white"
                            background: Rectangle { color: "#1c1c2e"; radius: 4; border.color: "#333"; border.width: 1 }
                        }
                        Button {
                            text: "SAVE"
                            width: 96; height: 34
                            contentItem: Text { text: parent.text; color: "white"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { color: parent.hovered ? "#3375ee" : "#224ecc"; radius: 4 }
                            onClicked: {
                                if (presetNameField.text.trim() !== "") {
                                    backend.saveCrosshairPreset(presetNameField.text.trim())
                                    presetNameField.text = ""
                                    presetList.model = backend.crosshairPresetNames()
                                }
                            }
                        }
                    }

                    // Preset list
                    Column {
                        id: presetListContainer
                        width: parent.width
                        spacing: 4

                        Connections {
                            target: backend
                            onCrosshairPresetsChanged: presetList.model = backend.crosshairPresetNames()
                        }

                        ListView {
                            id: presetList
                            width: parent.width
                            height: Math.min(model.length * 38, 160)
                            model: backend.crosshairPresetNames()
                            clip: true
                            delegate: Rectangle {
                                width: presetList.width
                                height: 34
                                color: "#1a1a2e"
                                radius: 4
                                Row {
                                    anchors { fill: parent; leftMargin: 8; rightMargin: 4 }
                                    spacing: 6
                                    Text {
                                        text: modelData
                                        color: "white"
                                        font.pixelSize: 11
                                        verticalAlignment: Text.AlignVCenter
                                        width: parent.width - 80
                                        elide: Text.ElideRight
                                        height: parent.height
                                    }
                                    Button { text: "Load"; width: 46; height: 26
                                        contentItem: Text { text: parent.text; color: "white"; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                        background: Rectangle { color: parent.hovered ? "#3a9e6e" : "#2a7a54"; radius: 3 }
                                        onClicked: { backend.loadCrosshairPreset(index); presetList.model = backend.crosshairPresetNames() }
                                    }
                                    Button { text: "✕"; width: 26; height: 26
                                        contentItem: Text { text: parent.text; color: "white"; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                        background: Rectangle { color: parent.hovered ? "#cc3333" : "#882222"; radius: 3 }
                                        onClicked: { backend.deleteCrosshairPreset(index); presetList.model = backend.crosshairPresetNames() }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }




        // ─── UPDATES ────────────────────────────────────────────────
        Rectangle {
            color: "#0d0d12"
            Column {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 15

                Text { text: "Version: " + backend.versionStr; color: "white"; font.pixelSize: 16 }
                
                Text { 
                    text: "Latest: " + backend.latestVersion
                    color: "#aaa"
                    visible: backend.latestVersion !== ""
                }

                Button {
                    text: {
                        if (backend.isDownloading) return "DOWNLOADING..."
                        if (backend.downloadComplete) return "INSTALL UPDATE"
                        if (backend.isCheckingForUpdates) return "CHECKING..."
                        if (backend.updateStatus === "Downloaded update was invalid. Click to retry.") return "RETRY DOWNLOAD"
                        if (backend.updateAvailable) return "UPDATE AVAILABLE"
                        if (backend.hasCheckedForUpdates && !backend.updateAvailable) return "UP TO DATE"
                        return "CHECK FOR UPDATES"
                    }
                    width: parent.width
                    height: 44
                    enabled: !backend.isDownloading && !backend.isCheckingForUpdates
                    contentItem: Text { text: parent.text; color: "white"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { 
                        color: backend.downloadComplete ? "#6a4cff" : (parent.hovered ? "#00cca3" : "#00a382")
                        radius: 4 
                    }
                    onClicked: {
                        if (backend.downloadComplete) {
                            backend.downloadUpdate() // Launches the downloaded installer
                        } else if (backend.updateAvailable) {
                            backend.downloadUpdate()
                        } else {
                            backend.checkForUpdates()
                        }
                    }
                }

                Row {
                    spacing: 10
                    width: parent.width

                    Text {
                        id: spinCog
                        text: "\uf013" // Font Awesome / Unicode Cog placeholder
                        font.family: "Segoe UI Symbol"
                        font.pixelSize: 20
                        color: "#00cca3"
                        visible: backend.isCheckingForUpdates || backend.isDownloading
                        
                        RotationAnimation on rotation {
                            from: 0; to: 360; duration: 1500; loops: Animation.Infinite
                            running: spinCog.visible
                        }
                    }

                    Text {
                        text: backend.updateStatus
                        color: {
                            if (backend.downloadComplete) return "#6a4cff"
                            if (backend.updateAvailable) return "#00cca3"
                            if (backend.isDownloading) return "#00ccff"
                            if (backend.isCheckingForUpdates) return "#aaa"
                            return "white"
                        }
                        font.bold: true
                        font.pixelSize: 14
                        verticalAlignment: Text.AlignVCenter
                        height: spinCog.height
                    }
                }

                // Confirm Latest Version Details
                Rectangle {
                    width: parent.width
                    height: 60
                    color: "#161625"
                    radius: 8
                    border.color: "#222"
                    visible: backend.hasCheckedForUpdates
                    
                    Column {
                        anchors.centerIn: parent
                        spacing: 4
                        Text { 
                            text: backend.updateAvailable ? "New Version Found!" : "Up to Date"
                            color: backend.updateAvailable ? "#00cca3" : "#888"
                            font.bold: true
                            font.pixelSize: 12
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                        Text { 
                            text: "Online: " + backend.latestVersion + "  |  Local: " + backend.versionStr
                            color: "#ccc"
                            font.pixelSize: 11
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }
                }

                Text {
                    text: "Update History: " + backend.updateHistory
                    color: "#888"
                    font.pixelSize: 12
                    visible: backend.updateHistory !== ""
                    horizontalAlignment: Text.AlignHCenter
                    width: parent.width
                }

                Item { height: 20 }

                Text {
                    text: "\"The best wins begin with the best drops\""
                    color: "#00ffa3"
                    font.pixelSize: 13
                    font.italic: true
                    horizontalAlignment: Text.AlignHCenter
                    width: parent.width
                }

            }
        }

        // ─── DEBUG ────────────────────────────────────────────────
        Rectangle {
            color: "#0d0d12"
            Flickable {
                anchors.fill: parent
                contentHeight: debugRootCol.implicitHeight + 40
                clip: true
                Column {
                    id: debugRootCol
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
                    spacing: 15

                    Text { text: "DEBUG DIAGNOSTICS"; color: "#666"; font.pixelSize: 11; font.bold: true }

                    RowLayout {
                        Switch {
                            checked: backend.showDebugOverlay
                            onCheckedChanged: backend.showDebugOverlay = checked
                        }
                        Text { text: "Show Debug Overlay on Screen"; color: "white"; font.pixelSize: 13 }
                    }

                    Rectangle {
                        width: parent.width; radius: 6; color: "#0e0e1a"; border.color: "#333"; border.width: 1
                        height: debugCol.implicitHeight + 30
                        Column {
                            id: debugCol
                            anchors { left: parent.left; right: parent.right; top: parent.top }
                            anchors.margins: 15
                            spacing: 9

                            // Header
                            Text { text: "LIVE DIAGNOSTICS"; color: "#444"; font.pixelSize: 10; font.bold: true; topPadding: 5 }

                            // Detection metrics
                            RowLayout { width: parent.width
                                Text { text: "Scanner Delay:"; color: "#aaa"; font.pixelSize: 13; Layout.fillWidth: true }
                                Text { text: backend.detectionDelayMs + " ms"; color: backend.detectionDelayMs < 15 ? "#00ffaa" : "#ff6644"; font.bold: true; font.pixelSize: 13 }
                            }
                            RowLayout { width: parent.width
                                Text { text: "Match Ratio:"; color: "#aaa"; font.pixelSize: 13; Layout.fillWidth: true }
                                Text { text: backend.detectionRatioPct + "%"; color: "#00ccff"; font.bold: true; font.pixelSize: 13 }
                            }
                            RowLayout { width: parent.width
                                Text { text: "Peak Match (2s):"; color: "#aaa"; font.pixelSize: 13; Layout.fillWidth: true }
                                Text { text: backend.peakMatchPct + "%"; color: backend.peakMatchPct > 0 ? "#ffaa00" : "#555"; font.bold: true; font.pixelSize: 13 }
                            }
                            RowLayout { width: parent.width
                                Text { text: "Dive State:"; color: "#aaa"; font.pixelSize: 13; Layout.fillWidth: true }
                                Text { text: backend.isDiving ? "DIVING" : "GLIDING"; color: backend.isDiving ? "#ff5050" : "#50ff80"; font.bold: true; font.pixelSize: 13 }
                            }
                            RowLayout { width: parent.width
                                Text { text: "Input Locked:"; color: "#aaa"; font.pixelSize: 13; Layout.fillWidth: true }
                                Text { text: backend.inputLocked ? "YES" : "NO"; color: backend.inputLocked ? "#cc88ff" : "#555"; font.bold: true; font.pixelSize: 13 }
                            }
                            RowLayout { width: parent.width
                                Text { text: "Lock Reason:"; color: "#aaa"; font.pixelSize: 13; Layout.fillWidth: true }
                                Text { text: backend.lockTriggerReason; color: backend.lockTriggerReason !== "None" ? "#ffaa00" : "#555"; font.bold: true; font.pixelSize: 13 }
                            }

                            Rectangle { width: parent.width; height: 1; color: "#222"; }

                            // Game gate status
                            RowLayout { width: parent.width
                                Text { text: "Fortnite Running:"; color: "#aaa"; font.pixelSize: 13; Layout.fillWidth: true }
                                Text { text: backend.fnRunning ? "YES" : "NO"; color: backend.fnRunning ? "#00ffcc" : "#ff4c4c"; font.bold: true; font.pixelSize: 13 }
                            }
                            RowLayout { width: parent.width
                                Text { text: "Fortnite Focused:"; color: "#aaa"; font.pixelSize: 13; Layout.fillWidth: true }
                                Text { text: backend.fnFocused ? "YES" : "NO"; color: backend.fnFocused ? "#00ffcc" : "#ff4c4c"; font.bold: true; font.pixelSize: 13 }
                            }
                            RowLayout { width: parent.width
                                Text { text: "Mouse in Fortnite Focus:"; color: "#aaa"; font.pixelSize: 13; Layout.fillWidth: true }
                                Text { text: backend.fnMouseHidden ? "YES" : "NO"; color: backend.fnMouseHidden ? "#00ffcc" : "#ff4c4c"; font.bold: true; font.pixelSize: 13 }
                            }

                            Rectangle { width: parent.width; height: 1; color: "#222"; }

                            // System info
                            RowLayout { width: parent.width
                                Text { text: "ROI:"; color: "#aaa"; font.pixelSize: 13; Layout.fillWidth: true }
                                Text { text: backend.roiDimensions; color: "#88aacc"; font.bold: true; font.pixelSize: 13 }
                            }
                            RowLayout { width: parent.width
                                Text { text: "Scanner CPU:"; color: "#aaa"; font.pixelSize: 13; Layout.fillWidth: true }
                                Text { text: backend.scannerCpuPct + "%"; color: backend.scannerCpuPct < 50 ? "#00ffaa" : "#ff6644"; font.bold: true; font.pixelSize: 13 }
                            }
                            RowLayout { width: parent.width
                                Text { text: "Version:"; color: "#aaa"; font.pixelSize: 13; Layout.fillWidth: true }
                                Text { text: "v" + backend.versionStr; color: "#666"; font.bold: true; font.pixelSize: 13 }
                            }

                            Rectangle { width: parent.width; height: 1; color: "#444"; }

                            // X-RAY MONITOR (v5.1.11)
                            Text { text: "NITRO X-RAY MONITOR (1ms)"; color: "#00ffa3"; font.pixelSize: 10; font.bold: true; topPadding: 5 }

                            RowLayout { width: parent.width
                                Text { text: "Physical Truth (P/T):"; color: "#aaa"; font.pixelSize: 12; Layout.fillWidth: true }
                                Text { text: backend.physicalKeyStates; color: "#00ffa3"; font.bold: true; font.pixelSize: 11; font.family: "Consolas" }
                            }

                            RowLayout { width: parent.width
                                Text { text: "RAW Hardware W:"; color: "#aaa"; font.pixelSize: 12; Layout.fillWidth: true }
                                Text { text: backend.rawWState; color: "#00ffa3"; font.bold: true; font.pixelSize: 12 }
                            }

                            RowLayout { width: parent.width
                                Text { text: "Input Lock State:"; color: "#aaa"; font.pixelSize: 12; Layout.fillWidth: true }
                                Text { text: backend.inputLockStatus; color: backend.inputLockStatus === "ACTIVE" ? "#ff4c4c" : "#00ffaa"; font.bold: true; font.pixelSize: 12 }
                            }

                            RowLayout { width: parent.width
                                Text { text: "Ghost Detector:"; color: "#aaa"; font.pixelSize: 12; Layout.fillWidth: true }
                                Text { text: backend.ghostMismatch ? "MISMATCH!" : "OK"; color: backend.ghostMismatch ? "#ff4c4c" : "#00ffaa"; font.bold: true; font.pixelSize: 12 }
                            }

                            Text { text: "Last Sync Event:"; color: "#aaa"; font.pixelSize: 12 }
                            Rectangle {
                                width: parent.width; height: 40; radius: 4; color: "#1c1c2e"; border.color: "#333"; border.width: 1
                                Text { 
                                    anchors.centerIn: parent; width: parent.width - 10
                                    text: backend.nitroSyncLog; color: "#cc88ff"; font.pixelSize: 11; font.family: "Consolas"; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                                }
                            }

                            Item { height: 5 }
                        }
                    }

                    Text {
                        text: "Angle updates only when Fortnite is focused AND mouse is hidden. Input locking pauses camera during FOV transitions."
                        color: "#555"; font.pixelSize: 11; width: parent.width; wrapMode: Text.WordWrap
                    }

                    Item { height: 10 }

                    Text { text: "PRO TIPS & SHORTCUTS"; color: "#666"; font.pixelSize: 11; font.bold: true }

                    Text {
                        text: "ROI Cancel: Press ROI key or Esc when in selection stages."
                        color: "#aaa"; font.pixelSize: 11; width: parent.width; wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
