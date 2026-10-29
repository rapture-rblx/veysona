#include <juce_audio_processors/juce_audio_processors.h>
#include "AudioCore.h"
#include <array>
#include <algorithm>
#include <cmath>

// Original four-band parametric VST3 equalizer. The same tested DSP is used by
// the DAW's integrated EQ graph, so the response and audio processing agree.
class SpectrumEQ final : public juce::AudioProcessor {
 std::array<juce::AudioParameterFloat*,4> frequency{},gain{},q{};
 juce::AudioParameterFloat* output=nullptr;
 std::array<std::array<veysona::BiquadState,4>,2> filterState{};
 double sr=48000;
public:
 SpectrumEQ():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)){
  constexpr float defaultFrequencies[]={95.f,360.f,2500.f,9500.f};
  for(int i=0;i<4;++i){
   const juce::String suffix=juce::String(i+1);
   frequency[size_t(i)]=new juce::AudioParameterFloat("f"+suffix,"Band "+suffix+" Frequency",juce::NormalisableRange<float>(30.f,18000.f,1.f,.4f),defaultFrequencies[i]);
   gain[size_t(i)]=new juce::AudioParameterFloat("g"+suffix,"Band "+suffix+" Gain (dB)",juce::NormalisableRange<float>(-18.f,18.f,.1f),0.f);
   q[size_t(i)]=new juce::AudioParameterFloat("q"+suffix,"Band "+suffix+" Q",juce::NormalisableRange<float>(.2f,10.f,.05f,.5f),1.0f);
   addParameter(frequency[size_t(i)]);addParameter(gain[size_t(i)]);addParameter(q[size_t(i)]);
  }
  output=new juce::AudioParameterFloat("out","Output Gain (dB)",juce::NormalisableRange<float>(-18,18,.1f),0.f);
  addParameter(output);
 }
 const juce::String getName()const override{return "Veysona Spectrum EQ 4";}
 bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}
 double getTailLengthSeconds()const override{return 0.;}
 int getNumPrograms()override{return 1;}int getCurrentProgram()override{return 0;}void setCurrentProgram(int)override{}
 const juce::String getProgramName(int)override{return "Flat";}void changeProgramName(int,const juce::String&)override{}
 void prepareToPlay(double sampleRate,int)override{sr=sampleRate;for(auto& chan:filterState)for(auto& band:chan)band.reset();}
 void releaseResources()override{}
 bool isBusesLayoutSupported(const BusesLayout& b)const override{return b.getMainInputChannelSet()==juce::AudioChannelSet::stereo()&&b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();}
 void processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer&)override{
  juce::ScopedNoDenormals noDenormals;
  std::array<veysona::BiquadCoefficients,4> coeffs;
  for(int i=0;i<4;++i){
   veysona::EqBand band;band.mode=(i==0?veysona::EqMode::LowShelf:i==3?veysona::EqMode::HighShelf:veysona::EqMode::Bell);
   band.frequency=*frequency[size_t(i)];band.gainDb=*gain[size_t(i)];band.q=*q[size_t(i)];
   coeffs[size_t(i)]=veysona::makeEqCoefficients(band,uint32_t(sr));
  }
  const float outputGain=std::pow(10.f,float(*output)/20.f);
  for(int ch=0;ch<std::min(audio.getNumChannels(),2);++ch){
   auto* samples=audio.getWritePointer(ch);
   for(int i=0;i<audio.getNumSamples();++i){
    float v=samples[i];for(int b=0;b<4;++b)v=filterState[size_t(ch)][size_t(b)].process(v,coeffs[size_t(b)]);
    samples[i]=v*outputGain;
   }
  }
 }
 bool hasEditor()const override{return true;}
 juce::AudioProcessorEditor* createEditor()override{return new juce::GenericAudioProcessorEditor(*this);}
 void getStateInformation(juce::MemoryBlock& dest)override{
  juce::MemoryOutputStream out(dest,false);out.writeInt(2);
  for(int i=0;i<4;++i){out.writeFloat(*frequency[size_t(i)]);out.writeFloat(*gain[size_t(i)]);out.writeFloat(*q[size_t(i)]);}
  out.writeFloat(*output);
 }
 void setStateInformation(const void* data,int size)override{
  if(size<4+13*4)return;
  juce::MemoryInputStream in(data,size,false);if(in.readInt()!=2)return;
  auto set=[](juce::AudioParameterFloat* param,float value){param->setValueNotifyingHost(param->convertTo0to1(value));};
  for(int i=0;i<4;++i){set(frequency[size_t(i)],in.readFloat());set(gain[size_t(i)],in.readFloat());set(q[size_t(i)],in.readFloat());}
  set(output,in.readFloat());
 }
};
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new SpectrumEQ();}
