# Runtime configuration and articulation selector

This directory contains the expression map and MuseScore QML selector used by the custom host. It contains routing data and plugin source, not VSL samples, factory preset binaries, activation data, or recordings.

- `expression-maps/active.json`: Synchron Solo Strings Velocity preset routes for violin, viola, and cello. The two violin instances share the host instrument ID and use preset-specific patch references.
- `plugins/vsl_articulation_selector/`: the three selector files; keep them together.
- `tests/test-selector.js`: portable Node.js tests for catalog consistency, exact metadata binding, target-instrument guards, and transaction mocks.

See [the setup and usage guide](../docs/USAGE.zh-CN.md) and [validation scope](../docs/VALIDATION.md). The default profile is deliberately matched to the `Synchron Smooth Bridge` resource ID and the tested factory preset layout; it is not a universal expression map for every VSL product or preset.

Run the packaged logic tests from the repository root:

```sh
node runtime/tests/test-selector.js
```

An optional Qt 6 QML syntax check is:

```sh
qmlformat --normalize runtime/plugins/vsl_articulation_selector/VSLArticulationSelector.qml
```

These checks do not load the commercial player or prove musical audio output. The QML version string retains its historical `candidate` label to preserve the tested source; the actual release scope is documented separately.

The public JSON retains the installed profile's runtime fields exactly. Obsolete development-status fields and local audit paths were removed. Product preset names and `vol://` patch identifiers remain because the host uses them to verify and enable the correct sample leaves. These identifiers do not contain the samples themselves.

SPDX-License-Identifier: GPL-3.0-only. Copyright (C) 2026 OliverCherryZ. VSL and its product names belong to their respective owner; this project is independent.
