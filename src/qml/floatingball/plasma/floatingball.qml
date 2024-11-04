/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Kai Uwe Broulik <kde@privat.broulik.de>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
import QtQuick 2.1
import QtQuick.Window 2.1
import org.kde.plasma.core 2.0 as PlasmaCore
import com.FloatingBall 1.0

Window {
    id: window
    flags: Qt.BypassWindowManagerHint | Qt.FramelessWindowHint
    title: "wallpaper"
    x: 300
    y: 110
    width: 800
    height: 500
    //color: Qt.rgba(0, 0, 0, 0)
    //opacity: 0.7
    visible: true
    //color: "#000000"
    /*Rectangle {
        id: winRectangle
        radius: 10//window.width / 2
        anchors.fill: parent
        color: "#000000"
        opacity: 0.7
    }*/

    AnimatedImage {
        id: animatedImage
        anchors.centerIn: parent
        source: "/home/deepin/Desktop/file-read-997043.gif" // 替换为你的GIF文件路径
    }

/*
    Window {
        id: winL
        flags: Qt.BypassWindowManagerHint | Qt.FramelessWindowHint
        x: window.x - 220
        y: window.y
        width: 200
        height: 200
        visible: false
        color: "#ffffff"

        FloatingBall {
            id: myClass
        }

        ListView {
            anchors.fill: parent
            model: floatingball.hasOpenWin//["11", "22", "33"]
            delegate: Text{
                text: modelData
                color: "blue"
                font.pixelSize: 16
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        // 处理点击事件
                        console.log("Item clicked:", index)
                        myClass.setActive(index)
                    }
                }
            }
        }
    }

    Window {
        id: winR
        flags: Qt.BypassWindowManagerHint | Qt.FramelessWindowHint
        x: window.x + window.width + 20
        y: window.y
        width: 200
        height: 200
        visible: false
        color: "#ffffff"
        ListView {
            anchors.fill: parent
            model: floatingball.hasUsedWin//["11", "22", "33"]
            delegate: Text{
                text: modelData
                color: "blue"
                font.pixelSize: 16
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        property int dragX: 0
        property int dragY: 0
        property int isShow: 0
        property bool isDrag: false
        property bool ismove: false

        onPressed: {
            dragX = mouseX;
            dragY = mouseY;
            isDrag = true;
        }

        onPositionChanged: {
            if (isDrag) {
                // Set the window position to current mouse position minus the drag start position
                var delta = Qt.point(mouse.x - dragX, mouse.y - dragY);
                window.x += delta.x
                window.y += delta.y
                ismove = true
            }
        }
        onReleased: {
                isDrag = false;
                
                if (!ismove){
                    isShow += 1
                    winL.visible = true;
                    winR.visible = true;
                    if (isShow % 2) {
                            winL.opacity = 1.0;
                            winL.width = 200;
                            winR.opacity = 1.0;
                            winR.width = 200;
                    } else {
                            winL.opacity = 0.0;
                            winL.width = 0;
                            winR.opacity = 0.0;
                            winL.width = 0;
                    }
                }
                ismove = false;
        }
        onWheel: {
            if (wheel.modifiers & Qt.ControlModifier) {
                if (wheel.angleDelta.y > 0 && winRectangle.opacity > 0.2) {
                    winRectangle.opacity -= 0.1;
                }
                else if (wheel.angleDelta.y < 0 && winRectangle.opacity < 1) {
                    winRectangle.opacity += 0.1;
                }
            }
        }
    }*/
}
