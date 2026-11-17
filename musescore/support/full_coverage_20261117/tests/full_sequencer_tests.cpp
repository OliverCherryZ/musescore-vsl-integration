#include "vstsequencer.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
using namespace muse;
using namespace muse::vst;
static int checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<"FAIL "<<__LINE__<<" " #x <<'\n';std::exit(1);}}while(false)
class Probe:public VstSequencer {public:const auto& mainEvents()const{return m_mainStreamEvents;}};
static mpe::NoteEvent note(const std::string& token)
{
    mpe::ArrangementContext a;a.nominalDuration=a.actualDuration=500000;
    mpe::PitchContext p;p.nominalPitchLevel=(60-mpe::ZERO_PITCH_LEVEL_MIDI_EQUIVALENT)*mpe::PITCH_LEVEL_STEP;
    mpe::ExpressionContext e;e.persistentPlayingTechnique=String::fromStdString(token);e.articulations[mpe::ArticulationType::Standard]={};
    return mpe::NoteEvent(std::move(a),std::move(p),std::move(e));
}
int main(int argc,char**argv)
{
    QCoreApplication app(argc,argv);CHECK(argc==2);QFile f(QString::fromLocal8Bit(argv[1]));CHECK(f.open(QIODevice::ReadOnly));
    auto profile=QJsonDocument::fromJson(f.readAll()).object();auto instruments=profile["instruments"].toObject();auto violin=instruments["violin"].toObject();auto routes=violin["articulations"].toObject();
    QString a,b;for(auto it=routes.begin();it!=routes.end();++it)if(it.value().toObject().contains("explicitToken")){if(a.isEmpty())a=it.key();else{b=it.key();break;}}
    CHECK(!a.isEmpty()&&!b.isEmpty());auto route=routes[a].toObject();route["controls"]=QJsonArray{QJsonObject{{"controller",3},{"value",127}},QJsonObject{{"controller",20},{"value",64}}};routes[a]=route;
    violin["articulations"]=routes;instruments["violin"]=violin;profile["instruments"]=instruments;
    QTemporaryDir dir;QFile out(dir.filePath("map.json"));CHECK(out.open(QIODevice::WriteOnly));out.write(QJsonDocument(profile).toJson());out.close();qputenv("MUSESCORE_VST_EXPRESSION_MAP",out.fileName().toLocal8Bit());qunsetenv("MUSESCORE_VST_EXPRESSION_TRACE_DIR");
    const std::string tokenA=route["explicitToken"].toString().toStdString(),tokenB=routes[b].toObject()["explicitToken"].toString().toStdString();
    const mpe::PlaybackSetupData setup(mpe::SoundId::Violin,mpe::SoundCategory::Strings);
    Probe seq;int callbacks=0;seq.setArticulationRequirementsCallback([&](const auto&){++callbacks;});CHECK(callbacks==1);
    seq.configureArticulations("Synchron Smooth Bridge",setup,"test");seq.init(ParamsMapping{{3,9003},{20,9020}},false);
    mpe::PlaybackData data;data.originEvents[0].emplace_back(note(tokenA));seq.load(data);CHECK(!seq.isActive());
    CHECK(seq.articulationRequirements().valid);CHECK(seq.articulationRequirements().generation>0);
    const auto& events=seq.mainEvents().at(0);CHECK(events.size()==4);
    CHECK(std::holds_alternative<ParamChangeEvent>(events[0]));CHECK(std::get<ParamChangeEvent>(events[0]).paramId==9003);
    CHECK(std::get<ParamChangeEvent>(events[0]).value==1.0);CHECK(std::get<ParamChangeEvent>(events[1]).paramId==9020);
    CHECK(std::abs(std::get<ParamChangeEvent>(events[1]).value-64.0/127.0)<1e-10);
    CHECK(std::holds_alternative<VstArticulationEvent>(events[2]));CHECK(std::get<VstEvent>(events[3]).type==VstEvent::kNoteOnEvent);
    CHECK(seq.mainEvents().at(500000).size()==1);CHECK(std::get<VstEvent>(seq.mainEvents().at(500000)[0]).type==VstEvent::kNoteOffEvent);
    auto previous=seq.articulationRequirements().generation;mpe::PlaybackEventsMap edited;edited[0].emplace_back(note(tokenB));
    data.mainStream.send(edited,mpe::DynamicLevelLayers{});CHECK(seq.articulationRequirements().generation>previous);
    auto names=seq.articulationRequirements().routeNames;CHECK(std::find(names.begin(),names.end(),b.toStdString())!=names.end());CHECK(std::find(names.begin(),names.end(),a.toStdString())==names.end());
    mpe::PlaybackEventsMap preview;preview[0].emplace_back(note(tokenA));data.offStream.send(preview,mpe::DynamicLevelLayers{},true);
    names=seq.articulationRequirements().routeNames;CHECK(std::find(names.begin(),names.end(),a.toStdString())!=names.end());CHECK(std::find(names.begin(),names.end(),b.toStdString())!=names.end());
    data.mainStream.send(edited,mpe::DynamicLevelLayers{});names=seq.articulationRequirements().routeNames;
    CHECK(std::find(names.begin(),names.end(),a.toStdString())==names.end());CHECK(seq.articulationRequirements().valid);
    Probe missing;missing.configureArticulations("Synchron Smooth Bridge",setup,"missing");missing.init(ParamsMapping{{3,9003}},false);missing.load(data);
    CHECK(!missing.articulationRequirements().valid);CHECK(!missing.articulationRequirements().diagnostics.empty());
    mpe::PlaybackEventsMap invalid;invalid[0].emplace_back(note("vsl:long/not-in-catalog"));data.mainStream.send(invalid,mpe::DynamicLevelLayers{});CHECK(!seq.articulationRequirements().valid);
    data.mainStream.send(edited,mpe::DynamicLevelLayers{});CHECK(seq.articulationRequirements().valid);
    std::cout<<checks<<" assertions passed: actual sequencer inactive edit, preview/main union, CC ParamID conversion/order, unchanged musical note times, invalid/recovery\n";
}
