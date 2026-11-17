#include "vstarticulationmapper.h"
#include "audio/common/rpc/rpcpacker.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
using namespace muse;
using namespace muse::vst;
using Art = mpe::ArticulationType;
static int checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::cerr<<"FAIL "<<__LINE__<<" " #x <<'\n';std::exit(1);}}while(false)
static mpe::NoteEvent note(const std::string& token, Art disclosed=Art::Undefined, int pitch=60, Art local=Art::Undefined)
{
    mpe::ArrangementContext a; a.nominalDuration=a.actualDuration=500000;
    mpe::PitchContext p;p.nominalPitchLevel=(pitch-mpe::ZERO_PITCH_LEVEL_MIDI_EQUIVALENT)*mpe::PITCH_LEVEL_STEP;
    mpe::ExpressionContext e;e.persistentPlayingTechnique=String::fromStdString(token);e.disclosedArticulation=disclosed;
    e.articulations[Art::Standard]={};
    if(local!=Art::Undefined)e.articulations[local]={};
    return mpe::NoteEvent(std::move(a),std::move(p),std::move(e));
}
int main(int argc,char**argv)
{
    QCoreApplication app(argc,argv); CHECK(argc==2);
    QFile file(QString::fromLocal8Bit(argv[1]));CHECK(file.open(QIODevice::ReadOnly));
    QJsonObject original=QJsonDocument::fromJson(file.readAll()).object();CHECK(!original.isEmpty());
    QTemporaryDir temp;CHECK(temp.isValid());
    auto use=[&](const QJsonObject& profile){QFile f(temp.filePath("profile.json"));CHECK(f.open(QIODevice::WriteOnly));
        CHECK(f.write(QJsonDocument(profile).toJson())>0);f.close();qputenv("MUSESCORE_VST_EXPRESSION_MAP",f.fileName().toLocal8Bit());};
    qunsetenv("MUSESCORE_VST_EXPRESSION_TRACE_DIR");
    const std::map<std::string,mpe::SoundId> families{{"violin",mpe::SoundId::Violin},{"viola",mpe::SoundId::Viola},{"violoncello",mpe::SoundId::Violoncello}};
    int routeCount=0;
    for(const auto& [family,sound]:families) {
        use(original); VstArticulationMapper mapper; const mpe::PlaybackSetupData setup(sound,mpe::SoundCategory::Strings);
        CHECK(mapper.configure("Synchron Smooth Bridge",setup));
        auto instrument=original["instruments"].toObject()[QString::fromStdString(family)].toObject();
        auto routes=instrument["articulations"].toObject();
        for(auto it=routes.begin();it!=routes.end();++it) {
            auto route=it.value().toObject();if(!route.contains("explicitToken"))continue;
            ++routeCount;const std::string token=route["explicitToken"].toString().toStdString();
            auto input=note(token);mpe::PlaybackEvent received;
            CHECK(audio::rpc::RpcPacker::unpack(audio::rpc::RpcPacker::pack(mpe::PlaybackEvent(input)),received));
            const auto& decoded=std::get<mpe::NoteEvent>(received);CHECK(decoded==input);
            const auto events=mapper.eventsForNotes({&decoded});CHECK(events.size()==1);
            const auto& command=events.begin()->second;const auto keys=route["keyswitches"].toArray();
            CHECK(command.keyCount==static_cast<size_t>(keys.size()));
            for(int i=0;i<keys.size();++i)CHECK(command.keys[i]==keys[i].toInt());
            const auto req=mapper.requirementsForNotes({&decoded},events);CHECK(req.valid);
            CHECK(std::find(req.routeNames.begin(),req.routeNames.end(),it.key().toStdString())!=req.routeNames.end());
            CHECK(req.requiredLeafPaths.size()==req.requiredPatchEntries.size());
            for(const auto& preset:req.requiredPatchEntriesByPresetInstrument)CHECK(preset.second.size()==req.requiredLeafPaths.size());
            if(route.contains("disclosedFallbackRoute")) {
                auto expanded=note(token,Art::Tremolo32nd);const auto ev=mapper.eventsForNotes({&expanded});
                const auto fallback=routes[route["disclosedFallbackRoute"].toString()].toObject()["keyswitches"].toArray();
                CHECK(ev.begin()->second.keyCount==static_cast<size_t>(fallback.size()));
                for(int i=0;i<fallback.size();++i)CHECK(ev.begin()->second.keys[i]==fallback[i].toInt());
                const auto needed=mapper.requirementsForNotes({&expanded},ev);CHECK(needed.valid);
                CHECK(std::find(needed.routeNames.begin(),needed.routeNames.end(),route["disclosedFallbackRoute"].toString().toStdString())!=needed.routeNames.end());
            }
        }
        auto unknown=note("vsl:long/ssv.b0000000000000000");
        CHECK(!mapper.requirementsForNotes({&unknown},mapper.eventsForNotes({&unknown})).valid);
        std::vector<std::string> exactTokens;
        for(auto it=routes.begin();it!=routes.end();++it) {
            const auto route=it.value().toObject();if(!route.contains("explicitToken"))continue;
            const auto token=route["explicitToken"].toString().toStdString();exactTokens.push_back(token);
            if(instrument["monophonicArticulations"].toArray().contains(it.key())) {
                auto low=note(token,Art::Undefined,60),high=note(token,Art::Undefined,67);
                const auto events=mapper.eventsForNotes({&low,&high});CHECK(events.size()==1);
                const auto fallback=routes[instrument["chordFallbackArticulation"].toString()].toObject()["keyswitches"].toArray();
                CHECK(events.begin()->second.keyCount==static_cast<size_t>(fallback.size()));
                for(int i=0;i<fallback.size();++i)CHECK(events.begin()->second.keys[i]==fallback[i].toInt());
                CHECK(mapper.requirementsForNotes({&low,&high},events).valid);
            }
        }
        CHECK(exactTokens.size()>2);
        std::vector<mpe::NoteEvent> score;
        for(size_t i=0;i<exactTokens.size();++i) {
            auto value=note(exactTokens[i]);auto arrangement=value.arrangementCtx();
            arrangement.nominalTimestamp=arrangement.actualTimestamp=static_cast<mpe::timestamp_t>(i)*500000;
            auto pitch=value.pitchCtx();auto expression=value.expressionCtx();
            score.emplace_back(std::move(arrangement),std::move(pitch),std::move(expression));
        }
        std::vector<const mpe::NoteEvent*> scoreNotes;for(const auto& value:score)scoreNotes.push_back(&value);
        const auto entire=mapper.eventsForNotes(scoreNotes);CHECK(entire.size()==scoreNotes.size());
        const auto dependencies=mapper.requirementsForNotes(scoreNotes,entire);CHECK(dependencies.valid);
        CHECK(dependencies.requiredLeafPaths.size()>900);
        CHECK(dependencies.requiredPatchEntries.size()==dependencies.requiredLeafPaths.size());
        for(const auto& variant:dependencies.requiredPatchEntriesByPresetInstrument)CHECK(variant.second.size()==dependencies.requiredLeafPaths.size());
        auto left=note(exactTokens[0]),right=note(exactTokens[1],Art::Undefined,67);
        CHECK(!mapper.requirementsForNotes({&left,&right},mapper.eventsForNotes({&left,&right})).valid);
        auto local=note(exactTokens[0],Art::Undefined,60,Art::Pizzicato);
        CHECK(mapper.eventsForNotes({&local}).begin()->second==mapper.eventsForNotes({&left}).begin()->second);
        CHECK(mapper.requirementsForNotes({&local},mapper.eventsForNotes({&local})).valid);
    }
    CHECK(routeCount>1500);
    auto mutableProfile=original;auto instruments=mutableProfile["instruments"].toObject();auto violin=instruments["violin"].toObject();auto routes=violin["articulations"].toObject();
    QString key;for(auto it=routes.begin();it!=routes.end();++it)if(it.value().toObject().contains("explicitToken")){key=it.key();break;}
    CHECK(!key.isEmpty()); auto route=routes[key].toObject();const std::string token=route["explicitToken"].toString().toStdString();
    route["controls"]=QJsonArray{QJsonObject{{"controller",20},{"value",64}},QJsonObject{{"controller",3},{"value",127}}};
    routes[key]=route;violin["articulations"]=routes;instruments["violin"]=violin;mutableProfile["instruments"]=instruments;use(mutableProfile);
    VstArticulationMapper mapper;const mpe::PlaybackSetupData setup(mpe::SoundId::Violin,mpe::SoundCategory::Strings);
    CHECK(mapper.configure("Synchron Smooth Bridge",setup));auto input=note(token);auto cmd=mapper.eventsForNotes({&input}).begin()->second;
    CHECK(cmd.controls.size()==2&&cmd.controls[0].controller==3&&cmd.controls[0].value==127&&cmd.controls[1].controller==20);
    auto altered=cmd;altered.controls[1].value=65;CHECK(!(cmd==altered));
    // More than 4096 routes: catalog size is not constrained by the old 256 limit.
    auto bulk=route;bulk.remove("explicitToken");for(int i=0;i<4200;++i)routes[QString("bulk_%1").arg(i)]=bulk;
    violin["articulations"]=routes;instruments["violin"]=violin;mutableProfile["instruments"]=instruments;use(mutableProfile);
    CHECK(mapper.configure("Synchron Smooth Bridge",setup));
    // Invalid controller and dependency declarations must fail before enabling.
    for(const QJsonValue invalid:{QJsonValue(-1),QJsonValue(128),QJsonValue(3.5),QJsonValue("3")}) {
        auto changed=route;changed["controls"]=QJsonArray{QJsonObject{{"controller",invalid},{"value",64}}};
        routes[key]=changed;violin["articulations"]=routes;instruments["violin"]=violin;mutableProfile["instruments"]=instruments;use(mutableProfile);
        CHECK(!mapper.configure("Synchron Smooth Bridge",setup));
    }
    auto changed=route;changed["requiredPatchEntries"]=QJsonArray{};routes[key]=changed;violin["articulations"]=routes;instruments["violin"]=violin;mutableProfile["instruments"]=instruments;use(mutableProfile);
    CHECK(!mapper.configure("Synchron Smooth Bridge",setup));
    VstArticulationRequirements first,second;first.requiredLeafPaths={{1,2}};first.requiredPatchEntries={"vol://one"};
    first.requiredPatchEntriesByPresetInstrument={{"vi",{"vol://one"}},{"vi2",{"vol://two"}}};second=first;
    mergeVstArticulationRequirements(first,second);CHECK(first.valid&&first.requiredLeafPaths.size()==1);
    second.requiredPatchEntriesByPresetInstrument["vi2"][0]="vol://wrong";mergeVstArticulationRequirements(first,second);CHECK(!first.valid);
    std::cout<<checks<<" assertions passed; "<<routeCount<<" actual catalog routes passed RPC, mapping, and dependency checks\n";
}
