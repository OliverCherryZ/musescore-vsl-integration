/* SPDX-License-Identifier: GPL-3.0-only */
// Small native test executable linked to the same mapper implementation as the host.
#include "vstarticulationmapper.h"
#include "vstarticulationordering.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QDir>
#include <cstdlib>
#include <iostream>

using namespace muse;
using namespace muse::vst;
static int assertions = 0;
#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition << '\n'; std::exit(1); } } while (false)

struct TestNoteEvent {
    enum { kNoteOnEvent = 0, kNoteOffEvent = 1 };
    int type = kNoteOnEvent;
    struct { int pitch; } noteOn { 0 };
};

static void checkOrdering()
{
    using Event = std::variant<TestNoteEvent, float, VstArticulationEvent>;
    VstArticulationEvent keys;
    keys.keys[0] = 25;
    keys.keys[1] = 105; // A high keyswitch must still precede musical MIDI 60.
    keys.keyCount = 2;
    std::vector<Event> events { TestNoteEvent { 0, { 72 } }, keys,
                               TestNoteEvent { 1, { 60 } }, 0.65f,
                               TestNoteEvent { 0, { 60 } } };
    sortVstArticulationEvents<TestNoteEvent>(events);
    CHECK(std::get<TestNoteEvent>(events[0]).type == TestNoteEvent::kNoteOffEvent);
    CHECK(std::get<float>(events[1]) == 0.65f);
    CHECK(std::get<VstArticulationEvent>(events[2]).keys[1] == 105);
    CHECK(std::get<TestNoteEvent>(events[3]).noteOn.pitch == 60);
    CHECK(std::get<TestNoteEvent>(events[4]).noteOn.pitch == 72);
    // Stable sorting is idempotent; preview and main share this function.
    sortVstArticulationEvents<TestNoteEvent>(events);
    CHECK(std::get<VstArticulationEvent>(events[2]).keys[0] == 25);
}

static mpe::NoteEvent note(mpe::timestamp_t at, const char* technique = "", int pitch = 60,
                          mpe::ArticulationType symbol = mpe::ArticulationType::Undefined,
                          mpe::ArticulationType disclosed = mpe::ArticulationType::Undefined,
                          mpe::ArticulationType legacy = mpe::ArticulationType::Undefined)
{
    mpe::ArrangementContext arrangement;
    arrangement.nominalTimestamp = at;
    arrangement.actualTimestamp = at;
    arrangement.nominalDuration = 500000;
    arrangement.actualDuration = 500000;
    mpe::PitchContext p;
    p.nominalPitchLevel = (pitch - mpe::ZERO_PITCH_LEVEL_MIDI_EQUIVALENT) * mpe::PITCH_LEVEL_STEP;
    mpe::ExpressionContext expression;
    expression.persistentPlayingTechnique = String::fromStdString(technique);
    expression.disclosedArticulation = disclosed;
    expression.legacyPersistentArticulation = legacy;
    if (symbol != mpe::ArticulationType::Undefined) {
        expression.articulations[symbol] = {};
    }
    return mpe::NoteEvent(std::move(arrangement), std::move(p), std::move(expression));
}

