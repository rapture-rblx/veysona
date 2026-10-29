#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>

class PrismSynth final : public juce::AudioProcessor {
 struct Voice {int note=-1;double phase=0;float level=0,velocity=0;bool release=false;};
 std::array<Voice,24> voices{};double sampleRate=48000;
 juce::AudioParameterFloat* attack=nullptr;juce::AudioParameterFloat* release=nullptr;juce::AudioParameterFloat* brightness=nullptr;
 public:
 PrismSynth():AudioProcessor(BusesProperties().withOutput("Output",juce::AudioChannelSet::stereo(),true)){
  attack=new juce::AudioParameterFloat("attack","Attack",juce::NormalisableRange<float>(.001f,1.f),.02f);
  release=new juce::AudioParameterFloat("release","Release",juce::NormalisableRange<float>(.02f,3.f),.3f);
  brightness=new juce::AudioParameterFloat("bright","Brightness",juce::NormalisableRange<float>(0.f,1.f),.45f);
  addParameter(attack);addParameter(release);addParameter(brightness);
 }
 const juce::String getName() const override{return "Veysona Prism Synth";}
 bool acceptsMidi() const override{return true;}bool producesMidi() const override{return false;}bool isMidiEffect() const override{return false;}
 double getTailLengthSeconds() const override{return 3.0;}
 int getNumPrograms() override{return 1;}int getCurrentProgram() override{return 0;}void setCurrentProgram(int) override{}
 const juce::String getProgramName(int) override{return "Prism Default";}void changeProgramName(int,const juce::String&) override{}
 void prepareToPlay(double sr,int) override{sampleRate=sr;for(auto& v:voices)v={};}
 void releaseResources() override{}
 bool isBusesLayoutSupported(const BusesLayout& b) const override{return b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();}
 void processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi) override{
  juce::ScopedNoDenormals noDenormals;buffer.clear();
  auto events=midi.begin();auto end=midi.end();
  for(int i=0;i<buffer.getNumSamples();++i){
   while(events!=end&&(*events).samplePosition<=i){auto m=(*events).getMessage();if(m.isNoteOn()){
      Voice* slot=&voices[0];for(auto& v:voices)if(v.note<0){slot=&v;break;}
      *slot={m.getNoteNumber(),0,0,m.getFloatVelocity(),false};
     }else if(m.isNoteOff()){for(auto& v:voices)if(v.note==m.getNoteNumber())v.release=true;}
     else if(m.isAllNotesOff()||m.isAllSoundOff()){for(auto& v:voices)v={};}++events;
   }
   float y=0;for(auto& v:voices){if(v.note<0)continue;
    float a=*attack,r=*release;
    v.level+=v.release?float(-1./(sampleRate*std::max(.01f,r))):float(1./(sampleRate*std::max(.001f,a)));
    v.level=juce::jlimit(0.f,1.f,v.level);if(v.release&&v.level<=0){v={};continue;}
    double p=v.phase-std::floor(v.phase);float wave=float(std::sin(juce::MathConstants<double>::twoPi*p))*(1.f-*brightness*.5f)+float(2*p-1)*(*brightness*.45f);
    y+=wave*v.level*v.velocity*.23f;
    v.phase+=440.*std::pow(2.,(v.note-69)/12.)/sampleRate;
   }
   for(int ch=0;ch<buffer.getNumChannels();++ch)buffer.setSample(ch,i,std::tanh(y));
  }
 }
 bool hasEditor() const override{return true;}
 juce::AudioProcessorEditor* createEditor() override{return new juce::GenericAudioProcessorEditor(*this);}
 void getStateInformation(juce::MemoryBlock& dest) override{juce::MemoryOutputStream out(dest,false);out.writeFloat(*attack);out.writeFloat(*release);out.writeFloat(*brightness);}
 void setStateInformation(const void* data,int size) override{juce::MemoryInputStream in(data,size,false);if(size>=12){*attack=in.readFloat();*release=in.readFloat();*brightness=in.readFloat();}}
};
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new PrismSynth();}
