/* SPDX-License-Identifier: GPL-3.0-only */
#include "vstarticulationmapper.h"
#include "audio/common/rpc/rpcpacker.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>

using namespace muse;
using namespace muse::vst;
using Art = mpe::ArticulationType;
static int assertions = 0;
#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition << '\n'; std::exit(1); } } while (false)

static mpe::NoteEvent note(const char* technique, std::initializer_list<Art> symbols = {},
                          Art disclosed = Art::Undefined, Art persistentSource = Art::Undefined)
{
    mpe::ArrangementContext arrangement;
    arrangement.nominalDuration = arrangement.actualDuration = 500000;
    mpe::PitchContext pitch;
    pitch.nominalPitchLevel = (60 - mpe::ZERO_PITCH_LEVEL_MIDI_EQUIVALENT) * mpe::PITCH_LEVEL_STEP;
    mpe::ExpressionContext expression;
    expression.persistentPlayingTechnique = String::fromStdString(technique);
    expression.disclosedArticulation = disclosed;
    expression.legacyPersistentArticulation = persistentSource;
    for (Art type : symbols) { expression.articulations[type] = {}; }
    return mpe::NoteEvent(std::move(arrangement), std::move(pitch), std::move(expression));
}

static VstArticulationEvent command(const VstArticulationMapper& mapper, const mpe::NoteEvent& event)
{
    // Exercise the real score-to-audio serialization boundary before selection.
    // Direct NoteEvent tests alone cannot detect metadata omitted from RPC.
    mpe::PlaybackEvent original = event;
    const ByteArray bytes = audio::rpc::RpcPacker::pack(original);
    mpe::PlaybackEvent received;
    CHECK(audio::rpc::RpcPacker::unpack(bytes, received));
    CHECK(std::holds_alternative<mpe::NoteEvent>(received));
    const auto& decoded = std::get<mpe::NoteEvent>(received);
    CHECK(decoded == event);
    const auto events = mapper.eventsForNotes({ &decoded });
    CHECK(events.size() == 1);
    return events.begin()->second;
}

