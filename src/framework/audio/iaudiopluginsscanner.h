/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
<<<<<<<< HEAD:src/project/qml/MuseScore/Project/UploadProgressDialog.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15

import MuseScore.UiComponents 1.0
import MuseScore.Project 1.0

StyledDialogView {
    id: root

    contentWidth: 314
    contentHeight: 52
    margins: 12

    modal: true
    frameless: true
    closeOnEscape: false

    ColumnLayout {
        anchors.fill: parent

        spacing: 8

        StyledTextLabel {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter

            text: qsTrc("project", "Saving online…")
            font: ui.theme.largeBodyBoldFont
        }
    }
}
========
#ifndef MU_AUDIO_IAUDIOPLUGINSSCANNER_H
#define MU_AUDIO_IAUDIOPLUGINSSCANNER_H

#include <memory>

#include "io/path.h"

namespace mu::audio {
class IAudioPluginsScanner
{
public:
    virtual ~IAudioPluginsScanner() = default;

    virtual io::paths_t scanPlugins() const = 0;
};

using IAudioPluginsScannerPtr = std::shared_ptr<IAudioPluginsScanner>;
}

#endif // MU_AUDIO_IAUDIOPLUGINSSCANNER_H
>>>>>>>> v4.3.2:src/framework/audio/iaudiopluginsscanner.h
