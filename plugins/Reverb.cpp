#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>
#include <vector>
#include <algorithm>

class Reverb final:public juce::AudioProcessor{
 struct Comb{std::vector<float> buffer;size_t pos=0;float low=0;float process(float x,float feedback,float damping){if(buffer.empty())return x;float v=buffer[pos];low+=(v-low)*damping;buffer[pos]=x+low*feedback;pos=(pos+1)%buffer.size();return v;}};
 std::array<Comb,8> combs;juce::AudioParameterFloat *room=nullptr,*damp=nullptr,*mix=nullptr;
 double sampleRate=48000;
public:
 Reverb():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)){
  room=new juce::AudioParameterFloat("room","Room Size",juce::NormalisableRange<float>(0,1),.55f);
  damp=new juce::AudioParameterFloat("damp","Damping",juce::NormalisableRange<float>(0,1),.45f);
  mix=new juce::AudioParameterFloat("mix","Wet",juce::NormalisableRange<float>(0,1),.23f);
  addParameter(room);addParameter(damp);addParameter(mix);
 }
 const juce::String getName()const override{return "Veysona Velvet Reverb";}
 bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}
 double getTailLengthSeconds()const override{return 7.;}
 int getNumPrograms()override{return 1;}int getCurrentProgram()override{return 0;}void setCurrentProgram(int)override{}
 const juce::String getProgramName(int)override{return "Small Hall";}void changeProgramName(int,const juce::String&)override{}
 void prepareToPlay(double rate,int)override{sampleRate=rate;const int primes[]={1493,1601,1747,1867,1501,1627,1783,1901};for(int i=0;i<8;++i){auto& c=combs[size_t(i)];c.buffer.assign(std::max(size_t(64),size_t(rate*primes[i]/48000.)),0);c.pos=0;c.low=0;}}
 void releaseResources()override{}
 bool isBusesLayoutSupported(const BusesLayout& b)const override{return b.getMainInputChannelSet()==juce::AudioChannelSet::stereo()&&b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();}
 void processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer&)override{
  juce::ScopedNoDenormals denormals;float feedback=.4f+*room*.5f;float damping=.1f+(1-*damp)*.7f;
  for(int i=0;i<audio.getNumSamples();++i){float l=audio.getSample(0,i),r=audio.getSample(1,i);float mono=(l+r)*.5f;float wetL=0,wetR=0;
   for(int c=0;c<4;++c)wetL+=combs[size_t(c)].process(mono,feedback,damping);
   for(int c=4;c<8;++c)wetR+=combs[size_t(c)].process(mono,feedback,damping);
   wetL*=.25f;wetR*=.25f;audio.setSample(0,i,l*(1-*mix)+wetL*(*mix));audio.setSample(1,i,r*(1-*mix)+wetR*(*mix));
  }
 }
 bool hasEditor()const override{return true;}
 juce::AudioProcessorEditor* createEditor()override{return new juce::GenericAudioProcessorEditor(*this);}
 void getStateInformation(juce::MemoryBlock& out)override{juce::MemoryOutputStream stream(out,false);stream.writeFloat(*room);stream.writeFloat(*damp);stream.writeFloat(*mix);}
 void setStateInformation(const void* ptr,int n)override{if(n<12)return;juce::MemoryInputStream stream(ptr,n,false);*room=stream.readFloat();*damp=stream.readFloat();*mix=stream.readFloat();}
};
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new Reverb();}