static void expectRoute(const VstArticulationMapper& mapper, const mpe::NoteEvent& event,
                        const QJsonObject& instrument, const char* name)
{
    const auto actual = command(mapper, event);
    const auto expected = instrument["articulations"].toObject()[name].toObject()["keyswitches"].toArray();
    CHECK(actual.keyCount == static_cast<size_t>(expected.size()));
    for (int i = 0; i < expected.size(); ++i) { CHECK(actual.keys[i] == expected[i].toInt()); }
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    CHECK(argc == 3); // extended candidate, unchanged original baseline
    auto read = [](const char* path) {
        QFile file(QString::fromLocal8Bit(path));
        CHECK(file.open(QIODevice::ReadOnly));
        const auto object = QJsonDocument::fromJson(file.readAll()).object();
        CHECK(!object.isEmpty());
        return object;
    };
    const QJsonObject extended = read(argv[1]);
    const QJsonObject baseline = read(argv[2]);
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    auto use = [&temporary](const QJsonObject& profile) {
        const QString path = temporary.filePath("map.json");
        QFile file(path);
        CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        CHECK(file.write(QJsonDocument(profile).toJson()) > 0);
        file.close();
        qputenv("MUSESCORE_VST_EXPRESSION_MAP", path.toLocal8Bit());
    };
    qunsetenv("MUSESCORE_VST_EXPRESSION_TRACE_DIR");
    mpe::ArticulationMeta provenance;
    provenance.type = Art::Harmonic;
    provenance.fromPersistentPlayingTechnique = true;
    mpe::ArticulationMeta decodedProvenance;
    CHECK(audio::rpc::RpcPacker::unpack(audio::rpc::RpcPacker::pack(provenance), decodedProvenance));
    CHECK(decodedProvenance.fromPersistentPlayingTechnique);
    CHECK(decodedProvenance == provenance);
    const std::map<const char*, mpe::SoundId> families {
        { "violin", mpe::SoundId::Violin }, { "viola", mpe::SoundId::Viola }, { "violoncello", mpe::SoundId::Violoncello }
    };
    const std::map<const char*, const char*> textRoutes {
        { "ricochet", "ricochet_a1" }, { "saltando", "saltando" },
        { "harmonic-staccato", "harmonic_staccato" }, { "harmonic-detache", "harmonic_detache" },
        { "non-vibrato", "long_senza_vibrato" }, { "molto-vibrato", "long_molto_vibrato" },
        { "vibrato", "arco_normal" }, { "staccatissimo", "staccato" }
    };
    for (const auto& family : families) {
        use(extended);
        const mpe::PlaybackSetupData setup(family.second, mpe::SoundCategory::Strings);
        VstArticulationMapper mapper;
        CHECK(mapper.configure("Synchron Smooth Bridge", setup));
        const auto instrument = extended["instruments"].toObject()[family.first].toObject();
        for (const auto& route : textRoutes) { expectRoute(mapper, note(route.first), instrument, route.second); }
        expectRoute(mapper, note("harmonic-staccato", { Art::Harmonic }), instrument, "harmonic_staccato");
        expectRoute(mapper, note("harmonic-detache", { Art::Harmonic }), instrument, "harmonic_detache");
        expectRoute(mapper, note("arco", { Art::Staccatissimo }), instrument, "staccato");
        expectRoute(mapper, note("arco", { Art::Harmonic, Art::Staccato }), instrument, "harmonic_staccato");
        expectRoute(mapper, note("arco", { Art::Harmonic, Art::Staccatissimo }), instrument, "harmonic_staccato");
        expectRoute(mapper, note("harmonics", { Art::Harmonic, Art::Staccato }, Art::Undefined, Art::Harmonic),
                    instrument, "harmonic_staccato");
        expectRoute(mapper, note("arco", { Art::Harmonic, Art::Detache }), instrument, "harmonic_detache");
        expectRoute(mapper, note("harmonics", { Art::Harmonic, Art::Detache }, Art::Undefined, Art::Harmonic),
                    instrument, "harmonic_detache");
        expectRoute(mapper, note("arco", { Art::Staccato }), instrument, "staccato");
        expectRoute(mapper, note("arco", { Art::Detache }), instrument, "detache");
        expectRoute(mapper, note("pizzicato", { Art::Staccato }), instrument, "pizzicato");
        expectRoute(mapper, note("pizzicato", { Art::Harmonic, Art::Staccato }), instrument, "pizzicato");
        expectRoute(mapper, note("pizzicato", { Art::SnapPizzicato }), instrument, "snap_pizzicato");
        expectRoute(mapper, note("col-legno", { Art::Harmonic, Art::Detache }), instrument, "col_legno");
        expectRoute(mapper, note("arco", { Art::Harmonic, Art::Staccato }, Art::Tremolo64th), instrument, "harmonic_sustain");
        expectRoute(mapper, note("arco", { Art::Harmonic, Art::Detache }, Art::Trill), instrument, "harmonic_sustain");
        expectRoute(mapper, note("ricochet", { Art::Tremolo64th }, Art::Tremolo64th), instrument, "staccato");
        expectRoute(mapper, note("saltando", { Art::Trill }, Art::Trill), instrument, "legato");
        // Legacy provenance cannot invent a harmonic after an explicit arco.
        expectRoute(mapper, note("arco", { Art::Harmonic, Art::Staccato }, Art::Undefined, Art::Harmonic), instrument, "staccato");
        // The unchanged 13-route profile remains supported and keeps its old priorities.
        use(baseline);
        CHECK(mapper.configure("Synchron Smooth Bridge", setup));
        const auto oldInstrument = baseline["instruments"].toObject()[family.first].toObject();
        expectRoute(mapper, note("arco", { Art::Harmonic, Art::Staccato }), oldInstrument, "harmonic_sustain");
        expectRoute(mapper, note("pizzicato", { Art::Harmonic, Art::Staccato }), oldInstrument, "harmonic_sustain");
        expectRoute(mapper, note("arco", { Art::Staccatissimo }), oldInstrument, "standard");
    }
    auto mutateRule = [&extended](const QJsonObject& rule) {
        auto changed = extended;
        auto instruments = changed["instruments"].toObject();
        auto instrument = instruments["violin"].toObject();
        instrument["compositeMappings"] = QJsonArray { rule };
        instruments["violin"] = instrument;
        changed["instruments"] = instruments;
        return changed;
    };
    const auto validRule = extended["instruments"].toObject()["violin"].toObject()["compositeMappings"].toArray()[0].toObject();
    const mpe::PlaybackSetupData violin(mpe::SoundId::Violin, mpe::SoundCategory::Strings);
    VstArticulationMapper mapper;
    auto bad = validRule;
    bad["unknownCondition"] = true;
    use(mutateRule(bad));
    CHECK(!mapper.configure("Synchron Smooth Bridge", violin));
    bad = validRule; bad["requires"] = QJsonArray {};
    use(mutateRule(bad));
    CHECK(!mapper.configure("Synchron Smooth Bridge", violin));
    bad = validRule; bad["requires"] = QJsonArray { QJsonObject { { "anyArticulations", QJsonArray { "NotAnMpeEnum" } } } };
    use(mutateRule(bad));
    CHECK(!mapper.configure("Synchron Smooth Bridge", violin));
    bad = validRule; bad["articulation"] = "missing_route";
    use(mutateRule(bad));
    CHECK(!mapper.configure("Synchron Smooth Bridge", violin));
    bad = validRule; bad["allowDisclosed"] = "yes";
    use(mutateRule(bad));
    CHECK(!mapper.configure("Synchron Smooth Bridge", violin));
    use(extended);
    CHECK(mapper.configure("Synchron Smooth Bridge", violin));
    std::cout << "PASS " << assertions << " extension mapper assertions\n";
}
