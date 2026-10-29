#include "AudioCore.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <limits>
#include <complex>

namespace veysona {
static uint16_t r16(std::istream& s){unsigned char a[2];if(!s.read((char*)a,2))throw std::runtime_error("Truncated WAV");return uint16_t(a[0]|(a[1]<<8));}
static uint32_t r32(std::istream& s){unsigned char a[4];if(!s.read((char*)a,4))throw std::runtime_error("Truncated WAV");return uint32_t(a[0])|uint32_t(a[1])<<8|uint32_t(a[2])<<16|uint32_t(a[3])<<24;}
static uint64_t r64(std::istream& s){const uint64_t lo=r32(s);return lo|(uint64_t(r32(s))<<32);}
static void w16(std::ostream& s,uint16_t a){unsigned char p[2]={(unsigned char)a,(unsigned char)(a>>8)};s.write((char*)p,2);}
static void w32(std::ostream& s,uint32_t a){unsigned char p[4]={(unsigned char)a,(unsigned char)(a>>8),(unsigned char)(a>>16),(unsigned char)(a>>24)};s.write((char*)p,4);}
static void w64(std::ostream& s,uint64_t a){w32(s,uint32_t(a));w32(s,uint32_t(a>>32));}
void buildWaveformPeaks(Clip& clip,size_t framesPerPeak){
 framesPerPeak=std::clamp(framesPerPeak,size_t(32),size_t(4096));
 const size_t frames=clip.stereo.size()/2;
 clip.waveformPeaks.assign((frames+framesPerPeak-1)/framesPerPeak,0.f);
 for(size_t frame=0;frame<frames;++frame){
  auto left=clip.stereo[frame*2],right=clip.stereo[frame*2+1];
  float peak=std::max(std::abs(left),std::abs(right));
  if(!std::isfinite(peak))peak=0;
  auto& x=clip.waveformPeaks[frame/framesPerPeak];x=std::max(x,peak);
 }
}
Clip splitAudioClip(Clip& first,double seconds,double bpm){
 if(!std::isfinite(seconds)||seconds<=0||first.sampleRate<8000||bpm<=0)
  throw std::runtime_error("Invalid clip split point");
 size_t cut=size_t(std::llround(seconds*first.sampleRate));
 const size_t total=first.stereo.size()/2;
 if(cut<1||cut>=total)throw std::runtime_error("Split point must be inside audio clip");
 Clip second=first;
 second.stereo.assign(first.stereo.begin()+std::ptrdiff_t(cut*2),first.stereo.end());
 first.stereo.resize(cut*2);
 second.startBeat=first.startBeat+(double(cut)/first.sampleRate)*bpm/60.;
 second.name=first.name+" (part 2)";
 first.fadeOutSeconds=0;second.fadeInSeconds=0;
 // After an edit these buffers no longer correspond to the full source WAV;
 // never allow a recovery/export path to treat them as unmodified imports.
 first.sourcePath.clear();second.sourcePath.clear();
 buildWaveformPeaks(first);buildWaveformPeaks(second);
 return second;
}
void reverseAudioClip(Clip& clip){
 for(size_t a=0,b=clip.stereo.size()/2;a<b/2;++a){
  size_t i=a*2,j=(b-1-a)*2;
  std::swap(clip.stereo[i],clip.stereo[j]);
  std::swap(clip.stereo[i+1],clip.stereo[j+1]);
 }
 // Reversing audio reverses its fades as well; sourcePath must not refer to an
 // unmodified WAV after the in-project buffer has been edited.
 std::swap(clip.fadeInSeconds,clip.fadeOutSeconds);
 clip.sourcePath.clear();
 buildWaveformPeaks(clip);
}
void normaliseAudioClip(Clip& clip,float targetPeak){
 float peak=0;for(float v:clip.stereo)if(std::isfinite(v))peak=std::max(peak,std::abs(v));
 if(peak>1e-8f)clip.gain=std::clamp(targetPeak,.1f,1.f)/peak;
}
Clip readWav(const std::string& path){
 std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot read: "+path);
 // Bound every chunk by the actual file AND the RIFF container. Seeking past
 // EOF is not an error for std::istream, so trusting declared sizes could
 // previously accept corrupt truncated files and underspecified chunks.
 in.seekg(0,std::ios::end);
 const auto end=in.tellg();
 if(end<std::streamoff(12))throw std::runtime_error("Truncated WAV header");
 const auto fileSize=std::streamoff(end);
 in.seekg(0);
 char tag[4]={};
 if(!in.read(tag,4))throw std::runtime_error("Not RIFF/RF64 WAV");
 const bool rf64=!std::memcmp(tag,"RF64",4);
 if(!rf64 && std::memcmp(tag,"RIFF",4))throw std::runtime_error("Not RIFF/RF64 WAV");
 const uint32_t riffSize=r32(in);
 if(!rf64 && (riffSize<4||std::uint64_t(riffSize)+8>std::uint64_t(fileSize)))
  throw std::runtime_error("Invalid or truncated RIFF container");
 if(rf64 && riffSize!=UINT32_MAX)throw std::runtime_error("Malformed RF64 header");
 std::streamoff riffEnd=rf64?fileSize:std::streamoff(riffSize)+8;
 if(!in.read(tag,4)||std::memcmp(tag,"WAVE",4))throw std::runtime_error("Not a WAV");
 int fmt=0,channels=0,bits=0;uint32_t rate=0;std::vector<char> data;
 bool foundFormat=false,foundData=false,foundDs64=false;
 uint64_t rf64DataBytes=0;
 while(std::streamoff(in.tellg())+8<=riffEnd){
  if(!in.read(tag,4))throw std::runtime_error("Incomplete WAV chunk header");
  const uint32_t size32=r32(in);
  const auto start=std::streamoff(in.tellg());
  uint64_t n=size32;
  if(!std::memcmp(tag,"ds64",4) && rf64){
   if(n<28)throw std::runtime_error("Invalid RF64 ds64 chunk");
   const auto fullRiffSize=r64(in);
   rf64DataBytes=r64(in);
   (void)r64(in); // 64-bit frame count
   const auto tableEntries=r32(in);
   if(tableEntries>0)throw std::runtime_error("RF64 chunk-size tables are unsupported");
   if(fullRiffSize<48||fullRiffSize+8>uint64_t(fileSize))throw std::runtime_error("Truncated RF64 container");
   riffEnd=std::streamoff(fullRiffSize+8);
   foundDs64=true;
  }
  if(rf64 && size32==UINT32_MAX){
   if(std::memcmp(tag,"data",4)||!foundDs64)throw std::runtime_error("RF64 chunk lacks ds64 length");
   n=rf64DataBytes;
  }
  if(n>uint64_t(riffEnd-start))throw std::runtime_error("WAV chunk extends beyond RIFF/RF64 container");
  if(!std::memcmp(tag,"fmt ",4)){
   if(n<16)throw std::runtime_error("Corrupt WAV format");
   fmt=r16(in);channels=r16(in);rate=r32(in);
   const auto byteRate=r32(in);const auto alignment=r16(in);bits=r16(in);
   if(channels<1||channels>2||bits<1||bits>32||alignment!=channels*((bits+7)/8)||
      uint64_t(byteRate)!=uint64_t(rate)*alignment)throw std::runtime_error("Invalid WAV format alignment");
   if(fmt==0xfffe){ // WAVE_FORMAT_EXTENSIBLE, mono/stereo PCM or IEEE float
    if(n<40||r16(in)<22)throw std::runtime_error("Truncated WAVE_FORMAT_EXTENSIBLE");
    const auto validBits=r16(in);(void)r32(in); // channel mask
    unsigned char guid[16];if(!in.read(reinterpret_cast<char*>(guid),16))throw std::runtime_error("Truncated WAV subtype GUID");
    const unsigned char tail[12]={0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
    if(!std::equal(guid+4,guid+16,tail)||guid[2]||guid[3]||guid[1])throw std::runtime_error("Unsupported WAV extensible subtype");
    fmt=guid[0];
    if(validBits!=bits)throw std::runtime_error("Packed WAV valid-bits format is unsupported");
   }
   foundFormat=true;
  }else if(!std::memcmp(tag,"data",4)){
   if(n>512ull*1024ull*1024ull)throw std::runtime_error("WAV import exceeds 512 MB RAM limit");
   if(foundData)throw std::runtime_error("Multiple WAV data chunks not supported");
   data.resize(size_t(n));
   if(n && !in.read(data.data(),std::streamsize(n)))throw std::runtime_error("Incomplete WAV data");
   foundData=true;
  }
  // Skip only when another byte remains, consistent with legal odd RIFF padding.
  const auto next=start+std::streamoff(n)+std::streamoff((n&1u)&&start+std::streamoff(n)<riffEnd);
  in.clear();in.seekg(next);
  if(!in)throw std::runtime_error("Invalid WAV chunk offset");
 }
 if(rf64&&!foundDs64)throw std::runtime_error("Missing RF64 ds64 chunk");
 if(!foundFormat||!foundData||!rate||rate<8000||rate>384000||(channels!=1&&channels!=2)||
    !((fmt==1&&(bits==16||bits==24||bits==32))||(fmt==3&&bits==32)))
  throw std::runtime_error("Unsupported WAV (use mono/stereo PCM16/24/32 or float32)");
 const auto bytes=bits/8;
 if(data.empty()||data.size()%size_t(bytes*channels))throw std::runtime_error("Empty or truncated WAV audio frames");
 const auto frames=data.size()/size_t(bytes*channels);
 Clip clip;clip.name=path;clip.sourcePath=path;clip.sampleRate=rate;clip.stereo.resize(frames*2);
 auto value=[&](size_t i){const auto* p=(const unsigned char*)data.data()+i*bytes;if(fmt==3){float f;std::memcpy(&f,p,4);return std::isfinite(f)?f:0.0f;}
  if(bits==16)return float(int16_t(uint16_t(p[0])|uint16_t(p[1])<<8))/32768.f;
  if(bits==24){int32_t v=int32_t(uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16);if(v&0x800000)v|=~0xffffff;return v/8388608.f;}
  int32_t v=int32_t(uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24);return v/2147483648.f;
 };
 for(size_t i=0;i<frames;++i){clip.stereo[2*i]=value(i*channels);clip.stereo[2*i+1]=value(i*channels+(channels==2?1:0));}
 buildWaveformPeaks(clip);return clip;
}
WavStreamWriter::WavStreamWriter(const std::string& path,uint32_t rate,WavFormat format,
 size_t expectedFrames,DitherMode dither)
 :expectedFrames_(expectedFrames),format_(format),dither_(dither){
 const uint64_t bytes=format==WavFormat::PCM16?2u:(format==WavFormat::PCM24?3u:4u);
 if(uint64_t(expectedFrames)>UINT64_MAX/(2u*bytes))throw std::runtime_error("Invalid WAV export length");
 const uint64_t dataBytes=uint64_t(expectedFrames)*2u*bytes;
 if(rate<8000||rate>384000||!expectedFrames||
    uint64_t(expectedFrames)>UINT64_MAX/(2u*bytes)||dataBytes>UINT64_MAX-80u)
  throw std::runtime_error("Invalid WAV export length or sample rate");
 const bool rf64=dataBytes>UINT32_MAX-36u;
 out_.open(path,std::ios::binary|std::ios::trunc);
 if(!out_)throw std::runtime_error("Cannot write WAV: "+path);
 if(rf64){
  // RF64 with mandatory ds64: standard readers can stream renders beyond 4 GiB.
  out_.write("RF64",4);w32(out_,UINT32_MAX);out_.write("WAVEds64",8);
  w32(out_,28);w64(out_,72u+dataBytes);w64(out_,dataBytes);w64(out_,expectedFrames);w32(out_,0);
 }else{out_.write("RIFF",4);w32(out_,uint32_t(36+dataBytes));out_.write("WAVE",4);}
 out_.write("fmt ",4);w32(out_,16);w16(out_,format==WavFormat::Float32?3:1);
 w16(out_,2);w32(out_,rate);w32(out_,uint32_t(rate*bytes*2));w16(out_,uint16_t(bytes*2));w16(out_,uint16_t(bytes*8));
 out_.write("data",4);w32(out_,rf64?UINT32_MAX:uint32_t(dataBytes));
 if(!out_)throw std::runtime_error("Cannot write WAV header");
}
double WavStreamWriter::random01(){
 rng_^=rng_<<13;rng_^=rng_>>17;rng_^=rng_<<5;
 return double(rng_)*(1.0/4294967296.0);
}
void WavStreamWriter::append(const float* audio,size_t frames){
 if(!audio&&frames)throw std::invalid_argument("Null WAV audio input");
 if(frames>expectedFrames_-writtenFrames_)throw std::length_error("WAV writer exceeded declared length");
 for(size_t i=0;i<frames*2;++i){
  const auto value=std::isfinite(audio[i])?audio[i]:0.f;
  if(format_==WavFormat::Float32){
   uint32_t bits;static_assert(sizeof(float)==4);std::memcpy(&bits,&value,4);w32(out_,bits);
  }else if(format_==WavFormat::PCM16){
   const double tpdf=dither_==DitherMode::TPDF?random01()-random01():0.;
   const auto v=int32_t(std::clamp(std::llround(double(std::clamp(value,-1.f,1.f))*32767.+tpdf),-32768LL,32767LL));
   w16(out_,uint16_t(v));
  }else{
   const double tpdf=dither_==DitherMode::TPDF?random01()-random01():0.;
   const auto v=int32_t(std::clamp(std::llround(double(std::clamp(value,-1.f,1.f))*8388607.+tpdf),-8388608LL,8388607LL));
   const unsigned char packed[3]={uint8_t(v),uint8_t(v>>8),uint8_t(v>>16)};
   out_.write(reinterpret_cast<const char*>(packed),3);
  }
 }
 if(!out_)throw std::runtime_error("WAV export failed while writing samples");
 writtenFrames_+=frames;
}
void WavStreamWriter::finish(){
 if(writtenFrames_!=expectedFrames_)throw std::runtime_error("Incomplete WAV export");
 out_.flush();if(!out_)throw std::runtime_error("WAV flush failed");
 out_.close();if(!out_)throw std::runtime_error("WAV close failed");
}
WavStreamWriter::~WavStreamWriter()=default;
void writeWavAdvanced(const std::string& path,const std::vector<float>& a,uint32_t rate,WavFormat format,DitherMode dither){
 if(a.size()%2||a.empty())throw std::runtime_error("Invalid stereo WAV audio");
 WavStreamWriter writer(path,rate,format,a.size()/2,dither);
 writer.append(a.data(),a.size()/2);
 writer.finish();
}
void writeWav(const std::string& path,const std::vector<float>& a,uint32_t rate){
 writeWavAdvanced(path,a,rate,WavFormat::PCM16);
}

float BiquadState::process(float x,const BiquadCoefficients& c){
 auto y=c.b0*double(x)+z1;
 z1=c.b1*double(x)-c.a1*y+z2;
 z2=c.b2*double(x)-c.a2*y;
 if(std::abs(z1)<1e-23)z1=0;
 if(std::abs(z2)<1e-23)z2=0;
 return float(y);
}
BiquadCoefficients makeEqCoefficients(const EqBand& band,uint32_t sampleRate){
 if(sampleRate<8000 || !band.enabled || std::abs(band.gainDb)<1e-6f)return {};
 const double f=std::clamp(double(band.frequency),15.,std::min(20000.,sampleRate*.47));
 const double q=std::clamp(double(band.q),.2,12.);
 const double g=std::clamp(double(band.gainDb),-24.,24.);
 const double A=std::pow(10.,g/40.),w=2*kPi*f/sampleRate,co=std::cos(w),si=std::sin(w);
 const double alpha=si/(2*q),sq=std::sqrt(A);
 double b0=1,b1=0,b2=0,a0=1,a1=0,a2=0;
 if(band.mode==EqMode::Bell){
  b0=1+alpha*A;b1=-2*co;b2=1-alpha*A;
  a0=1+alpha/A;a1=-2*co;a2=1-alpha/A;
 }else if(band.mode==EqMode::LowShelf){
  const double slope=std::clamp(q,.25,1.5);
  const double a=si*.5*std::sqrt(std::max(1e-6,(A+1./A)*(1./slope-1.)+2.));
  b0=A*((A+1)-(A-1)*co+2*sq*a);
  b1=2*A*((A-1)-(A+1)*co);
  b2=A*((A+1)-(A-1)*co-2*sq*a);
  a0=(A+1)+(A-1)*co+2*sq*a;
  a1=-2*((A-1)+(A+1)*co);
  a2=(A+1)+(A-1)*co-2*sq*a;
 }else{
  const double slope=std::clamp(q,.25,1.5);
  const double a=si*.5*std::sqrt(std::max(1e-6,(A+1./A)*(1./slope-1.)+2.));
  b0=A*((A+1)+(A-1)*co+2*sq*a);
  b1=-2*A*((A-1)+(A+1)*co);
  b2=A*((A+1)+(A-1)*co-2*sq*a);
  a0=(A+1)-(A-1)*co+2*sq*a;
  a1=2*((A-1)-(A+1)*co);
  a2=(A+1)-(A-1)*co-2*sq*a;
 }
 if(!std::isfinite(a0)||std::abs(a0)<1e-12)return {};
 return {b0/a0,b1/a0,b2/a0,a1/a0,a2/a0};
}
double eqMagnitudeDb(const EqSettings& eq,double frequency,uint32_t sr){
 if(!eq.enabled || sr<8000)return 0;
 const double w=2*kPi*std::clamp(frequency,1.,sr*.49)/sr;
 const auto cosW=std::cos(w),cos2W=std::cos(2*w),sinW=std::sin(w),sin2W=std::sin(2*w);
 double power=1;
 for(const auto& band:eq.bands){
  const auto c=makeEqCoefficients(band,sr);
  double nr=c.b0+c.b1*cosW+c.b2*cos2W,ni=-c.b1*sinW-c.b2*sin2W;
  double dr=1+c.a1*cosW+c.a2*cos2W,di=-c.a1*sinW-c.a2*sin2W;
  power*=std::max(1e-20,(nr*nr+ni*ni)/std::max(1e-20,dr*dr+di*di));
 }
 return 10*std::log10(std::max(1e-20,power));
}
void TrackFxState::prepare(size_t samples,uint32_t sr){delayL.assign(std::max(size_t(1),samples),0);delayR.assign(std::max(size_t(1),samples),0);delayPos=0;chorusL.assign(std::max(size_t(1024),samples),0.f);chorusR.assign(std::max(size_t(1024),samples),0.f);chorusPosition=0;heldL=heldR=0;crushCountdown=0;lpL=lpR=0;compressorEnvelope=0;gateEnvelope=0;for(auto& s:eqL)s.reset();for(auto& s:eqR)s.reset();eqCoefficientsReady=false;
 constexpr std::array<float,4> lengths{.0297f,.0371f,.0411f,.0437f};
 for(size_t c=0;c<4;++c){
  const auto n=std::max(size_t(32),size_t(std::round(double(sr)*lengths[c])));
  roomL[c].assign(n,0.f);roomR[c].assign(n+17,0.f);
  roomPositions[c]=0;roomFilterL[c]=roomFilterR[c]=0;
 }
 deEssLowL=deEssLowR=deEssDetector=0;transientFast=transientSlow=0;
 phaserInL.fill(0.f);phaserInR.fill(0.f);phaserOutL.fill(0.f);phaserOutR.fill(0.f);
 phaserFeedbackL=phaserFeedbackR=tiltLowL=tiltLowR=exciterLowL=exciterLowR=0.f;
 resonatorL.assign(std::max(size_t(1024),size_t(sr/25)),0.f);resonatorR.assign(resonatorL.size(),0.f);resonatorPos=0;
 pingL.assign(std::max(size_t(1024),size_t(sr*2)),0.f);pingR.assign(pingL.size(),0.f);pingPos=0;
 resonatorFilterL=resonatorFilterR=wahLowL=wahLowR=wahBandL=wahBandR=0.f;
}
namespace { struct WaveBank; WaveBank& waveBank(); }
void EngineState::prepare(size_t n,uint32_t rate){(void)waveBank();tracks.resize(n);for(auto& t:tracks)t.prepare(std::max(1u,rate/3u),rate);}
void EngineState::reset(){for(auto& s:tracks){std::fill(s.delayL.begin(),s.delayL.end(),0.f);std::fill(s.delayR.begin(),s.delayR.end(),0.f);s.lpL=s.lpR=0;s.delayPos=0;s.compressorEnvelope=0;s.gateEnvelope=0;s.chorusPosition=0;s.crushCountdown=0;s.heldL=s.heldR=0;
  std::fill(s.chorusL.begin(),s.chorusL.end(),0.f);std::fill(s.chorusR.begin(),s.chorusR.end(),0.f);for(auto& z:s.eqL)z.reset();for(auto& z:s.eqR)z.reset();s.eqCoefficientsReady=false;
  for(size_t c=0;c<4;++c){std::fill(s.roomL[c].begin(),s.roomL[c].end(),0.f);std::fill(s.roomR[c].begin(),s.roomR[c].end(),0.f);s.roomPositions[c]=0;s.roomFilterL[c]=s.roomFilterR[c]=0;}
  s.deEssLowL=s.deEssLowR=s.deEssDetector=s.transientFast=s.transientSlow=0;
  s.phaserInL.fill(0.f);s.phaserInR.fill(0.f);s.phaserOutL.fill(0.f);s.phaserOutR.fill(0.f);
  s.phaserFeedbackL=s.phaserFeedbackR=s.tiltLowL=s.tiltLowR=s.exciterLowL=s.exciterLowR=0.f;
  std::fill(s.resonatorL.begin(),s.resonatorL.end(),0.f);std::fill(s.resonatorR.begin(),s.resonatorR.end(),0.f);s.resonatorPos=0;
  std::fill(s.pingL.begin(),s.pingL.end(),0.f);std::fill(s.pingR.begin(),s.pingR.end(),0.f);s.pingPos=0;
  s.resonatorFilterL=s.resonatorFilterR=s.wahLowL=s.wahLowR=s.wahBandL=s.wahBandR=0.f;
 }}
// Band-limited harmonic wavetables. Constructed at engine preparation (never in
// the audio callback in the normal application path), with no dynamic allocations.
namespace {
struct WaveBank {
 static constexpr int points=2048;
 static constexpr int bands=9;
 std::array<std::array<std::array<float,points>,4>,bands> table{};
 WaveBank(){
  for(int b=0;b<bands;++b){
   const int maxHarmonics=std::min(256,1<<b);
   for(int wave=0;wave<4;++wave){
    float peak=0;
    for(int i=0;i<points;++i){
     const double phase=2*kPi*i/points;double value=0;
     for(int k=1;k<=maxHarmonics;++k){
      if(wave==0&&k>1)break;
      if(wave==2&&k%2==0)continue;
      if(wave==3&&k%2==0)continue;
      double amp=wave==0?1.:(wave==1?1./k:(wave==2?1./k:1./(k*k)));
      if(wave==3&&(k/2)%2)amp=-amp;
      value+=amp*std::sin(k*phase);
     }
     table[b][wave][i]=float(value);
     peak=std::max(peak,std::abs(float(value)));
    }
    if(peak>.00001f)for(auto& point:table[b][wave])point/=peak;
   }
  }
 }
 float sample(double phase,double hz,uint32_t rate,float morph) const {
  const auto nyquist=std::max(1.,double(rate)*.46);
  const int allowed=std::clamp(int(nyquist/std::max(1.,hz)),1,256);
  int bank=0;while(bank<bands-1&&(1<<(bank+1))<=allowed)++bank;
  const auto p=phase-std::floor(phase);const double at=p*points;
  const int i=int(at)&(points-1),j=(i+1)&(points-1);const float frac=float(at-std::floor(at));
  const float position=std::clamp(morph,0.f,1.f)*3.f;
  const int wave=std::min(2,int(position));const float blend=position-wave;
  const auto read=[&](int w){return table[bank][w][i]*(1.f-frac)+table[bank][w][j]*frac;};
  return read(wave)*(1.f-blend)+read(wave+1)*blend;
 }
};
WaveBank& waveBank(){static WaveBank instance;return instance;}
}
static inline float osc(Instrument type,double phase){auto p=phase-std::floor(phase);switch(type){case Instrument::Sine:return float(std::sin(2*kPi*p));case Instrument::Square:return p<.5?0.75f:-0.75f;case Instrument::Triangle:return float(1-4*std::abs(p-.5));case Instrument::Noise:{auto x=std::sin(phase*12981.7)*43758.5453;return float(2*(x-std::floor(x))-1)*.7f;}default:return float(2*p-1)*.68f;}}
// Lightweight deterministic GM channel-10 preview. Sampled kits can replace
// these synthesized voices; no random allocations or state on the audio thread.
static float renderGeneralMidiDrum(int pitch,double age,double velocity){
 if(age<0 || age>.72)return 0.f;
 const double decay=(pitch==35||pitch==36)?13.:(pitch>=42&&pitch<=46?37.:(pitch>=49&&pitch<=57?19.:24.));
 const double t=age;
 const double tone=(pitch==35||pitch==36)?(47.+115.*std::exp(-t*30.)):
   ((pitch>=41&&pitch<=47)?(77.+7.*(pitch-41)):(155.+pitch*3.));
 const double phase=2*kPi*(tone*t+((pitch<=36)?115./30.*(1-std::exp(-t*30.)):0.));
 // Irrational offsets yield distinct reproducible noisewaveforms per note.
 const double n=std::sin((t*28931.+pitch*39.71)*1.113)*43758.5453;
 const double noise=2.*(n-std::floor(n)) - 1.;
 const bool tonal=pitch==35||pitch==36||(pitch>=41&&pitch<=47)||(pitch>=60&&pitch<=81);
 const double filtered=tonal?.76*std::sin(phase)+.24*noise:
  (pitch==38||pitch==40?.45*std::sin(phase)+.55*noise:noise);
 return float(std::clamp(filtered*std::exp(-t*decay)*velocity*.42,-1.,1.));
}
static std::array<float,2> synthRegionSample(const Session& session,const Track& track,const MidiRegion& region,double localBeat){
 // Region-local position makes the same instrument renderer usable by arrangement
 // playback and quantised Session/Performance View clip looping.
 double l=0,r=0;
  for(const auto& note:region.notes){
   const double age=(localBeat-note.beat)*60./session.bpm;
   const double duration=std::max(.0001,note.length*60./session.bpm);
   const double release=std::max(.005,double(track.synth.releaseMs)/1000.);
   if(age<0)continue;
   if(track.instrument!=Instrument::DrumRack&&age>=duration+release)continue;
   double env=std::min(1.,age/std::max(.001,double(track.synth.attackMs)/1000.));
   if(age>duration)env*=std::max(0.,1-(age-duration)/release);
   else env*=std::clamp(double(track.synth.sustain),0.,1.);
   const double vel=std::clamp(double(note.velocity),0.,1.);
   if(track.instrument==Instrument::DrumRack){
    bool found=false, anyMapped=false;
    for(const auto& slot:track.drumRack){
     anyMapped=anyMapped||slot.stereo.size()>=2;
     if(slot.midiNote!=note.pitch||slot.stereo.size()<2||slot.sampleRate<8000)continue;
     auto idx=age*double(slot.sampleRate);
     auto frames=slot.stereo.size()/2;
     if(idx>=frames-1)continue;
     auto a=size_t(idx);auto frac=float(idx-a);
     l+=((1-frac)*slot.stereo[a*2]+frac*slot.stereo[(a+1)*2])*vel;
     r+=((1-frac)*slot.stereo[a*2+1]+frac*slot.stereo[(a+1)*2+1])*vel;
     found=true;break;
    }
    // Recorded packs are sample-only; legacy projects retain their GM synth preview.
    if(!found&&!anyMapped&&!track.drumRackRecordedOnly){auto drum=renderGeneralMidiDrum(note.pitch,age,vel);l+=drum;r+=drum;}
   }else if(note.channel==10){
    // GM percussion is always channel 10, even in mixed type-0 MIDI files.
    // Provide a deterministic built-in preview without external soundfonts.
    auto drum=renderGeneralMidiDrum(note.pitch,age,vel);l+=drum;r+=drum;
   }else if(track.instrument==Instrument::Sampler){
    // Prefer the nearest preloaded key-zone. Reading pointers only; never allocate
    // or read files on the realtime audio thread. Legacy 1-sample tracks remain valid.
    const std::vector<float>* stereo=&track.instrumentSample;
    uint32_t sourceRate=track.instrumentSampleRate;
    int root=track.synth.sampleRootNote;
    float bestDistance=1.e9f;
    for(const auto& zone:track.sampleZones){
     if(zone.stereo.size()<4||zone.sampleRate<8000)continue;
     const int pitch=std::clamp(note.pitch,0,127);
     const float pitchGap=float(pitch<zone.lowNote?zone.lowNote-pitch:(pitch>zone.highNote?pitch-zone.highNote:0));
     const float velocityGap=note.velocity<zone.velocityLow?zone.velocityLow-note.velocity:
        (note.velocity>zone.velocityHigh?note.velocity-zone.velocityHigh:0.f);
     const float distance=pitchGap*10.f+velocityGap*4.f;
     if(distance<bestDistance){
      bestDistance=distance;stereo=&zone.stereo;sourceRate=zone.sampleRate;root=zone.rootNote;
      if(distance==0.f)break;
     }
    }
    if(sourceRate<8000||stereo->size()<4)continue;
    const double pitchRatio=std::pow(2.,(std::clamp(note.pitch,0,127)-root)/12.);
    const double index=age*double(sourceRate)*pitchRatio;
    const size_t total=stereo->size()/2;
    if(index>=total-1)continue;
    const size_t a=size_t(index);const float f=float(index-a);
    l+=((1-f)*(*stereo)[a*2]+f*(*stereo)[(a+1)*2])*env*vel;
    r+=((1-f)*(*stereo)[a*2+1]+f*(*stereo)[(a+1)*2+1])*env*vel;
   }else{
    const double hz=440.*std::pow(2.,(std::clamp(note.pitch,0,127)-69)/12.);
    double source=osc(track.instrument,age*hz);
    if(track.instrument==Instrument::Wavetable){
     const auto& bank=waveBank();
     const auto& synth=track.synth;
     const auto otherHz=hz*std::pow(2.,std::clamp(double(synth.wavetableDetuneCents),-100.,100.)/1200.);
     const float blend=std::clamp(synth.wavetableSecondOscMix,0.f,1.f);
     source=(1.-blend)*bank.sample(age*hz,hz,session.sampleRate,synth.wavetableMorph)
       +blend*bank.sample(age*otherHz,otherHz,session.sampleRate,synth.wavetableMorph);
    }
    double sample=source*env*vel*.24;
    l+=sample;r+=sample;
   }
  }
 return {float(std::clamp(l,-3.,3.)),float(std::clamp(r,-3.,3.))};
}
std::array<float,2> renderMidiRegionSample(const Session& session,const Track& track,const MidiRegion& region,double localBeat){
 auto a=synthRegionSample(session,track,region,localBeat);
 // Notes spanning the loop boundary keep their release rather than cutting abruptly.
 if(region.lengthBeats>0 && localBeat>=0){
  auto previous=synthRegionSample(session,track,region,localBeat+region.lengthBeats);
  a[0]+=previous[0];a[1]+=previous[1];
 }
 return a;
}
static std::array<float,2> synthSample(const Session& session,const Track& track,double t){
 const double beat=t*session.bpm/60.;double l=0,r=0;
 for(const auto& region:track.midi){
  if(beat<region.startBeat||beat>region.startBeat+region.lengthBeats+.5)continue;
  auto out=synthRegionSample(session,track,region,beat-region.startBeat);
  l+=out[0];r+=out[1];
 }
 return {float(std::clamp(l,-3.,3.)),float(std::clamp(r,-3.,3.))};
}
void assignSamplerSample(Track& track,const Clip& source,int rootMidiNote){
 if(source.stereo.size()<2||source.stereo.size()%2||source.sampleRate<8000)
  throw std::runtime_error("Sampler requires a nonempty stereo audio file");
 track.instrument=Instrument::Sampler;
 track.instrumentSample=source.stereo;
 track.instrumentSampleRate=source.sampleRate;
 track.synth.sampleRootNote=std::clamp(rootMidiNote,0,127);
 track.instrumentSampleName=source.name;
 track.sampleZones.clear(); // Switching to a single sample should not leave stale zones sounding.
}
void assignSamplerZones(Track& track,std::vector<SamplerZone> zones,const std::string& programName){
 if(zones.empty()||zones.size()>32)throw std::runtime_error("Sampler program must have between 1 and 32 zones");
 for(const auto& z:zones){
  if(z.sampleRate<8000||z.sampleRate>384000||z.stereo.size()<4||z.stereo.size()%2||
     z.rootNote<0||z.rootNote>127||z.lowNote<0||z.highNote>127||z.lowNote>z.highNote||
     !std::isfinite(z.velocityLow)||!std::isfinite(z.velocityHigh)||
     z.velocityLow<0.f||z.velocityHigh>1.f||z.velocityLow>z.velocityHigh)
   throw std::runtime_error("Invalid multisample instrument zone");
 }
 track.sampleZones=std::move(zones);
 track.instrument=Instrument::Sampler;
 track.instrumentSample.clear();
 track.instrumentSampleName=programName;
}
void assignDrumSample(Track& track,size_t slot,int midiNote,const Clip& source){
 if(slot>=track.drumRack.size()||source.stereo.size()<2||source.sampleRate<8000)
  throw std::runtime_error("Invalid drum sample or drum slot");
 auto& d=track.drumRack[slot];d.midiNote=std::clamp(midiNote,0,127);
 d.stereo=source.stereo;d.sampleRate=source.sampleRate;d.name=source.name;
 track.instrument=Instrument::DrumRack;
}
void processMasterBlock(const MasterSettings& settings,int frames,float* left,float* right){
 if(frames<=0||!left||!right)return;
 const float gain=std::clamp(settings.gain,0.f,2.f);
 const float ceiling=std::pow(10.f,std::clamp(settings.ceilingDb,-24.f,0.f)/20.f);
 for(int i=0;i<frames;++i){
  float l=std::isfinite(left[i])?left[i]*gain:0;
  float r=std::isfinite(right[i])?right[i]*gain:0;
  if(settings.limiterEnabled){
   const float maxSample=std::max(std::abs(l),std::abs(r));
   if(maxSample>ceiling){const float reduction=ceiling/maxSample;l*=reduction;r*=reduction;}
  }
  left[i]=l;right[i]=r;
 }
}
void renderTrackBlock(const Session& s,size_t ti,int64_t position,int frames,float* left,float* right,EngineState& engine,bool synth,bool applyEffects,
                     std::optional<bool> anySoloHint){
 if(!left||!right||frames<=0)return;
 std::fill_n(left,frames,0.f);std::fill_n(right,frames,0.f);
 if(ti>=s.tracks.size()||ti>=engine.tracks.size()||s.bpm<=0||s.sampleRate<8000)return;
 const auto& t=s.tracks[ti];
 const bool anySolo=anySoloHint.has_value()?*anySoloHint:std::any_of(s.tracks.begin(),s.tracks.end(),[](const Track& tr){return tr.solo;});
 if(t.mute||(anySolo&&!t.solo))return;
 auto& fx=engine.tracks[ti];double beatToSeconds=60./s.bpm;
 for(const auto& c:t.clips){if(c.mute||c.stereo.empty()||!c.sampleRate)continue;
  auto clipStart=int64_t(std::llround(c.startBeat*beatToSeconds*s.sampleRate));auto total=c.stereo.size()/2;
  // v1.23: skip clips that cannot overlap this block. Old code iterated
  // every output sample of every clip, even when it was hours away from
  // the playhead. This preserves exact mixing for overlapping samples.
  const auto clipFrames=int64_t(std::ceil(double(total)*s.sampleRate/c.sampleRate));
  if(clipStart>=position+frames || clipFrames<=0 || clipStart+clipFrames<=position)continue;
  const int first=int(std::max(int64_t(0),clipStart-position));
  const int last=int(std::min(int64_t(frames),clipStart+clipFrames-position));
  for(int i=first;i<last;++i){auto relative=position+i-clipStart;if(relative<0)continue;
   double source=double(relative)*c.sampleRate/s.sampleRate;auto idx=size_t(source);if(idx>=total)continue;
   auto f=float(source-idx);auto next=std::min(idx+1,total-1);
   float envelope=1;
   const float playedSeconds=float(source/c.sampleRate),durationSeconds=float(total)/c.sampleRate;
   if(c.fadeInSeconds>0){const float u=std::clamp(playedSeconds/c.fadeInSeconds,0.f,1.f);envelope*=c.equalPowerFades?std::sin(u*float(kPi/2)):u;}
   if(c.fadeOutSeconds>0){const float u=std::clamp((durationSeconds-playedSeconds)/c.fadeOutSeconds,0.f,1.f);envelope*=c.equalPowerFades?std::sin(u*float(kPi/2)):u;}
   left[i]+=((1-f)*c.stereo[idx*2]+f*c.stereo[next*2])*c.gain*envelope;
   right[i]+=((1-f)*c.stereo[idx*2+1]+f*c.stereo[next*2+1])*c.gain*envelope;
  }
 }
 if(synth && !t.midi.empty())for(int i=0;i<frames;++i){auto v=synthSample(s,t,double(position+i)/s.sampleRate);left[i]+=v[0];right[i]+=v[1];}
 if(applyEffects)processTrackEffects(s,ti,frames,left,right,engine,position,anySolo);
}
void processTrackEffects(const Session& s,size_t ti,int frames,float* left,float* right,EngineState& engine,int64_t position,
                         std::optional<bool> anySoloHint){
 if(!left||!right||frames<=0||ti>=s.tracks.size()||ti>=engine.tracks.size())return;
 const auto& t=s.tracks[ti];
 const bool solo=anySoloHint.has_value()?*anySoloHint:std::any_of(s.tracks.begin(),s.tracks.end(),[](const Track& tr){return tr.solo;});
 if(t.mute||(solo&&!t.solo)){std::fill_n(left,frames,0.f);std::fill_n(right,frames,0.f);return;}
 auto& fx=engine.tracks[ti];
 // Per-channel four-band EQ and stereo-linked compressor, then vintage filter,
 // soft saturation, constant-power stereo pan and short feedback delay.
 std::array<BiquadCoefficients,8> eqCoeffs;
 for(size_t b=0;b<eqCoeffs.size();++b){
  auto band=t.eq.bands[b];
  const double atBeat=double(position)/s.sampleRate*s.bpm/60.;
  if(b==0)band.gainDb=automate(t.automation,AutomationTarget::EqLowGain,atBeat,band.gainDb);
  if(b==3)band.gainDb=automate(t.automation,AutomationTarget::EqHighGain,atBeat,band.gainDb);
  eqCoeffs[b]=makeEqCoefficients(band,s.sampleRate);
 }
 // Smooth coefficients between GUI edits without allocating on the audio thread.
 // A one-pole, sample-rate-independent ramp avoids clicks when sweeping an EQ node.
 if(!fx.eqCoefficientsReady){fx.eqSmoothed=eqCoeffs;fx.eqCoefficientsReady=true;}
 const double coefficientStep=1.-std::exp(-1./(std::max(8000u,s.sampleRate)*.009));
 const auto& comp=t.compressor;
 const auto attack=std::exp(-1./(s.sampleRate*std::max(.1, double(comp.attackMs))*.001));
 const auto release=std::exp(-1./(s.sampleRate*std::max(1., double(comp.releaseMs))*.001));
 const double ratio=std::clamp(double(comp.ratio),1.,20.);
 const double makeup=std::pow(10.,std::clamp(double(comp.makeupDb),-24.,24.)/20.);
 const auto gateThreshold=std::pow(10.,std::clamp(double(t.gate.thresholdDb),-90.,0.)/20.);
 const auto gateAttack=std::exp(-1./(s.sampleRate*std::max(.1,double(t.gate.attackMs))*.001));
 const auto gateRelease=std::exp(-1./(s.sampleRate*std::max(1.,double(t.gate.releaseMs))*.001));
 for(int i=0;i<frames;++i){
  auto l=left[i],r=right[i];
  if(t.eq.enabled){for(size_t b=0;b<eqCoeffs.size();++b){
   auto& c=fx.eqSmoothed[b];const auto& dest=eqCoeffs[b];
   c.b0+=(dest.b0-c.b0)*coefficientStep;c.b1+=(dest.b1-c.b1)*coefficientStep;
   c.b2+=(dest.b2-c.b2)*coefficientStep;c.a1+=(dest.a1-c.a1)*coefficientStep;
   c.a2+=(dest.a2-c.a2)*coefficientStep;
   l=fx.eqL[b].process(l,c);r=fx.eqR[b].process(r,c);
  }}
  if(comp.enabled){
   double detector=std::max(std::abs(double(l)),std::abs(double(r)));
   auto smoothing=detector>fx.compressorEnvelope?attack:release;
   fx.compressorEnvelope=smoothing*fx.compressorEnvelope+(1-smoothing)*detector;
   double levelDb=20*std::log10(std::max(1e-12,fx.compressorEnvelope));
   double over=std::max(0.,levelDb-std::clamp(double(comp.thresholdDb),-60.,0.));
   double reduction=over*(1.-1./ratio);
   float gain=float(std::pow(10.,-reduction/20.)*makeup);
   l*=gain;r*=gain;
  }
  if(t.gate.enabled){
    const double detector=std::max(std::abs(double(l)),std::abs(double(r)));
    const auto desired=detector>=gateThreshold?1.0:0.0;
    const auto smoothing=desired>fx.gateEnvelope?gateAttack:gateRelease;
    fx.gateEnvelope=smoothing*fx.gateEnvelope+(1-smoothing)*desired;
    l*=float(fx.gateEnvelope);r*=float(fx.gateEnvelope);
  }
  // Dynamic split-band de-esser. A linked high-frequency detector reduces
  // sibilance while retaining low frequencies and stereo image consistency.
  if(t.deEsser.enabled){
   const double f=std::clamp(double(t.deEsser.frequencyHz),1000.,s.sampleRate*.44);
   const float a=float(1.-std::exp(-2.*kPi*f/s.sampleRate));
   fx.deEssLowL+=a*(l-fx.deEssLowL);
   fx.deEssLowR+=a*(r-fx.deEssLowR);
   const float hiL=l-fx.deEssLowL,hiR=r-fx.deEssLowR;
   const float detector=std::max(std::abs(hiL),std::abs(hiR));
   const float smooth=detector>fx.deEssDetector?.035f:.0015f;
   fx.deEssDetector+=smooth*(detector-fx.deEssDetector);
   const float threshold=std::pow(10.f,std::clamp(t.deEsser.thresholdDb,-60.f,0.f)/20.f);
   const float excess=std::max(0.f,fx.deEssDetector/threshold-1.f);
   const float attenuation=1.f-std::clamp(t.deEsser.reduction,0.f,1.f)*excess/(1.f+excess);
   l=fx.deEssLowL+hiL*attenuation;
   r=fx.deEssLowR+hiR*attenuation;
  }
  // Linked envelope-following transient designer; positive attack enhances
  // fast transients, sustain controls the tail. Both channels share gain.
  if(t.transient.enabled){
   const float detector=std::max(std::abs(l),std::abs(r));
   const float alphaFast=1.f-std::exp(-1.f/(.005f*s.sampleRate));
   const float alphaSlow=1.f-std::exp(-1.f/(.075f*s.sampleRate));
   fx.transientFast+=alphaFast*(detector-fx.transientFast);
   fx.transientSlow+=alphaSlow*(detector-fx.transientSlow);
   const float difference=(fx.transientFast-fx.transientSlow)/(fx.transientFast+fx.transientSlow+.025f);
   const float strength=std::clamp(t.transient.attack,-1.f,1.f)*std::max(0.f,difference)
      +std::clamp(t.transient.sustain,-1.f,1.f)*std::max(0.f,-difference);
   const float amp=std::clamp(std::exp(strength*1.45f),.25f,3.f);
   l*=amp;r*=amp;
  }
  // Four diffusion combs: a lightweight, original stereo room algorithm.
  // The buffers are allocated by EngineState::prepare, never in this callback.
  if(t.room.enabled){
   const float wet=std::clamp(t.room.mix,0.f,1.f);
   const float feedback=.25f+.62f*std::clamp(t.room.size,0.f,1.f);
   const float damp=std::clamp(t.room.damping,0.f,.97f);
   float roomOutL=0,roomOutR=0;
   for(size_t c=0;c<4;++c){
    auto& ringL=fx.roomL[c];auto& ringR=fx.roomR[c];
    const size_t p=fx.roomPositions[c];
    const size_t pr=p%ringR.size();
    const float dl=ringL[p],dr=ringR[pr];
    fx.roomFilterL[c]=(1.f-damp)*dl+damp*fx.roomFilterL[c];
    fx.roomFilterR[c]=(1.f-damp)*dr+damp*fx.roomFilterR[c];
    ringL[p]=l+feedback*fx.roomFilterL[c];
    ringR[pr]=r+feedback*fx.roomFilterR[c];
    fx.roomPositions[c]=(p+1)%ringL.size();
    roomOutL+=dl;roomOutR+=dr;
   }
   l=l*(1.f-wet)+roomOutL*(wet*.25f);
   r=r*(1.f-wet)+roomOutR*(wet*.25f);
  }
  const double beat=double(position+i)/s.sampleRate*s.bpm/60.;
  const float cutoff=std::clamp(automate(t.automation,AutomationTarget::FilterCutoff,beat,t.cutoff),.01f,1.f);
  const float alpha=std::min(1.f,cutoff*cutoff*.78f+.008f);
  const float drive=std::clamp(automate(t.automation,AutomationTarget::Drive,beat,t.drive),0.f,1.f);
  const float wet=std::clamp(automate(t.automation,AutomationTarget::DelayMix,beat,t.delayMix),0.f,.8f);
  const float gain=std::clamp(automate(t.automation,AutomationTarget::TrackGain,beat,t.gain),0.f,2.f);
  const float pan=std::clamp(automate(t.automation,AutomationTarget::TrackPan,beat,t.pan),-1.f,1.f);
  const float lpan=std::sqrt(std::clamp((1-pan)*.5f,0.f,1.f))*1.41421356f;
  const float rpan=std::sqrt(std::clamp((1+pan)*.5f,0.f,1.f))*1.41421356f;
  if(cutoff>=.999f){fx.lpL=l;fx.lpR=r;}
  else{fx.lpL+=alpha*(l-fx.lpL);fx.lpR+=alpha*(r-fx.lpR);l=fx.lpL;r=fx.lpR;}
  if(drive>0){auto k=1.f+drive*9.f;l=std::tanh(l*k)/k*(1+drive*2);r=std::tanh(r*k)/k*(1+drive*2);}
  if(!fx.delayL.empty()){auto p=fx.delayPos;auto oldL=fx.delayL[p],oldR=fx.delayR[p];fx.delayL[p]=l+oldR*.35f;fx.delayR[p]=r+oldL*.35f;fx.delayPos=(p+1)%fx.delayL.size();l+=oldL*wet;r+=oldR*wet;}
  const auto& creative=t.creativeFx;
  if(creative.chorusEnabled&&!fx.chorusL.empty()){
   const float dryL=l,dryR=r;
   const double now=double(position+i)/s.sampleRate;
   const double phase=2*kPi*std::clamp(double(creative.chorusRateHz),.01,10.)*now;
   const double base=.009*s.sampleRate;
   const double depth=std::clamp(double(creative.chorusDepthMs),.1,12.)*.001*s.sampleRate;
   const double delaySamplesL=std::clamp(base+depth*std::sin(phase),2.,double(fx.chorusL.size()-2));
   const double delaySamplesR=std::clamp(base+depth*std::sin(phase+kPi*.5),2.,double(fx.chorusR.size()-2));
   auto read=[&](const std::vector<float>& ring,double delay){
    const double readPos=std::fmod(double(fx.chorusPosition)+ring.size()-delay,double(ring.size()));
    const auto a=size_t(readPos);const auto b=(a+1)%ring.size();
    return float(ring[a]+(ring[b]-ring[a])*float(readPos-a));
   };
   const float chorusL=read(fx.chorusL,delaySamplesL),chorusR=read(fx.chorusR,delaySamplesR);
   fx.chorusL[fx.chorusPosition]=dryL;fx.chorusR[fx.chorusPosition]=dryR;
   fx.chorusPosition=(fx.chorusPosition+1)%fx.chorusL.size();
   const float mix=std::clamp(creative.chorusMix,0.f,1.f);
   l=(1-mix)*dryL+mix*chorusL;r=(1-mix)*dryR+mix*chorusR;
  }
  if(creative.crusherEnabled){
   const auto bits=std::clamp(creative.crusherBits,4,16);
   const auto hold=std::clamp(creative.crusherHoldFrames,1,64);
   const float step=float(1<<(bits-1));
   if(fx.crushCountdown<=0){
    fx.heldL=std::round(std::clamp(l,-1.f,1.f)*step)/step;
    fx.heldR=std::round(std::clamp(r,-1.f,1.f)*step)/step;
    fx.crushCountdown=hold;
   }
   --fx.crushCountdown;
   const float mix=std::clamp(creative.crusherMix,0.f,1.f);
   l=l*(1-mix)+fx.heldL*mix;r=r*(1-mix)+fx.heldR*mix;
  }
  if(creative.autopanEnabled){
   const double period=std::clamp(double(creative.autopanPeriodBeats),.125,64.);
   const float x=float(std::sin(2*kPi*(beat/period)));
   const float depth=std::clamp(creative.autopanDepth,0.f,1.f);
   l*=1.f-depth*(.5f+.5f*x);r*=1.f-depth*(.5f-.5f*x);
  }
  // Beat-synchronised tremolo: continuity comes from absolute transport position.
  if(creative.tremoloEnabled){
   const double cycle=std::clamp(double(creative.tremoloPeriodBeats),.125,64.);
   const float depth=std::clamp(creative.tremoloDepth,0.f,1.f);
   const float modulation=1.f-depth*(.5f-.5f*float(std::cos(2*kPi*beat/cycle)));
   l*=modulation;r*=modulation;
  }
  // Stereo ring-modulation. Multiplying the carrier creates sidebands.
  if(creative.ringModEnabled){
   const double freq=std::clamp(double(creative.ringModFrequencyHz),.5,5000.);
   const float carrier=float(std::sin(2*kPi*freq*double(position+i)/s.sampleRate));
   const float mix=std::clamp(creative.ringModMix,0.f,1.f);
   l=l*(1-mix)+l*carrier*mix;
   r=r*(1-mix)+r*carrier*mix;
  }
  // Oversampling is not implemented; a compensated soft saturation stage
  // is provided for gentle colouring, with per-channel dry/wet mixing.
  if(creative.tapeEnabled){
   const float drive=std::clamp(creative.tapeDrive,1.f,12.f);
   const float mix=std::clamp(creative.tapeMix,0.f,1.f);
   const float normaliser=1.f/std::tanh(drive);
   l=l*(1-mix)+std::tanh(l*drive)*normaliser*mix;
   r=r*(1-mix)+std::tanh(r*drive)*normaliser*mix;
  }
  // 4-stage stereo phaser, with bounded feedback and smoothly varying allpass
  // coefficients. Each channel has independent state; no callback allocations.
  if(creative.phaserEnabled){
   const double now=double(position+i)/std::max(8000u,s.sampleRate);
   const double rate=std::clamp(double(creative.phaserRateHz),.02,8.);
   const double depth=std::clamp(double(creative.phaserDepth),0.,1.);
   const float feedback=std::clamp(creative.phaserFeedback,-.85f,.85f);
   const float mix=std::clamp(creative.phaserMix,0.f,1.f);
   auto allpasses=[&](float input, double offset, std::array<float,4>& prevIn,
                      std::array<float,4>& prevOut, float& previous){
    const float dry=input;
    float x=input+previous*feedback;
    for(size_t stage=0;stage<4;++stage){
     const double lfo=.5+.5*std::sin(2*kPi*rate*now+offset+stage*.18);
     const double freq=std::clamp(290.+depth*(240.+lfo*2150.),80.,s.sampleRate*.40);
     const double q=std::tan(kPi*freq/s.sampleRate);
     const float a=float((1.-q)/(1.+q));
     const float y=-a*x+prevIn[stage]+a*prevOut[stage];
     prevIn[stage]=x;prevOut[stage]=y;x=y;
    }
    previous=x;
    return dry*(1.f-mix)+x*mix;
   };
   l=allpasses(l,0.,fx.phaserInL,fx.phaserOutL,fx.phaserFeedbackL);
   r=allpasses(r,kPi*.5,fx.phaserInR,fx.phaserOutR,fx.phaserFeedbackR);
  }
  // Tilt EQ: linked low/high spectral balance with unity gain at zero tilt.
  if(creative.tiltEnabled){
   const float a=float(1.-std::exp(-2.*kPi*750./s.sampleRate));
   fx.tiltLowL+=a*(l-fx.tiltLowL);
   fx.tiltLowR+=a*(r-fx.tiltLowR);
   const float db=std::clamp(creative.tiltDb,-12.f,12.f);
   const float lo=std::pow(10.f,-db/40.f),hi=std::pow(10.f,db/40.f);
   l=fx.tiltLowL*lo+(l-fx.tiltLowL)*hi;
   r=fx.tiltLowR*lo+(r-fx.tiltLowR)*hi;
  }
  // Harmonic exciter: highpass extraction, bounded nonlinear processing,
  // and explicit dry/wet gain. This is not an oversampled mastering exciter.
  if(creative.exciterEnabled){
   const float a=float(1.-std::exp(-2.*kPi*3400./s.sampleRate));
   fx.exciterLowL+=a*(l-fx.exciterLowL);
   fx.exciterLowR+=a*(r-fx.exciterLowR);
   const float drive=std::clamp(creative.exciterDrive,1.f,10.f);
   const float mix=std::clamp(creative.exciterMix,0.f,.85f);
   const float hl=l-fx.exciterLowL,hr=r-fx.exciterLowR;
   l+=mix*(std::tanh(hl*drive)/drive-hl)*2.f;
   r+=mix*(std::tanh(hr*drive)/drive-hr)*2.f;
  }
  // Tuned comb resonator, with damping and feedback hard-limited for stability.
  if(creative.resonatorEnabled&&!fx.resonatorL.empty()){
   const auto hz=std::clamp(creative.resonatorHz,45.f,2500.f);
   const size_t delay=std::clamp(size_t(s.sampleRate/hz),size_t(2),fx.resonatorL.size()-1);
   const size_t read=(fx.resonatorPos+fx.resonatorL.size()-delay)%fx.resonatorL.size();
   const float feedback=std::clamp(creative.resonatorFeedback,0.f,.92f),mix=std::clamp(creative.resonatorMix,0.f,.8f);
   fx.resonatorFilterL+=.34f*(fx.resonatorL[read]-fx.resonatorFilterL);
   fx.resonatorFilterR+=.34f*(fx.resonatorR[read]-fx.resonatorFilterR);
   const float wl=fx.resonatorFilterL,wr=fx.resonatorFilterR;
   fx.resonatorL[fx.resonatorPos]=std::tanh(l+wl*feedback);
   fx.resonatorR[fx.resonatorPos]=std::tanh(r+wr*feedback);
   fx.resonatorPos=(fx.resonatorPos+1)%fx.resonatorL.size();
   l=l*(1-mix)+wl*mix;r=r*(1-mix)+wr*mix;
  }
  // Auto-wah: stable state-variable bandpass, animated by an independent LFO.
  if(creative.wahEnabled){
   const double time=double(position+i)/s.sampleRate;
   const float mod=float(.5+.5*std::sin(2*kPi*std::clamp(double(creative.wahRateHz),.03,9.)*time));
   const float depth=std::clamp(creative.wahDepth,0.f,1.f);
   const double freq=std::clamp(180.+(250.+mod*2200.)*depth,60.,s.sampleRate*.18);
   const float f=float(2.*std::sin(kPi*freq/s.sampleRate));
   auto process=[&](float in,float& low,float& band){
    low+=f*band;const float high=in-low-.7f*band;band+=f*high;
    low=std::clamp(low,-10.f,10.f);band=std::clamp(band,-10.f,10.f);
    return band;
   };
   const float wl=process(l,fx.wahLowL,fx.wahBandL),wr=process(r,fx.wahLowR,fx.wahBandR);
   const float mix=std::clamp(creative.wahMix,0.f,.95f);
   l=l*(1-mix)+wl*mix;r=r*(1-mix)+wr*mix;
  }
  // Wavefolding creates upper harmonics distinct from tape soft clipping.
  if(creative.foldEnabled){
   const float drive=std::clamp(creative.foldDrive,1.f,8.f),mix=std::clamp(creative.foldMix,0.f,1.f);
   auto fold=[&](float v){const float folded=std::asin(std::sin(v*drive))*float(2./kPi);return folded;};
   l=l*(1-mix)+fold(l)*mix;r=r*(1-mix)+fold(r)*mix;
  }
  // Cross-feedback ping-pong echo, separate from the original channel delay.
  if(creative.pingPongEnabled&&!fx.pingL.empty()){
   const auto ms=std::clamp(creative.pingPongDelayMs,35.f,1800.f);
   const size_t delay=std::clamp(size_t(s.sampleRate*ms*.001f),size_t(1),fx.pingL.size()-1);
   const size_t read=(fx.pingPos+fx.pingL.size()-delay)%fx.pingL.size();
   const float wl=fx.pingL[read],wr=fx.pingR[read];
   const float feedback=std::clamp(creative.pingPongFeedback,0.f,.85f),mix=std::clamp(creative.pingPongMix,0.f,.85f);
   fx.pingL[fx.pingPos]=std::tanh(l+wr*feedback);
   fx.pingR[fx.pingPos]=std::tanh(r+wl*feedback);
   fx.pingPos=(fx.pingPos+1)%fx.pingL.size();
   l+=wl*mix;r+=wr*mix;
  }
  // Asymmetric diode-style waveshaping (distinct from symmetric tape and wavefolding).
  if(creative.diodeEnabled){
   const float drive=std::clamp(creative.diodeDrive,1.f,12.f);
   const float mix=std::clamp(creative.diodeMix,0.f,1.f);
   auto shape=[&](float x){const float v=x*drive;return v>=0.f ? std::tanh(v)*.7f : std::tanh(v*.45f)*1.1f;};
   l=l*(1.f-mix)+shape(l)*mix;
   r=r*(1.f-mix)+shape(r)*mix;
  }
  // Full-wave octave fuzz: creates a strong second harmonic; DC reduced by input sign.
  if(creative.octaveFuzzEnabled){
   const float drive=std::clamp(creative.octaveFuzzDrive,1.f,10.f);
   const float mix=std::clamp(creative.octaveFuzzMix,0.f,1.f);
   auto fuzz=[&](float x){const float v=std::tanh(x*drive);return std::copysign(v*v,x)*.35f + (v*v-.22f)*.65f;};
   l=l*(1.f-mix)+fuzz(l)*mix;
   r=r*(1.f-mix)+fuzz(r)*mix;
  }
  // Stereo crossfeed for headphone compatibility; no additional latency.
  if(creative.crossfeedEnabled){
   const float a=std::clamp(creative.crossfeedAmount,0.f,.45f);
   const float oldL=l,oldR=r;
   l=oldL*(1.f-a)+oldR*a;
   r=oldR*(1.f-a)+oldL*a;
  }
  if(t.stereoWidth!=1.f){
    const float mid=.5f*(l+r), side=.5f*(l-r)*std::clamp(t.stereoWidth,0.f,2.f);
    l=mid+side;r=mid-side;
  }
  left[i]=l*gain*lpan;right[i]=r*gain*rpan;
 }
}
float automate(const std::vector<AutomationLane>& lanes,AutomationTarget target,double beat,float fallback){
 for(const auto& lane:lanes){if(!lane.enabled||lane.target!=target||lane.points.empty())continue;
  const auto& pts=lane.points;if(beat<=pts.front().beat)return pts.front().value;
  auto pos=std::upper_bound(pts.begin(),pts.end(),beat,[](double x,const AutomationPoint& p){return x<p.beat;});
  if(pos==pts.end())return pts.back().value;
  const auto& next=*pos;const auto& prev=*(pos-1);
  const double factor=lane.boolStep?0.:std::clamp((beat-prev.beat)/std::max(1.e-9,next.beat-prev.beat),0.,1.);
  const double curved=lane.smooth?factor*factor*(3.-2.*factor):factor;
  return float(prev.value+(next.value-prev.value)*curved);
 }
 return fallback;
}
void setAutomationPoint(std::vector<AutomationLane>& lanes,AutomationTarget target,double beat,float value){
 if(!std::isfinite(beat)||!std::isfinite(value))throw std::invalid_argument("Invalid automation");
 beat=std::clamp(beat,0.,1.e6);value=std::clamp(value,-24.f,24.f);
 auto it=std::find_if(lanes.begin(),lanes.end(),[&](const auto& l){return l.target==target;});
 if(it==lanes.end()){lanes.push_back({target,true,false,{}});it=lanes.end()-1;}
 auto& points=it->points;auto pos=std::lower_bound(points.begin(),points.end(),beat,[](const auto& p,double b){return p.beat<b;});
 if(pos!=points.end()&&std::abs(pos->beat-beat)<.0001)pos->value=value;
 else if(points.size()<10000)points.insert(pos,{beat,value});
}
void routeTrack(const Track& track,int frames,const float* l,const float* r,
                float* masterL,float* masterR,float* const* busL,float* const* busR,size_t numBuses){
 if(frames<=0||!l||!r||!masterL||!masterR)return;
 const int output=track.outputBus;
 // A host embedding the portable engine may supply fewer or no bus buffers;
 // never dereference null buses. Invalid output routing falls back to master.
 const bool hasBusArrays=busL!=nullptr&&busR!=nullptr;
 const bool sendMain=output<0||size_t(output)>=numBuses||!hasBusArrays||
                     !busL[output]||!busR[output];
 for(int i=0;i<frames;++i){
  if(sendMain){masterL[i]+=l[i];masterR[i]+=r[i];}
  else{busL[output][i]+=l[i];busR[output][i]+=r[i];}
  if(!hasBusArrays)continue;
  for(size_t send=0;send<std::min(numBuses,kMaxBuses);++send){
   // A route's destination already receives the full dry signal. Do not
   // silently amplify that signal by also applying a send to itself.
   if(int(send)==output || !busL[send]||!busR[send])continue;
   const auto level=std::isfinite(track.sends[send])?
    std::clamp(track.sends[send],0.f,1.f):0.f;
   if(level>0){busL[send][i]+=l[i]*level;busR[send][i]+=r[i]*level;}
  }
 }
}
void applyBus(const Bus& bus,const Session& session,int64_t position,int frames,float* l,float* r){
 if(!l||!r||frames<=0)return;
 if(bus.muted){std::fill_n(l,frames,0.f);std::fill_n(r,frames,0.f);return;}
 for(int i=0;i<frames;++i){
  const double beat=double(position+i)/session.sampleRate*session.bpm/60.;
  const float gain=std::clamp(automate(bus.automation,AutomationTarget::BusGain,beat,bus.gain),0.f,2.f);
  const float pan=std::clamp(bus.pan,-1.f,1.f);
  const float lg=std::sqrt(std::clamp((1-pan)*.5f,0.f,1.f))*1.41421356f;
  const float rg=std::sqrt(std::clamp((1+pan)*.5f,0.f,1.f))*1.41421356f;
  const float mid=(l[i]+r[i])*.5f,side=(l[i]-r[i])*.5f*std::clamp(bus.width,0.f,2.f);
  l[i]=(mid+side)*gain*lg;r[i]=(mid-side)*gain*rg;
 }
}
AudioStats measureAudio(const float* l,const float* r,int frames){
 AudioStats result{};if(frames<=0||!l||!r)return result;
 double sum=0,xy=0,ll=0,rr=0;
 for(int i=0;i<frames;++i){
  const double a=std::isfinite(l[i])?l[i]:0.,b=std::isfinite(r[i])?r[i]:0.;
  sum+=a*a+b*b;ll+=a*a;rr+=b*b;xy+=a*b;
  result.peak=std::max(result.peak,float(std::max(std::abs(a),std::abs(b))));
 }
 result.rms=float(std::sqrt(sum/double(frames*2)));
 result.crestDb=20.f*std::log10(std::max(1e-12f,result.peak)/std::max(1e-12f,result.rms));
 result.correlation=float(xy/std::sqrt(std::max(1e-20,ll*rr)));
 return result;
}
void stretchClipGranular(Clip& clip,double ratio){
 if(!std::isfinite(ratio)||ratio<.5||ratio>2.0)throw std::invalid_argument("Granular warp supports 0.5x–2x");
 if(clip.stereo.size()<512||clip.sampleRate<8000)return;
 const size_t total=clip.stereo.size()/2;
 if(total>10'000'000)throw std::runtime_error("Granular warp: clip too long");
 const size_t target=std::max(size_t(1),size_t(std::llround(total/ratio)));
 // 40ms grains with two overlapping windows, preserving source playback rate.
 const int grain=std::clamp(int(clip.sampleRate*.04),256,4096), hop=grain/4;
 std::vector<float> out(target*2,0.f),norm(target,0.f);
 for(size_t outStart=0;outStart<target+size_t(grain);outStart+=hop){
  const int64_t inStart=int64_t(std::llround(double(outStart)*ratio));
  for(int k=0;k<grain;++k){
   const auto dest=outStart+size_t(k);if(dest>=target)break;
   const auto src=inStart+k;if(src<0||size_t(src)>=total)continue;
   const float win=.5f-.5f*std::cos(float(2*kPi*k/grain));
   norm[dest]+=win;
   out[2*dest]+=clip.stereo[2*size_t(src)]*win;
   out[2*dest+1]+=clip.stereo[2*size_t(src)+1]*win;
  }
 }
 for(size_t k=0;k<target;++k)if(norm[k]>.00001f){out[2*k]/=norm[k];out[2*k+1]/=norm[k];}
 clip.stereo.swap(out);buildWaveformPeaks(clip);
}
Clip compAudioTakes(const std::vector<Clip>& takes,const std::vector<TakeSlice>& slices,double overlapSeconds){
 if(takes.empty()||slices.empty()||slices.size()>256||!std::isfinite(overlapSeconds)||overlapSeconds<0||overlapSeconds>.25)
  throw std::invalid_argument("Invalid take comp");
 const auto sr=takes.front().sampleRate;
 if(sr<8000||sr>384000)throw std::invalid_argument("Invalid take sample rate");
 struct SliceFrames {const Clip* clip;size_t begin,end;};
 std::vector<SliceFrames> spans;
 size_t outputFrames=0;
 for(const auto& edit:slices){
  if(edit.takeIndex>=takes.size()||!std::isfinite(edit.beginSeconds)||!std::isfinite(edit.endSeconds)||edit.beginSeconds<0||edit.endSeconds<=edit.beginSeconds)
   throw std::invalid_argument("Invalid take range");
  const Clip& take=takes[edit.takeIndex];
  if(take.sampleRate!=sr||take.stereo.size()%2)throw std::invalid_argument("Take sample rates/channels differ");
  const auto begin=size_t(std::llround(edit.beginSeconds*sr)),end=size_t(std::llround(edit.endSeconds*sr));
  if(begin>=end||end>take.stereo.size()/2)throw std::out_of_range("Take range outside source");
  if(end-begin>size_t(sr)*600||outputFrames>size_t(sr)*600-(end-begin))throw std::length_error("Comp exceeds ten minutes");
  spans.push_back({&take,begin,end});outputFrames+=end-begin;
 }
 // Crossfades overlap adjacent slices, retaining the requested total length
 // minus each overlap. Equal-power curves prevent artificial level dips.
 const size_t requestedOverlap=size_t(std::llround(overlapSeconds*sr));
 std::vector<size_t> overlaps(spans.size(),0);
 for(size_t j=1;j<spans.size();++j){
  const auto previous=spans[j-1].end-spans[j-1].begin;
  const auto current=spans[j].end-spans[j].begin;
  overlaps[j]=std::min({requestedOverlap,previous/2,current/2});
  outputFrames-=overlaps[j];
 }
 Clip result;result.name="Comped Take";result.sampleRate=sr;result.startBeat=takes.front().startBeat;
 result.stereo.reserve(outputFrames*2);
 for(size_t j=0;j<spans.size();++j){
  const auto& piece=spans[j];
  const auto begin=piece.clip->stereo.begin()+std::ptrdiff_t(piece.begin*2);
  const auto end=piece.clip->stereo.begin()+std::ptrdiff_t(piece.end*2);
  const auto cross=overlaps[j];
  if(cross){
   for(size_t k=0;k<cross;++k){
    // First/last sample of the overlap endpoints are near-exclusive.
    const float angle=float((double(k)+.5)/cross*kPi*.5);
    const float fadeOut=std::cos(angle),fadeIn=std::sin(angle);
    const size_t dst=result.stereo.size()-cross*2+k*2;
    const size_t source=(piece.begin+k)*2;
    for(size_t c=0;c<2;++c)
     result.stereo[dst+c]=result.stereo[dst+c]*fadeOut+piece.clip->stereo[source+c]*fadeIn;
   }
  }
  result.stereo.insert(result.stereo.end(),begin+std::ptrdiff_t(cross*2),end);
 }
 buildWaveformPeaks(result);return result;
}
std::vector<float> analyseSpectrum(const std::vector<float>& stereo,size_t startFrame,size_t n){
 if(n<64||n>8192||(n&(n-1))!=0||stereo.size()%2)throw std::invalid_argument("FFT size must be power of two [64,8192]");
 std::vector<std::complex<double>> a(n);
 for(size_t i=0;i<n;++i){
  const size_t src=startFrame+i;
  const double v=src<stereo.size()/2 ? .5*(double(stereo[2*src])+double(stereo[2*src+1])):0.;
  const double window=.5-.5*std::cos(2*kPi*i/(n-1));
  a[i]=std::complex<double>(std::isfinite(v)?v*window:0.,0.);
 }
 for(size_t i=1,j=0;i<n;++i){
  size_t bit=n>>1;
  for(;j&bit;bit>>=1)j^=bit;
  j^=bit;if(i<j)std::swap(a[i],a[j]);
 }
 for(size_t len=2;len<=n;len<<=1){
  const auto angle=-2*kPi/len;
  const std::complex<double> wlen(std::cos(angle),std::sin(angle));
  for(size_t i=0;i<n;i+=len){
   std::complex<double> w(1,0);
   for(size_t j=0;j<len/2;++j){
    const auto u=a[i+j],v=a[i+j+len/2]*w;
    a[i+j]=u+v;a[i+j+len/2]=u-v;w*=wlen;
   }
  }
 }
 std::vector<float> spectrum(n/2+1);
 for(size_t i=0;i<spectrum.size();++i){
  const double mag=(i==0||i==n/2?1.:2.)*std::abs(a[i])/(.5*n);
  spectrum[i]=float(std::max(-140.,20.*std::log10(std::max(1.e-12,mag))));
 }
 return spectrum;
}
double Session::duration() const {double end=0;if(bpm<=0)return 0;for(const auto& t:tracks){for(const auto& c:t.clips)if(c.sampleRate)end=std::max(end,c.startBeat*secondsPerBeat()+double(c.stereo.size()/2)/c.sampleRate);for(const auto& m:t.midi)end=std::max(end,(m.startBeat+m.lengthBeats)*secondsPerBeat()+.2);}return end;}
std::vector<float> Session::render(double seconds) const {
 if(!std::isfinite(seconds)||seconds<0||seconds>600)throw std::runtime_error("Export length must be between 0 and 600 seconds");
 if(buses.size()>kMaxBuses)throw std::runtime_error("Too many buses (max eight)");
 auto frames=size_t(std::ceil(seconds*sampleRate));std::vector<float> out(frames*2,0.f);
 EngineState state;state.prepare(tracks.size(),sampleRate);
 constexpr int block=512;std::array<float,block> l{},r{},masterL{},masterR{};
 std::array<std::array<float,block>,kMaxBuses> bL{},bR{};
 std::array<float*,kMaxBuses> ptrL{},ptrR{};
 for(size_t b=0;b<kMaxBuses;++b){ptrL[b]=bL[b].data();ptrR[b]=bR[b].data();}
 for(size_t offset=0;offset<frames;offset+=block){
  const auto n=int(std::min(size_t(block),frames-offset));
  std::fill_n(masterL.data(),n,0.f);std::fill_n(masterR.data(),n,0.f);
  for(size_t b=0;b<buses.size();++b){std::fill_n(bL[b].data(),n,0.f);std::fill_n(bR[b].data(),n,0.f);}
  const bool anySolo=std::any_of(tracks.begin(),tracks.end(),[](const Track& track){return track.solo;});
  for(size_t t=0;t<tracks.size();++t){
   renderTrackBlock(*this,t,int64_t(offset),n,l.data(),r.data(),state,true,true,anySolo);
   routeTrack(tracks[t],n,l.data(),r.data(),masterL.data(),masterR.data(),ptrL.data(),ptrR.data(),buses.size());
  }
  for(size_t b=0;b<buses.size();++b){
   applyBus(buses[b],*this,int64_t(offset),n,bL[b].data(),bR[b].data());
   for(int i=0;i<n;++i){masterL[i]+=bL[b][i];masterR[i]+=bR[b][i];}
  }
  processMasterBlock(master,n,masterL.data(),masterR.data());
  for(int i=0;i<n;++i){out[2*(offset+i)]=std::clamp(masterL[i],-1.f,1.f);out[2*(offset+i)+1]=std::clamp(masterR[i],-1.f,1.f);}
 }
 return out;
}
std::vector<float> makeDemoSound(int id,uint32_t sr){const double seconds= (id==0||id==1||id==2)?1.:2.;std::vector<float> samples(size_t(sr*seconds)*2);uint32_t rng=1234u+uint32_t(id)*815u;auto noise=[&](){rng=rng*1664525u+1013904223u;return float(double(rng)/UINT32_MAX*2-1);};
 for(size_t i=0;i<samples.size()/2;++i){double t=double(i)/sr;float v=0;
 switch(id){case 0:{double f=150.*std::exp(-t*28.)+46;v=float(std::sin(2*kPi*f*t)*std::exp(-t*14.)*.9);break;}
 case 1:v=float((noise()*.65+std::sin(2*kPi*186*t)*.25)*std::exp(-t*20.));break;
 case 2:v=float(noise()*std::exp(-t*39.)*.34);break;
 case 3:v=float((std::sin(2*kPi*110*t)+.22*std::sin(2*kPi*220*t))*std::exp(-t*.8)*.3);break;
 case 4:v=float((std::sin(2*kPi*261.63*t)+std::sin(2*kPi*329.63*t)+std::sin(2*kPi*392*t))*.1*std::min(1.,t*8)*std::exp(-t*.3));break;
 default:v=float(std::sin(2*kPi*440*t)*std::exp(-t*3.)*.3);}
 samples[i*2]=samples[i*2+1]=v;
 }return samples;}
}
