#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <algorithm>
#include <cmath>

class EchoEffect final : public juce::AudioProcessor {
 juce::AudioParameterFloat* time=nullptr,*feedback=nullptr,*mix=nullptr;
 std::vector<float> ringL,ringR;size_t cursor=0;double sr=48000;
public:
 EchoEffect():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)){
  time=new juce::AudioParameterFloat("time","Time (ms)",juce::NormalisableRange<float>(60,900,1),275);
  feedback=new juce::AudioParameterFloat("feedback","Feedback",juce::NormalisableRange<float>(0,.9f),.35f);
  mix=new juce::AudioParameterFloat("mix","Wet Mix",juce::NormalisableRange<float>(0,1),.3f);
  addParameter(time);addParameter(feedback);addParameter(mix);
 }
 const juce::String getName() const override{return "Veysona Orbit Echo";}
 bool acceptsMidi() const override{return false;}bool producesMidi() const override{return false;}bool isMidiEffect() const override{return false;}
 double getTailLengthSeconds() const override{return 5.;}
 int getNumPrograms() override{return 1;}int getCurrentProgram() override{return 0;}void setCurrentProgram(int) override{}
 const juce::String getProgramName(int) override{return "Orbit Default";}void changeProgramName(int,const juce::String&) override{}
 void prepareToPlay(double rate,int) override{sr=rate;ringL.assign(size_t(sr*1.2),0);ringR.assign(size_t(sr*1.2),0);cursor=0;}
 void releaseResources() override{}
 bool isBusesLayoutSupported(const BusesLayout& b) const override{return b.getMainInputChannelSet()==juce::AudioChannelSet::stereo()&&b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();}
 void processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer&) override{
  juce::ScopedNoDenormals noDenormals;if(ringL.empty())return;
  auto samples=std::clamp(size_t(sr*(*time)*.001),size_t(1),ringL.size()-1);
  for(int i=0;i<audio.getNumSamples();++i){size_t read=(cursor+ringL.size()-samples)%ringL.size();float dl=ringL[read],dr=ringR[read];float inL=audio.getSample(0,i),inR=audio.getSample(1,i);
   ringL[cursor]=inL+dr*(*feedback);ringR[cursor]=inR+dl*(*feedback);
   audio.setSample(0,i,inL*(1-*mix)+dl*(*mix));audio.setSample(1,i,inR*(1-*mix)+dr*(*mix));cursor=(cursor+1)%ringL.size();
  }
 }
 bool hasEditor() const override{return true;}
 juce::AudioProcessorEditor* createEditor() override{return new juce::GenericAudioProcessorEditor(*this);}
 void getStateInformation(juce::MemoryBlock& dest) override{juce::MemoryOutputStream out(dest,false);out.writeFloat(*time);out.writeFloat(*feedback);out.writeFloat(*mix);}
 void setStateInformation(const void* data,int size) override{juce::MemoryInputStream in(data,size,false);if(size>=12){*time=in.readFloat();*feedback=in.readFloat();*mix=in.readFloat();}}
};
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new EchoEffect();}
