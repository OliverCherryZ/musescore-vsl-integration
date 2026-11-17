# MuseScore integration test support

These directories are copied next to the restored upstream checkout by
`../prepare_source.py`. The complete source patch preserves the existing relative
CMake includes, so this support is part of the source distribution.

The C++/CMake files are copies of the development test sources. The three JSON
profiles are 13-, 19-, and 20-route regression fixtures. Their runtime fields are
unchanged; local paths and historical report metadata were removed. They are not
the current complete articulation catalog and do not contain audio samples or
commercial plugin-state blobs.

The `full_coverage_20261117/tests` and `shutdown_lifetime_review` C++ files preserve
additional focused harnesses; only the targets declared by the included CMake
files are built automatically. See `docs/BUILD-MUSESCORE.md` in the repository.

The local additions are distributed under GPL-3.0-only. Existing upstream
copyright and license notices remain in the corresponding files.
