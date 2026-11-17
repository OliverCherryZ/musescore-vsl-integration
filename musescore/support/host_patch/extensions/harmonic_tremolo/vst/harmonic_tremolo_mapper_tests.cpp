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
    const mpe::PlaybackEvent original = event;
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

static void expect(const VstArticulationMapper& mapper, const mpe::NoteEvent& event,
                   const QJsonObject& instrument, const char* route)
{
    const auto result = command(mapper, event);
    const auto keys = instrument["articulations"].toObject()[route].toObject()["keyswitches"].toArray();
    CHECK(result.keyCount == static_cast<size_t>(keys.size()));
    for (int i = 0; i < keys.size(); ++i) { CHECK(result.keys[i] == keys[i].toInt()); }
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    CHECK(argc == 3); // new 20-route profile, unchanged 19-route profile
    auto read = [](const char* name) {
        QFile file(QString::fromLocal8Bit(name));
        CHECK(file.open(QIODevice::ReadOnly));
        auto result = QJsonDocument::fromJson(file.readAll()).object();
        CHECK(!result.isEmpty());
        return result;
    };
    const QJsonObject profile = read(argv[1]), previous = read(argv[2]);
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    auto use = [&temporary](const QJsonObject& value) {
        const QString path = temporary.filePath("profile.json");
        QFile file(path);
        CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        CHECK(file.write(QJsonDocument(value).toJson()) > 0);
        file.close();
        qputenv("MUSESCORE_VST_EXPRESSION_MAP", path.toLocal8Bit());
    };
    qunsetenv("MUSESCORE_VST_EXPRESSION_TRACE_DIR");
    const std::map<const char*, mpe::SoundId> families {
        { "violin", mpe::SoundId::Violin }, { "viola", mpe::SoundId::Viola }, { "violoncello", mpe::SoundId::Violoncello }
    };
    const Art tremolos[] { Art::Tremolo8th, Art::Tremolo16th, Art::Tremolo32nd, Art::Tremolo64th, Art::TremoloBuzz };
    for (const auto& family : families) {
        use(profile);
        const mpe::PlaybackSetupData setup(family.second, mpe::SoundCategory::Strings);
        VstArticulationMapper mapper;
        CHECK(mapper.configure("Synchron Smooth Bridge", setup));
        const auto instrument = profile["instruments"].toObject()[family.first].toObject();
        const auto literal = command(mapper, note("harmonic-tremolo", { Art::Standard }));
        CHECK(literal.keyCount == 2);
        CHECK(literal.keys[0] == 31);
        CHECK(literal.keys[1] == (family.second == mpe::SoundId::Violoncello ? 85 : 37));
        expect(mapper, note("harmonic-tremolo", { Art::Harmonic }), instrument, "harmonic_tremolo");
        for (Art type : tremolos) {
            expect(mapper, note("arco", { Art::Harmonic, type }, type), instrument, "harmonic_staccato");
            expect(mapper, note("harmonics", { Art::Harmonic, type }, type, Art::Harmonic), instrument, "harmonic_staccato");
            expect(mapper, note("harmonic-tremolo", { Art::Standard, type }, type), instrument, "harmonic_staccato");
            // The dedicated disclosed field is sufficient; no origin enum needs
            // to be present in the articulation map to prove the real expansion.
            expect(mapper, note("arco", { Art::Harmonic }, type), instrument, "harmonic_staccato");
            expect(mapper, note("arco", { type }, type), instrument, "staccato");
            expect(mapper, note("pizzicato", { Art::Harmonic, type }, type), instrument, "pizzicato");
            expect(mapper, note("col-legno", { Art::Harmonic, type }, type), instrument, "col_legno");
        }
        // A matching enum alone is NOT evidence of a disclosed ornament.
        expect(mapper, note("arco", { Art::Harmonic, Art::Tremolo64th }), instrument, "harmonic_sustain");
        expect(mapper, note("tremolo", { Art::Tremolo64th }), instrument, "tremolo_unmeasured");
        expect(mapper, note("tremolo", { Art::Harmonic, Art::Tremolo64th }, Art::Tremolo64th), instrument, "harmonic_staccato");
        expect(mapper, note("arco", { Art::Trill }, Art::Trill), instrument, "legato");
        expect(mapper, note("arco", { Art::Harmonic, Art::Trill }, Art::Trill), instrument, "harmonic_sustain");
        expect(mapper, note("harmonic-tremolo", { Art::Trill }, Art::Trill), instrument, "legato");
        expect(mapper, note("pizzicato", { Art::Harmonic, Art::SnapPizzicato, Art::Tremolo32nd }, Art::Tremolo32nd),
               instrument, "snap_pizzicato");
        use(previous);
        CHECK(mapper.configure("Synchron Smooth Bridge", setup));
        const auto old = previous["instruments"].toObject()[family.first].toObject();
        expect(mapper, note("arco", { Art::Harmonic, Art::Tremolo32nd }, Art::Tremolo32nd), old, "harmonic_sustain");
        expect(mapper, note("tremolo", { Art::Tremolo64th }), old, "tremolo_unmeasured");
    }
    auto mutate = [&profile](const QJsonValue& field) {
        auto changed = profile;
        auto instruments = changed["instruments"].toObject();
        auto violin = instruments["violin"].toObject();
        auto rules = violin["compositeMappings"].toArray();
        auto rule = rules.last().toObject();
        auto groups = rule["requires"].toArray();
        groups[1] = QJsonObject { { "anyDisclosedArticulations", field } };
        rule["requires"] = groups;
        rules[rules.size()-1] = rule;
        violin["compositeMappings"] = rules;
        instruments["violin"] = violin;
        changed["instruments"] = instruments;
        return changed;
    };
    VstArticulationMapper mapper;
    const mpe::PlaybackSetupData violin(mpe::SoundId::Violin, mpe::SoundCategory::Strings);
    for (const QJsonValue& invalid : { QJsonValue("Tremolo32nd"), QJsonValue(QJsonArray {}),
                                      QJsonValue(QJsonArray { "NotAnMpeEnum" }), QJsonValue(QJsonArray { "Undefined" }) }) {
        use(mutate(invalid));
        CHECK(!mapper.configure("Synchron Smooth Bridge", violin));
    }
    use(profile);
    CHECK(mapper.configure("Synchron Smooth Bridge", violin));
    std::cout << "PASS " << assertions << " harmonic tremolo RPC mapper assertions\n";
}