static VstArticulationEvent command(const VstArticulationMapper& mapper, const mpe::NoteEvent& n)
{
    auto events = mapper.eventsForNotes({ &n });
    CHECK(events.size() == 1);
    return events.begin()->second;
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    checkOrdering();
    CHECK(argc == 2);
    const QByteArray profilePath(argv[1]);
    QFile input(QString::fromLocal8Bit(profilePath));
    CHECK(input.open(QIODevice::ReadOnly));
    const QJsonObject original = QJsonDocument::fromJson(input.readAll()).object();
    CHECK(!original.isEmpty());
    QTemporaryDir temp;
    CHECK(temp.isValid());
    auto use = [&temp](const QJsonObject& object) {
        const QString path = temp.filePath("expression-map.json");
        QFile output(path);
        CHECK(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
        CHECK(output.write(QJsonDocument(object).toJson()) > 0);
        output.close();
        qputenv("MUSESCORE_VST_EXPRESSION_MAP", path.toLocal8Bit());
    };
    const mpe::PlaybackSetupData violin(mpe::SoundId::Violin, mpe::SoundCategory::Strings);
    const mpe::PlaybackSetupData cello(mpe::SoundId::Violoncello, mpe::SoundCategory::Strings);
    VstArticulationMapper mapper;
    qunsetenv("MUSESCORE_VST_EXPRESSION_MAP");
    qunsetenv("MUSESCORE_VSL_ARTICULATION_MAP");
    CHECK(!mapper.configure("Synchron Smooth Bridge", violin));
    use(original);
    CHECK(!mapper.configure("Unrelated instrument", violin));
    CHECK(!mapper.configure("Synchron Smooth Bridge", mpe::PlaybackSetupData(mpe::SoundId::Piano, mpe::SoundCategory::Keyboards)));
    CHECK(mapper.configure("Synchron Smooth Bridge", violin));
    auto standard = command(mapper, note(0));
    CHECK(standard.keyCount == 5 && standard.keys[0] == 25 && standard.keys[1] == 36 && standard.keys[2] == 44);
    CHECK(mapper.configure("Synchron Smooth Bridge", cello));
    auto celloStandard = command(mapper, note(0));
    CHECK(celloStandard.keys[0] == 25 && celloStandard.keys[1] == 84 && celloStandard.keys[2] == 92);
    CHECK(mapper.configure("Synchron Smooth Bridge", violin));

    CHECK(command(mapper, note(0, "pizzicato")).keys[0] == 30);
    CHECK(command(mapper, note(1, "arco")).keys[0] == 25);
    CHECK(command(mapper, note(0, "arco", 60, mpe::ArticulationType::Staccato)).keys[0] == 24);
    CHECK(command(mapper, note(1, "arco")).keys[0] == 25);
    CHECK(command(mapper, note(0, "pizzicato", 60, mpe::ArticulationType::Staccato)).keys[0] == 30);
    CHECK(command(mapper, note(0, "arco", 60, mpe::ArticulationType::Pizzicato,
                              mpe::ArticulationType::Undefined, mpe::ArticulationType::Pizzicato)).keys[0] == 25);
    CHECK(command(mapper, note(0, "arco", 60, mpe::ArticulationType::Harmonic)).keys[0] == 31);
    CHECK(command(mapper, note(0, "", 60, mpe::ArticulationType::Legato)).keys[0] == 26);
    CHECK(command(mapper, note(1)).keys[0] == 25);

    auto lower = note(0, "legato", 60);
    auto upper = note(0, "legato", 67);
    auto chord = mapper.eventsForNotes({ &lower, &upper });
    CHECK(chord.size() == 1 && chord.begin()->second.keys[0] == 25);
    upper = note(500000, "legato", 67);
    auto melody = mapper.eventsForNotes({ &lower, &upper });
    CHECK(melody.size() == 2 && melody.begin()->second.keys[0] == 26 && melody.rbegin()->second.keys[0] == 26);

    CHECK(command(mapper, note(0, "tremolo")).keys[0] == 28);
    CHECK(command(mapper, note(0, "", 60, mpe::ArticulationType::Tremolo64th, mpe::ArticulationType::Tremolo64th)).keys[0] == 24);
    CHECK(command(mapper, note(0, "tremolo", 60, mpe::ArticulationType::Tremolo64th, mpe::ArticulationType::Tremolo64th)).keys[0] == 24);
    CHECK(command(mapper, note(0, "pizzicato", 60, mpe::ArticulationType::Tremolo64th, mpe::ArticulationType::Tremolo64th)).keys[0] == 30);
    CHECK(command(mapper, note(0, "trill-half-tone")).keys[0] == 29);
    CHECK(command(mapper, note(0, "trill-whole-tone")).keys[1] != command(mapper, note(0, "trill-half-tone")).keys[1]);
    CHECK(command(mapper, note(0, "", 60, mpe::ArticulationType::Trill, mpe::ArticulationType::Trill)).keys[0] == 26);
    CHECK(command(mapper, note(0, "harmonics")).keys[0] == 31);
    CHECK(command(mapper, note(0, "spiccato")).keys[0] == 24);
    CHECK(command(mapper, note(0, "col-legno")).keys[0] == 30);

    // Future resource/instrument names work through data, without hardcoded VSL gating.
    auto generic = original;
    generic["resourceIds"] = QJsonArray { "Future KS instrument" };
    auto instruments = generic["instruments"].toObject();
    instruments["piano"] = instruments["violin"];
    generic["instruments"] = instruments;
    use(generic);
    CHECK(mapper.configure("Future KS instrument", mpe::PlaybackSetupData(mpe::SoundId::Piano, mpe::SoundCategory::Keyboards)));

    auto malformed = original;
    instruments = malformed["instruments"].toObject();
    auto v = instruments["violin"].toObject();
    auto routes = v["articulations"].toObject();
    auto route = routes["standard"].toObject();
    route["keyswitches"] = QJsonArray { 128 };
    routes["standard"] = route;
    v["articulations"] = routes;
    instruments["violin"] = v;
    malformed["instruments"] = instruments;
    use(malformed);
    CHECK(!mapper.configure("Synchron Smooth Bridge", violin) && !mapper.enabled());
    route["keyswitches"] = QJsonArray { 25 };
    route["command"] = "cc";
    routes["standard"] = route;
    v["articulations"] = routes;
    instruments["violin"] = v;
    malformed["instruments"] = instruments;
    use(malformed);
    CHECK(!mapper.configure("Synchron Smooth Bridge", violin) && !mapper.enabled());
    malformed = original;
    malformed["schemaVersion"] = 99;
    use(malformed);
    CHECK(!mapper.configure("Synchron Smooth Bridge", violin));
    use(original);
    CHECK(mapper.configure("Synchron Smooth Bridge", violin));
    CHECK(command(mapper, note(0)).keys[0] == 25);
    // Optional diagnostics are planned-only, written outside audio processing,
    // and have unique names even when resource/instrument/track names coincide.
    qputenv("MUSESCORE_VST_EXPRESSION_TRACE_DIR", temp.path().toLocal8Bit());
    CHECK(mapper.configure("Synchron Smooth Bridge", violin, "same-track"));
    auto muted = note(0, "mute");
    auto planned = mapper.eventsForNotes({ &muted });
    mapper.writePlannedDiagnostic({ &muted }, planned, "main");
    VstArticulationMapper second;
    CHECK(second.configure("Synchron Smooth Bridge", violin, "same-track"));
    second.writePlannedDiagnostic({ &muted }, second.eventsForNotes({ &muted }), "main");
    const auto traces = QDir(temp.path()).entryList({ "planned-*.json" }, QDir::Files);
    CHECK(traces.size() == 2);
    QFile trace(temp.filePath(traces.front()));
    CHECK(trace.open(QIODevice::ReadOnly));
    const auto diagnostic = QJsonDocument::fromJson(trace.readAll()).object();
    CHECK(diagnostic["plannedOnly"].toBool());
    CHECK(!diagnostic["actualRuntimeSendsObserved"].toBool(true));
    CHECK(diagnostic["unmappedPersistentTechniques"].toObject()["mute"].toInt() == 1);
    CHECK(diagnostic["events"].toArray().size() == 1);
    qunsetenv("MUSESCORE_VST_EXPRESSION_TRACE_DIR");
    std::cout << "PASS " << assertions << " native mapper assertions\n";
}
