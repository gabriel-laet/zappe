pragma Singleton
import QtQuick

// Foco e scroll ficam curtos. Troca de tela um pouco mais longa.
// OutCubic assenta sem a cauda do OutExpo, do OutBack e do spring.
QtObject {
    readonly property int focusMs: 150
    readonly property int scrollMs: 160
    readonly property int screenMs: 230
    readonly property int fadeMs: 160
    readonly property int pressMs: 80
    readonly property int releaseMs: 140
    readonly property int ease: Easing.OutCubic
}
