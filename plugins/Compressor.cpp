#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <algorithm>

class Compressor final: public juce::AudioProcessor {
 juce::AudioParameterFloat *threshold=nullptr,*ratio=nullptr,*attack=nullptr,*release=nullptr,*makeup=nullptr;
 double sr=48000;float envelope=0,smoothedGain=1;
public:
 Compressor():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)){
  threshold=new juce::AudioParameterFloat("threshold","Threshold (dB)",juce::NormalisableRange<float>(-50,0,.1f),-16);
  ratio=new juce::AudioParameterFloat("ratio","Ratio",juce::NormalisableRange<float>(1,12,.1f),4);
  attack=new juce::AudioParameterFloat("attack","Attack (ms)",juce::NormalisableRange<float>(1,100,.1f),10);
  release=new juce::AudioParameterFloat("release","Release (ms)",juce::NormalisableRange<float>(20,600,.1f),135);
  makeup=new juce::AudioParameterFloat("makeup","Makeup (dB)",juce::NormalisableRange<float>(-12,24,.1f),0);
  for(auto* p:{threshold,ratio,attack,release,makeup})addParameter(p);
 }
 const juce::String getName()const override{return "Veysona Dynamics Compressor";}
 bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}
 double getTailLengthSeconds()const override{return 0.;}
 int getNumPrograms()override{return 1;}int getCurrentProgram()override{return 0;}void setCurrentProgram(int)override{}
 const juce::String getProgramName(int)override{return "Glue";}void changeProgramName(int,const juce::String&)override{}
 void prepareToPlay(double sampleRate,int)override{sr=sampleRate;envelope=0;smoothedGain=1;}
 void releaseResources()override{}
 bool isBusesLayoutSupported(const BusesLayout& b)const override{return b.getMainInputChannelSet()==juce::AudioChannelSet::stereo()&&b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();}
 void processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer&)override{
  juce::ScopedNoDenormals denormals;
  const float a=std::exp(-1.f/float(sr*std::max(1.f,*attack)*.001f));
  const float r=std::exp(-1.f/float(sr*std::max(1.f,*release)*.001f));
  const float thresholdDb=*threshold,compression=*ratio,makeupLin=std::pow(10.f,*makeup/20.f);
  for(int i=0;i<audio.getNumSamples();++i){float l=audio.getSample(0,i),rr=audio.getSample(1,i);float level=std::max(std::abs(l),std::abs(rr));
   envelope=(level>envelope?a:r)*envelope+(1-(level>envelope?a:r))*level;
   float db=20.f*std::log10(std::max(envelope,1e-9f));
   float reduct=db>thresholdDb?(db-thresholdDb)*(1-1/compression):0;
   float gain=std::pow(10.f,-reduct/20.f)*makeupLin;
   smoothedGain+=.02f*(gain-smoothedGain);
   audio.setSample(0,i,l*smoothedGain);audio.setSample(1,i,rr*smoothedGain);
  }
 }
 bool hasEditor()const override{return true;}
 juce::AudioProcessorEditor* createEditor()override{return new juce::GenericAudioProcessorEditor(*this);}
 void getStateInformation(juce::MemoryBlock& out)override{juce::MemoryOutputStream s(out,false);for(float x:{float(*threshold),float(*ratio),float(*attack),float(*release),float(*makeup)})s.writeFloat(x);}
 void setStateInformation(const void* data,int bytes)override{if(bytes<20)return;juce::MemoryInputStream s(data,bytes,false);*threshold=s.readFloat();*ratio=s.readFloat();*attack=s.readFloat();*release=s.readFloat();*makeup=s.readFloat();}
};
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new Compressor();}
