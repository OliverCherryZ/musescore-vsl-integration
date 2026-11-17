#include "raw_reference_mix.h"
#include "raw_reference_broker.h"
#include <array>
#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expr); std::abort(); } } while (0)
#include <cmath>
#include <cstdio>
#include <cstring>
extern "C" int __cdecl BridgeCopyLatestReference(std::uint32_t, float*, float*, BridgeReferenceSegment*, std::uint32_t, BridgeReferenceInfo*);
using namespace Steinberg;
using namespace Steinberg::Vst;
int main() {
    constexpr int N = 4096, B = 11;
    std::array<std::array<float, N>, B*2> samples{}, original{};
    std::array<std::array<float*, 2>, B> pointers{};
    std::array<AudioBusBuffers, B> buses{};
    for (int bus=0; bus<B; ++bus) {
        for (int channel=0; channel<2; ++channel) {
            auto& s=samples[bus*2+channel];
            for (int i=0; i<N; ++i) s[i]=float(0.025*std::sin(i*(0.013+bus*0.00007)+bus*0.31+channel*0.5));
            pointers[bus][channel]=s.data();
        }
        buses[bus].numChannels=2;
        buses[bus].channelBuffers32=pointers[bus].data();
        buses[bus].silenceFlags=bus%2; // Host fill ignores flags; reference must too.
    }
    original=samples;
    ProcessData data;
    data.symbolicSampleSize=kSample32; data.numSamples=N; data.numOutputs=B; data.outputs=buses.data();
    std::array<float,N> left{},right{},hostL{},hostR{},copiedL{},copiedR{};
    constexpr std::uint64_t mask=(1ull<<B)-1;
    CHECK(sumHostOutputReference(data,mask,left.data(),right.data()));
    constexpr float gain=0.83f;
    for (int bus=0; bus<B; ++bus) for (int i=0; i<N; ++i) {
        hostL[i]+=samples[bus*2][i]*gain; hostR[i]+=samples[bus*2+1][i]*gain;
    }
    double xx=0,xy=0,yy=0;
    for (int i=0; i<N; ++i) { xx+=double(left[i])*left[i]+double(right[i])*right[i]; xy+=double(left[i])*hostL[i]+double(right[i])*hostR[i]; yy+=double(hostL[i])*hostL[i]+double(hostR[i])*hostR[i]; }
    const double estimate=xy/xx;
    double error=0;
    for (int i=0; i<N; ++i) { const double a=hostL[i]-left[i]*estimate,b=hostR[i]-right[i]*estimate; error+=a*a+b*b; }
    CHECK(error <= yy*1e-10);
    CHECK(std::memcmp(samples.data(), original.data(), sizeof(samples))==0);
    for (int bus=0; bus<B; ++bus) { CHECK(buses[bus].silenceFlags==std::uint64_t(bus%2)); CHECK(buses[bus].channelBuffers32==pointers[bus].data()); }
    publishRawReference(5,1,48000,0,true,0,2,left.data(),right.data(),N,true,N);
    std::array<BridgeReferenceSegment,1024> segments{}; BridgeReferenceInfo info{};
    CHECK(BridgeCopyLatestReference(N,copiedL.data(),copiedR.data(),segments.data(),segments.size(),&info));
    CHECK(std::memcmp(left.data(),copiedL.data(),sizeof(left))==0 && std::memcmp(right.data(),copiedR.data(),sizeof(right))==0);
    CHECK(info.flags==1 && info.serialEnd==N);
    CHECK(!sumHostOutputReference(data,0,left.data(),right.data()));
    CHECK(!sumHostOutputReference(data,1ull<<11,left.data(),right.data()));
    buses[3].numChannels=3;
    const auto beforeL=left;
    CHECK(!sumHostOutputReference(data,mask,left.data(),right.data()));
    CHECK(left==beforeL);
    buses[3].numChannels=2;
    CHECK(sumHostOutputReference(data,1,left.data(),right.data()));
    CHECK(left==samples[0] && right==samples[1]);
    buses[0].numChannels=1;
    CHECK(sumHostOutputReference(data,1,left.data(),right.data()));
    for (float f:right) CHECK(f==0);
    std::printf("11-bus smoke passed: output bytes/pointers/flags unchanged, reference published, scalar relative SSE %.12g, mono/invalid guards\n",error/yy);
}
