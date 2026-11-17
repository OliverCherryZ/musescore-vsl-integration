// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 OliverCherryZ
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as C
import MuseScore 3.0
import "CatalogData.js" as Catalog
import "SelectionActions.js" as Actions

MuseScore {
    id: root
    version: "1.1-metadata-candidate"
    title: "VSL Articulations"
    description: "Find a Synchron Solo Strings articulation and insert a concise score marking."
    pluginType: "dialog"
    categoryCode: "playback"
    width: 1020
    height: 760

    property var selectedTarget: null
    property var selectedRoute: null
    property var rows: []
    property string statusText: ""
    property bool validTarget: false
    property bool updating: false
    property bool ready: false

    function choice(combo) {
        return combo.currentIndex >= 0 && combo.model.length ? combo.model[combo.currentIndex].value : "";
    }
    function fill(combo, key) {
        var previous = choice(combo);
        var options = Catalog.options(key, choice(instrument));
        combo.model = options;
        combo.currentIndex = 0;
        for (var i = 0; i < options.length; ++i) if (options[i].value === previous) combo.currentIndex = i;
    }
    function refreshOptions() {
        if (!ready) return;
        updating = true;
        fill(category, "category"); fill(attack, "attack"); fill(release, "release");
        fill(vibrato, "vibrato"); fill(length, "length"); fill(mode, "mode");
        updating = false;
        refreshRows();
    }
    function refreshRows() {
        if (updating || !ready) return;
        rows = Catalog.filter({instrument: choice(instrument), q: query.text,
            category: choice(category), attack: choice(attack), release: choice(release),
            vibrato: choice(vibrato), length: choice(length), mode: choice(mode)});
        if (selectedRoute && !rows.some(function(r) { return r.id === selectedRoute.id; })) selectedRoute = null;
    }
    function inspectSelection() {
        if (!curScore) { statusText = "Open a score first."; return; }
        var result = Actions.resolveTarget(curScore.selection.elements, Element, NoteType.NORMAL);
        if (result.error) { statusText = result.error; return; }
        selectedTarget = result.target;
        validTarget = true;
        statusText = result.replacement ? "Replace the selected staff text. One Undo restores it." : "Insert at the selected note or rest. One Undo removes it.";
        try {
            var part = selectedTarget.staff.part;
            var id = Actions.targetInstrumentId(selectedTarget);
            var index = id === "violoncello" ? 3 : id === "viola" ? 2 : 0;
            if (id === "violin" && /(?:II|2|二)/.test(part.partName + " " + part.longName)) index = 1;
            instrument.currentIndex = index;
        } catch (e) { /* Browsing remains available; apply validates the actual target before editing. */ }
        refreshOptions();
        if (result.replacement) {
            selectedRoute = Actions.matchingRow(rows, selectedTarget);
        }
    }
    function applySelection() {
        if (!validTarget || !selectedRoute || !curScore) return;
        if (!Catalog.isKnown(selectedRoute.id, selectedRoute.caption, selectedRoute.token)) { statusText = "Catalog mismatch; the score was not changed."; return; }
        try {
            selectedTarget = Actions.apply({score: curScore, knownRoute: Catalog.knownRoute,
                createStaffText: function() { return newElement(Element.STAFF_TEXT); }},
                selectedTarget, Element.STAFF_TEXT, selectedRoute);
            statusText = "Articulation inserted. Playback uses the current score; no sync step is needed.";
        } catch (e) { statusText = "Change failed: " + e.toString(); }
    }
    onRun: inspectSelection()
    Component.onCompleted: { ready = true; refreshOptions(); }

    Rectangle {
        anchors.fill: parent
        color: "#f3f1eb"
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 12
            Text { text: "VSL Articulations"; font.pixelSize: 24; font.bold: true; color: "#2e4437" }
            Text { text: "Select a sound, then insert a short marking. Double-click a result to apply it."; color: "#5e6a62"; font.pixelSize: 13 }
            C.TextField { id: query; Layout.fillWidth: true; placeholderText: "Search articulation or detail: spicc., soft attack, pizzicato..."; onTextChanged: root.refreshRows() }
            GridLayout {
                columns: 4
                columnSpacing: 14
                rowSpacing: 5
                Layout.fillWidth: true
                Text { text: "Instrument" } Text { text: "Articulation" } Text { text: "Attack" } Text { text: "Release" }
                C.ComboBox {
                    id: instrument; Layout.fillWidth: true; textRole: "text"
                    model: [{text:"Violin I",value:"vi"},{text:"Violin II",value:"vi2"},{text:"Viola",value:"va"},{text:"Cello",value:"vc"}]
                    onCurrentIndexChanged: root.refreshOptions()
                }
                C.ComboBox { id: category; Layout.fillWidth: true; textRole: "text"; model: []; onCurrentIndexChanged: root.refreshRows() }
                C.ComboBox { id: attack; Layout.fillWidth: true; textRole: "text"; model: []; onCurrentIndexChanged: root.refreshRows() }
                C.ComboBox { id: release; Layout.fillWidth: true; textRole: "text"; model: []; onCurrentIndexChanged: root.refreshRows() }
                Text { text: "Vibrato" } Text { text: "Length / Recorded tempo" } Text { text: "Mode" } Text { text: "" }
                C.ComboBox { id: vibrato; Layout.fillWidth: true; textRole: "text"; model: []; onCurrentIndexChanged: root.refreshRows() }
                C.ComboBox { id: length; Layout.fillWidth: true; textRole: "text"; model: []; onCurrentIndexChanged: root.refreshRows() }
                C.ComboBox { id: mode; Layout.fillWidth: true; textRole: "text"; model: []; onCurrentIndexChanged: root.refreshRows() }
                C.Button {
                    text: "Clear filters"; Layout.fillWidth: true
                    onClicked: { root.updating = true; query.text = ""; category.currentIndex = 0; attack.currentIndex = 0; release.currentIndex = 0; vibrato.currentIndex = 0; length.currentIndex = 0; mode.currentIndex = 0; root.updating = false; root.refreshRows(); }
                }
            }
            Text { text: root.rows.length + " articulations"; color: "#526457"; font.pixelSize: 13 }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "white"
                border.color: "#d4d9d2"
                radius: 5
                ListView {
                    id: list
                    anchors.fill: parent
                    anchors.margins: 2
                    model: root.rows
                    clip: true
                    C.ScrollBar.vertical: C.ScrollBar { }
                    delegate: C.ItemDelegate {
                        id: rowDelegate
                        required property var modelData
                        width: list.width
                        height: Math.max(78, rowContents.implicitHeight + 18)
                        highlighted: root.selectedRoute !== null && root.selectedRoute.id === modelData.id
                        background: Rectangle {
                            color: rowDelegate.highlighted ? "#dcebdc" : rowDelegate.hovered ? "#eef1ed" : "#ffffff"
                            border.width: rowDelegate.highlighted ? 1 : 0
                            border.color: "#7a997e"
                        }
                        onClicked: root.selectedRoute = modelData
                        onDoubleClicked: { root.selectedRoute = modelData; root.applySelection(); }
                        contentItem: Column {
                            id: rowContents
                            width: rowDelegate.availableWidth
                            spacing: 5
                            Text { width: parent.width; text: rowDelegate.modelData.caption; textFormat: Text.PlainText; wrapMode: Text.Wrap; font.pixelSize: 14; color: "#263f30" }
                            Text { width: parent.width; text: rowDelegate.modelData.en; textFormat: Text.PlainText; wrapMode: Text.Wrap; font.pixelSize: 11; color: "#526258" }
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: root.selectedRoute ? "Score marking: " + root.selectedRoute.caption : "Select an articulation."
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font.pixelSize: 14
                color: "#263f30"
            }
            Text {
                Layout.fillWidth: true
                text: root.selectedRoute ? root.selectedRoute.en : ""
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: "#748076"
            }
            Text { Layout.fillWidth: true; text: root.statusText; wrapMode: Text.Wrap; font.pixelSize: 12; color: root.validTarget ? "#5a6c60" : "#8d4234" }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                C.Button { text: "Close"; onClicked: quit() }
                C.Button {
                    text: root.selectedTarget && root.selectedTarget.type === Element.STAFF_TEXT ? "Replace staff text" : "Insert articulation"
                    enabled: root.validTarget && root.selectedRoute !== null
                    highlighted: true
                    onClicked: root.applySelection()
                }
            }
        }
    }
}
