.pragma library
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 OliverCherryZ

function same(a, b) { return a === b || (a && a.is && a.is(b)); }

function resolveTarget(elements, types, normalNoteType) {
    if (!elements || !elements.length) return { error: "Select one note, chord, rest, or staff text first." };
    var target = null;
    for (var i = 0; i < elements.length; ++i) {
        var el = elements[i];
        if (el.type === types.NOTE) el = el.parent;
        if (el.type !== types.CHORD && el.type !== types.REST && el.type !== types.STAFF_TEXT)
            return { error: "Select one normal note, chord, rest, or staff text." };
        if (el.type === types.CHORD && el.noteType !== normalNoteType)
            return { error: "Place the articulation on a normal note; grace-note positions are not supported." };
        if (target && !same(target, el))
            return { error: "The selection spans multiple positions. Select a single chord or staff text." };
        target = el;
    }
    return { target: target, replacement: target.type === types.STAFF_TEXT };
}

function escapeXml(text) {
    return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

function plainCaption(text) {
    // UI preselection only; native playback uses TextBase::plainText().
    return text.replace(/<br\s*\/?\s*>/gi, "\n").replace(/<[^>]*>/g, "")
        .replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&quot;/g, '"')
        .replace(/&apos;/g, "'").replace(/&amp;/g, "&");
}

function matchingRow(rows, target) {
    var text = plainCaption(target.text || "");
    for (var i = 0; i < rows.length; ++i) {
        if (target.vslArticulationToken && target.vslCaptionSnapshot === text
            && rows[i].token === target.vslArticulationToken) return rows[i];
        if (rows[i].label === text) return rows[i]; // Historical full-path alias only.
    }
    return null; // Never resolve an ambiguous short caption by its visible text.
}

function writeRoute(element, route) {
    if (typeof element.vslArticulationToken !== "string" || typeof element.vslCaptionSnapshot !== "string")
        throw new Error("This selector requires the VSL Studio build with native articulation metadata.");
    element.text = escapeXml(route.caption);
    element.vslArticulationToken = route.token;
    element.vslCaptionSnapshot = route.caption;
}

function targetInstrumentId(target) {
    var part = target && target.staff && target.staff.part;
    if (!part) throw new Error("The target staff's instrument could not be identified; the score was not changed.");
    // The browse filter and displayed part name are not the target instrument.
    // Prefer the instrument at this position so instrument changes are respected.
    var id;
    if (typeof part.instrumentIdAtTick === "function") {
        if (!target.fraction) throw new Error("The target position could not be identified; the score was not changed.");
        id = part.instrumentIdAtTick(target.fraction);
    } else {
        id = part.instrumentId;
    }
    if (id !== "violin" && id !== "viola" && id !== "violoncello")
        throw new Error("This selector requires a violin, viola, or cello target staff; the score was not changed.");
    return id;
}

function requireCompatibleInstrument(target, route) {
    var id = targetInstrumentId(target);
    // MuseScore identifies both violin parts as 'violin'. Require availability
    // in both violin presets; never guess Violin I/II from an editable part name.
    var required = id === "violin" ? ["vi", "vi2"] : id === "viola" ? ["va"] : ["vc"];
    for (var i = 0; i < required.length; ++i) {
        var found = false;
        for (var j = 0; j < route.instruments.length; ++j) {
            if (route.instruments[j] === required[i]) { found = true; break; }
        }
        if (!found)
            throw new Error("This articulation is unavailable for the target " + id + " staff. Choose a compatible articulation; the score was not changed.");
    }
}

function apply(api, target, staffTextType, route) {
    if (!api.score || !target) throw new Error("The score or target is no longer available.");
    if (!route || typeof route.caption !== "string" || !route.caption || typeof route.token !== "string"
        || !/^vsl:[a-z-]+\/ssv\.b[0-9a-f]{16}$/.test(route.token)
        || !api.knownRoute)
        throw new Error("The articulation does not match this catalog.");
    // ListView modelData may wrap nested arrays as QML sequences. Resolve the
    // three scalar fields back to our catalog; never trust model availability.
    route = api.knownRoute(route.id, route.caption, route.token);
    if (!route) throw new Error("The articulation does not match this catalog.");
    requireCompatibleInstrument(target, route);
    var created = target;
    api.score.startCmd(target.type === staffTextType ? "Replace VSL articulation" : "Insert VSL articulation");
    try {
        if (target.type === staffTextType) {
            writeRoute(target, route);
        } else {
            var cursor = api.score.newCursor();
            cursor.track = target.track;
            cursor.rewindToFraction(target.fraction);
            created = api.createStaffText();
            writeRoute(created, route);
            cursor.add(created);
        }
        api.score.endCmd();
    } catch (e) {
        api.score.endCmd(true);
        throw e;
    }
    return created;
}
