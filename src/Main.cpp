#include "RecordingSpool.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "AudioCore.h"
#include "TimeWarp.h"
#include "MidiTools.h"
#include "AdvancedMidi.h"
#include "MixWorkflow.h"
#include "Performance.h"
#include "ProductionAnalysis.h"
#include "RecoveryPolicy.h"
#include "SessionHealth.h"
#include "GrooveLab.h"
#include "ProductionWorkflow.h"
#include "UiGeometry.h"
#include "EqWorkflow.h"
#include "DrumSequencer.h"
#include "UpdatePolicy.h"
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <atomic>
#include <array>
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <memory>
#include <map>
#include <initializer_list>
#include <optional>
#include <functional>
#include <complex>
#include <vector>

#ifndef VEYSONA_UPDATE_MANIFEST_URL
#define VEYSONA_UPDATE_MANIFEST_URL ""
#endif
#ifndef VEYSONA_UPDATE_SIGNER_THUMBPRINT
#define VEYSONA_UPDATE_SIGNER_THUMBPRINT ""
#endif

namespace C {
 // Reconstructed v1.28 steel-blue studio palette. Track colours remain independent of accent.
 const auto bg=juce::Colour(0xff09111c), top=juce::Colour(0xff101b29), panel=juce::Colour(0xff182738), raised=juce::Colour(0xff2a3e55);
 const auto outline=juce::Colour(0xff516980), ink=juce::Colour(0xfff5f9ff), muted=juce::Colour(0xffbfcee0);
 const auto blue=juce::Colour(0xff78b7ff); juce::Colour purple=juce::Colour(0xff2c9eff); const auto aqua=juce::Colour(0xff56d4d0), green=juce::Colour(0xff49d5a3), gold=juce::Colour(0xfff0c872), pink=juce::Colour(0xfff27e9f);
 // Independent from the theme accent so an appearance change never recolours existing clips.
 const juce::Colour palette[]={juce::Colour(0xff918cfc),green,pink,blue,gold,aqua};
}
static juce::String jstr(const std::string& s){return juce::String::fromUTF8(s.c_str());}
static juce::Colour trackColour(int i){return C::palette[(i+6)%6];}
static void caption(juce::Graphics& g,const juce::String& s,juce::Rectangle<int> area,juce::Colour color=C::muted,int sz=12){
 g.setColour(color);g.setFont(juce::Font(float(sz)));
 g.drawFittedText(s,area,juce::Justification::centredLeft,1);
}
static void roundRect(juce::Graphics& g,juce::Rectangle<float> area,juce::Colour color,float r=6){g.setColour(color);g.fillRoundedRectangle(area,r);}

// Versioned automation data: malformed external project files are clamped.
static juce::var automationToJson(const std::vector<veysona::AutomationLane>& lanes){
 juce::Array<juce::var> all;
 for(const auto& lane:lanes){
  juce::var object(new juce::DynamicObject());auto* o=object.getDynamicObject();
  o->setProperty("target",int(lane.target));o->setProperty("enabled",lane.enabled);o->setProperty("step",lane.boolStep);o->setProperty("smooth",lane.smooth);
  juce::Array<juce::var> points;
  for(const auto& pt:lane.points){juce::Array<juce::var> pair;pair.add(pt.beat);pair.add(pt.value);points.add(juce::var(pair));}
  o->setProperty("points",juce::var(points));all.add(object);
 }
 return juce::var(all);
}
static std::vector<veysona::AutomationLane> automationFromJson(const juce::var& value){
 std::vector<veysona::AutomationLane> output;
 if(auto* lanes=value.getArray())for(int i=0;i<std::min(lanes->size(),16);++i){
  const auto& v=lanes->getReference(i);
  const int target=std::clamp(int(v.getProperty("target",0)),0,7);
  veysona::AutomationLane lane;lane.target=veysona::AutomationTarget(target);
  lane.enabled=bool(v.getProperty("enabled",true));lane.boolStep=bool(v.getProperty("step",false));lane.smooth=bool(v.getProperty("smooth",false));
  const auto pointsValue=v.getProperty("points",{});
  if(auto* points=pointsValue.getArray())for(int j=0;j<std::min(points->size(),8192);++j){
   if(auto* pair=points->getReference(j).getArray())if(pair->size()>=2){
    double beat=double(pair->getReference(0)),val=double(pair->getReference(1));
    if(std::isfinite(beat)&&std::isfinite(val))lane.points.push_back({std::clamp(beat,0.,1.e6),float(std::clamp(val,-24.,24.))});
   }
  }
  std::sort(lane.points.begin(),lane.points.end(),[](auto a,auto b){return a.beat<b.beat;});
  output.push_back(std::move(lane));
 }
 return output;
}
class StudioLookAndFeel final : public juce::LookAndFeel_V4 {
public:
 StudioLookAndFeel(){
  setColour(juce::PopupMenu::backgroundColourId,C::panel);
  setColour(juce::PopupMenu::textColourId,C::ink);
  setColour(juce::PopupMenu::highlightedBackgroundColourId,C::purple.withAlpha(.24f));
  setColour(juce::PopupMenu::highlightedTextColourId,C::ink);
  setColour(juce::ScrollBar::thumbColourId,C::outline);
  setColour(juce::ComboBox::backgroundColourId,C::raised);
  setColour(juce::ComboBox::textColourId,C::ink);
  setColour(juce::ComboBox::outlineColourId,C::outline);
  setColour(juce::ComboBox::arrowColourId,C::muted);
  setColour(juce::Label::textColourId,C::ink);
  setColour(juce::TextEditor::backgroundColourId,C::raised);
  setColour(juce::TextEditor::textColourId,C::ink);
  setColour(juce::TextEditor::outlineColourId,C::outline);
  setColour(juce::TextEditor::highlightedTextColourId,C::ink);
  setColour(juce::TextEditor::focusedOutlineColourId,C::purple);
  setColour(juce::TextEditor::highlightColourId,C::purple.withAlpha(.34f));
 }
 void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour& background,bool hover,bool down) override{
  const auto bounds=b.getLocalBounds().toFloat().reduced(.7f);
  auto base=background;
  if(down)base=base.brighter(.19f);
  else if(hover)base=base.brighter(.105f);
  const bool active=b.getToggleState();
  juce::ColourGradient fill(base.brighter(.055f),bounds.getX(),bounds.getY(),base.darker(.095f),bounds.getX(),bounds.getBottom(),false);
  g.setGradientFill(fill);g.fillRoundedRectangle(bounds,6.f);
  if(active){
   g.setColour(C::purple.withAlpha(.13f));g.fillRoundedRectangle(bounds.reduced(1.f),5.f);
   g.setColour(C::purple);g.fillRoundedRectangle(bounds.getX()+4.f,bounds.getBottom()-3.f,std::max(1.f,bounds.getWidth()-8.f),2.f,1.f);
  }
  if(hover||down){g.setColour(C::ink.withAlpha(.065f));g.fillRoundedRectangle(bounds.reduced(1.f),5.f);}
  const auto line=b.hasKeyboardFocus(true)?C::blue:(down||active?C::purple.withAlpha(.85f):hover?C::ink.withAlpha(.34f):C::outline.withAlpha(.72f));
  g.setColour(line);g.drawRoundedRectangle(bounds,6.f,b.hasKeyboardFocus(true)?1.75f:1.f);
 }
 void drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool) override{
  g.setColour(b.findColour(b.getToggleState()?juce::TextButton::textColourOnId:juce::TextButton::textColourOffId));
  g.setFont(juce::Font(13.f).boldened());
  g.drawFittedText(b.getButtonText(),b.getLocalBounds().reduced(8,2),juce::Justification::centred,1,.87f);
 }
 void drawLinearSlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float minPos,float maxPos,const juce::Slider::SliderStyle style,juce::Slider& slider) override{
  if(style!=juce::Slider::LinearHorizontal){juce::LookAndFeel_V4::drawLinearSlider(g,x,y,w,h,pos,minPos,maxPos,style,slider);return;}
  const auto trackY=float(y)+float(h)*.5f;
  const float start=float(x)+5.f,finish=float(x+w)-6.f;
  g.setColour(C::outline.withAlpha(.8f));g.fillRoundedRectangle(start,trackY-2.5f,std::max(1.f,finish-start),5.f,2.5f);
  g.setColour(C::purple);g.fillRoundedRectangle(start,trackY-2.5f,std::max(1.f,std::min(pos,finish)-start),5.f,2.5f);
  g.setColour(C::ink);g.fillEllipse(pos-6.5f,trackY-6.5f,13.f,13.f);
  g.setColour(C::purple.withAlpha(.6f));g.drawEllipse(pos-6.5f,trackY-6.5f,13.f,13.f,1.25f);
 }
 void drawMenuBarBackground(juce::Graphics& g,int width,int height,bool,juce::MenuBarComponent&) override{
  g.setColour(C::top);g.fillRect(0,0,width,height);
  g.setColour(C::outline.withAlpha(.68f));g.fillRect(0,height-1,width,1);
 }
 void drawMenuBarItem(juce::Graphics& g,int width,int height,int,const juce::String& itemText,bool hover,bool open,juce::MenuBarComponent&) override{
  auto r=juce::Rectangle<float>(3.f,2.f,float(width-6),float(height-4));
  if(hover||open){g.setColour(open?C::purple.withAlpha(.24f):C::raised);g.fillRoundedRectangle(r,5.f);}
  if(open){g.setColour(C::purple);g.fillRoundedRectangle(7.f,float(height-4),float(width-14),2.f,1.f);}
  g.setColour(open?C::ink:(hover?C::ink:C::muted));g.setFont(juce::Font(13.f).boldened());
  g.drawFittedText(itemText,5,0,width-10,height,juce::Justification::centred,1);
 }
 void drawPopupMenuBackground(juce::Graphics& g,int w,int h) override{
  juce::ColourGradient fill(C::raised.darker(.13f),0.f,0.f,C::panel,0.f,float(h),false);
  g.setGradientFill(fill);g.fillRect(0,0,w,h);
  g.setColour(C::outline);g.drawRect(0,0,w,h,1);
 }
};
class Studio;
class DockDivider final : public juce::Component {
 bool vertical;
 int dragOrigin=0;
public:
 std::function<void(int)> onDrag;
 std::function<void()> onDone;
 std::function<void()> onBegin;
 explicit DockDivider(bool isVertical):vertical(isVertical) {
  setMouseCursor(isVertical ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
  setRepaintsOnMouseActivity(true);
 }
 void paint(juce::Graphics& g) override {
  g.setColour(isMouseOverOrDragging()?C::blue.withAlpha(.66f):C::outline.withAlpha(.65f));
  if(vertical)g.fillRect(getWidth()/2-1,0,2,getHeight());
  else g.fillRect(0,getHeight()/2-1,getWidth(),2);
  g.setColour(isMouseOverOrDragging()?C::blue:C::muted.withAlpha(.8f));
  if(vertical)g.fillRoundedRectangle(float(getWidth()/2-2),float(getHeight()/2-24),4.f,48.f,2.f);
  else g.fillRoundedRectangle(float(getWidth()/2-26),float(getHeight()/2-2),52.f,4.f,2.f);
 }
 void mouseDown(const juce::MouseEvent& e) override {dragOrigin=vertical?e.getScreenX():e.getScreenY();if(onBegin)onBegin();}
 void mouseDrag(const juce::MouseEvent& e) override {if(onDrag)onDrag((vertical?e.getScreenX():e.getScreenY())-dragOrigin);}
 void mouseUp(const juce::MouseEvent&) override {if(onDone)onDone();repaint();}
};
class Arrangement : public juce::Component {
 Studio& host;bool dragging=false,draggingMidi=false,draggingAuto=false;int gainDragTrack=-1;float gainDragOriginal=1.f;int gainDragScreenY=0;int pickedTrack=-1,pickedClip=-1,pickedMidi=-1,pickedAutoPoint=-1;double dragBeat=0,originalBeat=0;
public:
 explicit Arrangement(Studio& s):host(s){setRepaintsOnMouseActivity(true);}
 void paint(juce::Graphics&) override;
 void mouseDown(const juce::MouseEvent&) override;
 void mouseDrag(const juce::MouseEvent&) override;
 void mouseUp(const juce::MouseEvent&) override{dragging=draggingMidi=draggingAuto=false;gainDragTrack=-1;}
 void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails&) override;
 void mouseDoubleClick(const juce::MouseEvent&) override;
 int contentWidth() const;
};
class PianoRoll : public juce::Component {
 Studio& host;
 int dragNote=-1, startingPitch=60;
 double startingBeat=0, startingLength=.25, startingVelocity=.8;
 bool dragging=false, resizing=false, velocityDrag=false;
 int drumGridStart=0, drumGridSteps=32, drumSelectedLane=0, drumLastStep=-1;
 bool drumPainting=false, drumPaintAdd=false, drumVelocityEditing=false;
 double beatPixels=42, visibleStartBeat=0;
 int topPitch=72;
 float noteHeight=11.f;
 int scaleRoot=0,scaleMode=0; // 0 chromatic; 1 major; 2 natural minor
 int scalePitch(int pitch) const {return scaleMode==0?pitch:veysona::midi::nearestScalePitch(pitch,scaleRoot,scaleMode==2);}
 double snapGrid() const;
 juce::Rectangle<float> noteRect(const veysona::Note&) const;
public:
 explicit PianoRoll(Studio& s):host(s){}
 void paint(juce::Graphics&) override;
 void mouseDown(const juce::MouseEvent&) override;
 void mouseDrag(const juce::MouseEvent&) override;
 void mouseUp(const juce::MouseEvent&) override;
 void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails&) override;
 void focusOnNotes();
 bool isDrumTrack() const;
 void paintDrumGrid(juce::Graphics&);
 void drumMouseDown(const juce::MouseEvent&);
 void drumMouseDrag(const juce::MouseEvent&);
 void setScale(int root,int mode){scaleRoot=std::clamp(root,0,11);scaleMode=std::clamp(mode,0,2);repaint();}
 void constrainActiveScale(veysona::MidiRegion& region) const {if(scaleMode!=0)veysona::midi::constrainToNamedScale(region.notes,scaleRoot,scaleMode==2);}
};
class EQView : public juce::Component {
 Studio& host; int draggingBand=-1, selectedBand=0, editedTrack=-1;
 bool analyserVisible=true;
 std::optional<veysona::EqSettings> dragBefore;
 juce::Rectangle<float> graphArea() const {
  const float reserved=getHeight()<265?139.f:167.f;
  return {42.f,41.f,std::max(48.f,float(getWidth()-59)),std::max(38.f,float(getHeight())-reserved)};
 }
 juce::Rectangle<int> liveButton() const {return {std::max(0,getWidth()-157),7,43,25};}
 juce::Rectangle<int> presetsButton() const {return {std::max(0,getWidth()-110),7,72,25};}
 juce::Rectangle<int> expandButton() const {return {std::max(0,getWidth()-34),7,29,25};}
 int bandChipY() const {return getHeight()-(getHeight()<265?70:98);}
 void showPresetMenu();
 void selectBand(int band) { selectedBand=std::clamp(band,0,7);repaint(); }

public:
 explicit EQView(Studio& s):host(s){}
 void paint(juce::Graphics&) override;
 void mouseDown(const juce::MouseEvent&) override;
 void mouseDrag(const juce::MouseEvent&) override;
 void mouseUp(const juce::MouseEvent&) override;
 void mouseDoubleClick(const juce::MouseEvent&) override;
 void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails&) override;
};
class MixerView : public juce::Component {
 Studio& host;int draggingFader=-1;int draggingBus=-1;bool draggingMaster=false;
 bool draggingPan=false;float startingDb=0.f,startingPan=0.f;
public:
 explicit MixerView(Studio& s):host(s){}
 void paint(juce::Graphics&) override;
 void mouseDown(const juce::MouseEvent&) override;
 void mouseDrag(const juce::MouseEvent&) override;
 void mouseUp(const juce::MouseEvent&) override{draggingFader=-1;draggingBus=-1;draggingMaster=false;draggingPan=false;}
 void mouseDoubleClick(const juce::MouseEvent&) override;
};

// Original clip-launch matrix. Each column is a track; rows are scenes.
// Triggered audio/MIDI loops use the separate quantised PerformanceLauncher.
class PerformanceGrid : public juce::Component {
 Studio& host;
public:
 explicit PerformanceGrid(Studio& s):host(s){setRepaintsOnMouseActivity(true);}
 void paint(juce::Graphics&) override;
 void mouseDown(const juce::MouseEvent&) override;
 int contentWidth() const;
 int contentHeight() const;
};
class Studio final : public juce::Component, public juce::MenuBarModel, private juce::Timer, private juce::ChangeListener, private juce::ListBoxModel, public juce::FileDragAndDropTarget {
 friend class Arrangement;friend class PianoRoll;friend class MixerView;friend class EQView;friend class PerformanceGrid;
 veysona::Session session;veysona::EngineState engine;veysona::PerformanceLauncher launcher;
 juce::CriticalSection audioLock;
 StudioLookAndFeel theme;
 juce::MenuBarComponent menuBar{this};
 juce::AudioDeviceManager devices;
 juce::AudioFormatManager formats;
 juce::AudioPluginFormatManager pluginFormats;
 juce::MidiMessageCollector livePluginMidi; // thread-safe MIDI queue for realtime VST3 monitoring
 std::vector<std::vector<std::unique_ptr<juce::AudioPluginInstance>>> plugins;
 std::atomic<bool> playing{false}, recording{false}, metronome{false}, midiArmed{false};
 std::atomic<int64_t> playCursor{0};
 std::atomic<float> peakL{0},peakR{0};
 std::atomic<uint64_t> audioLockMisses{0},slowAudioCallbacks{0};
 std::atomic<float> callbackMillis{0};
 std::array<std::atomic<float>,128> trackPeaks{};
 std::array<std::atomic<float>,veysona::kMaxBuses> busPeaks{};
 double audioRate=48000;int audioBlock=512;
 std::array<float,2048> scratchL{},scratchR{},mixL{},mixR{};
 struct StereoScratch {std::array<float,2048> l{},r{};};
 std::array<StereoScratch,veysona::kMaxBuses> busScratch{};
 std::unique_ptr<veysona::RecordingSpool> recordingSpool;
 juce::File recordingTempFile;
 std::atomic<bool> recordingOverrun{false};
 std::array<std::atomic<bool>,512> pluginTrackFaults{};
 bool pluginFaultReported=false,recordOverrunReported=false;
 bool looping=true, mixerMode=false,sessionMode=false,loadingUI=false,automationMode=false,autosaveEnabled=true,autosaving=false;
 bool welcomeCompleted=false, detailsVisible=true;
 bool snapEnabled=true,followPlayhead=false,browserVisible=true;
 int browserDockWidth=294,editorDockHeight=385;
 float pianoDockFraction=.42f,instrumentDockFraction=.22f;
 int splitterInitial=0;
 float splitterInitialRatio=0.f;
 std::array<std::atomic<float>,4096> eqInputRing{};
 std::atomic<uint32_t> eqInputWrite{0};
 std::array<float,2047> analyserBins{};
 void refreshAnalyser();

 int autosaveMinutes=3, recoverySlot=0, startupMode=0; // 0=last project, 1=demo, 2=blank, 3=ask
 juce::File autosaveFolder,lastOpenedProject;
 juce::StringArray recentProjectPaths;
 juce::String recoverySessionId=juce::String::toHexString(juce::Time::currentTimeMillis());
 int accentPreset=0;
 juce::StringArray additionalPluginFolders;
 std::optional<veysona::Clip> clipboardAudio;
 std::optional<veysona::MidiRegion> clipboardMidi;
 juce::File currentProject;
 juce::int64 lastAutosaveTick=0;
 veysona::eqtools::History eqHistory;
 std::optional<veysona::mix::ChannelSettings> channelClipboard;
 std::optional<veysona::EqSettings> eqClipboard;
 juce::Component::SafePointer<EQView> expandedEQ;
 void showExpandedEQ();
 veysona::Clip previewAudio;double previewFrame=0;bool previewPlaying=false;
 struct MidiSnapshot {int track=0, region=0;std::vector<veysona::Note> notes;};
 std::vector<MidiSnapshot> midiUndo,midiRedo;
 struct AudioHistory {int track=-1;std::vector<veysona::Clip> clips;};
 std::vector<AudioHistory> audioUndo,audioRedo;
 veysona::midi::Recorder midiRecorder;
 std::vector<juce::String> midiDeviceIds;
 std::vector<int64_t> pluginMidiEnds; // last contiguous rendered block per VST3 track
 int selectedTrack=0, selectedClip=-1, selectedMidiRegion=0, selectedNote=-1;double pixelsPerBar=110;int bars=64;
 int browserCategory=0;juce::StringArray browserItems;std::vector<juce::File> browserFiles;
 juce::File sampleFolder,midiFolder,instrumentFolder;
 // Optional CC0 recordings are installed to user-writable AppData, not Program Files.
 juce::File recordedAssetsFolder() const {
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
      .getChildFile("Veysona Studio").getChildFile("Recorded Assets");
 }
 juce::File recordedInstrumentPack(const juce::String& program) const {
  const auto userPack=recordedAssetsFolder().getChildFile("instrument_packs").getChildFile(program);
  return userPack.getChildFile("RECORDED_SOURCE.json").existsAsFile()?userPack:
      instrumentFolder.getParentDirectory().getChildFile("instrument_packs").getChildFile(program);
 }
 juce::File recordedDrumPack(const juce::String& kit) const {
  const auto userKit=recordedAssetsFolder().getChildFile("drum_kits").getChildFile(kit);
  return userKit.getChildFile("RECORDED_SOURCE.json").existsAsFile()?userKit:
      sampleFolder.getParentDirectory().getChildFile("drum_kits").getChildFile(kit);
 }
 std::vector<juce::File> fileLocations;
 std::unique_ptr<juce::FileChooser> chooser;

 juce::TextButton playButton{"Play"},stopButton{"Stop"},recordButton{"Record"},loopButton{"Loop: ON"},
 importButton{"+ Import Audio"},midiButton{"+ MIDI Track"},demoButton{"Demo Project"},previewButton{"Preview"},
 saveButton{"Save"},openButton{"Open"},exportButton{"Export WAV"}, pluginButton{"Load VST3"},pluginEditorButton{"Plugin Editor"},
 audioSettings{"Audio Settings"},addNoteButton{"+ Note"},metronomeButton{"Click: OFF"},
 samplerButton{"Load Instrument"},drumButton{"+ Drum Rack"},midiToolsButton{"MIDI Tools"},preferencesButton{"Preferences"},automationButton{"Automation"},midiArmButton{"MIDI Arm"},
 moreButton{"Actions"},detailsButton{"Hide Editor"},snapButton{"Snap: 1/16"},fitButton{"Fit Song"},followButton{"Follow: Off"};
 juce::Slider bpm,zoom,volume,pan,cutoff,drive,delay,compThreshold,compRatio,masterGain;
 juce::ComboBox category,instrument,midiGrid;
 juce::TextEditor search;
 juce::ListBox browser;
 juce::Label status,trackName,transportStatus;
 juce::TextButton browserSamplesTab{"Sounds"},browserPluginsTab{"Plug-ins"},browserToggle{"Browser"};
 juce::TextButton tabPiano{"MIDI EDITOR"},tabMixer{"MIXER"},tabArrangement{"ARRANGEMENT"},tabSession{"SESSION"},stopScenes{"STOP CLIPS"}, midiImportButton{"Import MIDI"},midiExportButton{"Export MIDI"};
 juce::TextButton pianoScaleButton{"Scale: All"},quantizeButton{"Quantize"},transposeUp{"Oct +"},transposeDown{"Oct -"},duplicateNotesButton{"Duplicate"},undoMidiButton{"Undo MIDI"},redoMidiButton{"Redo MIDI"};
 juce::TextButton eqToggle{"EQ On"},compressorToggle{"Comp Off"},fxPresetsButton{"FX Presets"};
 juce::Viewport timelineViewport,mixerViewport,performanceViewport;
 Arrangement arrangement;
 PerformanceGrid performanceGrid;
 PianoRoll piano;
 MixerView mixer;
 EQView eqView;
 DockDivider browserDivider{true}, editorDivider{false}, deviceDivider{true}, effectDivider{true};
 juce::Rectangle<int> editorArea,effectArea,masterArea;
 juce::Image brandIcon, brandLogo;
 juce::Component::SafePointer<juce::Label> splashUpdateLabel;
 bool updateBusy=false, updateIsDownload=false, updateCheckOnStartup=false;
 juce::int64 updateRequestStartMillis=0;
 juce::int64 lastUpdatePoll=0;
 juce::String updateRequestId, updateVersion, updateInstallerUrl, updateSha256, updateReleasePage, updateNotes, updateExpectedSize;
 juce::File updateStatusFile, downloadedInstaller;
 juce::File updaterToolFile();
 juce::File updateStatusLocation() const;
 bool launchUpdater(const juce::StringArray& flags);
 void checkForUpdates(bool fromStartup);
 void startUpdateDownload();
 void pollUpdateStatus();
 void displayUpdateOffer();
 void displayInstallOffer();
 void installVerifiedUpdate();
 void splashUpdateMessage(const juce::String& message);


 void style(juce::TextButton& b,juce::Colour base=C::raised){
  b.setColour(juce::TextButton::buttonColourId,base);
  b.setColour(juce::TextButton::buttonOnColourId,C::purple);
  b.setColour(juce::TextButton::textColourOffId,C::ink);
  b.setColour(juce::TextButton::textColourOnId,C::ink);
  b.setMouseCursor(juce::MouseCursor::PointingHandCursor);addAndMakeVisible(b);
 }
 void slider(juce::Slider& s,double min,double max,double start,const juce::String& suffix={}){
  s.setRange(min,max,max<=1?.01:1);s.setValue(start,juce::dontSendNotification);s.setSliderStyle(juce::Slider::LinearHorizontal);
  s.setTextBoxStyle(juce::Slider::TextBoxRight,false,54,22);s.setTextValueSuffix(suffix);s.setColour(juce::Slider::thumbColourId,C::purple);
  s.setColour(juce::Slider::trackColourId,C::purple);s.setColour(juce::Slider::backgroundColourId,C::raised);
  s.setColour(juce::Slider::textBoxTextColourId,C::ink);s.setColour(juce::Slider::textBoxOutlineColourId,C::outline);addAndMakeVisible(s);
 }
 void alert(const juce::String& title,const juce::String& msg){juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,title,msg);}
 void choose(int flags,juce::String title,std::function<void(juce::File)> action,juce::String wildcard="*"){
  chooser=std::make_unique<juce::FileChooser>(title,juce::File{},wildcard);
  chooser->launchAsync(flags,[this,action](const juce::FileChooser& ch){auto f=ch.getResult();if(f!=juce::File{})action(f);});
 }
 juce::File findSamples(){
  const auto cwd=juce::File::getCurrentWorkingDirectory();const auto exe=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
  for(auto d:{cwd,exe,exe.getParentDirectory(),exe.getParentDirectory().getParentDirectory(),exe.getParentDirectory().getParentDirectory().getParentDirectory()}){
   auto dir=d.getChildFile("assets").getChildFile("samples");if(dir.isDirectory())return dir;
  }
  return cwd.getChildFile("assets").getChildFile("samples");
 }
 void setStatus(juce::String s){status.setText(std::move(s),juce::dontSendNotification);}
 veysona::MidiRegion* activeMidi(){
  if(selectedTrack<0||selectedTrack>=int(session.tracks.size()))return nullptr;
  auto& regions=session.tracks[size_t(selectedTrack)].midi;
  if(selectedMidiRegion<0||selectedMidiRegion>=int(regions.size()))return nullptr;
  return &regions[size_t(selectedMidiRegion)];
 }
 void saveMidiUndo(){
  if(auto* region=activeMidi()){
   if(midiUndo.size()>=64)midiUndo.erase(midiUndo.begin());
   midiUndo.push_back({selectedTrack,selectedMidiRegion,region->notes});midiRedo.clear();
  }
 }
 void midiHistory(bool undo){
  auto& from=undo?midiUndo:midiRedo;auto& into=undo?midiRedo:midiUndo;
  if(from.empty())return;
  const juce::ScopedLock lock(audioLock);
  auto snap=std::move(from.back());from.pop_back();
  if(snap.track<0||snap.track>=int(session.tracks.size()))return;
  auto& regions=session.tracks[size_t(snap.track)].midi;
  if(snap.region<0||snap.region>=int(regions.size()))return;
  into.push_back({snap.track,snap.region,regions[size_t(snap.region)].notes});
  regions[size_t(snap.region)].notes=std::move(snap.notes);
  selectedTrack=snap.track;selectedMidiRegion=snap.region;selectedNote=-1;
  updateControls();piano.repaint();arrangement.repaint();
 }
 void previewSample(juce::File);
 void loadSampleInstrument(juce::File);
 void loadMultisampleInstrument(const juce::String&);
 void populateDrumKit(veysona::Track&, const juce::String&);
 void loadEffectPreset(juce::File);
 void addDrumRack(const juce::String& kit="Studio");
 void savePreferences();
 void showPreferences();
 void showWelcome();
 void showStartupSplash();
 void openStartupProject();
 void confirmDemoReplace();
 void showRecoveryMenu();
 void showQuickActions();
 void saveRecoverySnapshot();
 juce::File defaultAutosaveFolder() const;
 void chooseAutosaveFolder();
 void setDetailsVisible(bool show);
 void showProductionTools();
 void fitSongToView();
 void refreshWorkspaceButtons();
 void applyAccent(int choice);
 void createBlankProject();
 void createAudioTrack();
 void addOutputBus();
 void copyClip();
 void pasteClip();
 void deleteClip();
 void saveAs();
 void saveCurrent();
 void exportStems(juce::File folder);
 void handleExternalMidi(const juce::MidiMessage& message);
 void toggleMidiArm();
 void loadPreferences();
 juce::File preferencesFile() const{
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
   .getChildFile("VeysonaStudio").getChildFile("preferences.json");
 }
 void mixPreview(float* const* outputs,int channels,int frames);
 void refreshBrowser();
 void openAudio(juce::File f,double atBeat=0);
 void addMidiTrack();
 void loadDemo();
 void rememberAudioEdit();
 void audioHistory(bool undo);
 void analyseCurrentClip();
 void sliceCurrentClip();
 void resampleCurrentClip();
 void masterSelectedClip();
 void exportMastered(juce::File f);
 void generateChordProgression();
 void generateBassline();
 void generateDrumPattern();
 void showKeyEstimate();
 void showAudioDiagnostics();
 void showProjectHealth();
 void showTemplateChooser();
 void createFromTemplate(veysona::workflow::Template kind);
 void audioToMidi();
 void bounceSelectedTrack();
 void wavetablePreset(int preset);
 void showRecentProjects();
 void rememberRecentProject(juce::File file);
 void setAutomationCurve(bool smooth);
 void applyGroovePreset(veysona::groove::Style style);
 bool saveProject(juce::File f,bool recovery=false);
 void loadProject(juce::File f,bool recovery=false);
 void importMidi(juce::File f);
 void exportMidi(juce::File f);
 bool renderExport(juce::File f,veysona::WavFormat format=veysona::WavFormat::PCM16);
 void loadPlugin(juce::File file);
 void restoreSavedPlugins();
 void updateControls();
 void prepareEngine(){engine.prepare(session.tracks.size(),session.sampleRate);launcher.prepare(session.tracks.size());plugins.resize(session.tracks.size());pluginMidiEnds.assign(session.tracks.size(),-1);}
 void selectTrack(int t,int c=-1){eqInputWrite.store(0);for(auto& slot:eqInputRing)slot.store(0.f);arrangement.setSize(arrangement.contentWidth(),std::max(timelineViewport.getHeight(),36+int(session.tracks.size())*73));selectedTrack=std::clamp(t,0,std::max(0,int(session.tracks.size())-1));selectedClip=c;selectedMidiRegion=0;selectedNote=-1;updateControls();piano.focusOnNotes();mixer.setSize(std::max(600,int(session.tracks.size()+session.buses.size()+1)*150+16),227);arrangement.repaint();piano.repaint();mixer.repaint();eqView.repaint();}
 juce::File audioDeviceStateFile() const {
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
   .getChildFile("Veysona Studio").getChildFile("audio-device.xml");
 }
 void saveAudioDeviceState(){
  auto xml=devices.createStateXml();if(!xml)return;
  const auto f=audioDeviceStateFile();
  if(f.getParentDirectory().createDirectory().wasOk())xml->writeTo(f);
 }
 void changeListenerCallback(juce::ChangeBroadcaster* broadcaster) override {
  if(broadcaster==&devices)saveAudioDeviceState();
 }
 void configureDevice(){
  juce::DialogWindow::LaunchOptions opt;
  auto comp=std::make_unique<juce::AudioDeviceSelectorComponent>(devices,0,16,2,16,true,false,true,false);
  comp->setSize(610,470);opt.content.setOwned(comp.release());
  opt.dialogTitle="Veysona  |  Audio Settings (ASIO / WASAPI / buffers)";
  opt.dialogBackgroundColour=C::panel;opt.launchAsync();
 }
 void timerCallback() override{
  arrangement.repaint();performanceGrid.repaint();mixer.repaint();repaint(masterArea);
  if(detailsVisible||expandedEQ!=nullptr){refreshAnalyser();if(detailsVisible)eqView.repaint();if(expandedEQ!=nullptr)expandedEQ->repaint();}
  auto now=juce::Time::currentTimeMillis();
  if(updateBusy && now-updateRequestStartMillis > (updateIsDownload?1800000:90000)){
   updateBusy=false;
   splashUpdateMessage("Update check timed out");
   setStatus("Updater timed out. You can retry from Help → Check for Updates.");
  }
  if(updateBusy && now-lastUpdatePoll>=500){lastUpdatePoll=now;pollUpdateStatus();}
  if(recordingOverrun.load()&&!recordOverrunReported){recordOverrunReported=true;setStatus("Recording interrupted: audio buffer or disk error. Finish Take to discard incomplete audio.");}
  if(!pluginFaultReported){for(size_t i=0;i<pluginTrackFaults.size();++i)if(pluginTrackFaults[i].load()){pluginFaultReported=true;setStatus("VST3 processing error on track "+juce::String(int(i+1))+"; track inserts bypassed. Save and restart the session.");break;}}
  if(!autosaving && veysona::recovery::shouldSave(now,lastAutosaveTick,autosaveMinutes,
      autosaveEnabled,playing.load(),recording.load())) saveRecoverySnapshot();
  auto p=playCursor.load();double beats=double(p)/std::max(1u,session.sampleRate)*session.bpm/60.;
  const int bar=std::max(1,int(beats/session.beatsPerBar)+1);
  const double beatWithinBar=std::fmod(beats,session.beatsPerBar)+1.;
  const juce::String transport=juce::String(recording.load()?"REC":(playing.load()?"PLAY":"STOP"))+juce::String("  |  ")+
    juce::String("Bar ")+juce::String(bar)+juce::String("  Beat ")+juce::String(beatWithinBar,2)+
    juce::String("  |  ")+juce::String(session.bpm,0)+juce::String(" BPM  |  ")+
    juce::String(session.sampleRate/1000.0,1)+juce::String(" kHz");
  transportStatus.setText(transport,juce::dontSendNotification);
  if(followPlayhead && playing.load() && !sessionMode && timelineViewport.isVisible()){
   const int playX=int(180+beats/session.beatsPerBar*pixelsPerBar);
   const int viewLeft=timelineViewport.getViewPositionX();
   const int visibleWidth=timelineViewport.getViewWidth();
   if(playX>viewLeft+visibleWidth-95 || playX<viewLeft+180){
    timelineViewport.setViewPosition(veysona::ui::nextFollowScroll(playX,viewLeft,visibleWidth),timelineViewport.getViewPositionY());
   }
  }
 }
 void paint(juce::Graphics&) override;
 void resized() override;
 bool keyPressed(const juce::KeyPress&) override;
 int getNumRows() override{return browserItems.size();}
 void paintListBoxItem(int row,juce::Graphics& g,int w,int h,bool selected) override{
  if(row<0||row>=browserItems.size())return;
  if(selected){g.setColour(C::purple.withAlpha(.23f));g.fillRoundedRectangle(4.f,2.f,float(w-8),float(h-4),5.f);
   g.setColour(C::purple);g.fillRoundedRectangle(4.f,5.f,3.f,float(h-10),1.5f);}
  else if(row%2){g.setColour(C::ink.withAlpha(.019f));g.fillRect(0,0,w,h);}
  const auto label=browserItems[row];
  const bool midi=label.endsWithIgnoreCase(".mid")||label.endsWithIgnoreCase(".midi");
  const bool audio=label.endsWithIgnoreCase(".wav")||label.endsWithIgnoreCase(".flac")||label.endsWithIgnoreCase(".mp3");
  const juce::Colour dot=midi?C::gold:audio?C::aqua:C::purple;
  g.setColour(dot.withAlpha(.18f));g.fillRoundedRectangle(13.f,float(h/2-9),18.f,18.f,4.f);
  g.setColour(dot);g.fillEllipse(20.f,float(h/2-3),6.f,6.f);
  g.setColour(selected?C::ink:C::ink.withAlpha(.91f));g.setFont(juce::Font(12.5f));
  g.drawFittedText(label,39,0,std::max(12,w-49),h,juce::Justification::centredLeft,1,.86f);
 }
 void listBoxItemDoubleClicked(int row,const juce::MouseEvent&) override;
 void listBoxItemClicked(int row,const juce::MouseEvent&) override;
 bool isInterestedInFileDrag(const juce::StringArray& paths) override {
  for(const auto& p:paths)if(juce::File(p).hasFileExtension("wav;mp3;flac;ogg;aiff;mid;midi"))return true;
  return false;
 }
 void filesDropped(const juce::StringArray& paths,int x,int y) override {
  double beat=0;
  if(x>=180&&x<(getWidth()-(browserVisible?browserDockWidth:0))&&y>=136&&y<getHeight()-27-(detailsVisible?editorDockHeight:0)){
   beat=std::max(0.,(x-180+timelineViewport.getViewPositionX())/pixelsPerBar*session.beatsPerBar);
   beat=veysona::midi::snap(beat,.25);
  }
  for(const auto& path:paths){auto f=juce::File(path);
   if(f.hasFileExtension("mid;midi"))importMidi(f);
   else if(f.hasFileExtension("wav;mp3;flac;ogg;aiff"))openAudio(f,beat);
  }
 }
 void audioCallback(const float* const* inputs,int inputChannels,float* const* outputs,int outputChannels,int frames);
 struct MidiCallback final : juce::MidiInputCallback{
  Studio& owner;explicit MidiCallback(Studio& s):owner(s){}
  void handleIncomingMidiMessage(juce::MidiInput*,const juce::MidiMessage& msg) override{
   // MIDI monitoring must not depend on the UI thread or transport state.
   // The recorder still receives its separate message-thread copy below.
   if(owner.midiArmed.load(std::memory_order_relaxed))
    owner.livePluginMidi.addMessageToQueue(msg);
   juce::Component::SafePointer<Studio> safe(&owner);
   juce::MessageManager::callAsync([safe,msg]{if(safe!=nullptr)safe->handleExternalMidi(msg);});
  }
 } midiCallback{*this};
 struct Callback final : juce::AudioIODeviceCallback{
  Studio& owner;explicit Callback(Studio& s):owner(s){}
  void audioDeviceIOCallbackWithContext(const float* const* in,int inCh,float* const* out,int outCh,int frames,const juce::AudioIODeviceCallbackContext&) override{
   const auto start=juce::Time::getMillisecondCounterHiRes();
   owner.audioCallback(in,inCh,out,outCh,frames);
   const auto elapsed=juce::Time::getMillisecondCounterHiRes()-start;
   owner.callbackMillis.store(float(elapsed),std::memory_order_relaxed);
   if(frames>0&&owner.audioRate>0&&elapsed>frames*1000./owner.audioRate*.9)
    owner.slowAudioCallbacks.fetch_add(1,std::memory_order_relaxed);
  }
  void audioDeviceAboutToStart(juce::AudioIODevice* d) override{
   if(!d)return;const juce::ScopedLock lock(owner.audioLock);
   owner.audioRate=d->getCurrentSampleRate();owner.audioBlock=d->getCurrentBufferSizeSamples();owner.session.sampleRate=uint32_t(std::max(8000.,owner.audioRate));owner.engine.prepare(owner.session.tracks.size(),owner.session.sampleRate);
   owner.livePluginMidi.reset(owner.audioRate);
   for(auto& chain:owner.plugins)for(auto& p:chain)if(p){p->releaseResources();p->prepareToPlay(owner.audioRate,owner.audioBlock);}
  }
  void audioDeviceStopped() override {}
 } callback{*this};
public:
 Studio();~Studio() override;
 juce::StringArray getMenuBarNames() override;
 juce::PopupMenu getMenuForIndex(int index,const juce::String&) override;
 void menuItemSelected(int itemId,int index) override;
};

// Fully functional native menu bar. Command IDs are intentionally stable for future key-mapping.
enum MenuCommand {
 fileNew=101,fileOpen,fileSave,fileSaveAs,fileImportAudio,fileImportMidi,fileExportAudio,fileExportMidi,fileExportStems,fileQuit,fileExportMastered,fileRecovery,fileSaveRecovery,fileOpenRecent,fileRecentMenu,
 editCopy=201,editCut,editPaste,editDelete,editDuplicate,editUndoMidi,editRedoMidi,editQuantize,editRename,
 createAudio=301,createMidi,createDrums,createSampler,createBus,createTemplate,
 viewArrangement=401,viewSession,viewMixer,viewPiano,viewAutomation,viewZoomIn,viewZoomOut,viewDetails,viewFitSong,viewSnap,viewFollow,viewBrowser,viewResetLayout,
 optionsPrefs=501,optionsAudio,optionsPluginScan,optionsAdvanced,optionsMetronome,optionsLoop,optionsAutosave,optionsAutosaveFolder,optionsWelcome,optionsRestorePluginChain,
 toolClipAnalysis=701,toolSlice,toolResample,toolMasterClip,toolUndoAudio,toolRedoAudio,
 toolChordProgression,toolBassline,toolDrumPattern,toolFindKey,toolGrooveHouse,toolGrooveGarage,toolGrooveBreakbeat,toolGrooveTrap,toolProjectHealth,toolAudioToMidi,toolBounce,toolWavetable,toolAutomationSmooth,toolAutomationLinear,
 helpShortcuts=601,helpAbout,helpAudioDiagnostics,helpCheckUpdates
};

juce::StringArray Studio::getMenuBarNames(){return {"File","Edit","Add","View","Settings","Process","Help"};}
juce::PopupMenu Studio::getMenuForIndex(int index,const juce::String&){
 juce::PopupMenu menu;
 switch(index){
 case 0:
  menu.addSectionHeader("PROJECT");
  menu.addItem(fileNew,"New Project  (Ctrl+N)");
  menu.addItem(fileOpen,"Open Project...  (Ctrl+O)");
  menu.addItem(fileSave,"Save  (Ctrl+S)");
  menu.addItem(fileSaveAs,"Save As...  (Ctrl+Shift+S)");
  menu.addSeparator();menu.addSectionHeader("RECENT & RECOVERY");
  menu.addItem(fileRecentMenu,"Recent Projects...");
  menu.addItem(fileOpenRecent,"Reopen Last Project",lastOpenedProject.existsAsFile());
  menu.addItem(fileRecovery,"Open Recovery Snapshot...");
  menu.addItem(fileSaveRecovery,"Save Recovery Snapshot Now");
  menu.addSeparator();menu.addSectionHeader("IMPORT");
  menu.addItem(fileImportAudio,"Import Audio...  (Ctrl+I)");
  menu.addItem(fileImportMidi,"Import MIDI...");
  menu.addSeparator();menu.addSectionHeader("EXPORT");
  menu.addItem(fileExportAudio,"Export Mix...  (Ctrl+E)");
  menu.addItem(fileExportMidi,"Export MIDI...");
  menu.addItem(fileExportStems,"Export Stems...");
  menu.addItem(fileExportMastered,"Export Mastered WAV (24-bit)...");
  menu.addSeparator();menu.addItem(fileQuit,"Quit Veysona");break;
 case 1:
  menu.addSectionHeader("HISTORY");
  menu.addItem(editUndoMidi,"Undo MIDI Edit  (Ctrl+Z)",!midiUndo.empty());
  menu.addItem(editRedoMidi,"Redo MIDI Edit  (Ctrl+Y)",!midiRedo.empty());
  menu.addItem(toolUndoAudio,"Undo Audio Edit",!audioUndo.empty());
  menu.addItem(toolRedoAudio,"Redo Audio Edit",!audioRedo.empty());
  menu.addSeparator();menu.addSectionHeader("CLIPS");
  menu.addItem(editCopy,"Copy Clip  (Ctrl+C)");
  menu.addItem(editCut,"Cut Clip  (Ctrl+X)");
  menu.addItem(editPaste,"Paste at Playhead  (Ctrl+V)",clipboardAudio.has_value()||clipboardMidi.has_value());
  menu.addItem(editDuplicate,"Duplicate Clip  (Ctrl+D)");
  menu.addItem(editDelete,"Delete Selected Clip");
  menu.addSeparator();menu.addItem(editQuantize,"Quantise MIDI  (Ctrl+Q)");
  menu.addItem(editRename,"Rename Track...");break;
 case 2:
  menu.addSectionHeader("TRACKS & INSTRUMENTS");
  menu.addItem(createAudio,"Audio Track");
  menu.addItem(createMidi,"MIDI Track  (Ctrl+M)");
  menu.addItem(createDrums,"Drum Rack");
  menu.addItem(createSampler,"Sample Instrument...");
  menu.addItem(createBus,"Group / Output Bus");
  menu.addSeparator();menu.addItem(createTemplate,"Create from Template...  (Ctrl+T)");break;
 case 3:
  menu.addSectionHeader("WORKSPACE");
  menu.addItem(viewArrangement,"Arrangement",true,!sessionMode);
  menu.addItem(viewSession,"Session  (Ctrl+L)",true,sessionMode);
  menu.addSeparator();menu.addSectionHeader("EDITOR");
  menu.addItem(viewPiano,"Piano Roll",true,!mixerMode);
  menu.addItem(viewMixer,"Mixer",true,mixerMode);
  menu.addItem(viewAutomation,"Toggle Automation Lane",true,automationMode);
  menu.addItem(viewDetails,"Show Lower Editor",true,detailsVisible);
  menu.addItem(viewBrowser,"Show Browser  (Ctrl+Shift+B)",true,browserVisible);
  menu.addSeparator();menu.addSectionHeader("TIMELINE");
  menu.addItem(viewZoomIn,"Zoom In");menu.addItem(viewZoomOut,"Zoom Out");
  menu.addItem(viewFitSong,"Fit Song  (F)");
  menu.addItem(viewSnap,"Snap to Grid  (G)",true,snapEnabled);
  menu.addItem(viewFollow,"Follow Playhead",true,followPlayhead);
  menu.addSeparator();menu.addItem(viewResetLayout,"Reset Workspace Layout");break;
 case 4:
  menu.addSectionHeader("CONFIGURATION");
  menu.addItem(optionsPrefs,"Preferences...  (Ctrl+P)");
  menu.addItem(optionsAudio,"Audio Device Settings...");
  menu.addItem(optionsPluginScan,"VST3 Plug-ins...");
  menu.addItem(optionsRestorePluginChain,"Restore Saved VST3 Chains...");
  menu.addItem(optionsAdvanced,"Advanced Production Tools...");
  menu.addSeparator();menu.addSectionHeader("PLAYBACK");
  menu.addItem(optionsMetronome,"Metronome",true,metronome.load());
  menu.addItem(optionsLoop,"Loop Arrangement",true,looping);
  menu.addSeparator();menu.addSectionHeader("RECOVERY");
  menu.addItem(optionsAutosave,"Autosave Snapshots",true,autosaveEnabled);
  menu.addItem(optionsAutosaveFolder,"Choose Autosave Folder...");
  menu.addSeparator();menu.addItem(optionsWelcome,"Welcome Screen...");break;
 case 5:
  menu.addSectionHeader("AUDIO CLIP");
  menu.addItem(toolClipAnalysis,"Analyse BPM / Loudness / True Peak...");
  menu.addItem(toolSlice,"Slice at Transients");
  menu.addItem(toolResample,"Consolidate to Project Sample Rate");
  menu.addItem(toolMasterClip,"Normalise to -14 LUFS / -1 dBTP");
  menu.addSeparator();menu.addSectionHeader("MIDI GENERATION");
  menu.addItem(toolChordProgression,"Generate I-V-vi-IV Chords");
  menu.addItem(toolBassline,"Generate Bassline from Chords");
  menu.addItem(toolDrumPattern,"Generate 4-Bar Drum Pattern");
  menu.addItem(toolFindKey,"Estimate MIDI Key...");
  menu.addItem(toolAudioToMidi,"Audio to MIDI (Experimental)...");
  menu.addSeparator();menu.addSectionHeader("TRACK PROCESSING");
  menu.addItem(toolBounce,"Bounce Selected Track to Audio...");
  menu.addItem(toolWavetable,"Veysona Wave Presets...");
  menu.addItem(toolAutomationSmooth,"Smooth Automation Curves");
  menu.addItem(toolAutomationLinear,"Linear Automation Curves");
  menu.addSeparator();menu.addSectionHeader("GROOVE LAB");
  menu.addItem(toolGrooveHouse,"House (4 bars)");
  menu.addItem(toolGrooveGarage,"UK Garage (4 bars)");
  menu.addItem(toolGrooveBreakbeat,"Breakbeat (4 bars)");
  menu.addItem(toolGrooveTrap,"Trap (4 bars)");
  menu.addSeparator();menu.addItem(toolProjectHealth,"Project Health Check...");break;
 case 6:
  menu.addSectionHeader("FIND YOUR WAY");
  menu.addItem(helpShortcuts,"Keyboard Shortcuts");
  menu.addItem(optionsWelcome,"Welcome Screen...");
  menu.addItem(helpCheckUpdates,"Check for Updates...");
  menu.addSeparator();menu.addSectionHeader("DIAGNOSTICS");
  menu.addItem(helpAudioDiagnostics,"Audio Engine Diagnostics...");
  menu.addItem(toolProjectHealth,"Project Health Check...");
  menu.addSeparator();menu.addItem(helpAbout,"About Veysona Studio");break;
 }
 return menu;
}
void Studio::menuItemSelected(int itemId,int){
 switch(itemId){
 case fileNew:createBlankProject();break;
 case fileOpen:openButton.triggerClick();break;
 case fileSave:saveCurrent();break;
 case fileSaveAs:saveAs();break;
 case fileRecovery:showRecoveryMenu();break;
 case fileSaveRecovery:saveRecoverySnapshot();break;
 case fileOpenRecent:if(lastOpenedProject.existsAsFile())loadProject(lastOpenedProject);break;
 case fileRecentMenu:showRecentProjects();break;
 case fileImportAudio:importButton.triggerClick();break;
 case fileImportMidi:midiImportButton.triggerClick();break;
 case fileExportAudio:exportButton.triggerClick();break;
 case fileExportMidi:midiExportButton.triggerClick();break;
 case fileExportStems:choose(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,"Export stems to folder",[this](juce::File f){exportStems(f);});break;
 case fileExportMastered:choose(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles,
  "Export loudness-matched 24-bit WAV",[this](juce::File f){exportMastered(f);},"*.wav");break;
 case fileQuit:juce::JUCEApplication::getInstance()->systemRequestedQuit();break;
 case editCopy:copyClip();break;
 case editCut:copyClip();deleteClip();break;
 case editPaste:pasteClip();break;
 case editDelete:deleteClip();break;
 case editDuplicate:keyPressed(juce::KeyPress('D',juce::ModifierKeys::commandModifier,0));break;
 case editUndoMidi:midiHistory(true);break;
 case editRedoMidi:midiHistory(false);break;
 case editQuantize:quantizeButton.triggerClick();break;
 case editRename:trackName.showEditor();break;
 case createAudio:createAudioTrack();break;
 case createMidi:midiButton.triggerClick();break;
 case createDrums:drumButton.triggerClick();break;
 case createSampler:samplerButton.triggerClick();break;
 case createBus:addOutputBus();break;
 case createTemplate:showTemplateChooser();break;
 case viewArrangement:tabArrangement.triggerClick();break;
 case viewSession:tabSession.triggerClick();break;
 case viewMixer:tabMixer.triggerClick();break;
 case viewPiano:tabPiano.triggerClick();break;
 case viewAutomation:automationButton.triggerClick();break;
 case viewDetails:setDetailsVisible(!detailsVisible);break;
 case viewZoomIn:zoom.setValue(std::min(215.,zoom.getValue()+15));break;
 case viewZoomOut:zoom.setValue(std::max(65.,zoom.getValue()-15));break;
 case viewFitSong:fitSongToView();break;
 case viewSnap:snapButton.triggerClick();break;
 case viewBrowser:browserToggle.triggerClick();break;
 case viewResetLayout:
  browserDockWidth=294;editorDockHeight=385;pianoDockFraction=.42f;instrumentDockFraction=.22f;
  browserVisible=true;detailsVisible=true;resized();repaint();savePreferences();
  setStatus("Workspace panels reset to the default sizes");break;
 case viewFollow:followButton.triggerClick();break;
 case optionsPrefs:showPreferences();break;
 case optionsAudio:configureDevice();break;
 case optionsPluginScan:pluginButton.triggerClick();break;
 case optionsRestorePluginChain:restoreSavedPlugins();break;
 case optionsAdvanced:showProductionTools();break;
 case optionsMetronome:metronomeButton.triggerClick();break;
 case optionsLoop:loopButton.triggerClick();break;
 case optionsAutosave:autosaveEnabled=!autosaveEnabled;savePreferences();setStatus(autosaveEnabled?"Autosave enabled":"Autosave disabled");break;
 case optionsAutosaveFolder:chooseAutosaveFolder();break;
 case optionsWelcome:showWelcome();break;
 case toolClipAnalysis:analyseCurrentClip();break;
 case toolSlice:sliceCurrentClip();break;
 case toolResample:resampleCurrentClip();break;
 case toolMasterClip:masterSelectedClip();break;
 case toolUndoAudio:audioHistory(true);break;
 case toolRedoAudio:audioHistory(false);break;
 case toolChordProgression:generateChordProgression();break;
 case toolBassline:generateBassline();break;
 case toolDrumPattern:generateDrumPattern();break;
 case toolFindKey:showKeyEstimate();break;
 case toolGrooveHouse:applyGroovePreset(veysona::groove::Style::House);break;
 case toolGrooveGarage:applyGroovePreset(veysona::groove::Style::Garage);break;
 case toolGrooveBreakbeat:applyGroovePreset(veysona::groove::Style::Breakbeat);break;
 case toolGrooveTrap:applyGroovePreset(veysona::groove::Style::Trap);break;
 case toolProjectHealth:showProjectHealth();break;
 case toolAudioToMidi:audioToMidi();break;
 case toolAutomationSmooth:setAutomationCurve(true);break;
 case toolAutomationLinear:setAutomationCurve(false);break;
 case toolBounce:bounceSelectedTrack();break;
 case toolWavetable:{juce::PopupMenu menu;menu.addSectionHeader("Veysona Wave: morph presets");
   menu.addItem("Silky sine",[this]{wavetablePreset(0);});
   menu.addItem("Warm analogue",[this]{wavetablePreset(1);});
   menu.addItem("Aggressive lead",[this]{wavetablePreset(2);});
   menu.addItem("Hollow keys",[this]{wavetablePreset(3);});
   menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&moreButton));break;}
 case helpCheckUpdates:checkForUpdates(false);break;
 case helpAudioDiagnostics:showAudioDiagnostics();break;
 case helpShortcuts:alert("Keyboard shortcuts","Space  Play / Pause\nCtrl+N  New project\nCtrl+O  Open project\nCtrl+S  Save project\nCtrl+Shift+S  Save as\nCtrl+I  Import audio\nCtrl+C / V / X  Copy, paste, cut clip\nCtrl+D  Duplicate audio clip\nCtrl+Z / Y  MIDI undo / redo\nCtrl+Q  Quantise MIDI\nCtrl+L  Session View\nCtrl+M  New MIDI track\nCtrl+P  Preferences\nCtrl+K  Quick actions\nCtrl+T  Project templates\nCtrl+B  Bounce built-in track\nCtrl+R  Recent projects\nCtrl+F  Find samples\nDelete  Remove selected audio clip");break;
 case helpAbout:{
  auto artwork=sampleFolder.getParentDirectory().getChildFile("branding").getChildFile("Veysona-Logo.png");
  auto img=juce::ImageFileFormat::loadFrom(artwork);
  if(!img.isValid()){alert("Veysona Studio 1.7","Veysona Studio Development Preview - C++20 / JUCE 8 / VST3");break;}
  auto display=std::make_unique<juce::ImageComponent>();display->setImage(img,juce::RectanglePlacement::centred);display->setSize(440,440);
  juce::DialogWindow::LaunchOptions options;options.content.setOwned(display.release());
  options.dialogTitle="About Veysona Studio 1.7";options.dialogBackgroundColour=C::bg;options.launchAsync();break;
 }
 }
}

// Keep legacy .odaw project support, but use .veys for new saves.
void Studio::saveAs(){
 choose(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::warnAboutOverwriting,
  "Save Veysona project",[this](juce::File file){saveProject(file.withFileExtension("veys"));},"*.veys");
}
void Studio::saveCurrent(){if(currentProject.existsAsFile())saveProject(currentProject);else saveAs();}

void Studio::createBlankProject(){
 auto opt=juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon)
   .withTitle("New Veysona project")
   .withMessage("Start an empty project? Unsaved edits in the current session will be lost.")
   .withButton("Create").withButton("Cancel");
 juce::AlertWindow::showAsync(opt,[this](int result){
  if(result!=1)return;
  {
   const juce::ScopedLock lock(audioLock);
   playing=false;recording=false;session.tracks.clear();session.buses.clear();
   session.bpm=120;session.loopStartBeat=0;session.loopEndBeat=0;
   plugins.clear();engine.prepare(0,session.sampleRate);launcher.prepare(0);
   playCursor=0;currentProject=juce::File{};midiUndo.clear();midiRedo.clear();audioUndo.clear();audioRedo.clear();eqHistory.clear();
   recoverySessionId=juce::String::toHexString(juce::Time::currentTimeMillis());recoverySlot=0;
  }
  clipboardAudio.reset();clipboardMidi.reset();selectedTrack=0;selectedClip=-1;selectedMidiRegion=0;
  createAudioTrack();bpm.setValue(120,juce::dontSendNotification);
  setStatus("New project  -  save as a .veys file");
 });
}
void Studio::createAudioTrack(){
 int target=0;
 {
  const juce::ScopedLock lock(audioLock);
  if(session.tracks.size()>=128){alert("Track limit","The current engine supports up to 128 tracks.");return;}
  veysona::Track t;t.name="Audio " + std::to_string(session.tracks.size()+1);
  session.tracks.push_back(std::move(t));plugins.emplace_back();
  engine.prepare(session.tracks.size(),session.sampleRate);
  launcher.prepare(session.tracks.size());target=int(session.tracks.size())-1;
 }
 selectTrack(target);setStatus("Added audio track");
}
void Studio::addOutputBus(){
 {
  const juce::ScopedLock lock(audioLock);
  if(session.buses.size()>=veysona::kMaxBuses){alert("Bus limit","Up to eight routed buses are supported in this engine.");return;}
  veysona::Bus b;b.name="Bus "+std::to_string(session.buses.size()+1);
  session.buses.push_back(std::move(b));
 }
 setStatus("Bus created. Right-click a track header to route its output to the bus.");
}
void Studio::copyClip(){
 const juce::ScopedLock lock(audioLock);
 if(selectedTrack<0||selectedTrack>=int(session.tracks.size()))return;
 auto& track=session.tracks[size_t(selectedTrack)];
 if(selectedClip>=0&&selectedClip<int(track.clips.size())){
  clipboardAudio=track.clips[size_t(selectedClip)];clipboardMidi.reset();
  setStatus("Audio clip copied  -  paste at the playhead");return;
 }
 if(selectedMidiRegion>=0&&selectedMidiRegion<int(track.midi.size())){
  clipboardMidi=track.midi[size_t(selectedMidiRegion)];clipboardAudio.reset();
  setStatus("MIDI region copied  -  paste at the playhead");
 }
}
void Studio::pasteClip(){
 if(selectedTrack<0||selectedTrack>=int(session.tracks.size()))return;
 const auto beat=std::max(0.,double(playCursor.load())/std::max(1u,session.sampleRate)*session.bpm/60.);
 {
  const juce::ScopedLock lock(audioLock);
  auto& track=session.tracks[size_t(selectedTrack)];
  if(clipboardAudio){auto c=*clipboardAudio;c.startBeat=beat;track.clips.push_back(std::move(c));selectedClip=int(track.clips.size())-1;}
  else if(clipboardMidi){auto m=*clipboardMidi;m.startBeat=beat;track.midi.push_back(std::move(m));selectedMidiRegion=int(track.midi.size())-1;selectedClip=-1;}
  else return;
 }
 arrangement.repaint();piano.repaint();setStatus("Pasted clip at playhead");
}
void Studio::deleteClip(){
 if(selectedTrack<0||selectedTrack>=int(session.tracks.size()))return;
 {
  const juce::ScopedLock lock(audioLock);
  auto& track=session.tracks[size_t(selectedTrack)];
  if(selectedClip>=0&&selectedClip<int(track.clips.size())){track.clips.erase(track.clips.begin()+selectedClip);selectedClip=-1;}
  else if(selectedMidiRegion>=0&&selectedMidiRegion<int(track.midi.size())){track.midi.erase(track.midi.begin()+selectedMidiRegion);selectedMidiRegion=0;}
  else return;
 }
 arrangement.repaint();piano.repaint();setStatus("Selected clip deleted");
}

// Owner container: each Settings tab destroys its child controls with the dialog.
class VeysonaSettingsPage final : public juce::Component {
 std::vector<std::unique_ptr<juce::Component>> controls;
public:
 template<typename T> T* own(std::unique_ptr<T> c){auto* ptr=c.get();addAndMakeVisible(*ptr);controls.emplace_back(std::move(c));return ptr;}
 juce::Label* heading(const juce::String& value,int y){
  auto* label=own(std::make_unique<juce::Label>());
  label->setText(value,juce::dontSendNotification);label->setFont(juce::Font(18.f));
  label->setColour(juce::Label::textColourId,C::ink);label->setBounds(20,y,690,32);return label;
 }
 juce::Label* note(const juce::String& value,int y){
  auto* label=own(std::make_unique<juce::Label>());
  label->setText(value,juce::dontSendNotification);label->setFont(juce::Font(13.f));
  label->setColour(juce::Label::textColourId,C::muted);label->setBounds(20,y,680,30);return label;
 }
};

void Studio::applyAccent(int choice){
 accentPreset=std::clamp(choice,0,2);
 C::purple=accentPreset==1?juce::Colour(0xff1fcac8):accentPreset==2?juce::Colour(0xfff1f5fc):juce::Colour(0xff2c9eff);
 theme.setColour(juce::PopupMenu::highlightedBackgroundColourId,C::purple.withAlpha(.24f));
 for(auto* sl:{&bpm,&zoom,&volume,&pan,&cutoff,&drive,&delay,&compThreshold,&compRatio,&masterGain}){
  sl->setColour(juce::Slider::thumbColourId,C::purple);sl->setColour(juce::Slider::trackColourId,C::purple);
 }
 for(auto* b:{&browserSamplesTab,&browserPluginsTab,&browserToggle,&snapButton,&fitButton,&followButton,&playButton,&stopButton,&recordButton,&loopButton,&importButton,&midiButton,&demoButton,
  &saveButton,&openButton,&exportButton,&pluginButton,&pluginEditorButton,&audioSettings,&addNoteButton,
  &tabPiano,&tabMixer,&tabArrangement,&tabSession,&stopScenes,&preferencesButton}){
  b->setColour(juce::TextButton::buttonOnColourId,C::purple);
 }
 repaint();arrangement.repaint();piano.repaint();mixer.repaint();eqView.repaint();menuBar.repaint();
}


// Veysona 1.4: offline production tools. Long analyses execute without holding audioLock.
// Selected clip edits stop playback and are recorded in a bounded undo stack.
void Studio::rememberAudioEdit(){
 const juce::ScopedLock lock(audioLock);
 if(selectedTrack<0||selectedTrack>=int(session.tracks.size()))return;
 const auto& clips=session.tracks[size_t(selectedTrack)].clips;
 size_t bytes=0;for(const auto& c:clips)bytes+=c.stereo.size()*sizeof(float);
 if(bytes>64*1024*1024){setStatus("Audio Undo disabled for tracks larger than 64 MB");return;}
 if(audioUndo.size()>=6)audioUndo.erase(audioUndo.begin());
 audioUndo.push_back({selectedTrack,clips});audioRedo.clear();
}
void Studio::audioHistory(bool undo){
 auto& source=undo?audioUndo:audioRedo;
 auto& destination=undo?audioRedo:audioUndo;
 if(source.empty())return;
 playing=false;
 int track=-1;
 {
  const juce::ScopedLock lock(audioLock);
  auto entry=std::move(source.back());source.pop_back();
  if(entry.track<0||entry.track>=int(session.tracks.size()))return;
  track=entry.track;
  destination.push_back({track,std::move(session.tracks[size_t(track)].clips)});
  session.tracks[size_t(track)].clips=std::move(entry.clips);
 }
 selectTrack(track);setStatus(undo?"Audio edit undone":"Audio edit redone");
}
void Studio::analyseCurrentClip(){
 veysona::Clip clip;
 {
  const juce::ScopedLock lock(audioLock);
  if(selectedTrack<0||selectedTrack>=int(session.tracks.size())){setStatus("Select a track with audio first");return;}
  const auto& clips=session.tracks[size_t(selectedTrack)].clips;
  if(selectedClip<0||selectedClip>=int(clips.size())){setStatus("Select an audio clip first");return;}
  clip=clips[size_t(selectedClip)];
 }
 try{
  const auto r=veysona::production::analyseLoop(clip.stereo,clip.sampleRate);
  juce::String text="Detected BPM: "+(r.estimatedBpm>0?juce::String(r.estimatedBpm,1):juce::String("Not enough rhythmic evidence"));
  text+="\nBPM confidence: "+juce::String(r.confidence*100.,0)+"%";
  text+="\nApprox. integrated loudness: "+juce::String(r.approximateLufs,1)+" LUFS";
  text+="\nEstimated true peak: "+juce::String(r.estimatedTruePeakDbfs,1)+" dBTP";
  text+="\nRMS: "+juce::String(r.rmsDbfs,1)+" dBFS";
  text+="\nStereo correlation: "+juce::String(r.stereoCorrelation,2)+" (-1 inverted, +1 mono)";
  text+="\nL/R balance: "+juce::String(r.channelBalanceDb,1)+" dB";
  if(r.stereoCorrelation<-.25)text+="\nWarning: strong anti-phase content may cancel in mono.";
  text+="\nTransients: "+juce::String(int(r.transientsSeconds.size()));
  text+="\nSpectral centroid: "+juce::String(r.spectralCentroidHz,0)+" Hz";
  text+="\n\nOffline estimates only; not a certified LUFS/true-peak meter.";
  alert("Veysona Audio Analysis  -  "+jstr(clip.name),text);
 }catch(const std::exception& ex){alert("Clip analysis failed",ex.what());}
}
void Studio::sliceCurrentClip(){
 veysona::Clip clip;int track=selectedTrack,index=selectedClip;
 {
  const juce::ScopedLock lock(audioLock);
  if(track<0||track>=int(session.tracks.size()))return;
  const auto& clips=session.tracks[size_t(track)].clips;
  if(index<0||index>=int(clips.size())){setStatus("Select an audio clip first");return;}
  clip=clips[size_t(index)];
 }
 try{
  auto slices=veysona::production::sliceClipAtTransients(clip,1.05,.11,64,session.bpm);
  if(slices.size()<2){setStatus("Not enough distinct transients to slice");return;}
  rememberAudioEdit();playing=false;
  {const juce::ScopedLock lock(audioLock);
   auto& clips=session.tracks[size_t(track)].clips;
   clips.erase(clips.begin()+index);
   clips.insert(clips.begin()+index,std::make_move_iterator(slices.begin()),std::make_move_iterator(slices.end()));
  }
  selectTrack(track,index);setStatus("Sliced audio at "+juce::String(int(slices.size())-1)+" transient boundaries (Undo Audio Edit in Edit menu)");
 }catch(const std::exception& e){alert("Transient slicing failed",e.what());}
}
void Studio::resampleCurrentClip(){
 veysona::Clip clip;int track=selectedTrack,index=selectedClip;
 {const juce::ScopedLock lock(audioLock);
  if(track<0||track>=int(session.tracks.size()))return;
  const auto& clips=session.tracks[size_t(track)].clips;
  if(index<0||index>=int(clips.size())){setStatus("Select an audio clip");return;}clip=clips[size_t(index)];}
 if(clip.sampleRate==session.sampleRate){setStatus("Clip already uses project sample rate");return;}
 try{
  const auto target=session.sampleRate;
  auto audio=veysona::production::resampleSinc(clip.stereo,clip.sampleRate,target);
  rememberAudioEdit();playing=false;
  {const juce::ScopedLock lock(audioLock);auto& c=session.tracks[size_t(track)].clips[size_t(index)];
   c.stereo=std::move(audio);c.sampleRate=target;c.sourcePath.clear();veysona::buildWaveformPeaks(c);}
  arrangement.repaint();setStatus("Consolidated audio to "+juce::String(int(target))+" Hz");
 }catch(const std::exception& e){alert("Resampling failed",e.what());}
}
void Studio::masterSelectedClip(){
 veysona::Clip clip;int track=selectedTrack,index=selectedClip;
 {const juce::ScopedLock lock(audioLock);
  if(track<0||track>=int(session.tracks.size()))return;
  const auto& clips=session.tracks[size_t(track)].clips;
  if(index<0||index>=int(clips.size())){setStatus("Select an audio clip first");return;}clip=clips[size_t(index)];}
 try{
  auto r=veysona::production::normalizeLoudnessOffline(clip.stereo,clip.sampleRate,-14.,-1.);
  rememberAudioEdit();playing=false;
  {const juce::ScopedLock lock(audioLock);auto& c=session.tracks[size_t(track)].clips[size_t(index)];
   c.stereo=std::move(clip.stereo);c.sourcePath.clear();veysona::buildWaveformPeaks(c);}
  arrangement.repaint();
  setStatus("Normalised clip "+juce::String(r.appliedGainDb,1)+" dB; approx. "+juce::String(r.outputLufs,1)+" LUFS (ceiling protected)");
 }catch(const std::exception& e){alert("Loudness matching failed",e.what());}
}
void Studio::exportMastered(juce::File file){
 if(!file.hasFileExtension(".wav"))file=file.withFileExtension(".wav");
 try{
  juce::TemporaryFile temporary(file);
  const auto staging=temporary.getFile();
  renderExport(staging,veysona::WavFormat::Float32);
  if(!staging.existsAsFile()){alert("Mastered export failed","Initial render did not produce an audio file");return;}
  auto clip=veysona::readWav(staging.getFullPathName().toStdString());
  const auto r=veysona::production::normalizeLoudnessOffline(clip.stereo,clip.sampleRate,-14.,-1.);
  veysona::writeWavAdvanced(staging.getFullPathName().toStdString(),clip.stereo,clip.sampleRate,veysona::WavFormat::PCM24);
  if(!temporary.overwriteTargetFileWithTemporary())throw std::runtime_error("Could not commit mastered file to its destination");
  setStatus("Exported 24-bit master: "+file.getFileName()+" | approx "+juce::String(r.outputLufs,1)+" LUFS, "+juce::String(r.outputTruePeak,1)+" dBTP");
 }catch(const std::exception& ex){alert("Mastered export failed",ex.what());}
}

void Studio::generateChordProgression(){
 if(!activeMidi()){setStatus("Select a MIDI clip to generate chords");return;}
 saveMidiUndo();
 {const juce::ScopedLock lock(audioLock);activeMidi()->notes=veysona::midi::chordProgression(60,{1,5,6,4},false,4.);activeMidi()->lengthBeats=16.;}
 piano.focusOnNotes();piano.repaint();arrangement.repaint();setStatus("Generated 4-chord C major progression (I - V - vi - IV)");
}
void Studio::generateBassline(){
 if(!activeMidi()){setStatus("Select a MIDI clip containing chords");return;}
 saveMidiUndo();
 {const juce::ScopedLock lock(audioLock);auto& notes=activeMidi()->notes;
  auto bass=veysona::midi::basslineFromChords(notes,.5,42);
  if(!bass.empty())notes=std::move(bass);}
 piano.focusOnNotes();piano.repaint();arrangement.repaint();setStatus("Generated bassline from chord roots");
}
void Studio::generateDrumPattern(){
 if(!activeMidi()){setStatus("Select a MIDI clip to generate drums");return;}
 saveMidiUndo();
 {const juce::ScopedLock lock(audioLock);activeMidi()->notes=veysona::midi::drumStepPattern(4,session.beatsPerBar,42,.55);activeMidi()->lengthBeats=4*session.beatsPerBar;}
 piano.focusOnNotes();piano.repaint();arrangement.repaint();setStatus("Generated 4-bar step drum sequence (MIDI channel 10)");
}
void Studio::showAudioDiagnostics(){
 auto* d=devices.getCurrentAudioDevice();
 juce::String data="Audio engine: "+juce::String(d?d->getName():"No current audio device");
 data+="\nSample rate: "+juce::String(audioRate,0)+" Hz";
 data+="\nBuffer size: "+juce::String(audioBlock)+" frames";
 data+="\nAudio backend: "+(d?d->getTypeName():juce::String("NONE"));
 data+="\nASIO compiled: "+juce::String(JUCE_ASIO?"yes":"no (WASAPI available)");
 data+="\nLast callback processing time: "+juce::String(callbackMillis.load(),3)+" ms";
 data+="\nCallbacks exceeding 90% buffer budget: "+juce::String(juce::int64(slowAudioCallbacks.load()));
 data+="\nAudio callback lock misses (silence): "+juce::String(juce::int64(audioLockMisses.load()));
 data+="\n\nThis panel reports local callback timings, not driver-level xrun telemetry.";
 alert("Veysona Audio Diagnostics",data);
}
void Studio::showKeyEstimate(){
 if(!activeMidi()){setStatus("Select a MIDI clip first");return;}
 std::vector<veysona::Note> notes;
 {const juce::ScopedLock lock(audioLock);notes=activeMidi()->notes;}
 if(notes.empty()){setStatus("No notes to analyse");return;}
 auto r=veysona::midi::estimateKey(notes);
 const char* names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
 alert("MIDI Scale Assistant",juce::String("Possible key: ")+names[r.root]+(r.minor?" minor":" major")+
       "\nConfidence: "+juce::String(r.confidence*100.,0)+"%\nStatistical estimate; ambiguous progressions can fit multiple keys.");
}

void Studio::showProductionTools(){
  juce::PopupMenu menu;menu.addSectionHeader("Session / playback");
  menu.addItem("Master peak limiter",true,session.master.limiterEnabled,[this]{{const juce::ScopedLock lock(audioLock);session.master.limiterEnabled=!session.master.limiterEnabled;}savePreferences();});
  menu.addItem("Metronome",true,metronome.load(),[this]{metronomeButton.triggerClick();});
  menu.addItem("Master gain: 100%",[this]{masterGain.setValue(1);savePreferences();});
  menu.addItem("Master gain: 75%",[this]{masterGain.setValue(.75);savePreferences();});
  menu.addItem("Master gain: 50%",[this]{masterGain.setValue(.5);savePreferences();});
  menu.addItem("Set loop range: bars 1 - 8",[this]{const juce::ScopedLock lock(audioLock);session.loopStartBeat=0;session.loopEndBeat=8*session.beatsPerBar;setStatus("Loop set to bars 1 - 8");});
  menu.addItem("Set loop range: bars 1 - 16",[this]{const juce::ScopedLock lock(audioLock);session.loopStartBeat=0;session.loopEndBeat=16*session.beatsPerBar;setStatus("Loop set to bars 1 - 16");});
  menu.addItem("Loop entire song",[this]{const juce::ScopedLock lock(audioLock);session.loopStartBeat=0;session.loopEndBeat=0;setStatus("Loop entire arrangement");});
  menu.addSeparator();
  menu.addItem("Create Group / Return Bus",[this]{
   const juce::ScopedLock lock(audioLock);
   if(session.buses.size()>=veysona::kMaxBuses){setStatus("Maximum eight buses");return;}
   veysona::Bus bus;bus.name="Bus "+std::to_string(session.buses.size()+1);
   session.buses.push_back(std::move(bus));
   setStatus("Created "+jstr(session.buses.back().name)+". Route tracks using right-click > Routing.");
  });
  menu.addItem("Set selected track routing...",[this]{
   if(selectedTrack>=0&&selectedTrack<int(session.tracks.size())){
    juce::PopupMenu routing;routing.addSectionHeader("Track output");
    routing.addItem("Master",[this]{const juce::ScopedLock lock(audioLock);session.tracks[size_t(selectedTrack)].outputBus=-1;});
    for(int i=0;i<int(session.buses.size());++i)routing.addItem(jstr(session.buses[size_t(i)].name),[this,i]{const juce::ScopedLock lock(audioLock);session.tracks[size_t(selectedTrack)].outputBus=i;});
    routing.addSeparator();routing.addSectionHeader("Auxiliary send levels");
    for(int i=0;i<int(session.buses.size());++i)for(float level:{0.f,.25f,.5f,1.f}){
     routing.addItem(jstr(session.buses[size_t(i)].name)+" send "+juce::String(int(level*100))+"%",[this,i,level]{const juce::ScopedLock lock(audioLock);session.tracks[size_t(selectedTrack)].sends[size_t(i)]=level;});
    }
    routing.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&preferencesButton));
   }
  });
  menu.addItem("Noise gate: toggle selected track",[this]{if(selectedTrack>=0&&selectedTrack<int(session.tracks.size())){const juce::ScopedLock lock(audioLock);auto& gate=session.tracks[size_t(selectedTrack)].gate;gate.enabled=!gate.enabled;setStatus(gate.enabled?"Noise gate ON (-43 dB)":"Noise gate OFF");}});
  menu.addItem("Stereo Width: Mono",[this]{if(selectedTrack>=0&&selectedTrack<int(session.tracks.size())){const juce::ScopedLock lock(audioLock);session.tracks[size_t(selectedTrack)].stereoWidth=0;}});
  menu.addItem("Stereo Width: Normal",[this]{if(selectedTrack>=0&&selectedTrack<int(session.tracks.size())){const juce::ScopedLock lock(audioLock);session.tracks[size_t(selectedTrack)].stereoWidth=1;}});
  menu.addItem("Stereo Width: Wide",[this]{if(selectedTrack>=0&&selectedTrack<int(session.tracks.size())){const juce::ScopedLock lock(audioLock);session.tracks[size_t(selectedTrack)].stereoWidth=1.5f;}});
  menu.addSeparator();
  juce::PopupMenu creative;
  auto updateCreative=[this](auto configure,const juce::String& label){
   if(selectedTrack<0||selectedTrack>=int(session.tracks.size()))return;
   const juce::ScopedLock lock(audioLock);
   configure(session.tracks[size_t(selectedTrack)].creativeFx);
   setStatus("FX: "+label+" on "+jstr(session.tracks[size_t(selectedTrack)].name));
  };
  creative.addItem("Clean / disable all",[updateCreative]{updateCreative([](auto& c){c={};},"clean");});
  creative.addItem("Wide stereo chorus",[updateCreative]{updateCreative([](auto& c){c.chorusEnabled=true;c.chorusMix=.52f;c.chorusDepthMs=4;c.chorusRateHz=.4f;},"chorus");});
  creative.addItem("Slow moving auto-pan",[updateCreative]{updateCreative([](auto& c){c.autopanEnabled=true;c.autopanDepth=.75f;c.autopanPeriodBeats=4.f;},"auto-pan");});
  creative.addItem("Chopping auto-pan",[updateCreative]{updateCreative([](auto& c){c.autopanEnabled=true;c.autopanDepth=1.f;c.autopanPeriodBeats=.5f;},"rhythmic pan");});
  creative.addItem("Lo-fi 8-bit effect",[updateCreative]{updateCreative([](auto& c){c.crusherEnabled=true;c.crusherBits=8;c.crusherHoldFrames=5;c.crusherMix=.8f;},"lo-fi");});
  creative.addItem("Classic 12-bit sampler",[updateCreative]{updateCreative([](auto& c){c.crusherEnabled=true;c.crusherBits=12;c.crusherHoldFrames=2;c.crusherMix=.5f;},"12-bit sampler");});
  creative.addSeparator();creative.addSectionHeader("Veysona 1.1 colour and modulation");
  creative.addItem("Warm tape saturation",[updateCreative]{updateCreative([](auto& c){c.tapeEnabled=true;c.tapeDrive=3.f;c.tapeMix=.52f;},"tape saturation");});
  creative.addItem("Heavy tape distortion",[updateCreative]{updateCreative([](auto& c){c.tapeEnabled=true;c.tapeDrive=9.f;c.tapeMix=.88f;},"heavy saturation");});
  creative.addItem("Gentle 2-beat tremolo",[updateCreative]{updateCreative([](auto& c){c.tremoloEnabled=true;c.tremoloDepth=.48f;c.tremoloPeriodBeats=2.f;},"tremolo");});
  creative.addItem("Chopped 1/8 tremolo",[updateCreative]{updateCreative([](auto& c){c.tremoloEnabled=true;c.tremoloDepth=.94f;c.tremoloPeriodBeats=.5f;},"rhythmic tremolo");});
  creative.addItem("Metallic ring mod 34 Hz",[updateCreative]{updateCreative([](auto& c){c.ringModEnabled=true;c.ringModFrequencyHz=34.f;c.ringModMix=.70f;},"ring modulator");});
  creative.addItem("Subtle ring mod 6 Hz",[updateCreative]{updateCreative([](auto& c){c.ringModEnabled=true;c.ringModFrequencyHz=6.f;c.ringModMix=.28f;},"subtle ring modulation");});
  creative.addSeparator();creative.addSectionHeader("Veysona 1.12 modulation and tone");
  creative.addItem("Stereo phaser - gentle",[updateCreative]{updateCreative([](auto& c){c.phaserEnabled=true;c.phaserRateHz=.24f;c.phaserDepth=.5f;c.phaserFeedback=.16f;c.phaserMix=.35f;},"gentle phaser");});
  creative.addItem("Stereo phaser - deep",[updateCreative]{updateCreative([](auto& c){c.phaserEnabled=true;c.phaserRateHz=.67f;c.phaserDepth=.92f;c.phaserFeedback=.48f;c.phaserMix=.75f;},"deep phaser");});
  creative.addItem("Tilt EQ - brighter",[updateCreative]{updateCreative([](auto& c){c.tiltEnabled=true;c.tiltDb=4.f;},"bright tilt");});
  creative.addItem("Tilt EQ - warmer",[updateCreative]{updateCreative([](auto& c){c.tiltEnabled=true;c.tiltDb=-4.f;},"warm tilt");});
  creative.addItem("Harmonic exciter - clarity",[updateCreative]{updateCreative([](auto& c){c.exciterEnabled=true;c.exciterDrive=4.f;c.exciterMix=.45f;},"exciter");});
  creative.addSeparator();creative.addSectionHeader("Veysona 1.25 sound design");
  creative.addItem("Harmonic comb resonator",[updateCreative]{updateCreative([](auto& f){f.resonatorEnabled=true;f.resonatorHz=196.f;f.resonatorFeedback=.76f;f.resonatorMix=.4f;},"comb resonator");});
  creative.addItem("Moving analogue wah",[updateCreative]{updateCreative([](auto& f){f.wahEnabled=true;f.wahRateHz=.54f;f.wahDepth=.86f;f.wahMix=.65f;},"auto-wah");});
  creative.addItem("Aggressive wavefolder",[updateCreative]{updateCreative([](auto& f){f.foldEnabled=true;f.foldDrive=4.2f;f.foldMix=.65f;},"wavefolder");});
  creative.addItem("Ping-pong dub echoes",[updateCreative]{updateCreative([](auto& f){f.pingPongEnabled=true;f.pingPongDelayMs=375.f;f.pingPongFeedback=.66f;f.pingPongMix=.44f;},"ping-pong delay");});
  creative.addSeparator();creative.addSectionHeader("Veysona 1.29 additional processors");
  creative.addItem("Asymmetric diode drive",[updateCreative]{updateCreative([](auto& f){f.diodeEnabled=true;f.diodeDrive=4.f;f.diodeMix=.65f;},"diode drive");});
  creative.addItem("Octave-up fuzz",[updateCreative]{updateCreative([](auto& f){f.octaveFuzzEnabled=true;f.octaveFuzzDrive=4.f;f.octaveFuzzMix=.6f;},"octave fuzz");});
  creative.addItem("Headphone crossfeed",[updateCreative]{updateCreative([](auto& f){f.crossfeedEnabled=true;f.crossfeedAmount=.18f;},"stereo crossfeed");});
  menu.addSubMenu("Creative effects (selected track)",creative);
  // Professional processors use the same track DSP chain as EQ and compression.
  juce::PopupMenu pro;
  auto configurePro=[this](auto apply,const juce::String& description){
   if(selectedTrack<0||selectedTrack>=int(session.tracks.size())){setStatus("Select a track first");return;}
   {const juce::ScopedLock lock(audioLock);apply(session.tracks[size_t(selectedTrack)]);}
   setStatus("Veysona Pro FX: "+description);
  };
  pro.addItem("Clean / disable Pro FX",[configurePro]{configurePro([](auto& t){t.room={};t.deEsser={};t.transient={};},"effects bypassed");});
  pro.addItem("Room reverb  -  small studio",[configurePro]{configurePro([](auto& t){t.room={true,.22f,.7f,.23f};},"small studio");});
  pro.addItem("Room reverb  -  large hall",[configurePro]{configurePro([](auto& t){t.room={true,.85f,.38f,.38f};},"large hall");});
  pro.addItem("Room reverb  -  wet ambient",[configurePro]{configurePro([](auto& t){t.room={true,.93f,.15f,.70f};},"wet ambient");});
  pro.addSeparator();
  pro.addItem("De-esser  -  vocal presence",[configurePro]{configurePro([](auto& t){t.deEsser={true,5600.f,-34.f,.75f};},"vocal de-esser");});
  pro.addItem("De-esser  -  soft cymbals",[configurePro]{configurePro([](auto& t){t.deEsser={true,7200.f,-30.f,.45f};},"soft cymbals");});
  pro.addSeparator();
  pro.addItem("Transient shaper  -  punchy drums",[configurePro]{configurePro([](auto& t){t.transient={true,.9f,-.25f};},"punchy drums");});
  pro.addItem("Transient shaper  -  smooth tails",[configurePro]{configurePro([](auto& t){t.transient={true,-.55f,.6f};},"smooth tails");});
  menu.addSubMenu("Pro effects  -  room, de-ess, transient",pro);
  juce::PopupMenu editor;
  editor.addItem("Spectrum analysis  -  selected audio clip...",[this]{
   veysona::Clip selected;
   {
    const juce::ScopedLock lock(audioLock);
    if(selectedTrack<0||selectedTrack>=int(session.tracks.size())){setStatus("Select an audio track and clip");return;}
    const auto& clips=session.tracks[size_t(selectedTrack)].clips;
    if(selectedClip<0||selectedClip>=int(clips.size())){setStatus("Select an audio clip for analysis");return;}
    selected=clips[size_t(selectedClip)];
   }
   if(selected.stereo.size()<128){setStatus("Clip is too short for spectrum analysis");return;}
   try{
    const auto n=size_t(4096);const auto start=std::min(selected.stereo.size()/4,selected.stereo.size()/2-1);
    const auto magnitudes=veysona::analyseSpectrum(selected.stereo,start,n);
    auto best=std::max_element(magnitudes.begin()+1,magnitudes.end());
    const auto bin=std::distance(magnitudes.begin(),best);
    const double hz=double(bin)*selected.sampleRate/n;
    alert("Veysona Spectrum Analysis", "Dominant spectral bin: "+juce::String(hz,1)+" Hz\nLevel: "+juce::String(*best,1)+" dBFS\n(Offline FFT; not a certified loudness meter)");
   }catch(const std::exception& ex){alert("Analysis error",ex.what());}
  });
  editor.addItem("Comp first two audio takes (50/50)...",[this]{
   if(selectedTrack<0||selectedTrack>=int(session.tracks.size())){setStatus("Select track with at least two takes");return;}
   try{
    veysona::Clip a,b;
    {
     const juce::ScopedLock lock(audioLock);
     const auto& t=session.tracks[size_t(selectedTrack)];
     if(t.clips.size()<2){setStatus("This operation needs two source clips on the selected track");return;}
     a=t.clips[0];b=t.clips[1];
    }
    const double end=std::min(double(a.stereo.size()/2)/a.sampleRate,double(b.stereo.size()/2)/b.sampleRate);
    if(end<.15){setStatus("These takes are too short to comp");return;}
    auto comp=veysona::compAudioTakes({a,b},{{0,0,end*.5},{1,end*.5,end}},.012);
    comp.name="Take Comp A+B";comp.startBeat=std::min(a.startBeat,b.startBeat);
    {const juce::ScopedLock lock(audioLock);session.tracks[size_t(selectedTrack)].clips.push_back(std::move(comp));}
    arrangement.repaint();setStatus("Created comp clip. Mute source takes to audition the comp on its own.");
   }catch(const std::exception& ex){alert("Take comp failed",ex.what());}
  });
  menu.addSubMenu("Audio analysis and take comp",editor);
  menu.addItem("New in v1.4: analysis, transient slicing and mastering...",[this]{analyseCurrentClip();});
  juce::PopupMenu capture;
  for(int scene=0;scene<8;++scene){
   capture.addItem("Scene "+juce::String(scene+1)+"  ->  append 4 bars",[this,scene]{
    const juce::ScopedLock lock(audioLock);
    // Append after the previous arrangement, avoiding existing audio clips.
    const double end=std::max(0.,session.duration()*session.bpm/60.);
    const double position=std::ceil(end/session.beatsPerBar)*session.beatsPerBar;
    const auto n=veysona::commitPerformanceScene(session,size_t(scene),position,session.beatsPerBar*4.);
    if(n){sessionMode=false;timelineViewport.setVisible(true);performanceViewport.setVisible(false);
     arrangement.setSize(arrangement.contentWidth(),std::max(600,36+int(session.tracks.size())*73));
     arrangement.repaint();setStatus("Captured scene "+juce::String(scene+1)+" into "+juce::String(int(n))+" arrangement tracks");}
    else setStatus("Scene has no clips to capture");
   });
  }
  menu.addSubMenu("Capture scene into arrangement",capture);
  menu.addSectionHeader("Performance launch quantisation");
  menu.addItem("Immediate",[this]{const juce::ScopedLock lock(audioLock);launcher.setQuantisation(0);});
  menu.addItem("1/4 beat",[this]{const juce::ScopedLock lock(audioLock);launcher.setQuantisation(.25);});
  menu.addItem("1 beat",[this]{const juce::ScopedLock lock(audioLock);launcher.setQuantisation(1);});
  menu.addItem("1 bar",[this]{const juce::ScopedLock lock(audioLock);launcher.setQuantisation(double(session.beatsPerBar));});
  menu.addItem("2 bars",[this]{const juce::ScopedLock lock(audioLock);launcher.setQuantisation(double(session.beatsPerBar)*2);});
  menu.addSeparator();
  menu.addItem("Export individual stems (includes inserts)...",[this]{choose(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,"Choose stems folder",[this](juce::File f){exportStems(f);});});
  menu.addItem("Autosave recovery snapshots",true,autosaveEnabled,[this]{autosaveEnabled=!autosaveEnabled;savePreferences();});
  menu.addItem("Browse recovery snapshots...",[this]{showRecoveryMenu();});
  menu.addItem("Choose autosave folder...",[this]{chooseAutosaveFolder();});
  menu.addItem("Audio drivers / interface...",[this]{configureDevice();});
  menu.addSeparator();
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&menuBar));
}

void Studio::showPreferences(){
 auto tabs=std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
 tabs->setSize(760,516);tabs->setTabBarDepth(35);
 auto audio=std::make_unique<VeysonaSettingsPage>();
 audio->heading("Audio and MIDI devices",7);
 auto* selector=audio->own(std::make_unique<juce::AudioDeviceSelectorComponent>(devices,0,2,0,2,true,false,true,false));
 selector->setBounds(12,46,710,405);
 tabs->addTab("Audio & MIDI",C::panel,audio.release(),true);

 auto project=std::make_unique<VeysonaSettingsPage>();
 project->heading("Project, autosave and startup",13);
 project->note("Recovery snapshots are separate from your .veys project. Autosave runs while transport is stopped.",46);
 auto* autosave=project->own(std::make_unique<juce::ToggleButton>("Automatically save project recovery snapshots"));
 autosave->setBounds(20,79,680,32);autosave->setToggleState(autosaveEnabled,juce::dontSendNotification);
 autosave->onClick=[this,autosave]{autosaveEnabled=autosave->getToggleState();lastAutosaveTick=juce::Time::currentTimeMillis();savePreferences();};
 project->note("Autosave interval:",118);
 auto* interval=project->own(std::make_unique<juce::ComboBox>());
 for(int i:{1,2,3,5,10,15,30})interval->addItem(juce::String(i)+" minute"+(i==1?"":"s"),i);
 interval->setSelectedId(autosaveMinutes,juce::dontSendNotification);interval->setBounds(20,143,250,32);
 interval->onChange=[this,interval]{autosaveMinutes=veysona::recovery::clampInterval(interval->getSelectedId());
   lastAutosaveTick=juce::Time::currentTimeMillis();savePreferences();};
 project->note("Recovery folder (choose any writable location):",181);
 auto* folderText=project->own(std::make_unique<juce::TextEditor>());
 folderText->setReadOnly(true);folderText->setMultiLine(false);
 folderText->setColour(juce::TextEditor::backgroundColourId,C::raised);
 folderText->setColour(juce::TextEditor::textColourId,C::ink);
 folderText->setBounds(20,208,690,31);
 folderText->setText((autosaveFolder==juce::File{}?defaultAutosaveFolder():autosaveFolder).getFullPathName(),false);
 auto* chooseFolder=project->own(std::make_unique<juce::TextButton>("Change folder..."));
 chooseFolder->setBounds(20,247,180,31);
 juce::Component::SafePointer<juce::TextEditor> safeFolder(folderText);
 chooseFolder->onClick=[this,safeFolder]{
  choose(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,
   "Choose Veysona autosave folder",[this,safeFolder](juce::File folder){
    if(!folder.isDirectory())return;
    autosaveFolder=folder;recoverySlot=0;lastAutosaveTick=juce::Time::currentTimeMillis();savePreferences();
    if(safeFolder!=nullptr)safeFolder->setText(folder.getFullPathName(),false);
    setStatus("Recovery folder selected: "+folder.getFullPathName());
   });
 };
 auto* recovery=project->own(std::make_unique<juce::TextButton>("Browse snapshots..."));
 recovery->setBounds(212,247,180,31);recovery->onClick=[this]{showRecoveryMenu();};
 project->note("When Veysona starts:",288);
 auto* startup=project->own(std::make_unique<juce::ComboBox>());
 startup->addItem("Reopen last project (if available)",1);
 startup->addItem("Load included demo project",2);
 startup->addItem("Start a blank project",3);
 startup->addItem("Ask me every time",4);
 startup->setSelectedId(startupMode+1,juce::dontSendNotification);
 startup->setBounds(20,315,340,33);
 startup->onChange=[this,startup]{startupMode=std::clamp(startup->getSelectedId()-1,0,3);savePreferences();};
 auto* click=project->own(std::make_unique<juce::ToggleButton>("Metronome"));
 click->setBounds(20,365,190,32);click->setToggleState(metronome.load(),juce::dontSendNotification);
 click->onClick=[this,click]{metronome=click->getToggleState();metronomeButton.setButtonText(metronome?"Click: ON":"Click: OFF");savePreferences();};
 auto* loop=project->own(std::make_unique<juce::ToggleButton>("Loop arrangement playback"));
 loop->setBounds(210,365,300,32);loop->setToggleState(looping,juce::dontSendNotification);
 loop->onClick=[this,loop]{looping=loop->getToggleState();loopButton.setButtonText(looping?"Loop: ON":"Loop: OFF");savePreferences();};
 auto* welcome=project->own(std::make_unique<juce::TextButton>("Show welcome screen now"));
 welcome->setBounds(20,417,235,33);welcome->onClick=[this]{showWelcome();};
 tabs->addTab("Project",C::panel,project.release(),true);

 auto performance=std::make_unique<VeysonaSettingsPage>();
 performance->heading("Session and performance",13);
 performance->note("Launch quantisation sets when queued clips and scenes begin.",54);
 auto* quant=performance->own(std::make_unique<juce::ComboBox>());
 const char* quantNames[]={"Immediately","1/4 beat","1 beat","1 bar","2 bars"};
 for(int i=1;i<=5;++i)quant->addItem(quantNames[i-1],i);
 const double q=launcher.quantisation();
 quant->setSelectedId(q==0?1:q<.5?2:q<session.beatsPerBar?3:q<=session.beatsPerBar?4:5,juce::dontSendNotification);
 quant->setBounds(20,99,300,32);
 quant->onChange=[this,quant]{
  const double value[]={0,.25,1.,double(session.beatsPerBar),double(session.beatsPerBar)*2.};
  {const juce::ScopedLock lock(audioLock);launcher.setQuantisation(value[std::clamp(quant->getSelectedId()-1,0,4)]);}
  savePreferences();
 };
 performance->note("Switch Arrangement / Session in the top workspace bar. Use Stop Clips to cancel launches.",165);
 performance->note("The full MIDI tools are in the Tools / Instruments menu and the main menu bar.",205);
 tabs->addTab("Session",C::panel,performance.release(),true);

 auto appearance=std::make_unique<VeysonaSettingsPage>();
 appearance->heading("Appearance and workspace",13);
 appearance->note("Veysona's panels and transport controls use your selected accent colour.",56);
 auto* accent=appearance->own(std::make_unique<juce::ComboBox>());
 accent->addItem("Electric Blue (default)",1);accent->addItem("Teal",2);accent->addItem("Brushed Silver",3);
 accent->setSelectedId(accentPreset+1,juce::dontSendNotification);accent->setBounds(20,104,325,34);
 accent->onChange=[this,accent]{applyAccent(accent->getSelectedId()-1);savePreferences();};
 appearance->note("Panel switching: use the Arrangement / Session and MIDI Editor / Mixer tabs.",162);
 appearance->note("Track names are editable: click a selected track's name in the device panel.",198);
 appearance->note("You can resize the Veysona window and zoom the arrangement from the top bar.",234);
 tabs->addTab("Appearance",C::panel,appearance.release(),true);

 auto plug=std::make_unique<VeysonaSettingsPage>();
 plug->heading("VST3 plugin locations",13);
 plug->note("Veysona also scans the standard Windows VST3 directory and its bundled plugins.",54);
 auto* locations=plug->own(std::make_unique<juce::TextEditor>());
 locations->setMultiLine(true,true);locations->setReadOnly(true);
 locations->setColour(juce::TextEditor::backgroundColourId,C::raised);locations->setColour(juce::TextEditor::textColourId,C::ink);
 locations->setBounds(20,94,685,170);
 juce::Component::SafePointer<juce::TextEditor> safeLocations(locations);
 auto refresh=[this,safeLocations]{if(safeLocations!=nullptr)safeLocations->setText(additionalPluginFolders.isEmpty()?"No additional directories configured":additionalPluginFolders.joinIntoString("\n"),false);};
 refresh();
 auto* add=plug->own(std::make_unique<juce::TextButton>("Add VST3 folder..."));add->setBounds(20,289,180,36);
 add->onClick=[this,refresh]{choose(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,
  "Select a VST3 plugin folder",[this,refresh](juce::File folder){
   if(folder.isDirectory()&&!additionalPluginFolders.contains(folder.getFullPathName())){
    additionalPluginFolders.add(folder.getFullPathName());savePreferences();refresh();
   }
  });};
 auto* clear=plug->own(std::make_unique<juce::TextButton>("Clear custom folders"));clear->setBounds(210,289,190,36);
 clear->onClick=[this,refresh]{additionalPluginFolders.clear();savePreferences();refresh();};
 auto* browse=plug->own(std::make_unique<juce::TextButton>("Open VST3 browser"));browse->setBounds(410,289,190,36);
 browse->onClick=[this]{pluginButton.triggerClick();};
 plug->note("The host currently opens plugins in-process. Untrusted VST3s can crash the application.",350);
 tabs->addTab("Plugins",C::panel,plug.release(),true);

 juce::DialogWindow::LaunchOptions options;
 options.content.setOwned(tabs.release());
 options.dialogTitle="Veysona Studio  |  Preferences";
 options.dialogBackgroundColour=C::bg;
 options.resizable=false;options.useNativeTitleBar=true;
 options.launchAsync();
}

Studio::Studio():arrangement(*this),performanceGrid(*this),piano(*this),mixer(*this),eqView(*this){
 setSize(1680,960);setLookAndFeel(&theme);addAndMakeVisible(menuBar);setWantsKeyboardFocus(true);
 analyserBins.fill(-90.f);
 for(auto* split:{&browserDivider,&editorDivider,&deviceDivider,&effectDivider}){
  addAndMakeVisible(*split);
  split->onDone=[this]{savePreferences();};
 }
 browserDivider.onBegin=[this]{splitterInitial=browserDockWidth;};
 browserDivider.onDrag=[this](int dx){browserDockWidth=std::clamp(splitterInitial-dx,250,std::min(560,getWidth()-900));resized();repaint();};
 editorDivider.onBegin=[this]{splitterInitial=editorDockHeight;};
 editorDivider.onDrag=[this](int dy){editorDockHeight=std::clamp(splitterInitial-dy,310,std::max(310,getHeight()-394));resized();repaint();};
 deviceDivider.onBegin=[this]{splitterInitialRatio=pianoDockFraction;};
 deviceDivider.onDrag=[this](int dx){
  const int space=std::max(900,getWidth()-(browserVisible?browserDockWidth:0));
  pianoDockFraction=std::clamp(splitterInitialRatio+float(dx)/space,.33f,.60f);resized();repaint();
 };
 effectDivider.onBegin=[this]{splitterInitialRatio=instrumentDockFraction;};
 effectDivider.onDrag=[this](int dx){
  const int space=std::max(900,getWidth()-(browserVisible?browserDockWidth:0));
  instrumentDockFraction=std::clamp(splitterInitialRatio+float(dx)/space,.18f,.38f);resized();repaint();
 };
 tabArrangement.setButtonText("Arrange");tabSession.setButtonText("Session");tabPiano.setButtonText("Piano Roll");tabMixer.setButtonText("Mixer");stopScenes.setButtonText("Stop Clips");
 autosaveFolder=defaultAutosaveFolder();session.sampleRate=48000;session.bpm=120;formats.registerBasicFormats();pluginFormats.addFormat(std::make_unique<juce::VST3PluginFormat>());
 sampleFolder=findSamples();midiFolder=sampleFolder.getParentDirectory().getChildFile("midi");instrumentFolder=sampleFolder.getParentDirectory().getChildFile("instruments");session.tracks.clear();prepareEngine();
 for(auto* b:{&browserSamplesTab,&browserPluginsTab,&browserToggle,&snapButton,&fitButton,&followButton,&playButton,&stopButton,&recordButton,&loopButton,&importButton,&midiButton,&demoButton,&saveButton,&openButton,&exportButton,&pluginButton,&pluginEditorButton,&audioSettings,&addNoteButton,&tabPiano,&tabMixer,&midiImportButton,&midiExportButton,&moreButton,&detailsButton,
 &previewButton,&automationButton,&midiArmButton,&tabArrangement,&tabSession,&stopScenes,&pianoScaleButton,&quantizeButton,&transposeUp,&transposeDown,&duplicateNotesButton,&undoMidiButton,&redoMidiButton,&eqToggle,&compressorToggle,&fxPresetsButton,&metronomeButton,&samplerButton,&drumButton,&midiToolsButton,&preferencesButton})style(*b);
 playButton.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff126748));
 recordButton.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff793047));
 loopButton.setColour(juce::TextButton::buttonColourId,C::purple.withAlpha(.65f));
 slider(bpm,40,240,120," BPM");bpm.setNumDecimalPlacesToDisplay(0);
 slider(zoom,65,215,110);zoom.setNumDecimalPlacesToDisplay(0);
 slider(masterGain,0,1.5,.85);masterGain.setNumDecimalPlacesToDisplay(2);
 masterGain.onValueChange=[this]{const juce::ScopedLock lock(audioLock);session.master.gain=float(masterGain.getValue());};
 slider(volume,0,1.5,.8);slider(pan,-1,1,0);slider(cutoff,.05,1,1);slider(drive,0,1,0);slider(delay,0,.8,0);slider(compThreshold,-48,0,-18," dB");slider(compRatio,1,12,3," :1");
 bpm.onValueChange=[this]{if(loadingUI)return;const juce::ScopedLock lock(audioLock);session.bpm=bpm.getValue();arrangement.repaint();piano.repaint();};
 zoom.onValueChange=[this]{pixelsPerBar=zoom.getValue();arrangement.setSize(arrangement.contentWidth(),arrangement.getHeight());arrangement.repaint();};
 auto applyFx=[this]{if(loadingUI)return;const juce::ScopedLock lock(audioLock);if(selectedTrack>=0&&selectedTrack<int(session.tracks.size())){
 auto& tr=session.tracks[selectedTrack];tr.gain=float(volume.getValue());tr.pan=float(pan.getValue());tr.cutoff=float(cutoff.getValue());tr.drive=float(drive.getValue());tr.delayMix=float(delay.getValue());}mixer.repaint();};
 for(auto* s:{&volume,&pan,&cutoff,&drive,&delay})s->onValueChange=applyFx;
 auto applyComp=[this]{if(loadingUI)return;const juce::ScopedLock lock(audioLock);if(selectedTrack>=0&&selectedTrack<int(session.tracks.size())){
 auto& c=session.tracks[size_t(selectedTrack)].compressor;c.thresholdDb=float(compThreshold.getValue());c.ratio=float(compRatio.getValue());}};
 compThreshold.onValueChange=applyComp;compRatio.onValueChange=applyComp;
 eqToggle.onClick=[this]{
  if(selectedTrack<0||selectedTrack>=int(session.tracks.size()))return;
  const juce::ScopedLock lock(audioLock);auto& eq=session.tracks[size_t(selectedTrack)].eq;
  const auto before=eq;eq.enabled=!eq.enabled;eqHistory.commit(selectedTrack,before,eq);
  eqToggle.setButtonText(eq.enabled?"EQ On":"EQ Off");eqView.repaint();if(expandedEQ!=nullptr)expandedEQ->repaint();
 };
 compressorToggle.onClick=[this]{if(selectedTrack>=int(session.tracks.size()))return;const juce::ScopedLock lock(audioLock);auto& c=session.tracks[size_t(selectedTrack)].compressor;c.enabled=!c.enabled;compressorToggle.setButtonText(c.enabled?"Comp On":"Comp Off");};
 fxPresetsButton.onClick=[this]{
  if(selectedTrack<0||selectedTrack>=int(session.tracks.size()))return;
  juce::PopupMenu menu;menu.addSectionHeader("Eight-band EQ presets");
  using P=veysona::eqtools::Preset;
  for(auto preset:{P::Flat,P::VocalPresence,P::BassWeight,P::DrumPunch,P::WarmKeys,P::AiryPads,P::DeMud,P::Telephone}){
   menu.addItem(veysona::eqtools::presetName(preset),[this,preset]{
    if(selectedTrack<0||selectedTrack>=int(session.tracks.size()))return;
    const juce::ScopedLock lock(audioLock);auto& eq=session.tracks[size_t(selectedTrack)].eq;
    const auto before=eq;eq=veysona::eqtools::makePreset(preset);eqHistory.commit(selectedTrack,before,eq);
    updateControls();eqView.repaint();if(expandedEQ!=nullptr)expandedEQ->repaint();
    setStatus("EQ: "+juce::String(veysona::eqtools::presetName(preset)));
   });
  }
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&fxPresetsButton));
 };
 instrument.addItem("Saw Synth",1);instrument.addItem("Sine Synth",2);instrument.addItem("Square Synth",3);instrument.addItem("Triangle Synth",4);instrument.addItem("Noise Synth",5);
 instrument.addItem("Sample Instrument",6);instrument.addItem("Drum Rack",7);
 instrument.addItem("Veysona Wave (Wavetable)",8);
 instrument.setSelectedId(1,juce::dontSendNotification);instrument.setColour(juce::ComboBox::backgroundColourId,C::raised);instrument.setColour(juce::ComboBox::textColourId,C::ink);addAndMakeVisible(instrument);
 instrument.onChange=[this]{if(loadingUI)return;const juce::ScopedLock lock(audioLock);if(selectedTrack<int(session.tracks.size())){int value=instrument.getSelectedId();const veysona::Instrument map[]={veysona::Instrument::Saw,veysona::Instrument::Sine,veysona::Instrument::Square,veysona::Instrument::Triangle,veysona::Instrument::Noise,veysona::Instrument::Sampler,veysona::Instrument::DrumRack,veysona::Instrument::Wavetable};session.tracks[selectedTrack].instrument=map[std::clamp(value-1,0,7)];}piano.focusOnNotes();piano.repaint();refreshWorkspaceButtons();};
 category.addItem("Plug-ins",9);category.addItem("All Samples",1);category.addItem("Drums",2);category.addItem("Bass / Synths",3);category.addItem("Loops",4);category.addItem("MIDI",5);category.addItem("FX / Texture",6);category.addItem("Demo Songs",7);category.addItem("Instruments",8);category.addItem("FX Presets",10);
 category.setSelectedId(1);category.setColour(juce::ComboBox::backgroundColourId,C::raised);category.setColour(juce::ComboBox::textColourId,C::ink);addAndMakeVisible(category);
 category.onChange=[this]{browserCategory=category.getSelectedId()-1;refreshBrowser();refreshWorkspaceButtons();};
 search.setTextToShowWhenEmpty("Search library... (Ctrl+F)",C::muted);search.setColour(juce::TextEditor::backgroundColourId,C::raised);search.setColour(juce::TextEditor::textColourId,C::ink);search.setColour(juce::TextEditor::outlineColourId,C::outline);search.onTextChange=[this]{refreshBrowser();};addAndMakeVisible(search);
 browser.setModel(this);browser.setColour(juce::ListBox::backgroundColourId,C::panel);browser.setRowHeight(35);addAndMakeVisible(browser);
 status.setColour(juce::Label::textColourId,C::muted);status.setFont(juce::Font(13.f));addAndMakeVisible(status);
 transportStatus.setColour(juce::Label::textColourId,C::blue);transportStatus.setFont(juce::Font(13.f));addAndMakeVisible(transportStatus);
 trackName.setColour(juce::Label::textColourId,C::ink);trackName.setFont(juce::Font(15.f));addAndMakeVisible(trackName);
 trackName.setEditable(true,true,false);
 trackName.onTextChange=[this]{
  if(loadingUI)return;
  auto name=trackName.getText().trim();if(name.isEmpty())return;
  const juce::ScopedLock lock(audioLock);
  if(selectedTrack>=0&&selectedTrack<int(session.tracks.size()))session.tracks[size_t(selectedTrack)].name=name.toStdString();
  arrangement.repaint();mixer.repaint();
 };
 timelineViewport.setViewedComponent(&arrangement,false);timelineViewport.setScrollBarsShown(true,true);timelineViewport.setScrollBarThickness(9);addAndMakeVisible(timelineViewport);
 performanceViewport.setViewedComponent(&performanceGrid,false);performanceViewport.setScrollBarsShown(true,true);
 performanceViewport.setScrollBarThickness(9);addAndMakeVisible(performanceViewport);performanceViewport.setVisible(false);
 addAndMakeVisible(piano);
 mixerViewport.setViewedComponent(&mixer,false);mixerViewport.setScrollBarsShown(false,true);
 addAndMakeVisible(mixerViewport);mixerViewport.setVisible(false);addAndMakeVisible(eqView);
 for(const auto& [name,id]:std::initializer_list<std::pair<const char*,int>>{{"1/4",1},{"1/8",2},{"1/16",3},{"1/32",4}})midiGrid.addItem(name,id);
 midiGrid.setSelectedId(3);midiGrid.setColour(juce::ComboBox::backgroundColourId,C::raised);midiGrid.setColour(juce::ComboBox::textColourId,C::ink);addAndMakeVisible(midiGrid);
 playButton.onClick=[this]{playing=true;};
 stopButton.onClick=[this]{playing=false;recording=false;playCursor=0;peakL=0;peakR=0;const juce::ScopedLock lock(audioLock);launcher.clear();engine.reset();for(auto& chain:plugins)for(auto& p:chain)if(p)p->reset();};
 loopButton.onClick=[this]{looping=!looping;loopButton.setButtonText(looping?"Loop: ON":"Loop: OFF");};
 metronomeButton.onClick=[this]{bool newValue=!metronome.load();metronome=newValue;metronomeButton.setButtonText(newValue?"Click: ON":"Click: OFF");savePreferences();};
 samplerButton.onClick=[this]{
  juce::PopupMenu menu;menu.addItem("Choose WAV / MP3 / FLAC instrument...",[this]{
   choose(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,"Choose a keyboard sample",[this](juce::File f){loadSampleInstrument(f);},"*.wav;*.mp3;*.flac;*.ogg;*.aiff");
  });
  if(instrumentFolder.isDirectory()){
   juce::Array<juce::File> presets;instrumentFolder.findChildFiles(presets,juce::File::findFiles,false,"*.wav");
   int count=0;
   for(const auto& f:presets){
    if(count++>=24)break;
    menu.addItem(f.getFileNameWithoutExtension(),[this,f]{loadSampleInstrument(f);});
   }
   if(presets.size()>24){
    menu.addSeparator();
    menu.addItem("Browse all "+juce::String(presets.size())+" instruments...",[this]{
     browserVisible=true;category.setSelectedId(8,juce::sendNotification);search.clear();
     resized();setStatus("Search the complete sampled-instrument library on the right");
    });
   }
  }
  const auto rootPacks=instrumentFolder.getParentDirectory().getChildFile("instrument_packs");
  const auto userPacks=recordedAssetsFolder().getChildFile("instrument_packs");
  for(const auto& family: {juce::String("Piano"),juce::String("Guitar")}){
   juce::PopupMenu familyMenu;
   juce::StringArray shown;
   for(const auto& packs: {userPacks,rootPacks}){
    juce::Array<juce::File> folders;
    if(packs.isDirectory())packs.findChildFiles(folders,juce::File::findDirectories,false);
    for(const auto& folder:folders){
     const auto program=folder.getFileName();
     if(!program.startsWith(family+" - ") || shown.contains(program) ||
        !folder.getChildFile("RECORDED_SOURCE.json").existsAsFile())continue;
     shown.add(program);
     const auto display=program.fromFirstOccurrenceOf(" - ",false,false);
     familyMenu.addItem(display,[this,program]{loadMultisampleInstrument(program);});
    }
   }
   if(familyMenu.getNumItems()>0)menu.addSubMenu(family+" instruments (recorded)",familyMenu);
  }
  const auto docs=instrumentFolder.getParentDirectory().getParentDirectory().getChildFile("RECORDED_INSTRUMENTS.md");
  menu.addSeparator();
  menu.addItem("Install CC0 recorded piano / guitars / drums...",[this,docs]{
   if(docs.existsAsFile())docs.startAsProcess();
   else alert("Recorded instrument installation","Run Python 3 tools/install_recorded_instruments.py --quality compact from the Veysona source folder, then restart.");
  });
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&samplerButton));
 };
 drumButton.onClick=[this]{
  juce::PopupMenu kits;kits.addSectionHeader("Drum Rack - select a kit");
  const auto rootDrums=sampleFolder.getParentDirectory().getChildFile("drum_kits");
  const auto userDrums=recordedAssetsFolder().getChildFile("drum_kits");
  juce::StringArray shownKits;
  for(const auto& kitsDir: {userDrums,rootDrums}){
   juce::Array<juce::File> folders;
   if(kitsDir.isDirectory())kitsDir.findChildFiles(folders,juce::File::findDirectories,false);
   for(const auto& folder:folders){
    const auto name=folder.getFileName();
    if(shownKits.contains(name)||!folder.getChildFile("RECORDED_SOURCE.json").existsAsFile())continue;
    shownKits.add(name);
    kits.addItem(name,[this,name]{addDrumRack(name);});
   }
  }
  if(kits.getNumItems()<=1)kits.addItem("No recorded kit installed — see RECORDED_INSTRUMENTS.md",false,false,[]{});
  kits.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&drumButton));
 };
 midiArmButton.onClick=[this]{toggleMidiArm();};
 midiToolsButton.onClick=[this]{
  juce::PopupMenu menu;menu.addSectionHeader("MIDI pattern tools");
  auto action=[this](std::function<void(std::vector<veysona::Note>&,double)> fn){
   const juce::ScopedLock lock(audioLock);auto* m=activeMidi();if(!m)return;
   saveMidiUndo();fn(m->notes,m->lengthBeats);piano.repaint();arrangement.repaint();
  };
  menu.addItem("Major triads",[action]{action([](auto& n,double){veysona::midi::makeTriads(n,false);});});
  menu.addItem("Minor triads",[action]{action([](auto& n,double){veysona::midi::makeTriads(n,true);});});
  menu.addItem("Legato (same pitch)",[action]{action([](auto& n,double len){veysona::midi::legato(n,len);});});
  menu.addItem("Polyphonic legato (separate voices)",[action]{action([](auto& n,double){veysona::advanced_midi::makePolyphonicLegato(n,8.);});});
  menu.addItem("Fix repeated-note overlaps",[action]{action([](auto& n,double){veysona::advanced_midi::resolveSameVoiceOverlaps(n);});});
  menu.addItem("Reverse MIDI clip",[action]{action([](auto& n,double len){veysona::midi::reverse(n,len);});});
  menu.addItem("Swing 55%",[action]{action([](auto& n,double){veysona::midi::applySwing(n,.5,55.);});});
  menu.addItem("Humanise timing + velocity",[action]{action([](auto& n,double){veysona::midi::humanize(n,.035f,.075f,34827);});});
  menu.addSeparator();menu.addSectionHeader("Timing and velocity shaping");
  menu.addItem("Quantise 50% (keep some feel)",[action]{action([](auto& n,double){veysona::midi::quantizeStrength(n,.25,.5f);});});
  menu.addItem("Quantise 75%",[action]{action([](auto& n,double){veysona::midi::quantizeStrength(n,.25,.75f);});});
  menu.addItem("Velocity: soften",[action]{action([](auto& n,double){veysona::midi::velocityCurve(n,1.35f);});});
  menu.addItem("Velocity: lift quiet notes",[action]{action([](auto& n,double){veysona::midi::velocityCurve(n,.7f);});});
  menu.addItem("Velocity: accent",[action]{action([](auto& n,double){veysona::midi::velocityCurve(n,.75f,1.1f);});});
  menu.addItem("Set notes to C major",[action]{action([](auto& n,double){veysona::midi::constrainToScale(n,0,{0,2,4,5,7,9,11});});});
  menu.addItem("Set notes to C minor",[action]{action([](auto& n,double){veysona::midi::constrainToScale(n,0,{0,2,3,5,7,8,10});});});
  menu.addItem("Arpeggiate ascending (1/8)",[action]{action([](auto& n,double){veysona::midi::addArpeggio(n,.5,2,true);});});
  menu.addItem("Arpeggiate descending (1/16)",[action]{action([](auto& n,double){veysona::midi::addArpeggio(n,.25,2,false);});});
  menu.addItem("Chord inversion +1",[action]{action([](auto& n,double){veysona::midi::addChordInversions(n,1);});});
  menu.addItem("Chord inversion -1",[action]{action([](auto& n,double){veysona::midi::addChordInversions(n,-1);});});
  menu.addSeparator();menu.addSectionHeader("Veysona 1.3 groove and generators");
  menu.addItem("Euclidean kick  -  5 hits / 16 steps (replace)",[action]{action([](auto& n,double){n=veysona::midi::makeEuclideanRhythm(16,5,36,.25);});});
  menu.addItem("Euclidean hi-hat  -  11 hits / 16 (replace)",[action]{action([](auto& n,double){n=veysona::midi::makeEuclideanRhythm(16,11,42,.25,3,.68f);});});
  menu.addItem("Strum chord  -  low to high 20 ms",[action]{action([](auto& n,double){veysona::midi::strumChords(n,.04,true);});});
  menu.addItem("Strum chord  -  high to low 20 ms",[action]{action([](auto& n,double){veysona::midi::strumChords(n,.04,false);});});
  menu.addItem("Note repeat 1/2",[action]{action([](auto& n,double){veysona::midi::repeatNotes(n,2);});});
  menu.addItem("Note repeat 1/4",[action]{action([](auto& n,double){veysona::midi::repeatNotes(n,4);});});
  menu.addItem("Probability 75% (deterministic)",[action]{action([](auto& n,double){veysona::midi::applyProbability(n,.75f,1077);});});
  menu.addItem("Velocity crescendo 35% to 100%",[action]{action([](auto& n,double){veysona::midi::velocityRamp(n,.35f,1.f);});});
  menu.addItem("Groove: delayed backbeats",[action]{action([](auto& n,double){veysona::midi::applyGroove(n,.25,{0,.0,.25,0});});});
  menu.addSeparator();
  menu.addItem("CC1 Mod wheel crescendo",[this]{const juce::ScopedLock lock(audioLock);auto* m=activeMidi();if(!m)return;
   for(int i=0;i<=16;++i)m->controllers.push_back({veysona::MidiControlType::CC,1,i*127/16,1,i*.25});
   setStatus("Created MIDI CC1 expression ramp");
  });
  menu.addItem("Sustain pedal (first 4 beats)",[this]{const juce::ScopedLock lock(audioLock);auto* m=activeMidi();if(!m)return;
   m->controllers.push_back({veysona::MidiControlType::CC,64,127,1,0});
   m->controllers.push_back({veysona::MidiControlType::CC,64,0,1,4});
  });
  menu.addItem("Pitch bend  -  up and back",[this]{const juce::ScopedLock lock(audioLock);auto* m=activeMidi();if(!m)return;
   m->controllers.push_back({veysona::MidiControlType::PitchWheel,0,8192,1,0});
   m->controllers.push_back({veysona::MidiControlType::PitchWheel,0,11000,1,1});
   m->controllers.push_back({veysona::MidiControlType::PitchWheel,0,8192,1,2});
  });
  menu.addSeparator();menu.addSectionHeader("Groove Lab  -  drum rack pattern presets");
  menu.addItem("House groove (4 bars)",[this]{applyGroovePreset(veysona::groove::Style::House);});
  menu.addItem("UK Garage groove (4 bars)",[this]{applyGroovePreset(veysona::groove::Style::Garage);});
  menu.addItem("Breakbeat groove (4 bars)",[this]{applyGroovePreset(veysona::groove::Style::Breakbeat);});
  menu.addItem("Trap groove (4 bars)",[this]{applyGroovePreset(veysona::groove::Style::Trap);});
  menu.addItem("Velocity +10%",[action]{action([](auto& n,double){veysona::midi::scaleVelocity(n,1.1f);});});
  menu.addItem("Velocity -10%",[action]{action([](auto& n,double){veysona::midi::scaleVelocity(n,.9f);});});
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&midiToolsButton));
 };
 preferencesButton.onClick=[this]{showPreferences();};
 detailsButton.onClick=[this]{setDetailsVisible(!detailsVisible);savePreferences();};
 snapButton.onClick=[this]{snapEnabled=!snapEnabled;snapButton.setButtonText(snapEnabled?"Snap: 1/16":"Snap: Off");savePreferences();setStatus(snapEnabled?"Clip snapping enabled":"Free clip movement enabled");};
 fitButton.onClick=[this]{fitSongToView();};
 followButton.onClick=[this]{followPlayhead=!followPlayhead;followButton.setButtonText(followPlayhead?"Follow: On":"Follow: Off");savePreferences();};
 moreButton.onClick=[this]{showQuickActions();};
 browserSamplesTab.onClick=[this]{category.setSelectedId(1,juce::sendNotificationSync);};
 browserPluginsTab.onClick=[this]{category.setSelectedId(9,juce::sendNotificationSync);};
 browserToggle.onClick=[this]{browserVisible=!browserVisible;resized();repaint();savePreferences();};
 recordButton.onClick=[this]{
  // A spool may remain after an overrun so the user can deliberately stop
  // and see the error; never silently import a truncated recording.
  if(recordingSpool){
   std::unique_ptr<veysona::RecordingSpool> finished;
   juce::File temp;
   {
    const juce::ScopedLock lock(audioLock);
    recording=false;playing=false;
    finished=std::move(recordingSpool);
    temp=recordingTempFile;
   }
   finished->finish();
   recordButton.setButtonText("Record");
   if(finished->failed()||recordingOverrun.load()){
    temp.deleteFile();
    alert("Recording interrupted","The audio disk spool could not keep up or the disk failed. The incomplete take was not imported.");
    return;
   }
   try{
    const auto count=finished->writtenFrames();
    if(count){
     auto recorded=veysona::RecordingSpool::readInterleaved(finished->path(),count);
     {
      const juce::ScopedLock lock(audioLock);
      veysona::Track tr;tr.name="Recorded Audio";
      veysona::Clip clip;clip.name="Mic Recording";clip.sampleRate=session.sampleRate;
      clip.stereo=std::move(recorded);veysona::buildWaveformPeaks(clip);
      tr.clips.push_back(std::move(clip));session.tracks.push_back(std::move(tr));prepareEngine();
     }
     selectTrack(int(session.tracks.size())-1);
     setStatus("Recording added as a new audio track ("+juce::String(double(count)/session.sampleRate,1)+" seconds)");
    }
   }catch(const std::exception& e){
    alert("Recording import failed",juce::String(e.what())+"\nThe original capture was retained at: "+temp.getFullPathName());
    return;
   }
   temp.deleteFile();return;
  }
  auto* dev=devices.getCurrentAudioDevice();
  if(!dev||dev->getActiveInputChannels().countNumberOfSetBits()==0){alert("Audio input unavailable","Open Audio Settings and enable a microphone or line input before recording.");return;}
  try{
   auto temp=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("Veysona-Recording",".raw",false);
   auto spool=std::make_unique<veysona::RecordingSpool>(temp.getFullPathName().toStdString());
   spool->start();
   {
    const juce::ScopedLock lock(audioLock);
    recordingTempFile=temp;recordingOverrun=false;recordOverrunReported=false;recordingSpool=std::move(spool);
    playCursor=0;engine.reset();playing=true;recording=true;
   }
   recordButton.setButtonText("Finish Take");
   setStatus("Recording to disk; finish the take to import it into the project");
  }catch(const std::exception& e){alert("Cannot start recording",e.what());}
 };
 importButton.onClick=[this]{choose(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,"Import audio",[this](juce::File f){openAudio(f);},"*.wav;*.mp3;*.flac;*.ogg;*.aiff");};
 midiButton.onClick=[this]{addMidiTrack();};demoButton.onClick=[this]{confirmDemoReplace();};
 automationButton.onClick=[this]{
  automationMode=!automationMode;
  automationButton.setButtonText(automationMode?"Automation ON":"Automation OFF");
  setStatus(automationMode?"Automation lane: click to add a gain point, drag to edit, right-click to delete":"Automation editing disabled");
  arrangement.repaint();
 };
 previewButton.onClick=[this]{int row=browser.getSelectedRow();if(row>=0&&row<int(browserFiles.size()))previewSample(browserFiles[size_t(row)]);};
 pianoScaleButton.onClick=[this]{
  juce::PopupMenu menu;menu.addSectionHeader("PIANO ROLL SCALE GUIDE");
  menu.addItem("Chromatic / Show all notes",[this]{piano.setScale(0,0);pianoScaleButton.setButtonText("Scale: All");});
  const char* names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  juce::PopupMenu major,minor;
  for(int root=0;root<12;++root){
   const juce::String name(names[root]);
   major.addItem(name+" major",[this,root,name]{piano.setScale(root,1);pianoScaleButton.setButtonText(name+" major");});
   minor.addItem(name+" minor",[this,root,name]{piano.setScale(root,2);pianoScaleButton.setButtonText(name+" minor");});
  }
  menu.addSeparator();menu.addSubMenu("Major keys",major);menu.addSubMenu("Minor keys",minor);
  menu.addSeparator();menu.addItem("Snap every existing note to displayed scale",[this]{
   const juce::ScopedLock lock(audioLock);auto* region=activeMidi();if(!region)return;
   saveMidiUndo(); // scale positions are applied through the same editing history as note drags.
   piano.constrainActiveScale(*region);piano.repaint();arrangement.repaint();
  });
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&pianoScaleButton));
 };
 quantizeButton.onClick=[this]{if(auto* region=activeMidi()){const juce::ScopedLock lock(audioLock);saveMidiUndo();veysona::midi::quantize(region->notes,std::pow(2.,1-midiGrid.getSelectedId()),true);piano.repaint();arrangement.repaint();}};
 transposeUp.onClick=[this]{if(auto* region=activeMidi()){const juce::ScopedLock lock(audioLock);saveMidiUndo();veysona::midi::transpose(region->notes,12);piano.repaint();arrangement.repaint();}};
 transposeDown.onClick=[this]{if(auto* region=activeMidi()){const juce::ScopedLock lock(audioLock);saveMidiUndo();veysona::midi::transpose(region->notes,-12);piano.repaint();arrangement.repaint();}};
 duplicateNotesButton.onClick=[this]{if(auto* region=activeMidi()){const juce::ScopedLock lock(audioLock);saveMidiUndo();veysona::midi::duplicate(region->notes,region->lengthBeats);region->lengthBeats*=2;piano.repaint();arrangement.repaint();}};
 undoMidiButton.onClick=[this]{midiHistory(true);};redoMidiButton.onClick=[this]{midiHistory(false);};
 midiImportButton.onClick=[this]{choose(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,"Import MIDI",[this](juce::File f){importMidi(f);},"*.mid;*.midi");};
 midiExportButton.onClick=[this]{choose(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::warnAboutOverwriting,"Export MIDI",[this](juce::File f){exportMidi(f.withFileExtension("mid"));},"*.mid");};
 saveButton.onClick=[this]{saveCurrent();};
 openButton.onClick=[this]{choose(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,"Open Veysona project",[this](juce::File f){loadProject(f);},"*.veys;*.odaw");};
 exportButton.onClick=[this]{
  juce::PopupMenu menu;
  menu.addSectionHeader("Export bit depth");
  auto exportAs=[this](veysona::WavFormat format){
   choose(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::warnAboutOverwriting,
     "Export WAV mixdown",[this,format](juce::File f){renderExport(f.withFileExtension("wav"),format);},"*.wav");
  };
  menu.addItem("16-bit PCM WAV (TPDF dither)",[exportAs]{exportAs(veysona::WavFormat::PCM16);});
  menu.addItem("24-bit PCM WAV (TPDF dither)",[exportAs]{exportAs(veysona::WavFormat::PCM24);});
  menu.addItem("32-bit float WAV (retain headroom)",[exportAs]{exportAs(veysona::WavFormat::Float32);});
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&exportButton));
 };
 pluginButton.onClick=[this]{
  juce::PopupMenu menu;
  menu.addItem("Browse for a .vst3 file or bundle...",[this]{choose(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::canSelectDirectories,"Select a VST3 plugin",[this](juce::File f){loadPlugin(f);},"*.vst3");});
  menu.addSeparator();
  auto exe=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
  juce::Array<juce::File> roots;
  roots.add(exe.getChildFile("VST3"));
  for(const auto& path:additionalPluginFolders)roots.add(juce::File(path));
  #if JUCE_WINDOWS
  roots.add(juce::File("C:\\Program Files\\Common Files\\VST3"));
  roots.add(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getParentDirectory().getChildFile("Local").getChildFile("Programs").getChildFile("Common").getChildFile("VST3"));
  #endif
  int n=0;
  for(const auto& dir:roots){if(!dir.isDirectory())continue;
   juce::Array<juce::File> list;dir.findChildFiles(list,juce::File::findFilesAndDirectories,true,"*.vst3");
   for(const auto& file:list){
    // Windows VST3 bundles contain internal .vst3 modules; avoid showing both.
    bool nested=false;for(auto parent=file.getParentDirectory();parent!=dir&&parent!=parent.getParentDirectory();parent=parent.getParentDirectory())
      if(parent.hasFileExtension("vst3")){nested=true;break;}
    if(nested)continue;if(++n>100)break;
    menu.addItem(file.getFileNameWithoutExtension(),[this,file]{loadPlugin(file);});
   }
   if(n>100)break;
  }
  if(n==0)menu.addSectionHeader("No installed VST3 files detected");
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&pluginButton));
 };
 pluginEditorButton.onClick=[this]{
  if(selectedTrack<0||selectedTrack>=int(plugins.size())||plugins[size_t(selectedTrack)].empty()){
   alert("No VST3 loaded","Choose a track and use Load VST3 to insert an instrument or effect.");return;
  }
  juce::PopupMenu menu;menu.addSectionHeader("Plugin chain  -  select editor");
  for(size_t i=0;i<plugins[size_t(selectedTrack)].size();++i){
   auto* plugin=plugins[size_t(selectedTrack)][i].get();
   if(!plugin)continue;
   menu.addItem(juce::String(int(i+1))+". "+plugin->getName(),[this,i]{
    const int t=selectedTrack;if(t<0||t>=int(plugins.size())||i>=plugins[size_t(t)].size())return;
    auto* p=plugins[size_t(t)][i].get();auto* ed=p->createEditorIfNeeded();
    if(ed){juce::DialogWindow::LaunchOptions opt;opt.content.setOwned(ed);opt.dialogTitle=p->getName();opt.dialogBackgroundColour=C::panel;opt.launchAsync();}
    else alert("No editor","This plugin does not provide a graphical editor.");
   });
  }
  menu.addSeparator();
  menu.addItem("Test VST3 instrument (C4)",[this]{
   if(selectedTrack<0||selectedTrack>=int(plugins.size())||plugins[size_t(selectedTrack)].empty())return;
   bool synth=false;
   for(const auto& p:plugins[size_t(selectedTrack)])if(p&&p->isSynth())synth=true;
   if(!synth){alert("VST3 test", "This track has an effect plug-in, not a VST3 instrument. Import audio on the track and press Play to hear its effect.");return;}
   if(audioRate<=0){alert("VST3 test","Configure an audio output device first.");return;}
   livePluginMidi.addMessageToQueue(juce::MidiMessage::noteOn(1,60,(juce::uint8)100));
   juce::Component::SafePointer<Studio> safe(this);
   juce::Timer::callAfterDelay(420,[safe]{if(safe!=nullptr)
    safe->livePluginMidi.addMessageToQueue(juce::MidiMessage::noteOff(1,60));});
   setStatus("Sent C4 to selected VST3 instrument. Check track mute, gain, plugin preset and audio output if silent.");
  });
  menu.addItem("VST3 audio diagnostics",[this]{
   if(selectedTrack<0||selectedTrack>=int(plugins.size())||selectedTrack>=int(session.tracks.size()))return;
   const auto& t=session.tracks[size_t(selectedTrack)];
   const auto& chain=plugins[size_t(selectedTrack)];
   juce::String report="Track: "+jstr(t.name)+"\n";
   report+="Track muted: "+juce::String(t.mute?"YES":"no")+"\n";
   report+="Track gain: "+juce::String(t.gain,2)+"\n";
   report+="Transport: "+juce::String(playing.load()?"playing":"stopped")+"\n";
   report+="MIDI keyboard monitoring: "+juce::String(midiArmed.load()?"armed":"not armed")+"\n";
   report+="Output device: "+(devices.getCurrentAudioDevice()?devices.getCurrentAudioDevice()->getName():juce::String("NOT CONFIGURED"))+"\n";
   report+="Audio rate: "+juce::String(audioRate,0)+" Hz / buffer "+juce::String(audioBlock)+"\n";
   report+="Processing fault: "+juce::String(pluginTrackFaults[size_t(selectedTrack)].load()?"YES":"no")+"\n";
   for(size_t i=0;i<chain.size();++i)if(const auto* p=chain[i].get()){
    report+="\n"+juce::String(int(i+1))+". "+p->getName();
    report+=" | "+juce::String(p->isSynth()?"instrument":"effect");
    report+=" | MIDI "+juce::String(p->acceptsMidi()?"in":"no input");
    report+=" | audio "+juce::String(p->getTotalNumInputChannels())+" in / "+juce::String(p->getTotalNumOutputChannels())+" out";
   }
   report+="\n\nFor instruments, select the track and use 'Test VST3 instrument (C4)'.\nFor effects, import WAV audio and press Play.";
   alert("VST3 audio diagnostics",report);
  });
  menu.addSeparator();menu.addItem("Remove last plugin",[this]{
   const juce::ScopedLock lock(audioLock);auto& chain=plugins[size_t(selectedTrack)];
   if(!chain.empty())chain.pop_back();auto& track=session.tracks[size_t(selectedTrack)];
   if(!track.pluginChain.empty())track.pluginChain.pop_back();
   track.pluginPath=track.pluginChain.empty()?"":track.pluginChain.back().path;
   track.pluginStateBase64.clear();setStatus("Removed last plugin insert");
  });
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&pluginEditorButton));
 };
 audioSettings.onClick=[this]{configureDevice();};
 tabArrangement.onClick=[this]{const juce::ScopedLock lock(audioLock);sessionMode=false;timelineViewport.setVisible(true);performanceViewport.setVisible(false);refreshWorkspaceButtons();setStatus("Arrangement view");};
 tabSession.onClick=[this]{const juce::ScopedLock lock(audioLock);sessionMode=true;launcher.clear();performanceGrid.setSize(performanceGrid.contentWidth(),performanceGrid.contentHeight());timelineViewport.setVisible(false);performanceViewport.setVisible(true);refreshWorkspaceButtons();setStatus("Session view: launch clips and scenes");};
 stopScenes.onClick=[this]{const juce::ScopedLock lock(audioLock);launcher.stopAll(double(playCursor.load())/session.sampleRate*session.bpm/60.);performanceGrid.repaint();};
 tabPiano.onClick=[this]{mixerMode=false;piano.setVisible(true);mixerViewport.setVisible(false);refreshWorkspaceButtons();repaint();};
 tabMixer.onClick=[this]{mixerMode=true;piano.setVisible(false);mixerViewport.setVisible(true);refreshWorkspaceButtons();repaint();};
 addNoteButton.onClick=[this]{if(auto* r=activeMidi()){const juce::ScopedLock lock(audioLock);saveMidiUndo();r->notes.push_back({60,0,.25,.8f});piano.repaint();arrangement.repaint();}else addMidiTrack();};
 {
  const auto dir=findSamples().getParentDirectory().getChildFile("branding");
  const auto icon=dir.getChildFile("Veysona-Icon.png");
  const auto logo=dir.getChildFile("Veysona-Logo.png");
  if(icon.existsAsFile())brandIcon=juce::ImageFileFormat::loadFrom(icon);
  if(logo.existsAsFile())brandLogo=juce::ImageFileFormat::loadFrom(logo);
 }
 loadPreferences();refreshBrowser();
 auto savedDeviceXml=juce::XmlDocument::parse(audioDeviceStateFile());
 auto err=devices.initialise(0,2,savedDeviceXml.get(),true);
 if(err.isNotEmpty()&&savedDeviceXml)err=devices.initialise(0,2,nullptr,true);if(err.isNotEmpty())setStatus("Audio device: "+err);devices.addAudioCallback(&callback);devices.addChangeListener(this);
 lastAutosaveTick=juce::Time::currentTimeMillis();startTimerHz(25);
 juce::Component::SafePointer<Studio> safe(this);
 juce::MessageManager::callAsync([safe]{if(safe!=nullptr)safe->showStartupSplash();});
}
Studio::~Studio(){
 stopTimer();midiArmed=false;
 for(const auto& id:midiDeviceIds)devices.removeMidiInputDeviceCallback(id,&midiCallback);
 savePreferences(); saveAudioDeviceState();devices.removeChangeListener(this);devices.removeAudioCallback(&callback);playing=false;setLookAndFeel(nullptr);
}

bool Studio::keyPressed(const juce::KeyPress& k){
 auto mod=k.getModifiers();
 if(k.getKeyCode()==juce::KeyPress::spaceKey){playing=!playing.load();return true;}
 if(!mod.isCommandDown() && !mod.isAltDown() && k.getKeyCode()=='G'){snapButton.triggerClick();return true;}
 if(!mod.isCommandDown() && !mod.isAltDown() && k.getKeyCode()=='F'){fitSongToView();return true;}
 if(mod.isCommandDown()){
  if(k.getKeyCode()=='N'){createBlankProject();return true;}
  if(k.getKeyCode()=='P'){showPreferences();return true;}
  if(k.getKeyCode()=='K'){showQuickActions();return true;}
  if(k.getKeyCode()=='T'){showTemplateChooser();return true;}
  if(k.getKeyCode()=='B'){if(mod.isShiftDown())browserToggle.triggerClick();else bounceSelectedTrack();return true;}
  if(k.getKeyCode()=='R'){showRecentProjects();return true;}
  if(k.getKeyCode()=='F'){search.grabKeyboardFocus();search.selectAll();return true;}
  if(k.getKeyCode()=='C'){copyClip();return true;}
  if(k.getKeyCode()=='V'){pasteClip();return true;}
  if(k.getKeyCode()=='X'){copyClip();deleteClip();return true;}
  if(k.getKeyCode()=='Z'){midiHistory(mod.isShiftDown()?false:true);return true;}
  if(k.getKeyCode()=='Y'){midiHistory(false);return true;}
  if(k.getKeyCode()=='Q'){quantizeButton.triggerClick();return true;}
  if(k.getKeyCode()=='L'){tabSession.triggerClick();return true;}
  if(k.getKeyCode()=='S'){mod.isShiftDown()?saveAs():saveCurrent();return true;}
  if(k.getKeyCode()=='O'){openButton.triggerClick();return true;}
  if(k.getKeyCode()=='I'){importButton.triggerClick();return true;}
  if(k.getKeyCode()=='M'){midiButton.triggerClick();return true;}
  if(k.getKeyCode()=='E'){exportButton.triggerClick();return true;}
  if(k.getKeyCode()=='D'){
   if(selectedTrack>=0&&selectedTrack<int(session.tracks.size())&&selectedClip>=0){
    const juce::ScopedLock lock(audioLock);auto& clips=session.tracks[size_t(selectedTrack)].clips;
    if(selectedClip<int(clips.size())){auto dup=clips[size_t(selectedClip)];dup.startBeat+=double(dup.stereo.size()/2)/dup.sampleRate*session.bpm/60.;clips.push_back(std::move(dup));arrangement.repaint();}
   }
   return true;
  }
 }
 if((k.getKeyCode()==juce::KeyPress::deleteKey||k.getKeyCode()==juce::KeyPress::backspaceKey)&&selectedTrack>=0&&selectedTrack<int(session.tracks.size())&&selectedClip>=0){
  const juce::ScopedLock lock(audioLock);auto& clips=session.tracks[size_t(selectedTrack)].clips;
  if(selectedClip<int(clips.size()))clips.erase(clips.begin()+selectedClip);selectedClip=-1;arrangement.repaint();return true;
 }
 return false;
}
void Studio::audioCallback(const float* const* inputs,int inputCh,float* const* outputs,int outputCh,int frames){
 for(int ch=0;ch<outputCh;ch++)if(outputs[ch])juce::FloatVectorOperations::clear(outputs[ch],frames);
 if(!audioLock.tryEnter()){
  audioLockMisses.fetch_add(1,std::memory_order_relaxed);
  if(recording.exchange(false,std::memory_order_acq_rel))recordingOverrun.store(true,std::memory_order_release);
  return;
 }
 if(recording.load(std::memory_order_relaxed)&&recordingSpool){
  const float* left=(inputCh>0&&inputs)?inputs[0]:nullptr;
  const float* right=(inputCh>1&&inputs)?inputs[1]:nullptr;
  if(!left || !recordingSpool->push(left,right,size_t(frames))){
   recordingOverrun.store(true,std::memory_order_release);
   recording.store(false,std::memory_order_release);
  }
 }
 // Drain incoming MIDI once for the whole device block; offsets are then
 // translated for each sub-block. A held VST3 instrument can be auditioned
 // even while the arrangement transport is stopped.
 juce::MidiBuffer liveInput;
 livePluginMidi.removeNextBlockOfMessages(liveInput,frames);
 if(!playing||outputCh<1||!outputs[0]){
  std::fill(pluginMidiEnds.begin(),pluginMidiEnds.end(),-1);
  if(outputCh>0&&outputs[0]&&selectedTrack>=0&&selectedTrack<int(plugins.size())
     &&selectedTrack<int(session.tracks.size())&&!plugins[size_t(selectedTrack)].empty()
     &&!pluginTrackFaults[size_t(selectedTrack)].load(std::memory_order_relaxed)){
   const auto tr=size_t(selectedTrack);
   bool synth=false;
   for(const auto& p:plugins[tr])if(p&&p->isSynth())synth=true;
   // Effect-only chains need transport audio. Instruments can generate
   // live notes and release their envelopes with the transport stopped.
   if(synth){
    for(int pos=0;pos<frames;){
     const int n=std::min(2048,frames-pos);
     std::fill_n(scratchL.data(),n,0.f);std::fill_n(scratchR.data(),n,0.f);
     float* channels[]={scratchL.data(),scratchR.data()};
     juce::AudioBuffer<float> block(channels,2,n);
     juce::MidiBuffer midi;
     for(const auto event:liveInput)
      if(event.samplePosition>=pos&&event.samplePosition<pos+n)
       midi.addEvent(event.getMessage(),event.samplePosition-pos);
     try{for(auto& p:plugins[tr])if(p)p->processBlock(block,midi);}
     catch(...){pluginTrackFaults[tr].store(true,std::memory_order_release);
      std::fill_n(scratchL.data(),n,0.f);std::fill_n(scratchR.data(),n,0.f);}
     for(int k=0;k<n;++k){
      const float l=std::isfinite(scratchL[size_t(k)])?scratchL[size_t(k)]:0.f;
      const float r=std::isfinite(scratchR[size_t(k)])?scratchR[size_t(k)]:0.f;
      scratchL[size_t(k)]=l;scratchR[size_t(k)]=r;
     }
     // Use the same channel gain/pan and master limiter as playback.
     std::fill_n(mixL.data(),n,0.f);std::fill_n(mixR.data(),n,0.f);
     std::array<float*,veysona::kMaxBuses> busL{},busR{};
     for(size_t b=0;b<session.buses.size()&&b<veysona::kMaxBuses;++b){
      busL[b]=busScratch[b].l.data();busR[b]=busScratch[b].r.data();
      std::fill_n(busL[b],n,0.f);std::fill_n(busR[b],n,0.f);
     }
     veysona::routeTrack(session.tracks[tr],n,scratchL.data(),scratchR.data(),mixL.data(),mixR.data(),busL.data(),busR.data(),session.buses.size());
     for(size_t b=0;b<session.buses.size()&&b<veysona::kMaxBuses;++b){
      veysona::applyBus(session.buses[b],session,playCursor.load(std::memory_order_relaxed),n,busL[b],busR[b]);
      for(int i=0;i<n;++i){mixL[size_t(i)]+=busL[b][i];mixR[size_t(i)]+=busR[b][i];}
     }
     veysona::processMasterBlock(session.master,n,mixL.data(),mixR.data());
     for(int i=0;i<n;++i){outputs[0][pos+i]=std::clamp(mixL[size_t(i)],-1.f,1.f);
      if(outputCh>1&&outputs[1])outputs[1][pos+i]=std::clamp(mixR[size_t(i)],-1.f,1.f);}
     pos+=n;
    }
   }
  }
  mixPreview(outputs,outputCh,frames);
  audioLock.exit();return;
 }
 // Reuse the same solo state and arrangement length for all chunks in a
 // device callback. These scans previously happened repeatedly per track.
 const bool anySolo=std::any_of(session.tracks.begin(),session.tracks.end(),[](const veysona::Track& t){return t.solo;});
 const double arrangementDuration=session.duration();
 for(int pos=0;pos<frames;){
  int n=std::min(2048,frames-pos);
  int64_t cursor=playCursor.load(std::memory_order_relaxed);
  auto loopBars=std::max(1.0,std::ceil(arrangementDuration*session.bpm/60.0/session.beatsPerBar));
  const double loopStart=std::max(0.,session.loopStartBeat);
  const double loopEnd=session.loopEndBeat>loopStart?session.loopEndBeat:loopBars*session.beatsPerBar;
  const auto loopStartSamples=int64_t(std::llround(loopStart*60./session.bpm*session.sampleRate));
  const auto loopSamples=int64_t(std::llround(loopEnd*60./session.bpm*session.sampleRate));
  if(!sessionMode&&looping&&loopSamples>0&&cursor>=loopSamples){cursor=loopStartSamples;engine.reset();for(auto& chain:plugins)for(auto& p:chain)if(p)p->reset();}
  if(!sessionMode&&looping&&loopSamples>cursor)n=std::min(n,int(loopSamples-cursor));
  if(!sessionMode&&!looping&&cursor>=int64_t((arrangementDuration+1)*session.sampleRate)){playing=false;break;}
  std::fill_n(mixL.data(),n,0.f);std::fill_n(mixR.data(),n,0.f);
  std::array<float*,veysona::kMaxBuses> busL{},busR{};
  for(size_t b=0;b<session.buses.size()&&b<veysona::kMaxBuses;++b){
   busL[b]=busScratch[b].l.data();busR[b]=busScratch[b].r.data();
   std::fill_n(busL[b],n,0.f);std::fill_n(busR[b],n,0.f);
  }
  for(size_t tr=0;tr<session.tracks.size();++tr){
   bool vst=tr<plugins.size()&&!plugins[tr].empty();
   bool pluginSynth=false;if(vst)for(const auto& plug:plugins[tr])if(plug&&plug->isSynth())pluginSynth=true;
   std::array<veysona::PerformanceMidiEvent,512> liveMidi{};
   const int liveCount=sessionMode&&vst?launcher.collectMidiEvents(session,tr,cursor,n,liveMidi.data(),int(liveMidi.size())):0;
   if(sessionMode)launcher.renderTrack(session,tr,cursor,n,scratchL.data(),scratchR.data(),!pluginSynth);
   else veysona::renderTrackBlock(session,tr,cursor,n,scratchL.data(),scratchR.data(),engine,!pluginSynth,false,anySolo);
   if(vst){float* channels[]={scratchL.data(),scratchR.data()};juce::AudioBuffer<float> block(channels,2,n);juce::MidiBuffer midi;
    const auto& track=session.tracks[tr];
    // MIDI chase: after a seek, pause/resume or loop wrap, external instruments
    // need the active sustained notes re-sent, otherwise playback may be silent
    // until the next note-on. Send all-notes-off before the chase to avoid hangs.
    const bool discontinuity=tr>=pluginMidiEnds.size()||pluginMidiEnds[tr]!=cursor;
    if(discontinuity){
     for(int ch=1;ch<=16;++ch)midi.addEvent(juce::MidiMessage::allNotesOff(ch),0);
     if(!sessionMode)for(const auto& region:track.midi)for(const auto& note:region.notes){
      const auto on=int64_t(std::llround((region.startBeat+note.beat)*60./session.bpm*session.sampleRate));
      const auto off=int64_t(std::llround((region.startBeat+note.beat+note.length)*60./session.bpm*session.sampleRate));
      if(on<cursor&&off>cursor){
       midi.addEvent(juce::MidiMessage::noteOn(std::clamp(note.channel,1,16),std::clamp(note.pitch,0,127),
        (juce::uint8)std::clamp(int(note.velocity*127),1,127)),0);
      }
     }
    }
    if(tr<pluginMidiEnds.size())pluginMidiEnds[tr]=cursor+n;
    if(!sessionMode)for(const auto& region:track.midi){for(const auto& note:region.notes){
      for(int edge=0;edge<2;++edge){double beat=region.startBeat+note.beat+(edge?note.length:0);auto at=int64_t(std::llround(beat*60./session.bpm*session.sampleRate));if(at>=cursor&&at<cursor+n){auto msg=edge?juce::MidiMessage::noteOff(std::clamp(note.channel,1,16),note.pitch):juce::MidiMessage::noteOn(std::clamp(note.channel,1,16),note.pitch,(juce::uint8)std::clamp(int(note.velocity*127),1,127));midi.addEvent(msg,int(at-cursor));}}
    }}
    if(!sessionMode)for(const auto& region:track.midi)for(const auto& control:region.controllers){
     const auto at=int64_t(std::llround((region.startBeat+control.beat)*60./session.bpm*session.sampleRate));
     if(at<cursor||at>=cursor+n)continue;
     const int channel=std::clamp(control.channel,1,16);
     juce::MidiMessage msg;
     if(control.type==veysona::MidiControlType::CC)msg=juce::MidiMessage::controllerEvent(channel,std::clamp(control.number,0,127),std::clamp(control.value,0,127));
     else if(control.type==veysona::MidiControlType::PitchWheel)msg=juce::MidiMessage::pitchWheel(channel,std::clamp(control.value,0,16383));
     else msg=juce::MidiMessage::channelPressureChange(channel,std::clamp(control.value,0,127));
     midi.addEvent(msg,int(at-cursor));
    }
    // Live keyboard monitoring is independent of MIDI recording. The
    // messages are delivered only to the selected VST3 instrument track.
    if(int(tr)==selectedTrack)for(const auto event:liveInput)
     if(event.samplePosition>=pos&&event.samplePosition<pos+n)
      midi.addEvent(event.getMessage(),event.samplePosition-pos);
    if(sessionMode)for(int i=0;i<liveCount;++i){
     const auto& event=liveMidi[size_t(i)];
     juce::MidiMessage message;
     const int channel=event.channel;
     switch(event.type){
      case veysona::PerformanceMidiType::NoteOn:message=juce::MidiMessage::noteOn(channel,event.number,(juce::uint8)event.value);break;
      case veysona::PerformanceMidiType::NoteOff:message=juce::MidiMessage::noteOff(channel,event.number);break;
      case veysona::PerformanceMidiType::CC:message=juce::MidiMessage::controllerEvent(channel,event.number,event.value);break;
      case veysona::PerformanceMidiType::PitchWheel:message=juce::MidiMessage::pitchWheel(channel,event.value);break;
      case veysona::PerformanceMidiType::Pressure:message=juce::MidiMessage::channelPressureChange(channel,event.value);break;
      case veysona::PerformanceMidiType::AllNotesOff:message=juce::MidiMessage::allNotesOff(channel);break;
     }
     midi.addEvent(message,event.offset);
    }
    if(tr>=pluginTrackFaults.size()||!pluginTrackFaults[tr].load(std::memory_order_relaxed)){
     // C++ exceptions can be contained here. Native access violations from
     // untrusted DLLs still require an out-of-process plugin host.
     try{for(auto& plug:plugins[tr])if(plug)plug->processBlock(block,midi);}
     catch(...){if(tr<pluginTrackFaults.size())pluginTrackFaults[tr].store(true,std::memory_order_release);
      juce::FloatVectorOperations::clear(scratchL.data(),n);
      juce::FloatVectorOperations::clear(scratchR.data(),n);
     }
    }
    // Never propagate an unstable plugin's NaN/Inf values to the master bus.
    for(int i=0;i<n;++i){
     if(!std::isfinite(scratchL[size_t(i)]))scratchL[size_t(i)]=0.f;
     if(!std::isfinite(scratchR[size_t(i)]))scratchR[size_t(i)]=0.f;
    }
   }
   veysona::processTrackEffects(session,tr,n,scratchL.data(),scratchR.data(),engine,cursor,anySolo);
   // Analyzer source: a fixed-size, lock-free ring of post-EQ track samples.
   // Analysis and FFT happen on the UI thread, never in the audio callback.
   if(int(tr)==selectedTrack && detailsVisible){
    auto write=eqInputWrite.load(std::memory_order_relaxed);
    for(int k=0;k<n;++k)eqInputRing[size_t(write++ & 4095u)].store(.5f*(scratchL[k]+scratchR[k]),std::memory_order_relaxed);
    eqInputWrite.store(write,std::memory_order_release);
   }
   if(tr<trackPeaks.size()){
    float peak=0;for(int i=0;i<n;++i)peak=std::max(peak,std::max(std::abs(scratchL[i]),std::abs(scratchR[i])));
    trackPeaks[tr].store(peak,std::memory_order_relaxed);
   }
   veysona::routeTrack(session.tracks[tr],n,scratchL.data(),scratchR.data(),mixL.data(),mixR.data(),busL.data(),busR.data(),session.buses.size());
  }
  for(size_t b=0;b<session.buses.size()&&b<veysona::kMaxBuses;++b){
   veysona::applyBus(session.buses[b],session,cursor,n,busL[b],busR[b]);
   float busPeak=0.f;
   for(int i=0;i<n;++i){
    busPeak=std::max(busPeak,std::max(std::abs(busL[b][i]),std::abs(busR[b][i])));
    mixL[i]+=busL[b][i];mixR[i]+=busR[b][i];
   }
   busPeaks[b].store(busPeak,std::memory_order_relaxed);
  }
  veysona::processMasterBlock(session.master,n,mixL.data(),mixR.data());
  for(int i=0;i<n;++i){outputs[0][pos+i]=mixL[i];if(outputCh>1&&outputs[1])outputs[1][pos+i]=mixR[i];}
  // Metronome is monitoring-only: not exported as musical audio.
  if(metronome.load()){
   const double samplesPerBeat=60.0/session.bpm*session.sampleRate;
   for(int i=0;i<n;++i){const double time=double(cursor+i)/samplesPerBeat;
    const double fraction=time-std::floor(time);
    const double seconds=fraction*60./session.bpm;
    if(seconds<.033){const int64_t beatIndex=int64_t(std::floor(time));
     const float accent=(beatIndex%session.beatsPerBar==0)?.13f:.082f;
     float tick=accent*std::exp(float(-seconds*115))*std::sin(float(2*veysona::kPi*(beatIndex%session.beatsPerBar==0?1450.:1050.)*seconds));
     outputs[0][pos+i]+=tick;if(outputCh>1&&outputs[1])outputs[1][pos+i]+=tick;
    }
   }
  }
  for(int i=0;i<n;++i){outputs[0][pos+i]=std::clamp(outputs[0][pos+i],-1.f,1.f);if(outputCh>1&&outputs[1])outputs[1][pos+i]=std::clamp(outputs[1][pos+i],-1.f,1.f);}
  pos+=n;playCursor.store(cursor+n,std::memory_order_relaxed);
 }
 mixPreview(outputs,outputCh,frames);
 float l=0,r=0;for(int i=0;i<frames;++i){l=std::max(l,std::abs(outputs[0][i]));if(outputCh>1&&outputs[1])r=std::max(r,std::abs(outputs[1][i]));}peakL.store(l);peakR.store(r);
 audioLock.exit();
}
void Studio::toggleMidiArm(){
 if(midiArmed.exchange(false)){
  // Release held notes so disarming cannot leave a stuck VST3 voice.
  for(int ch=1;ch<=16;++ch)livePluginMidi.addMessageToQueue(juce::MidiMessage::allNotesOff(ch));
  for(const auto& id:midiDeviceIds)devices.removeMidiInputDeviceCallback(id,&midiCallback);
  midiDeviceIds.clear();
  {const juce::ScopedLock lock(audioLock);
   if(auto* r=activeMidi())midiRecorder.flush(std::max(0.,double(playCursor.load())/session.sampleRate*session.bpm/60.-r->startBeat),r->notes);
   else midiRecorder.held.clear();
  }
  midiArmButton.setButtonText("MIDI Arm");setStatus("External MIDI recording disabled");return;
 }
 int count=0;
 for(const auto& input:juce::MidiInput::getAvailableDevices()){
  devices.setMidiInputDeviceEnabled(input.identifier,true);
  if(devices.isMidiInputDeviceEnabled(input.identifier)){
   devices.addMidiInputDeviceCallback(input.identifier,&midiCallback);
   midiDeviceIds.push_back(input.identifier);++count;
  }
 }
 if(count==0){setStatus("No MIDI keyboard detected. Connect one and re-arm MIDI.");return;}
 midiArmed=true;midiArmButton.setButtonText("MIDI REC + MON");
 setStatus("MIDI monitoring enabled for "+juce::String(count)+" input(s). Audition the selected VST3 while stopped, or press Play to record.");
}
void Studio::handleExternalMidi(const juce::MidiMessage& message){
 if(!midiArmed.load()||!playing.load())return;
 if(!message.isNoteOn()&&!message.isNoteOff())return;
 const juce::ScopedLock lock(audioLock);
 if(selectedTrack<0||selectedTrack>=int(session.tracks.size()))return;
 auto& track=session.tracks[size_t(selectedTrack)];
 if(track.midi.empty()){
  veysona::MidiRegion clip;clip.name="Live MIDI Take";clip.startBeat=0;clip.lengthBeats=64;
  track.midi.push_back(std::move(clip));selectedMidiRegion=0;
 }
 if(selectedMidiRegion<0||selectedMidiRegion>=int(track.midi.size()))selectedMidiRegion=0;
 auto& region=track.midi[size_t(selectedMidiRegion)];
 const double globalBeat=double(playCursor.load())/session.sampleRate*session.bpm/60.;
 const double beat=std::max(0.,globalBeat-region.startBeat);
 region.lengthBeats=std::max(region.lengthBeats,std::ceil((beat+1)/session.beatsPerBar)*session.beatsPerBar);
 if(message.isNoteOn())midiRecorder.noteOn(message.getNoteNumber(),message.getChannel(),beat,message.getFloatVelocity());
 else midiRecorder.noteOff(message.getNoteNumber(),message.getChannel(),beat,region.notes);
}
juce::File Studio::defaultAutosaveFolder() const {
 return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
    .getChildFile("Veysona Studio").getChildFile("Autosaves");
}

void Studio::setDetailsVisible(bool show){
 detailsVisible=show;
 detailsButton.setButtonText(show?"Hide Editor":"Show Editor");
 resized();repaint();
}

void Studio::chooseAutosaveFolder(){
 choose(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,
  "Choose a folder for Veysona recovery snapshots",[this](juce::File folder){
   if(!folder.isDirectory()) {alert("Autosave folder","Please select an existing folder.");return;}
   autosaveFolder=folder;lastAutosaveTick=juce::Time::currentTimeMillis();
   recoverySlot=0;savePreferences();setStatus("Autosave folder: "+folder.getFullPathName());
  });
}

void Studio::saveRecoverySnapshot(){
 // Snapshot only on the GUI timer while transport is stopped, never on the audio callback.
 // Three independent file+asset slots retain older snapshots if a new write fails.
 if(autosaving)return;
 if(playing.load()||recording.load()){
  setStatus("Stop playback or recording before writing a recovery snapshot.");return;
 }
 autosaving=true;
 lastAutosaveTick=juce::Time::currentTimeMillis();
 const auto folder=autosaveFolder==juce::File{}?defaultAutosaveFolder():autosaveFolder;
 if(!folder.isDirectory()&&folder.createDirectory().failed()){
  autosaving=false;setStatus("Autosave error: cannot create "+folder.getFullPathName());return;
 }
 const auto named=currentProject.existsAsFile();
 const auto display=named?currentProject.getFileNameWithoutExtension():juce::String("Untitled");
 const auto identity=named?currentProject.getFullPathName():"session-"+recoverySessionId;
 const auto name=veysona::recovery::snapshotFilename(display.toStdString(),identity.toStdString(),recoverySlot);
 auto destination=folder.getChildFile(juce::String(name));
 const bool ok=saveProject(destination,true);
 if(ok){recoverySlot=veysona::recovery::nextSlot(recoverySlot);
  setStatus("Autosaved recovery: "+destination.getFileName());}
 autosaving=false;
}

void Studio::rememberRecentProject(juce::File file){
 if(!file.existsAsFile()||!juce::File::isAbsolutePath(file.getFullPathName()))return;
 const auto path=file.getFullPathName();recentProjectPaths.removeString(path);
 recentProjectPaths.insert(0,path);
 while(recentProjectPaths.size()>12)recentProjectPaths.remove(recentProjectPaths.size()-1);
 lastOpenedProject=file;
}
void Studio::showRecentProjects(){
 juce::PopupMenu menu;menu.addSectionHeader("Open recently used Veysona projects");
 int count=0;
 for(const auto& path:recentProjectPaths){
  const juce::File f(path);if(!f.existsAsFile())continue;
  if(++count>12)break;
  menu.addItem(f.getFileNameWithoutExtension()+"   |   "+f.getParentDirectory().getFileName(),[this,f]{loadProject(f);});
 }
 if(count==0)menu.addItem("No projects opened yet",false,false,[]{});
 menu.addSeparator();menu.addItem("Browse project files...",[this]{openButton.triggerClick();});
 menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&menuBar));
}
void Studio::setAutomationCurve(bool smooth){
 if(selectedTrack<0||selectedTrack>=int(session.tracks.size()))return;
 const juce::ScopedLock lock(audioLock);
 auto& lanes=session.tracks[size_t(selectedTrack)].automation;
 for(auto& lane:lanes)lane.smooth=smooth;
 setStatus(smooth?"Selected track automation: smooth eased curves":"Selected track automation: linear segments");
 arrangement.repaint();
}
void Studio::showTemplateChooser(){
 juce::PopupMenu menu;menu.addSectionHeader("Editable original starter sessions");
 menu.addItem("House  |  124 BPM",[this]{createFromTemplate(veysona::workflow::Template::House);});
 menu.addItem("Drum & Bass  |  174 BPM",[this]{createFromTemplate(veysona::workflow::Template::DrumAndBass);});
 menu.addItem("Hip-Hop  |  92 BPM",[this]{createFromTemplate(veysona::workflow::Template::HipHop);});
 menu.addItem("Ambient  |  76 BPM",[this]{createFromTemplate(veysona::workflow::Template::Ambient);});
 menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&moreButton));
}
void Studio::createFromTemplate(veysona::workflow::Template kind){
 if(recording.load()){alert("Finish recording","Stop recording before switching projects.");return;}
 auto options=juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon)
  .withTitle("Start template project?").withMessage("The current unsaved changes will be replaced. Save first if necessary.")
  .withButton("Use Template").withButton("Cancel");
 juce::AlertWindow::showAsync(options,[this,kind](int accepted){
  if(accepted!=1)return;
  try{
   auto next=veysona::workflow::createTemplate(kind,session.sampleRate);
   {
    const juce::ScopedLock lock(audioLock);playing=false;recording=false;
    session=std::move(next);plugins.clear();prepareEngine();launcher.clear();playCursor=0;
    currentProject=juce::File{};midiUndo.clear();midiRedo.clear();audioUndo.clear();audioRedo.clear();eqHistory.clear();
    recoverySessionId=juce::String::toHexString(juce::Time::currentTimeMillis());recoverySlot=0;
   }
   clipboardAudio.reset();clipboardMidi.reset();bpm.setValue(session.bpm,juce::dontSendNotification);
   selectTrack(0);setStatus("New template session  |  Save As to create your own project");
  }catch(const std::exception& e){alert("Template error",e.what());}
 });
}
void Studio::audioToMidi(){
 if(playing.load()||recording.load()){alert("Stop transport","Stop playback and recording before analysing an audio clip.");return;}
 if(selectedTrack<0||selectedTrack>=int(session.tracks.size())||selectedClip<0||
    selectedClip>=int(session.tracks[size_t(selectedTrack)].clips.size())){
  alert("Select an audio clip","Click a monophonic audio clip first. Polyphonic audio is not supported.");return;
 }
 if(session.tracks.size()>=128){alert("Track limit","Cannot create another MIDI track.");return;}
 const int target=selectedTrack;veysona::Clip clip;
 {const juce::ScopedLock lock(audioLock);clip=session.tracks[size_t(target)].clips[size_t(selectedClip)];}
 try{
  auto result=veysona::workflow::transcribeMonophonic(clip,session.bpm);
  if(result.notes.empty()){alert("No clear notes detected","Try a clean solo bass or melody recording with little background noise.");return;}
  result.startBeat=clip.startBeat;
  const int count=int(result.notes.size());
  int newTrack=0;
  {
   const juce::ScopedLock lock(audioLock);
   veysona::Track notes;notes.name=clip.name+" - MIDI transcription";
   notes.instrument=veysona::Instrument::Wavetable;
   notes.midi.push_back(std::move(result));
   session.tracks.push_back(std::move(notes));plugins.emplace_back();prepareEngine();
   newTrack=int(session.tracks.size())-1;
  }
  selectTrack(newTrack);selectedMidiRegion=0;selectedClip=-1;
  if(!detailsVisible)setDetailsVisible(true);tabPiano.triggerClick();
  setStatus("Created independent MIDI track: "+juce::String(count)+" editable notes  |  verify pitches manually");
  arrangement.repaint();piano.repaint();
 }catch(const std::exception& e){alert("Audio to MIDI failed",e.what());}
}
void Studio::bounceSelectedTrack(){
 if(playing.load()||recording.load()){alert("Stop transport","Stop playback and recording before bouncing a track.");return;}
 if(selectedTrack<0||selectedTrack>=int(session.tracks.size())||session.tracks.size()>=128){
  alert("Cannot bounce","Select a valid track with fewer than 128 total tracks.");return;}
 try{
  // Rendering involves CPU work and allocations, always outside audio callback.
  auto clip=veysona::workflow::bounceBuiltInTrack(session,size_t(selectedTrack));
  veysona::Track bounce;bounce.name=session.tracks[size_t(selectedTrack)].name+" - Bounced";
  bounce.instrument=veysona::Instrument::Sine;bounce.clips.push_back(std::move(clip));
  int index=0;
  {
   const juce::ScopedLock lock(audioLock);
   session.tracks[size_t(selectedTrack)].mute=true; // Keep original for re-editing.
   session.tracks.push_back(std::move(bounce));plugins.emplace_back();prepareEngine();
   index=int(session.tracks.size())-1;
  }
  selectTrack(index,0);setStatus("Bounced native instrument; source track preserved and muted");
 }catch(const std::exception& e){alert("Bounce unavailable",e.what());}
}
void Studio::wavetablePreset(int preset){
 if(selectedTrack<0||selectedTrack>=int(session.tracks.size()))return;
 const juce::ScopedLock lock(audioLock);
 auto& t=session.tracks[size_t(selectedTrack)];t.instrument=veysona::Instrument::Wavetable;
 constexpr float morph[]={0.f,.34f,.68f,.94f};
 constexpr float detune[]={0.f,6.f,18.f,2.f};
 t.synth.wavetableMorph=morph[std::clamp(preset,0,3)];
 t.synth.wavetableDetuneCents=detune[std::clamp(preset,0,3)];
 t.synth.wavetableSecondOscMix=preset==0?0.f:.28f;
 updateControls();setStatus("Veysona Wave preset applied to selected track");
}
void Studio::showQuickActions(){
 juce::PopupMenu menu;
 menu.addSectionHeader("Quick actions");
 menu.addItem("New project             Ctrl+N",[this]{createBlankProject();});
 menu.addItem("Open a project          Ctrl+O",[this]{openButton.triggerClick();});
 menu.addItem("Recent projects",[this]{showRecentProjects();});
 menu.addItem("Save project            Ctrl+S",[this]{saveCurrent();});
 menu.addSeparator();
 menu.addItem("Import audio             Ctrl+I",[this]{importButton.triggerClick();});
 menu.addItem("Add MIDI instrument",[this]{midiButton.triggerClick();});
 menu.addItem("Add sampler",[this]{samplerButton.triggerClick();});
 menu.addItem("Add drum rack",[this]{drumButton.triggerClick();});
 menu.addItem("New song from template",[this]{showTemplateChooser();});
 menu.addItem("Bounce built-in instrument to audio",[this]{bounceSelectedTrack();});
 menu.addItem("Load VST3",[this]{pluginButton.triggerClick();});
 menu.addSeparator();
 menu.addItem("Open demo song",[this]{confirmDemoReplace();});
 menu.addItem("Recovery snapshots",[this]{showRecoveryMenu();});
 menu.addItem("Audio to MIDI (mono)",[this]{audioToMidi();});
 menu.addItem("Preferences             Ctrl+P",[this]{showPreferences();});
 menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&moreButton));
}

void Studio::showRecoveryMenu(){
 auto folder=autosaveFolder==juce::File{}?defaultAutosaveFolder():autosaveFolder;
 juce::Array<juce::File> all;
 if(folder.isDirectory())folder.findChildFiles(all,juce::File::findFiles,false,"*.veys");
 std::vector<juce::File> snapshots;
 for(const auto& file:all)if(file.getFileName().contains(".recovery."))snapshots.push_back(file);
 std::sort(snapshots.begin(),snapshots.end(),[](const auto& a,const auto& b){return a.getLastModificationTime()>b.getLastModificationTime();});
 juce::PopupMenu menu;
 menu.addSectionHeader("Recover an autosaved project");
 if(snapshots.empty())menu.addItem("No snapshots found",false,false,[]{});
 for(int i=0;i<std::min(15,int(snapshots.size()));++i){
  const auto file=snapshots[size_t(i)];
  menu.addItem(file.getFileName()+"   |   "+file.getLastModificationTime().toString(true,true),
    [this,file]{loadProject(file,true);});
 }
 menu.addSeparator();
 menu.addItem("Open autosave folder",[folder]{folder.revealToUser();});
 menu.addItem("Choose a different folder...",[this]{chooseAutosaveFolder();});
 menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&menuBar));
}

void Studio::confirmDemoReplace(){
 if(session.tracks.empty()){loadDemo();return;}
 juce::Component::SafePointer<Studio> safe(this);
 auto msg=juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon)
  .withTitle("Open Veysona demo project")
  .withMessage("Loading the demo replaces the current arrangement. Save your work first if needed.")
  .withButton("Replace with Demo").withButton("Cancel");
 juce::AlertWindow::showAsync(msg,[safe](int choice){if(safe!=nullptr&&choice==1)safe->loadDemo();});
}


juce::File Studio::updateStatusLocation() const {
 const auto local=juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA",{});
 const auto base=local.isNotEmpty()?juce::File(local):juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
 return base.getChildFile("Veysona Studio").getChildFile("updates").getChildFile("current-check.json");
}
juce::File Studio::updaterToolFile() {
 const auto exe=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
 auto tool=exe.getChildFile("tools").getChildFile("veysona_update_client.ps1");
 if(tool.existsAsFile())return tool;
 return findSamples().getParentDirectory().getParentDirectory().getChildFile("tools").getChildFile("veysona_update_client.ps1");
}
bool Studio::launchUpdater(const juce::StringArray& flags) {
#if JUCE_WINDOWS
 const auto script=updaterToolFile();
 if(!script.existsAsFile()) {setStatus("Update service missing: tools/veysona_update_client.ps1");return false;}
 juce::StringArray cmd{"powershell.exe","-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",script.getFullPathName()};
 cmd.addArray(flags);
 juce::ChildProcess child;
 if(!child.start(cmd)){setStatus("Could not start the Windows update service");return false;}
 return true;
#else
 juce::ignoreUnused(flags);
 setStatus("Automatic updates are currently Windows-only.");
 return false;
#endif
}
void Studio::splashUpdateMessage(const juce::String& message){
 if(splashUpdateLabel!=nullptr)splashUpdateLabel->setText(message,juce::dontSendNotification);
}
void Studio::checkForUpdates(bool fromStartup){
 if(updateBusy)return;
 const juce::String manifest=VEYSONA_UPDATE_MANIFEST_URL;
 if(!veysona::update::isHttpsReleaseUrl(manifest.toStdString())){
  splashUpdateMessage("Updates not connected — configure a HTTPS release channel");
  if(!fromStartup)alert("Veysona updates","No update feed has been configured in this build. See PUBLISH_UPDATES_v1.31.md.");
  return;
 }
#if !JUCE_WINDOWS
 splashUpdateMessage("Automatic update checks require Windows");
 return;
#endif
 updateRequestId=juce::Uuid().toString();updateStatusFile=updateStatusLocation();
 updateStatusFile.getParentDirectory().createDirectory();updateStatusFile.deleteFile();
 updateBusy=true;updateIsDownload=false;updateCheckOnStartup=fromStartup;
 updateRequestStartMillis=juce::Time::currentTimeMillis();
 splashUpdateMessage("Checking for updates...");
 setStatus("Checking for Veysona Studio updates...");
 juce::StringArray args{"-Mode","Check","-ManifestUrl",manifest,"-InstalledVersion",juce::JUCEApplication::getInstance()->getApplicationVersion(),
  "-StatusPath",updateStatusFile.getFullPathName(),"-RequestId",updateRequestId};
 if(!launchUpdater(args)){updateBusy=false;splashUpdateMessage("Update service unavailable");}
}
void Studio::startUpdateDownload(){
 if(updateBusy || updateVersion.isEmpty())return;
 if(!veysona::update::canInstallAutomatically(juce::String(VEYSONA_UPDATE_SIGNER_THUMBPRINT).toStdString())){
  alert("Veysona updates","This build has no pinned Authenticode publisher certificate. Automatic installation is disabled. Download from the official release page instead.");return;
 }
 updateRequestId=juce::Uuid().toString();updateStatusFile=updateStatusLocation();updateStatusFile.deleteFile();
 updateBusy=true;updateIsDownload=true;
 updateRequestStartMillis=juce::Time::currentTimeMillis();
 splashUpdateMessage("Downloading verified update...");setStatus("Downloading Veysona "+updateVersion+" in the background...");
 juce::StringArray args{"-Mode","Download","-InstallerUrl",updateInstallerUrl,
  "-ReleaseVersion",updateVersion,"-ExpectedSha256",updateSha256,
  "-ExpectedSize",updateExpectedSize,"-ExpectedThumbprint",VEYSONA_UPDATE_SIGNER_THUMBPRINT,
  "-StatusPath",updateStatusFile.getFullPathName(),"-RequestId",updateRequestId};
 if(!launchUpdater(args)){updateBusy=false;}
}
void Studio::displayUpdateOffer(){
 juce::Component::SafePointer<Studio> safe(this);
 const bool signedBuild=veysona::update::canInstallAutomatically(juce::String(VEYSONA_UPDATE_SIGNER_THUMBPRINT).toStdString());
 auto message=juce::String("Veysona Studio ")+updateVersion+" is available.\n\n"+updateNotes;
 if(!signedBuild)message+="\n\nAutomatic installation requires a configured code-signing certificate. You can open the official release page.";
 auto options=juce::MessageBoxOptions().withTitle("Veysona Studio update available").withMessage(message)
  .withButton(signedBuild?"Download & Install":"Open release page").withButton("Later");
 juce::AlertWindow::showAsync(options,[safe,signedBuild](int choice){
  if(safe==nullptr||choice!=1)return;
  if(signedBuild)safe->startUpdateDownload();
  else if(veysona::update::isHttpsReleaseUrl(safe->updateReleasePage.toStdString()))juce::URL(safe->updateReleasePage).launchInDefaultBrowser();
  else juce::URL(safe->updateInstallerUrl).launchInDefaultBrowser();
 });
}
void Studio::displayInstallOffer(){
 juce::Component::SafePointer<Studio> safe(this);
 auto options=juce::MessageBoxOptions().withTitle("Veysona Studio update ready")
  .withMessage("Veysona Studio "+updateVersion+" has downloaded and its SHA-256 and Windows publisher signature were verified.\n\nSave your project first. Installing will close Veysona and restart it once Setup completes.")
  .withButton("Install & Restart").withButton("Later");
 juce::AlertWindow::showAsync(options,[safe](int choice){if(choice==1&&safe!=nullptr)safe->installVerifiedUpdate();});
}
void Studio::installVerifiedUpdate(){
#if JUCE_WINDOWS
 if(!downloadedInstaller.existsAsFile()) {alert("Update missing","The downloaded installer is no longer available.");return;}
 juce::StringArray args{"-Mode","Install","-InstallerPath",downloadedInstaller.getFullPathName(),
  "-ExpectedSha256",updateSha256,"-ExpectedThumbprint",VEYSONA_UPDATE_SIGNER_THUMBPRINT,
  "-ParentProcessId",juce::String(::GetCurrentProcessId()),
  "-AppExe",juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName()};
 if(launchUpdater(args)){
  setStatus("Closing Veysona for verified update installation...");
  juce::JUCEApplication::getInstance()->systemRequestedQuit();
 }
#endif
}
void Studio::pollUpdateStatus(){
 if(!updateBusy||!updateStatusFile.existsAsFile())return;
 if(updateStatusFile.getSize()>65536){updateBusy=false;updateStatusFile.deleteFile();return;}
 const auto status=juce::JSON::parse(updateStatusFile.loadFileAsString());
 if(!status.isObject() || status.getProperty("request_id",{})!=juce::var(updateRequestId))return;
 const auto state=status.getProperty("state",{}).toString();
 if(state=="downloading")return; // A downloaded update is never launched before both checks finish.
 updateBusy=false;updateStatusFile.deleteFile();
 const auto message=status.getProperty("message",{}).toString();
 if(state=="up_to_date") {splashUpdateMessage("You're up to date");setStatus(message);return;}
 if(state=="error") {splashUpdateMessage("Couldn't check for updates");setStatus("Update check: "+message);return;}
 if(state=="available" && !updateIsDownload){
  updateVersion=status.getProperty("version",{}).toString();
  updateInstallerUrl=status.getProperty("installer_url",{}).toString();
  updateSha256=status.getProperty("sha256",{}).toString();
  updateExpectedSize=status.getProperty("size_bytes",{}).toString();
  updateReleasePage=status.getProperty("release_page",{}).toString();
  updateNotes=status.getProperty("release_notes",{}).toString();
  if(!veysona::update::isNewer(updateVersion.toStdString(),juce::JUCEApplication::getInstance()->getApplicationVersion().toStdString()) ||
     !veysona::update::isHttpsReleaseUrl(updateInstallerUrl.toStdString()) ||
     !veysona::update::isHexDigest(updateSha256.toStdString(),64))return;
  splashUpdateMessage("Update "+updateVersion+" available");setStatus(message);
  juce::Component::SafePointer<Studio> safe(this);
  juce::Timer::callAfterDelay(updateCheckOnStartup?2500:100,[safe]{if(safe!=nullptr)safe->displayUpdateOffer();});
  return;
 }
 if(state=="ready" && updateIsDownload){
  downloadedInstaller=juce::File(status.getProperty("installer_path",{}).toString());
  if(downloadedInstaller.existsAsFile()){
   splashUpdateMessage("Update ready to install");setStatus(message);
   displayInstallOffer();
  }
 }
}

void Studio::showStartupSplash(){
 // No image recreation: the same transparent asset is used on the splash
 // and the application header. The window is non-blocking and short-lived.
 if(!brandLogo.isValid()){openStartupProject();return;}
 class VeysonaSplashContent final : public juce::Component {
  juce::Image logo;
 public:
  juce::Label updateStatus;
  explicit VeysonaSplashContent(const juce::Image& image):logo(image){
   setSize(640,360);addAndMakeVisible(updateStatus);
   updateStatus.setJustificationType(juce::Justification::centred);
   updateStatus.setColour(juce::Label::textColourId,C::muted);
   updateStatus.setFont(juce::Font(12.f));
   updateStatus.setText("Checking for updates...",juce::dontSendNotification);
   updateStatus.setBounds(45,294,550,30);
  }
  juce::Label* statusLabel(){return &updateStatus;}
  void paint(juce::Graphics& g) override{
   g.fillAll(C::bg);
   juce::ColourGradient background(C::top.brighter(.12f),0.f,0.f,C::bg,0.f,float(getHeight()),false);
   g.setGradientFill(background);g.fillRect(getLocalBounds());
   g.setColour(C::purple.withAlpha(.035f));g.fillEllipse(130.f,15.f,380.f,300.f);
   g.drawImageWithin(logo,65,26,getWidth()-130,getHeight()-85,
                     juce::RectanglePlacement::centred|juce::RectanglePlacement::onlyReduceInSize);
   g.setColour(C::outline);g.fillRect(0,getHeight()-32,getWidth(),1);
   g.setColour(C::muted);g.setFont(juce::Font(12.f));
   g.drawFittedText("VEYSONA STUDIO  |  AUDIO PRODUCTION",16,getHeight()-26,getWidth()-32,20,
                    juce::Justification::centred,1);
  }
 };
 juce::DialogWindow::LaunchOptions opts;
 auto* splashContents=new VeysonaSplashContent(brandLogo);
 splashUpdateLabel=splashContents->statusLabel();
 opts.content.setOwned(splashContents);
 opts.dialogTitle="Veysona Studio";
 opts.dialogBackgroundColour=C::bg;
 opts.useNativeTitleBar=false;
 opts.resizable=false;
 auto* splash=opts.launchAsync();
 checkForUpdates(true);
 juce::Component::SafePointer<juce::DialogWindow> safeSplash(splash);
 juce::Component::SafePointer<Studio> safe(this);
 juce::Timer::callAfterDelay(1400,[safe,safeSplash]{
  if(safeSplash!=nullptr)safeSplash->exitModalState(0);
  if(safe!=nullptr)safe->openStartupProject();
 });
}

void Studio::showWelcome(){
 juce::Component::SafePointer<Studio> safe(this);
 auto msg=juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::InfoIcon)
  .withTitle("Welcome to Veysona Studio")
  .withMessage("Create music your way. Try our original demo song, begin a blank project, or open an existing session.\n\nYou can change startup behaviour under Preferences  ->  Project.")
  .withButton("Load Demo").withButton("Blank Project").withButton("Open Project");
 juce::AlertWindow::showAsync(msg,[safe](int choice){
  if(safe==nullptr)return;
  safe->welcomeCompleted=true;safe->savePreferences();
  if(choice==1)safe->confirmDemoReplace();
  else if(choice==3){
   safe->choose(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,
    "Open Veysona project",[safe](juce::File file){if(safe!=nullptr)safe->loadProject(file);},"*.veys;*.odaw");
  }
  else {
   if(safe->session.tracks.empty()){safe->createAudioTrack();safe->setStatus("New blank project. Save it with Ctrl+S.");}
   else safe->createBlankProject();
  }
 });
}

void Studio::openStartupProject(){
 switch(veysona::recovery::chooseStartup(welcomeCompleted,startupMode,lastOpenedProject.existsAsFile())){
  case veysona::recovery::StartupAction::Welcome:showWelcome();return;
  case veysona::recovery::StartupAction::Demo:loadDemo();return;
  case veysona::recovery::StartupAction::LastProject:loadProject(lastOpenedProject);return;
  case veysona::recovery::StartupAction::Blank:createAudioTrack();
   setStatus("Ready  -  import audio, record or start a MIDI track");return;
 }
}

void Studio::savePreferences(){
 try{
  auto f=preferencesFile();if(f.getParentDirectory().createDirectory().failed())return;
  juce::var root(new juce::DynamicObject());auto* obj=root.getDynamicObject();
  obj->setProperty("metronome",metronome.load());
  obj->setProperty("masterGain",session.master.gain);
  obj->setProperty("masterLimiter",session.master.limiterEnabled);
  obj->setProperty("looping",looping);obj->setProperty("autosave",autosaveEnabled);obj->setProperty("launchQuantisation",launcher.quantisation());
  obj->setProperty("autosaveMinutes",autosaveMinutes);
  obj->setProperty("autosaveFolder",(autosaveFolder==juce::File{}?defaultAutosaveFolder():autosaveFolder).getFullPathName());
  obj->setProperty("welcomeCompleted",welcomeCompleted);obj->setProperty("startupMode",startupMode);
  obj->setProperty("lastOpenedProject",lastOpenedProject.getFullPathName());
  juce::Array<juce::var> recentItems;for(const auto& path:recentProjectPaths)recentItems.add(path);
  obj->setProperty("recentProjects",juce::var(recentItems));
  obj->setProperty("detailsVisible",detailsVisible);obj->setProperty("browserVisible",browserVisible);
  obj->setProperty("browserDockWidth",browserDockWidth);obj->setProperty("editorDockHeight",editorDockHeight);
  obj->setProperty("pianoDockFraction",pianoDockFraction);obj->setProperty("instrumentDockFraction",instrumentDockFraction);
  obj->setProperty("snapEnabled",snapEnabled);obj->setProperty("followPlayhead",followPlayhead);
  obj->setProperty("accent",accentPreset);
  juce::Array<juce::var> locations;for(const auto& item:additionalPluginFolders)locations.add(item);
  obj->setProperty("pluginFolders",juce::var(locations));
  juce::TemporaryFile temporary(f);
  if(!temporary.getFile().replaceWithText(juce::JSON::toString(root,true)) ||
     !temporary.overwriteTargetFileWithTemporary()) setStatus("Could not safely save preferences");
 }catch(...){setStatus("Could not save settings");}
}
void Studio::loadPreferences(){
 auto f=preferencesFile();
 if(!f.existsAsFile()){
  const auto old=juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("OpenDAWStudio").getChildFile("preferences.json");
  if(old.existsAsFile())f=old;
 }
 if(!f.existsAsFile())return;
 auto root=juce::JSON::parse(f);if(!root.isObject())return;
 bool click=bool(root.getProperty("metronome",false));metronome.store(click);
 metronomeButton.setButtonText(click?"Click: ON":"Click: OFF");
 session.master.gain=std::clamp(float(double(root.getProperty("masterGain",.85))),0.f,1.5f);
 session.master.limiterEnabled=bool(root.getProperty("masterLimiter",true));
 masterGain.setValue(session.master.gain,juce::dontSendNotification);
 autosaveEnabled=bool(root.getProperty("autosave",true));
 autosaveMinutes=veysona::recovery::clampInterval(int(root.getProperty("autosaveMinutes",3)));
 auto savedAutosaveFolder=root.getProperty("autosaveFolder","").toString();
 autosaveFolder=savedAutosaveFolder.isNotEmpty()&&juce::File::isAbsolutePath(savedAutosaveFolder)
  ?juce::File(savedAutosaveFolder):defaultAutosaveFolder();
 welcomeCompleted=bool(root.getProperty("welcomeCompleted",false));
 startupMode=std::clamp(int(root.getProperty("startupMode",0)),0,3);
 auto recent=root.getProperty("lastOpenedProject","").toString();
 lastOpenedProject=recent.isNotEmpty()&&juce::File::isAbsolutePath(recent)?juce::File(recent):juce::File{};
 auto recentValue=root.getProperty("recentProjects",{});
 if(auto* recentArray=recentValue.getArray())for(const auto& item:*recentArray){
  const auto path=item.toString();if(path.isNotEmpty()&&juce::File::isAbsolutePath(path)&&
     !recentProjectPaths.contains(path)&&recentProjectPaths.size()<12)recentProjectPaths.add(path);
 }
 if(recentProjectPaths.isEmpty()&&lastOpenedProject!=juce::File{})recentProjectPaths.add(lastOpenedProject.getFullPathName());
 browserVisible=bool(root.getProperty("browserVisible",true));
 browserDockWidth=std::clamp(int(root.getProperty("browserDockWidth",294)),250,560);
 editorDockHeight=std::clamp(int(root.getProperty("editorDockHeight",385)),310,1100);
 pianoDockFraction=std::clamp(float(double(root.getProperty("pianoDockFraction",.42))),.33f,.60f);
 instrumentDockFraction=std::clamp(float(double(root.getProperty("instrumentDockFraction",.22))),.18f,.38f);
 setDetailsVisible(bool(root.getProperty("detailsVisible",true)));
 snapEnabled=bool(root.getProperty("snapEnabled",true));
 followPlayhead=bool(root.getProperty("followPlayhead",false));
 snapButton.setButtonText(snapEnabled?"Snap: 1/16":"Snap: Off");
 followButton.setButtonText(followPlayhead?"Follow: On":"Follow: Off");
 {const juce::ScopedLock lock(audioLock);
  launcher.setQuantisation(std::clamp(double(root.getProperty("launchQuantisation",4.)),0.,double(session.beatsPerBar)*2.));}
 looping=bool(root.getProperty("looping",true));loopButton.setButtonText(looping?"Loop: ON":"Loop: OFF");
 accentPreset=std::clamp(int(root.getProperty("accent",0)),0,2);applyAccent(accentPreset);
 auto folders=root.getProperty("pluginFolders",{});
 if(auto* entries=folders.getArray())for(const auto& path:*entries){
  auto item=path.toString();if(item.isNotEmpty()&&!additionalPluginFolders.contains(item))additionalPluginFolders.add(item);
 }
}
void Studio::loadSampleInstrument(juce::File file){
 if(!file.existsAsFile())return;
 std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
 if(!reader||reader->lengthInSamples<=0||reader->lengthInSamples>int64_t(reader->sampleRate*15)){
  alert("Sampler","Choose a supported WAV, MP3, FLAC, OGG or AIFF instrument sample up to 15 seconds.");return;
 }
 juce::AudioBuffer<float> data(2,int(reader->lengthInSamples));
 if(!reader->read(&data,0,data.getNumSamples(),0,true,true)){
  alert("Sampler","Could not decode instrument sample.");return;
 }
 veysona::Clip source;source.name=file.getFileNameWithoutExtension().toStdString();
 source.sampleRate=uint32_t(reader->sampleRate);source.stereo.resize(size_t(data.getNumSamples())*2);
 for(int i=0;i<data.getNumSamples();++i){source.stereo[size_t(i)*2]=data.getSample(0,i);source.stereo[size_t(i)*2+1]=data.getSample(1,i);}
 {
  const juce::ScopedLock lock(audioLock);
  if(selectedTrack<0||selectedTrack>=int(session.tracks.size())){
   veysona::Track track;track.name="Sample Instrument";veysona::MidiRegion clip;
   clip.name="Sampler Pattern";clip.lengthBeats=8;clip.notes.push_back({60,0,1,.8f});track.midi.push_back(std::move(clip));
   session.tracks.push_back(std::move(track));prepareEngine();selectedTrack=int(session.tracks.size())-1;
  }
  auto& track=session.tracks[size_t(selectedTrack)];
  const int root=file.getFileName().containsIgnoreCase("C2")?36:(file.getFileName().containsIgnoreCase("C3")?48:60);
  veysona::assignSamplerSample(track,source,root);
  if(track.midi.empty()){
   veysona::MidiRegion region;region.name="Sampler Pattern";region.lengthBeats=8;region.notes.push_back({60,0,1,.8f});
   track.midi.push_back(region);
  }
 }
 selectTrack(selectedTrack);
 setStatus("Loaded keyboard sampler: "+file.getFileName()+" (root note shown in project settings)");
}
void Studio::loadMultisampleInstrument(const juce::String& program){
 const auto dir=recordedInstrumentPack(program);
 juce::Array<juce::File> wavs;
 if(dir.isDirectory())dir.findChildFiles(wavs,juce::File::findFiles,false,"*.wav");
 struct SampleFile {int root,layer;juce::File file;};
 std::vector<SampleFile> sources;
 for(const auto& f:wavs){
  const auto base=f.getFileNameWithoutExtension();
  const auto rootString=base.upToFirstOccurrenceOf("_v",false,false);
  if(!rootString.containsOnly("0123456789"))continue;
  const int pitch=rootString.getIntValue();
  int layer=1;
  if(base.contains("_v")){
   const auto layerString=base.fromFirstOccurrenceOf("_v",false,false);
   if(!layerString.containsOnly("0123456789"))continue;
   layer=layerString.getIntValue();
  }
  if(pitch>=0&&pitch<=127&&layer>=1&&layer<=4)sources.push_back({pitch,layer,f});
 }
 std::sort(sources.begin(),sources.end(),[](const auto& a,const auto& b){
  return a.root==b.root?a.layer<b.layer:a.root<b.root;
 });
 if(sources.empty()||sources.size()>32){alert("Recorded instruments","This instrument has no valid recorded WAV zones. See RECORDED_INSTRUMENTS.md to install the CC0 recording packs.");return;}
 std::vector<veysona::SamplerZone> zones;
 try{
  for(size_t i=0;i<sources.size();++i){
   const auto clip=veysona::readWav(sources[i].file.getFullPathName().toStdString());
   veysona::SamplerZone z;z.rootNote=sources[i].root;
   int previousRoot=-1,nextRoot=-1,maxLayer=1;
   for(const auto& candidate:sources){
    if(candidate.root<z.rootNote)previousRoot=std::max(previousRoot,candidate.root);
    if(candidate.root>z.rootNote&&(nextRoot<0||candidate.root<nextRoot))nextRoot=candidate.root;
    if(candidate.root==z.rootNote)maxLayer=std::max(maxLayer,candidate.layer);
   }
   z.lowNote=previousRoot<0?0:(previousRoot+z.rootNote+1)/2;
   z.highNote=nextRoot<0?127:(z.rootNote+nextRoot)/2;
   z.velocityLow=float(sources[i].layer-1)/float(maxLayer);
   z.velocityHigh=float(sources[i].layer)/float(maxLayer);
   z.sampleRate=clip.sampleRate;z.stereo=clip.stereo;z.name=clip.name;
   zones.push_back(std::move(z));
  }
 }catch(const std::exception& e){alert("Recorded instrument load failed",e.what());return;}
 int target=selectedTrack;
 {
  const juce::ScopedLock lock(audioLock);
  if(target<0||target>=int(session.tracks.size())||session.tracks.empty()){
   if(session.tracks.size()>=128){alert("Track limit","128 tracks maximum");return;}
   session.tracks.emplace_back();prepareEngine();target=int(session.tracks.size())-1;
  }
  auto& t=session.tracks[size_t(target)];
  veysona::assignSamplerZones(t,std::move(zones),program.toStdString());
  t.name=program.toStdString();
  if(t.midi.empty()){
   veysona::MidiRegion pattern;pattern.name=program.toStdString()+" Pattern";
   pattern.lengthBeats=4;pattern.notes.push_back({60,0,1,.85f});
   t.midi.push_back(std::move(pattern));
  }
 }
 selectTrack(target);
 setStatus("Loaded recorded instrument ("+juce::String(int(sources.size()))+" velocity/key recordings): "+program);
}
void Studio::populateDrumKit(veysona::Track& track,const juce::String& kit){
 const std::array<std::pair<int,const char*>,8> names{{
  {36,"Kick"},{38,"Snare"},{42,"Closed Hat"},{46,"Open Hat"},
  {37,"Rim"},{41,"Low Tom"},{49,"Crash"},{43,"High Tom"}
 }};
 const auto dir=recordedDrumPack(kit);
 for(size_t pad=0;pad<names.size();++pad){
  const auto note=names[pad].first;
  const auto wav=dir.getChildFile(juce::String::formatted("%03d - ",note)+names[pad].second+".wav");
  if(!wav.existsAsFile())continue;
  try{
   const auto clip=veysona::readWav(wav.getFullPathName().toStdString());
   veysona::assignDrumSample(track,pad,note,clip);
   track.drumRack[pad].name=names[pad].second;
  }catch(...){/* Missing or corrupt samples must not be replaced with synthesized kicks/snares. */}
 }
 track.instrument=veysona::Instrument::DrumRack;
 track.drumRackRecordedOnly=dir.getChildFile("RECORDED_SOURCE.json").existsAsFile();
}
void Studio::addDrumRack(const juce::String& kit){
 const auto base=recordedDrumPack(kit);
 if(!base.getChildFile("RECORDED_SOURCE.json").existsAsFile()){
  alert("Recorded drum kit not installed","Install the CC0 acoustic drum pack using tools/install_recorded_instruments.py before creating a Drum Rack. No synthesized substitutes will be presented as recorded drums.");
  return;
 }
 veysona::Track track;track.name=(kit+" Drum Rack").toStdString();track.instrument=veysona::Instrument::DrumRack;
 populateDrumKit(track,kit);
 for(const auto& slot:track.drumRack)if(slot.stereo.empty()){
  alert("Incomplete recorded drum kit","Some recorded drum WAVs are missing or invalid. Re-run the sample installer.");
  return;
 }
 veysona::MidiRegion pattern;pattern.name=kit.toStdString()+" Drum Groove";pattern.lengthBeats=16;
 for(int bar=0;bar<4;++bar)for(int step=0;step<16;++step){
  const int absoluteStep=bar*16+step;
  if(step%2==0)veysona::drums::setStep(pattern,42,absoluteStep, true,step%4==0?.72f:.45f);
  if(step==0||step==8)veysona::drums::setStep(pattern,36,absoluteStep,true,.93f);
  if(step==4||step==12)veysona::drums::setStep(pattern,38,absoluteStep,true,.86f);
 }
 track.midi.push_back(std::move(pattern));
 int newIndex=-1;
 {const juce::ScopedLock lock(audioLock);
  if(session.tracks.size()>=128){alert("Track limit","128 tracks maximum");return;}
  session.tracks.push_back(std::move(track));prepareEngine();newIndex=int(session.tracks.size())-1;
 }
 selectTrack(newIndex);
 setStatus(kit+" Drum Rack ready - click steps, Shift for accents, right-click to erase.");
}

void Studio::showProjectHealth(){
 if(playing.load()||recording.load()){
  alert("Project Health", "Stop playback or recording before running a full project check.");return;
 }
 juce::String summary;
 {
  const juce::ScopedLock lock(audioLock);
  summary=juce::String::fromUTF8(veysona::health::inspect(session).summary().c_str());
 }
 // Keep the user informed without manipulating or overwriting their project.
 alert("Veysona Project Health",summary.substring(0,5000));
}

void Studio::applyGroovePreset(veysona::groove::Style style){
 if(playing.load()||recording.load()){
  alert("Groove Lab", "Stop playback or recording before replacing a MIDI pattern.");return;
 }
 if(selectedTrack<0 || selectedTrack>=int(session.tracks.size()) ||
    session.tracks[size_t(selectedTrack)].instrument!=veysona::Instrument::DrumRack){
  addDrumRack();
 }
 {
  const juce::ScopedLock lock(audioLock);
  if(selectedTrack<0||selectedTrack>=int(session.tracks.size()))return;
  auto& track=session.tracks[size_t(selectedTrack)];
  if(track.midi.empty())track.midi.emplace_back();
  selectedMidiRegion=std::clamp(selectedMidiRegion,0,int(track.midi.size())-1);
  saveMidiUndo();
  auto& region=track.midi[size_t(selectedMidiRegion)];
  veysona::groove::Settings options;options.style=style;options.bars=4;
  options.swing=style==veysona::groove::Style::Garage?.20:(style==veysona::groove::Style::Trap?.09:.12);
  options.density=.65;
  region.notes=veysona::groove::generate(options);
  region.lengthBeats=16;
  const char* name=style==veysona::groove::Style::House?"House Groove":
    style==veysona::groove::Style::Garage?"UK Garage Groove":
    style==veysona::groove::Style::Breakbeat?"Breakbeat Groove":"Trap Groove";
  region.name=name;
 }
 piano.repaint();arrangement.repaint();
 setStatus("Groove Lab: created editable four-bar MIDI drum pattern (Ctrl+Z to undo notes)");
}

void Studio::previewSample(juce::File file){
 if(!file.existsAsFile())return;
 try{
  auto reader=std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
  if(!reader||reader->lengthInSamples<=0){setStatus("Unable to preview "+file.getFileName());return;}
  const int frames=int(std::min<int64_t>(reader->lengthInSamples,int64_t(reader->sampleRate*10)));
  juce::AudioBuffer<float> data(2,frames);
  if(!reader->read(&data,0,frames,0,true,true))return;
  veysona::Clip clip;clip.name=file.getFileNameWithoutExtension().toStdString();clip.sampleRate=uint32_t(reader->sampleRate);clip.stereo.resize(size_t(frames)*2);
  for(int i=0;i<frames;++i){clip.stereo[size_t(i)*2]=data.getSample(0,i);clip.stereo[size_t(i)*2+1]=data.getSample(1,i);}
  const juce::ScopedLock lock(audioLock);previewAudio=std::move(clip);previewFrame=0;previewPlaying=true;
  setStatus("Previewing "+file.getFileName());
 }catch(...){setStatus("Unable to decode sample preview");}
}
void Studio::mixPreview(float* const* output,int channels,int count){
 if(!previewPlaying||channels<=0||!output[0]||previewAudio.stereo.empty())return;
 auto total=previewAudio.stereo.size()/2;
 const auto step=double(previewAudio.sampleRate)/std::max(8000.,audioRate);
 for(int i=0;i<count;++i){auto index=size_t(previewFrame);if(index>=total){previewPlaying=false;break;}
  auto f=float(previewFrame-index),next=std::min(index+1,total-1);
  for(int ch=0;ch<std::min(channels,2);++ch)if(output[ch]){
   float val=(1-f)*previewAudio.stereo[index*2+size_t(ch)]+f*previewAudio.stereo[next*2+size_t(ch)];
   output[ch][i]=std::clamp(output[ch][i]+val*.64f,-1.f,1.f);
  }
  previewFrame+=step;
 }
}
void Studio::loadEffectPreset(juce::File file){
 if(!file.existsAsFile()||file.getSize()>65536||!file.getFileName().endsWithIgnoreCase(".vfx.json")){
  alert("Effect preset", "Invalid or missing effects-chain preset.");return;
 }
 const auto json=juce::JSON::parse(file.loadFileAsString());
 if(!json.isObject() || json.getProperty("format","").toString()!="veysona-fx-v1"){
  alert("Effect preset", "This file is not a Veysona effects-chain preset.");return;
 }
 if(selectedTrack<0||selectedTrack>=int(session.tracks.size())){
  alert("Effect preset", "Select an audio or instrument track first.");return;
 }
 const auto read=[](const juce::var& value,const char* key,float def,float lo,float hi){
  const auto raw=value.getProperty(key,def);
  const double n=double(raw);
  return std::isfinite(n)?std::clamp(float(n),lo,hi):def;
 };
 const auto flag=[](const juce::var& value,const char* key){return bool(value.getProperty(key,false));};
 {
  const juce::ScopedLock lock(audioLock);
  auto& t=session.tracks[size_t(selectedTrack)];
  t.eq=veysona::EqSettings{};
  auto eq=json.getProperty("eq",{});
  if(auto* arr=eq.getArray())for(int i=0;i<std::min(8,arr->size());++i){
   const auto& v=arr->getReference(i);auto& b=t.eq.bands[size_t(i)];
   b.frequency=read(v,"freq",b.frequency,20.f,20000.f);
   b.gainDb=read(v,"gainDb",0.f,-18.f,18.f);
   b.q=read(v,"q",b.q,.12f,12.f);
   b.mode=veysona::EqMode(std::clamp(int(v.getProperty("type",1)),0,2));
   b.enabled=bool(v.getProperty("enabled",true));
  }
  const auto compressor=json.getProperty("compressor",{});
  t.compressor={};t.compressor.enabled=flag(compressor,"enabled");
  t.compressor.thresholdDb=read(compressor,"thresholdDb",-18.f,-60.f,0.f);
  t.compressor.ratio=read(compressor,"ratio",3.f,1.f,20.f);
  t.compressor.attackMs=read(compressor,"attackMs",10.f,.1f,250.f);
  t.compressor.releaseMs=read(compressor,"releaseMs",120.f,5.f,3000.f);
  t.compressor.makeupDb=read(compressor,"makeupDb",0.f,-12.f,18.f);
  const auto room=json.getProperty("room",{});
  t.room={};t.room.enabled=flag(room,"enabled");
  t.room.size=read(room,"size",.58f,0.f,1.f);
  t.room.damping=read(room,"damping",.45f,0.f,.97f);
  t.room.mix=read(room,"mix",.25f,0.f,1.f);
  const auto deEss=json.getProperty("deEsser",{});
  t.deEsser={};t.deEsser.enabled=flag(deEss,"enabled");
  t.deEsser.frequencyHz=read(deEss,"frequencyHz",5600.f,1500.f,16000.f);
  t.deEsser.thresholdDb=read(deEss,"thresholdDb",-30.f,-70.f,0.f);
  t.deEsser.reduction=read(deEss,"reduction",.6f,0.f,1.f);
  const auto tr=json.getProperty("transient",{});
  t.transient={};t.transient.enabled=flag(tr,"enabled");
  t.transient.attack=read(tr,"attack",.45f,-1.f,1.f);
  t.transient.sustain=read(tr,"sustain",0.f,-1.f,1.f);
  const auto c=json.getProperty("creative",{});
  t.creativeFx={};auto& f=t.creativeFx;
  f.chorusEnabled=flag(c,"chorusEnabled");
  f.chorusMix=read(c,"chorusMix",.28f,0.f,1.f);
  f.chorusDepthMs=read(c,"chorusDepthMs",3.f,.1f,25.f);
  f.chorusRateHz=read(c,"chorusRateHz",.35f,.01f,15.f);
  f.autopanEnabled=flag(c,"autopanEnabled");
  f.autopanDepth=read(c,"autopanDepth",.5f,0.f,1.f);
  f.autopanPeriodBeats=read(c,"autopanPeriodBeats",2.f,.125f,32.f);
  f.crusherEnabled=flag(c,"crusherEnabled");
  f.crusherBits=int(read(c,"crusherBits",10.f,3.f,16.f));
  f.crusherHoldFrames=int(read(c,"crusherHoldFrames",4.f,1.f,48.f));
  f.crusherMix=read(c,"crusherMix",.65f,0.f,1.f);
  f.tremoloEnabled=flag(c,"tremoloEnabled");
  f.tremoloDepth=read(c,"tremoloDepth",.65f,0.f,1.f);
  f.tremoloPeriodBeats=read(c,"tremoloPeriodBeats",1.f,.125f,32.f);
  f.ringModEnabled=flag(c,"ringModEnabled");
  f.ringModFrequencyHz=read(c,"ringModFrequencyHz",34.f,1.f,500.f);
  f.ringModMix=read(c,"ringModMix",.55f,0.f,1.f);
  f.tapeEnabled=flag(c,"tapeEnabled");
  f.tapeDrive=read(c,"tapeDrive",2.5f,0.f,12.f);
  f.tapeMix=read(c,"tapeMix",.65f,0.f,1.f);
  f.phaserEnabled=flag(c,"phaserEnabled");
  f.phaserRateHz=read(c,"phaserRateHz",.35f,.02f,8.f);
  f.phaserDepth=read(c,"phaserDepth",.7f,0.f,1.f);
  f.phaserFeedback=read(c,"phaserFeedback",.15f,-.85f,.85f);
  f.phaserMix=read(c,"phaserMix",.45f,0.f,1.f);
  f.tiltEnabled=flag(c,"tiltEnabled");
  f.tiltDb=read(c,"tiltDb",0.f,-12.f,12.f);
  f.exciterEnabled=flag(c,"exciterEnabled");
  f.exciterDrive=read(c,"exciterDrive",2.5f,1.f,10.f);
  f.exciterMix=read(c,"exciterMix",.3f,0.f,.85f);
  f.resonatorEnabled=flag(c,"resonatorEnabled");
  f.resonatorHz=read(c,"resonatorHz",220.f,45.f,2500.f);
  f.resonatorFeedback=read(c,"resonatorFeedback",.7f,0.f,.92f);
  f.resonatorMix=read(c,"resonatorMix",.35f,0.f,.8f);
  f.wahEnabled=flag(c,"wahEnabled");
  f.wahRateHz=read(c,"wahRateHz",.7f,.03f,9.f);
  f.wahDepth=read(c,"wahDepth",.7f,0.f,1.f);
  f.wahMix=read(c,"wahMix",.55f,0.f,.95f);
  f.foldEnabled=flag(c,"foldEnabled");
  f.foldDrive=read(c,"foldDrive",2.5f,1.f,8.f);
  f.foldMix=read(c,"foldMix",.5f,0.f,1.f);
  f.diodeEnabled=flag(c,"diodeEnabled");f.diodeDrive=read(c,"diodeDrive",3.f,1.f,12.f);f.diodeMix=read(c,"diodeMix",.5f,0.f,1.f);
  f.octaveFuzzEnabled=flag(c,"octaveFuzzEnabled");f.octaveFuzzDrive=read(c,"octaveFuzzDrive",2.5f,1.f,10.f);f.octaveFuzzMix=read(c,"octaveFuzzMix",.4f,0.f,1.f);
  f.crossfeedEnabled=flag(c,"crossfeedEnabled");f.crossfeedAmount=read(c,"crossfeedAmount",.16f,0.f,.45f);
  f.pingPongEnabled=flag(c,"pingPongEnabled");
  f.pingPongDelayMs=read(c,"pingPongDelayMs",280.f,35.f,1800.f);
  f.pingPongFeedback=read(c,"pingPongFeedback",.42f,0.f,.85f);
  f.pingPongMix=read(c,"pingPongMix",.35f,0.f,.85f);
  const auto gate=json.getProperty("gate",{});
  t.gate={};t.gate.enabled=flag(gate,"enabled");
  t.gate.thresholdDb=read(gate,"thresholdDb",-43.f,-90.f,0.f);
  t.gate.attackMs=read(gate,"attackMs",2.f,.1f,250.f);
  t.gate.releaseMs=read(gate,"releaseMs",100.f,5.f,2000.f);
  t.stereoWidth=read(json,"stereoWidth",1.f,0.f,2.f);
  t.drive=read(json,"drive",0.f,0.f,1.f);
  t.delayMix=read(json,"delayMix",0.f,0.f,1.f);
 }
 updateControls();eqView.repaint();mixer.repaint();arrangement.repaint();
 setStatus("Applied FX chain: "+json.getProperty("name",file.getFileNameWithoutExtension()).toString());
}

void Studio::refreshBrowser(){
 browserItems.clear();browserFiles.clear();auto query=search.getText().trim().toLowerCase();juce::Array<juce::File> files;
 // Original built-in effects-chain presets. Applying one configures the *existing*
 // real-time track EQ, dynamics and creative processors; it does not load a plugin.
 if(browserCategory==9){
  const auto folder=sampleFolder.getParentDirectory().getChildFile("effect_presets");
  if(folder.isDirectory()){
   juce::Array<juce::File> presets;
   folder.findChildFiles(presets,juce::File::findFiles,false,"*.vfx.json");
   for(const auto& file:presets){
    const auto name=file.getFileNameWithoutExtension().replace(".vfx", "");
    if(query.isNotEmpty()&&!name.toLowerCase().contains(query))continue;
    browserItems.add(name);browserFiles.push_back(file);
   }
  }
  if(browserItems.isEmpty())browserItems.add("No effects presets match your search");
  browser.updateContent();browser.repaint();return;
 }
 // Dedicated VST3 browser. Scan directory entries only to keep opening the browser responsive.
 if(browserCategory==8){
  std::vector<juce::File> roots;
  #if JUCE_WINDOWS
  roots.emplace_back(juce::File("C:\\Program Files\\Common Files\\VST3"));
  roots.emplace_back(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getParentDirectory().getChildFile("Local").getChildFile("Programs").getChildFile("Common").getChildFile("VST3"));
  #endif
  for(const auto& path:additionalPluginFolders)roots.emplace_back(juce::File(path));
  juce::StringArray added;
  for(const auto& root:roots){
   if(!root.isDirectory())continue;
   juce::Array<juce::File> candidates;
   root.findChildFiles(candidates,juce::File::findFilesAndDirectories,false,"*");
   for(const auto& plug:candidates){
    if(!plug.hasFileExtension(".vst3"))continue;
    const auto location=plug.getFullPathName();
    if(added.contains(location))continue;
    const auto name=plug.getFileNameWithoutExtension();
    if(query.isNotEmpty()&&!name.toLowerCase().contains(query))continue;
    added.add(location);browserItems.add(name+"    |    VST3");browserFiles.push_back(plug);
    if(browserFiles.size()>=250)break;
   }
   if(browserFiles.size()>=250)break;
  }
  if(browserItems.isEmpty())browserItems.add("No VST3 found. Add a plug-in folder in Preferences.");
  browser.updateContent();browser.repaint();return;
 }
 auto libraryDir=(browserCategory==6?sampleFolder.getParentDirectory().getChildFile("demos"):(browserCategory==7?instrumentFolder:sampleFolder));
 if(libraryDir.isDirectory())libraryDir.findChildFiles(files,juce::File::findFiles,true,"*.wav;*.flac;*.mp3;*.ogg");
 // Hide closely matching legacy sounds from the normal browser, without
 // deleting them: older sessions may still reference the original WAV paths.
 juce::StringArray hiddenNearDuplicates;
 if(libraryDir==sampleFolder){
  const auto exclusionFile=sampleFolder.getParentDirectory().getChildFile("V125_LEGACY_DUPLICATE_HIDES.txt");
  if(exclusionFile.existsAsFile())hiddenNearDuplicates.addLines(exclusionFile.loadFileAsString());
 }
 for(const auto& f:files){
  if(hiddenNearDuplicates.contains(f.getFileName()))continue;
  auto name=f.getFileNameWithoutExtension();bool drums=name.containsIgnoreCase("kick")||name.containsIgnoreCase("snare")||name.containsIgnoreCase("hat")||name.containsIgnoreCase("clap")||name.containsIgnoreCase("perc")||name.containsIgnoreCase("tom");bool loop=name.containsIgnoreCase("loop");
  bool synth=name.containsIgnoreCase("bass")||name.containsIgnoreCase("pad")||name.containsIgnoreCase("synth")||name.containsIgnoreCase("keys")||name.containsIgnoreCase("pluck")||name.containsIgnoreCase("chord")||name.containsIgnoreCase("lead")||name.containsIgnoreCase("bell");
  bool fx=name.containsIgnoreCase("fx")||name.containsIgnoreCase("rise")||name.containsIgnoreCase("impact")||name.containsIgnoreCase("texture")||name.containsIgnoreCase("sweep");
  if(browserCategory==1&&!drums)continue;if(browserCategory==2&&!synth)continue;if(browserCategory==3&&!loop)continue;if(browserCategory==4)continue;if(browserCategory==5&&!fx)continue;
  if(!query.isEmpty()&&!name.toLowerCase().contains(query))continue;browserItems.add("   "+name);browserFiles.push_back(f);
 }
 if(browserCategory==4){
  if(midiFolder.isDirectory()){
   juce::Array<juce::File> midiFiles;midiFolder.findChildFiles(midiFiles,juce::File::findFiles,true,"*.mid;*.midi");
   for(const auto& f:midiFiles){if(query.isNotEmpty()&&!f.getFileNameWithoutExtension().toLowerCase().contains(query))continue;
    browserItems.add("   "+f.getFileNameWithoutExtension());browserFiles.push_back(f);}
  }
  if(browserItems.isEmpty()){browserItems.add("Import MIDI file...");browserFiles.push_back(juce::File{});}
 }
 if(browserItems.isEmpty())browserItems.add("No matches  -  check assets/samples");browser.updateContent();browser.repaint();
}
void Studio::listBoxItemClicked(int row,const juce::MouseEvent&){
 // A single click auditions audio without changing the project. Double-click imports.
 if(row<0||row>=int(browserFiles.size())||browserCategory==8||browserCategory==9||browserCategory==4)return;
 const auto f=browserFiles[size_t(row)];
 if(f.existsAsFile()&&browserCategory!=7)previewSample(f);
}
void Studio::listBoxItemDoubleClicked(int row,const juce::MouseEvent&){
 if(row<0||row>=int(browserFiles.size()))return;
 auto file=browserFiles[size_t(row)];
 if(browserCategory==8){if(file.exists())loadPlugin(file);return;}
 if(browserCategory==9){if(file.existsAsFile())loadEffectPreset(file);return;}
 if(browserCategory==4){if(file.existsAsFile())importMidi(file);
  else choose(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,"Import MIDI",[this](juce::File f){importMidi(f);},"*.mid;*.midi");
 }else if(file.existsAsFile()&&browserCategory==7)loadSampleInstrument(file);
 else if(file.existsAsFile())openAudio(file);
}
void Studio::openAudio(juce::File file,double beat){
 std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));if(!reader){alert("Import failed","This audio format could not be decoded: "+file.getFullPathName());return;}
 if(reader->lengthInSamples<=0||reader->lengthInSamples>int64_t(reader->sampleRate*600)){alert("Import limit","Import audio files shorter than 10 minutes in this build.");return;}
 juce::AudioBuffer<float> buf(2,int(reader->lengthInSamples));if(!reader->read(&buf,0,buf.getNumSamples(),0,true,true)){alert("Import error","Unable to read the selected audio file.");return;}
 veysona::Clip clip;clip.name=file.getFileNameWithoutExtension().toStdString();clip.sourcePath=file.getFullPathName().toStdString();clip.sampleRate=uint32_t(reader->sampleRate);clip.startBeat=beat;clip.stereo.resize(size_t(buf.getNumSamples())*2);
 for(int i=0;i<buf.getNumSamples();++i){clip.stereo[size_t(i)*2]=buf.getSample(0,i);clip.stereo[size_t(i)*2+1]=buf.getSample(1,i);}
 veysona::buildWaveformPeaks(clip);
 veysona::Track track;track.name=clip.name;track.clips.push_back(std::move(clip));
 {const juce::ScopedLock lock(audioLock);session.tracks.push_back(std::move(track));prepareEngine();}
 selectTrack(int(session.tracks.size())-1,0);arrangement.repaint();setStatus("Imported "+file.getFileName());
}
void Studio::addMidiTrack(){
 veysona::Track t;t.name="Synth "+std::to_string(session.tracks.size()+1);t.instrument=veysona::Instrument::Saw;
 veysona::MidiRegion clip;clip.name="New MIDI Pattern";clip.lengthBeats=16;clip.notes.push_back({60,0,1,.8f});clip.notes.push_back({64,1,1,.75f});clip.notes.push_back({67,2,1,.75f});t.midi.push_back(clip);
 {const juce::ScopedLock lock(audioLock);session.tracks.push_back(std::move(t));prepareEngine();}
 selectTrack(int(session.tracks.size())-1);arrangement.repaint();
}
void Studio::loadDemo(){
 playing=false;recording=false;currentProject=juce::File{};
 recoverySessionId=juce::String::toHexString(juce::Time::currentTimeMillis());recoverySlot=0;
 midiRecorder.held.clear();juce::Array<juce::File> files;sampleFolder.findChildFiles(files,juce::File::findFiles,true,"*.wav");
 veysona::Session demo;demo.sampleRate=session.sampleRate;demo.bpm=120;demo.master=session.master;
 auto makeTrack=[&](const juce::String& name,const juce::String& filename,const std::vector<int>& beats){
  auto f=sampleFolder.getChildFile(filename);if(!f.existsAsFile())return;
  try{auto sample=veysona::readWav(f.getFullPathName().toStdString());veysona::Track t;t.name=name.toStdString();for(auto b:beats){auto c=sample;c.startBeat=b;c.name=name.toStdString();t.clips.push_back(std::move(c));}demo.tracks.push_back(std::move(t));}catch(...){ }
 };
 makeTrack("Deep Kick","Kick - Deep.wav",{0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30});
 makeTrack("Snare","Snare - Clean.wav",{1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31});
 makeTrack("Hi Hats","HiHat - Bright.wav",{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31});
 auto midiTrack=[&](const std::string& name,veysona::Instrument osc,int row,const std::vector<int>& pitches){veysona::Track t;t.name=name;t.instrument=osc;t.gain=.65f;veysona::MidiRegion r;r.name=name+" Pattern";r.startBeat=0;r.lengthBeats=32;for(int i=0;i<32;++i)r.notes.push_back({pitches[size_t(i)%pitches.size()],double(i),row==0?.65:1.7f,.68f});t.midi.push_back(std::move(r));demo.tracks.push_back(std::move(t));};
 midiTrack("Analog Bass",veysona::Instrument::Square,0,{36,36,43,36,41,41,43,38});midiTrack("Dream Keys",veysona::Instrument::Sine,1,{60,64,67,72,69,65,64,67});
 auto pad=sampleFolder.getChildFile("Pad - Warm.wav");if(pad.existsAsFile())makeTrack("Atmosphere","Pad - Warm.wav",{0,4,8,12,16,20,24,28});
 {const juce::ScopedLock lock(audioLock);plugins.clear();session=std::move(demo);prepareEngine();playCursor=0;engine.reset();}
 selectedTrack=0;selectedClip=-1;selectedMidiRegion=0;selectedNote=-1;midiUndo.clear();midiRedo.clear();audioUndo.clear();audioRedo.clear();eqHistory.clear();bpm.setValue(120,juce::dontSendNotification);updateControls();arrangement.repaint();piano.repaint();mixer.repaint();
 setStatus("Loaded original demo project  -  press Play");
}
void Studio::updateControls(){
 loadingUI=true;bool valid=selectedTrack>=0&&selectedTrack<int(session.tracks.size());
 juce::String displayName=valid?jstr(session.tracks[size_t(selectedTrack)].name):"No track selected";
 if(valid&&selectedTrack<int(plugins.size())&&!plugins[size_t(selectedTrack)].empty())displayName+="  ["+juce::String(int(plugins[size_t(selectedTrack)].size()))+" VST3]";
 trackName.setText(displayName,juce::dontSendNotification);
 for(auto* s:{&volume,&pan,&cutoff,&drive,&delay,&compThreshold,&compRatio})s->setEnabled(valid);instrument.setEnabled(valid);eqToggle.setEnabled(valid);compressorToggle.setEnabled(valid);
 if(valid){const auto& t=session.tracks[selectedTrack];volume.setValue(t.gain,juce::dontSendNotification);pan.setValue(t.pan,juce::dontSendNotification);cutoff.setValue(t.cutoff,juce::dontSendNotification);drive.setValue(t.drive,juce::dontSendNotification);delay.setValue(t.delayMix,juce::dontSendNotification);
  compThreshold.setValue(t.compressor.thresholdDb,juce::dontSendNotification);compRatio.setValue(t.compressor.ratio,juce::dontSendNotification);eqToggle.setButtonText(t.eq.enabled?"EQ On":"EQ Off");compressorToggle.setButtonText(t.compressor.enabled?"Comp On":"Comp Off");
  int choice=t.instrument==veysona::Instrument::Saw?1:t.instrument==veysona::Instrument::Sine?2:t.instrument==veysona::Instrument::Square?3:t.instrument==veysona::Instrument::Triangle?4:t.instrument==veysona::Instrument::Noise?5:t.instrument==veysona::Instrument::Sampler?6:t.instrument==veysona::Instrument::DrumRack?7:8;
  instrument.setSelectedId(choice,juce::dontSendNotification);
 }
 loadingUI=false;
 pianoScaleButton.setEnabled(!valid || session.tracks[size_t(selectedTrack)].instrument!=veysona::Instrument::DrumRack);
 tabPiano.setButtonText(valid && session.tracks[size_t(selectedTrack)].instrument==veysona::Instrument::DrumRack?"DRUM SEQUENCER":"PIANO ROLL");
 piano.repaint();repaint();eqView.repaint();
}
bool Studio::saveProject(juce::File file,bool recovery){
 juce::File incompleteAssets;
 bool committed=false;
 try{
  // Every save writes a fresh asset generation. A failed save must never
  // overwrite files referenced by the previously committed .veys document.
  const auto oldRoot=juce::JSON::parse(file);
  juce::File previousAssets;
  if(oldRoot.isObject()){
   auto previous=oldRoot.getProperty("assetsDirectory","").toString();
   if(previous.isNotEmpty()&&!juce::File::isAbsolutePath(previous) &&
      !previous.contains("..") && !previous.containsAnyOf("/\\"))
    previousAssets=file.getParentDirectory().getChildFile(previous);
  }
  const auto generation=juce::String::toHexString(juce::Time::currentTimeMillis())+"-"+
   juce::String::toHexString(juce::Random::getSystemRandom().nextInt());
  auto assets=file.getSiblingFile(file.getFileNameWithoutExtension()+"_Assets_"+generation);
  incompleteAssets=assets;
  if(assets.createDirectory().failed())throw std::runtime_error("Cannot create new project assets generation");
  auto root=juce::var(new juce::DynamicObject());auto* o=root.getDynamicObject();o->setProperty("format",1);o->setProperty("name","Veysona Studio");o->setProperty("bpm",session.bpm);o->setProperty("beatsPerBar",session.beatsPerBar);
  o->setProperty("loopStart",session.loopStartBeat);o->setProperty("loopEnd",session.loopEndBeat);
  o->setProperty("masterGain",session.master.gain);o->setProperty("masterLimiter",session.master.limiterEnabled);
  o->setProperty("masterCeilingDb",session.master.ceilingDb);
  o->setProperty("revision",4);
  o->setProperty("assetsDirectory",assets.getFileName());
  juce::Array<juce::var> busJson;
  for(const auto& bus:session.buses){juce::var item(new juce::DynamicObject());auto* bo=item.getDynamicObject();
   bo->setProperty("name",jstr(bus.name));bo->setProperty("gain",bus.gain);bo->setProperty("pan",bus.pan);bo->setProperty("width",bus.width);
   bo->setProperty("mute",bus.muted);bo->setProperty("automation",automationToJson(bus.automation));busJson.add(item);
  }
  o->setProperty("buses",juce::var(busJson));
  juce::Array<juce::var> list;
  for(int i=0;i<int(session.tracks.size());++i){const auto& t=session.tracks[size_t(i)];auto v=juce::var(new juce::DynamicObject());auto* obj=v.getDynamicObject();
   obj->setProperty("name",jstr(t.name));obj->setProperty("gain",t.gain);obj->setProperty("pan",t.pan);obj->setProperty("cutoff",t.cutoff);obj->setProperty("drive",t.drive);obj->setProperty("delay",t.delayMix);obj->setProperty("mute",t.mute);obj->setProperty("solo",t.solo);obj->setProperty("instrument",int(t.instrument));obj->setProperty("pluginPath",jstr(t.pluginPath));
   obj->setProperty("synthAttackMs",t.synth.attackMs);obj->setProperty("synthReleaseMs",t.synth.releaseMs);
   obj->setProperty("synthSustain",t.synth.sustain);obj->setProperty("samplerRoot",t.synth.sampleRootNote);
   obj->setProperty("waveMorph",t.synth.wavetableMorph);obj->setProperty("waveDetune",t.synth.wavetableDetuneCents);obj->setProperty("waveSecondMix",t.synth.wavetableSecondOscMix);
   obj->setProperty("eqOn",t.eq.enabled);juce::Array<juce::var> eqBands;
   for(const auto& b:t.eq.bands){auto bv=juce::var(new juce::DynamicObject());auto* bo=bv.getDynamicObject();bo->setProperty("freq",b.frequency);bo->setProperty("db",b.gainDb);bo->setProperty("q",b.q);bo->setProperty("mode",int(b.mode));bo->setProperty("enabled",b.enabled);eqBands.add(bv);}obj->setProperty("eq",juce::var(eqBands));
   obj->setProperty("compOn",t.compressor.enabled);obj->setProperty("thresholdDb",t.compressor.thresholdDb);obj->setProperty("ratio",t.compressor.ratio);obj->setProperty("attackMs",t.compressor.attackMs);obj->setProperty("releaseMs",t.compressor.releaseMs);obj->setProperty("makeupDb",t.compressor.makeupDb);
   juce::Array<juce::var> chain;
   const auto count=std::max(t.pluginChain.size(),i<int(plugins.size())?plugins[size_t(i)].size():size_t(0));
   for(size_t slot=0;slot<count;++slot){
    std::string path=slot<t.pluginChain.size()?t.pluginChain[slot].path:"";
    std::string preset=slot<t.pluginChain.size()?t.pluginChain[slot].stateBase64:"";
    if(i<int(plugins.size())&&slot<plugins[size_t(i)].size()&&plugins[size_t(i)][slot]){
     juce::MemoryBlock state;plugins[size_t(i)][slot]->getStateInformation(state);preset=state.toBase64Encoding().toStdString();
    }
    juce::var vslot(new juce::DynamicObject());auto* os=vslot.getDynamicObject();
    os->setProperty("path",jstr(path));os->setProperty("state",jstr(preset));chain.add(vslot);
   }
   obj->setProperty("pluginChain",juce::var(chain));
   obj->setProperty("automation",automationToJson(t.automation));
   obj->setProperty("outputBus",t.outputBus);obj->setProperty("stereoWidth",t.stereoWidth);
   obj->setProperty("gateOn",t.gate.enabled);obj->setProperty("gateThreshold",t.gate.thresholdDb);
   obj->setProperty("gateAttack",t.gate.attackMs);obj->setProperty("gateRelease",t.gate.releaseMs);
   juce::var creative(new juce::DynamicObject());auto* ec=creative.getDynamicObject();
   const auto& fx=t.creativeFx;
   ec->setProperty("chorusOn",fx.chorusEnabled);ec->setProperty("chorusMix",fx.chorusMix);
   ec->setProperty("chorusDepthMs",fx.chorusDepthMs);ec->setProperty("chorusRateHz",fx.chorusRateHz);
   ec->setProperty("autopanOn",fx.autopanEnabled);ec->setProperty("autopanDepth",fx.autopanDepth);
   ec->setProperty("autopanBeats",fx.autopanPeriodBeats);
   ec->setProperty("crusherOn",fx.crusherEnabled);ec->setProperty("crusherBits",fx.crusherBits);
   ec->setProperty("crusherHold",fx.crusherHoldFrames);ec->setProperty("crusherMix",fx.crusherMix);
   ec->setProperty("tremoloOn",fx.tremoloEnabled);ec->setProperty("tremoloDepth",fx.tremoloDepth);ec->setProperty("tremoloBeats",fx.tremoloPeriodBeats);
   ec->setProperty("ringOn",fx.ringModEnabled);ec->setProperty("ringFreq",fx.ringModFrequencyHz);ec->setProperty("ringMix",fx.ringModMix);
   ec->setProperty("tapeOn",fx.tapeEnabled);ec->setProperty("tapeDrive",fx.tapeDrive);ec->setProperty("tapeMix",fx.tapeMix);
   ec->setProperty("phaserOn",fx.phaserEnabled);ec->setProperty("phaserRate",fx.phaserRateHz);
   ec->setProperty("phaserDepth",fx.phaserDepth);ec->setProperty("phaserFeedback",fx.phaserFeedback);ec->setProperty("phaserMix",fx.phaserMix);
   ec->setProperty("tiltOn",fx.tiltEnabled);ec->setProperty("tiltDb",fx.tiltDb);
   ec->setProperty("exciterOn",fx.exciterEnabled);ec->setProperty("exciterDrive",fx.exciterDrive);ec->setProperty("exciterMix",fx.exciterMix);
   ec->setProperty("resonatorOn",fx.resonatorEnabled);ec->setProperty("resonatorHz",fx.resonatorHz);ec->setProperty("resonatorFeedback",fx.resonatorFeedback);ec->setProperty("resonatorMix",fx.resonatorMix);
   ec->setProperty("wahOn",fx.wahEnabled);ec->setProperty("wahRate",fx.wahRateHz);ec->setProperty("wahDepth",fx.wahDepth);ec->setProperty("wahMix",fx.wahMix);
   ec->setProperty("foldOn",fx.foldEnabled);ec->setProperty("foldDrive",fx.foldDrive);ec->setProperty("foldMix",fx.foldMix);
   ec->setProperty("diodeOn",fx.diodeEnabled);ec->setProperty("diodeDrive",fx.diodeDrive);ec->setProperty("diodeMix",fx.diodeMix);
   ec->setProperty("octaveFuzzOn",fx.octaveFuzzEnabled);ec->setProperty("octaveFuzzDrive",fx.octaveFuzzDrive);ec->setProperty("octaveFuzzMix",fx.octaveFuzzMix);
   ec->setProperty("crossfeedOn",fx.crossfeedEnabled);ec->setProperty("crossfeedAmount",fx.crossfeedAmount);
   ec->setProperty("pingOn",fx.pingPongEnabled);ec->setProperty("pingMs",fx.pingPongDelayMs);ec->setProperty("pingFeedback",fx.pingPongFeedback);ec->setProperty("pingMix",fx.pingPongMix);
   obj->setProperty("creativeFx",creative);
   juce::var professional(new juce::DynamicObject());auto* pf=professional.getDynamicObject();
   pf->setProperty("roomOn",t.room.enabled);pf->setProperty("roomSize",t.room.size);
   pf->setProperty("roomDamping",t.room.damping);pf->setProperty("roomMix",t.room.mix);
   pf->setProperty("deEssOn",t.deEsser.enabled);pf->setProperty("deEssHz",t.deEsser.frequencyHz);
   pf->setProperty("deEssThreshold",t.deEsser.thresholdDb);pf->setProperty("deEssReduction",t.deEsser.reduction);
   pf->setProperty("transientOn",t.transient.enabled);pf->setProperty("transientAttack",t.transient.attack);
   pf->setProperty("transientSustain",t.transient.sustain);
   obj->setProperty("proFx",professional);
   juce::Array<juce::var> sends;for(auto amount:t.sends)sends.add(amount);obj->setProperty("sends",juce::var(sends));
   obj->setProperty("pluginState",count>0?chain[0].getProperty("state",""):jstr(t.pluginStateBase64));
   juce::Array<juce::var> clips;
   for(int j=0;j<int(t.clips.size());++j){const auto& c=t.clips[size_t(j)];auto wav=assets.getChildFile(juce::String::formatted("track_%03d_clip_%03d.wav",i,j));veysona::writeWavAdvanced(wav.getFullPathName().toStdString(),c.stereo,c.sampleRate,veysona::WavFormat::Float32);
    auto vclip=juce::var(new juce::DynamicObject());auto* oc=vclip.getDynamicObject();oc->setProperty("name",jstr(c.name));oc->setProperty("path",wav.getRelativePathFrom(file.getParentDirectory()));oc->setProperty("beat",c.startBeat);oc->setProperty("gain",c.gain);oc->setProperty("fadeIn",c.fadeInSeconds);oc->setProperty("fadeOut",c.fadeOutSeconds);oc->setProperty("fadeShapePower",c.equalPowerFades);clips.add(vclip);}
   obj->setProperty("clips",juce::var(clips));
   if(t.instrument==veysona::Instrument::Sampler&&!t.instrumentSample.empty()){
    auto wav=assets.getChildFile(juce::String::formatted("track_%03d_instrument.wav",i));
    veysona::writeWavAdvanced(wav.getFullPathName().toStdString(),t.instrumentSample,t.instrumentSampleRate,veysona::WavFormat::Float32);
    obj->setProperty("samplerPath",wav.getRelativePathFrom(file.getParentDirectory()));obj->setProperty("samplerName",jstr(t.instrumentSampleName));
   }
   if(t.instrument==veysona::Instrument::Sampler&&!t.sampleZones.empty()){
    juce::Array<juce::var> zones;
    for(int z=0;z<int(t.sampleZones.size());++z){
     const auto& zone=t.sampleZones[size_t(z)];
     if(zone.stereo.size()<4)continue;
     const auto wav=assets.getChildFile(juce::String::formatted("track_%03d_sampler_zone_%02d.wav",i,z));
     veysona::writeWavAdvanced(wav.getFullPathName().toStdString(),zone.stereo,zone.sampleRate,veysona::WavFormat::Float32);
     auto item=juce::var(new juce::DynamicObject());auto* zoneJson=item.getDynamicObject();
     zoneJson->setProperty("path",wav.getRelativePathFrom(file.getParentDirectory()));
     zoneJson->setProperty("root",zone.rootNote);zoneJson->setProperty("low",zone.lowNote);zoneJson->setProperty("high",zone.highNote);
     zoneJson->setProperty("velLow",zone.velocityLow);zoneJson->setProperty("velHigh",zone.velocityHigh);
     zoneJson->setProperty("name",jstr(zone.name));zones.add(item);
    }
    obj->setProperty("sampleZones",juce::var(zones));
    obj->setProperty("samplerName",jstr(t.instrumentSampleName));
   }
   juce::Array<juce::var> drums;
   for(int slot=0;slot<int(t.drumRack.size());++slot){const auto& d=t.drumRack[size_t(slot)];if(d.stereo.empty())continue;
    auto wav=assets.getChildFile(juce::String::formatted("track_%03d_drum_%02d.wav",i,slot));
    veysona::writeWavAdvanced(wav.getFullPathName().toStdString(),d.stereo,d.sampleRate,veysona::WavFormat::Float32);
    auto vdrum=juce::var(new juce::DynamicObject());auto* oDrum=vdrum.getDynamicObject();
    oDrum->setProperty("path",wav.getRelativePathFrom(file.getParentDirectory()));oDrum->setProperty("slot",slot);oDrum->setProperty("note",d.midiNote);oDrum->setProperty("name",jstr(d.name));drums.add(vdrum);
   }
   obj->setProperty("drums",juce::var(drums));
   obj->setProperty("drumRackRecordedOnly",t.drumRackRecordedOnly);
   juce::Array<juce::var> regions;
   for(const auto& m:t.midi){auto vm=juce::var(new juce::DynamicObject());auto* om=vm.getDynamicObject();om->setProperty("name",jstr(m.name));om->setProperty("beat",m.startBeat);om->setProperty("length",m.lengthBeats);
    juce::Array<juce::var> notes;for(const auto& n:m.notes){auto vn=juce::var(new juce::DynamicObject());auto* on=vn.getDynamicObject();on->setProperty("pitch",n.pitch);on->setProperty("beat",n.beat);on->setProperty("length",n.length);on->setProperty("velocity",n.velocity);on->setProperty("channel",n.channel);notes.add(vn);}om->setProperty("notes",juce::var(notes));
    juce::Array<juce::var> controls;
    for(const auto& c:m.controllers){juce::var vcc(new juce::DynamicObject());auto* oc=vcc.getDynamicObject();
     oc->setProperty("type",int(c.type));oc->setProperty("number",c.number);oc->setProperty("value",c.value);
     oc->setProperty("channel",c.channel);oc->setProperty("beat",c.beat);controls.add(vcc);
    }
    om->setProperty("controllers",juce::var(controls));regions.add(vm);}
   obj->setProperty("midi",juce::var(regions));list.add(v);
  }
  o->setProperty("tracks",juce::var(list));
  juce::TemporaryFile temporary(file);
  if(!temporary.getFile().replaceWithText(juce::JSON::toString(root,true)) || !temporary.overwriteTargetFileWithTemporary())
   throw std::runtime_error("Could not safely commit project JSON");
  committed=true;
  // Retire only a previous generated assets folder belonging to this exact
  // project after the replacement project file is safely committed.
  if(previousAssets.isDirectory() && previousAssets!=assets &&
     previousAssets.getParentDirectory()==file.getParentDirectory() &&
     previousAssets.getFileName().startsWith(file.getFileNameWithoutExtension()+"_Assets_"))
   previousAssets.deleteRecursively();
  if(!recovery){currentProject=file;rememberRecentProject(file);
   lastAutosaveTick=juce::Time::currentTimeMillis();savePreferences();
   setStatus("Saved "+file.getFileName()+" with portable audio assets");}
  return true;
 }catch(const std::exception& e){
   // Remove only the uncommitted generation. Never touch the last saved media.
   if(!committed&&incompleteAssets.isDirectory())incompleteAssets.deleteRecursively();
   if(recovery)setStatus("Autosave failed: "+juce::String(e.what()));
   else alert("Save failed",e.what());
   return false;
 }
}
void Studio::loadProject(juce::File file,bool recovery){
 auto root=juce::JSON::parse(file);if(!root.isObject()||int(root.getProperty("format",-1))!=1){alert("Invalid project","This file is not a compatible Veysona (.veys) or legacy OpenDAW (.odaw) project.");return;}
 try{
  veysona::Session newSession;newSession.sampleRate=session.sampleRate;newSession.bpm=std::clamp(double(root.getProperty("bpm",120)),40.,240.);newSession.beatsPerBar=std::clamp(int(root.getProperty("beatsPerBar",4)),1,12);
  newSession.loopStartBeat=std::max(0.,double(root.getProperty("loopStart",0)));
  newSession.loopEndBeat=std::max(0.,double(root.getProperty("loopEnd",0)));
  newSession.master.gain=std::clamp(float(double(root.getProperty("masterGain",.85))),0.f,1.5f);
  newSession.master.limiterEnabled=bool(root.getProperty("masterLimiter",true));
  newSession.master.ceilingDb=std::clamp(float(double(root.getProperty("masterCeilingDb",-1))),-24.f,0.f);
  const auto busData=root.getProperty("buses",{});
  if(auto* items=busData.getArray())for(int i=0;i<std::min(items->size(),int(veysona::kMaxBuses));++i){
   const auto& v=items->getReference(i);veysona::Bus bus;
   bus.name=v.getProperty("name","Bus").toString().toStdString();
   bus.gain=std::clamp(float(double(v.getProperty("gain",1.))),0.f,2.f);
   bus.pan=std::clamp(float(double(v.getProperty("pan",0.))),-1.f,1.f);
   bus.width=std::clamp(float(double(v.getProperty("width",1.))),0.f,2.f);
   bus.muted=bool(v.getProperty("mute",false));bus.automation=automationFromJson(v.getProperty("automation",{}));
   newSession.buses.push_back(std::move(bus));
  }
  const auto tracksValue=root.getProperty("tracks",{});auto* tracks=tracksValue.getArray();if(!tracks)throw std::runtime_error("Missing tracks");if(tracks->size()>128)throw std::runtime_error("Too many tracks");
  for(const auto& tr:*tracks){veysona::Track t;t.name=tr.getProperty("name","Track").toString().toStdString();t.gain=float(double(tr.getProperty("gain",.8)));t.pan=float(double(tr.getProperty("pan",0)));t.cutoff=float(double(tr.getProperty("cutoff",1)));t.drive=float(double(tr.getProperty("drive",0)));t.delayMix=float(double(tr.getProperty("delay",0)));t.mute=bool(tr.getProperty("mute",false));t.solo=bool(tr.getProperty("solo",false));t.instrument=veysona::Instrument(std::clamp(int(tr.getProperty("instrument",1)),0,7));t.pluginPath=tr.getProperty("pluginPath","").toString().toStdString();t.pluginStateBase64=tr.getProperty("pluginState","").toString().toStdString();
   t.outputBus=std::clamp(int(tr.getProperty("outputBus",-1)),-1,int(newSession.buses.size())-1);
   t.stereoWidth=std::clamp(float(double(tr.getProperty("stereoWidth",1.))),0.f,2.f);
   t.gate.enabled=bool(tr.getProperty("gateOn",false));
   t.gate.thresholdDb=std::clamp(float(double(tr.getProperty("gateThreshold",-43.))),-90.f,0.f);
   t.gate.attackMs=std::clamp(float(double(tr.getProperty("gateAttack",2.))),.1f,100.f);
   t.gate.releaseMs=std::clamp(float(double(tr.getProperty("gateRelease",100.))),1.f,2000.f);
   const auto creative=tr.getProperty("creativeFx",{});
   auto& fx=t.creativeFx;
   fx.chorusEnabled=bool(creative.getProperty("chorusOn",false));
   fx.chorusMix=std::clamp(float(double(creative.getProperty("chorusMix",.28))),0.f,1.f);
   fx.chorusDepthMs=std::clamp(float(double(creative.getProperty("chorusDepthMs",3.))),.1f,12.f);
   fx.chorusRateHz=std::clamp(float(double(creative.getProperty("chorusRateHz",.35))),.01f,10.f);
   fx.autopanEnabled=bool(creative.getProperty("autopanOn",false));
   fx.autopanDepth=std::clamp(float(double(creative.getProperty("autopanDepth",.5))),0.f,1.f);
   fx.autopanPeriodBeats=std::clamp(float(double(creative.getProperty("autopanBeats",2.))),.125f,64.f);
   fx.crusherEnabled=bool(creative.getProperty("crusherOn",false));
   fx.crusherBits=std::clamp(int(creative.getProperty("crusherBits",10)),4,16);
   fx.crusherHoldFrames=std::clamp(int(creative.getProperty("crusherHold",4)),1,64);
   fx.crusherMix=std::clamp(float(double(creative.getProperty("crusherMix",.65))),0.f,1.f);
   fx.tremoloEnabled=bool(creative.getProperty("tremoloOn",false));
   fx.tremoloDepth=std::clamp(float(double(creative.getProperty("tremoloDepth",.65))),0.f,1.f);
   fx.tremoloPeriodBeats=std::clamp(float(double(creative.getProperty("tremoloBeats",1.))),.125f,64.f);
   fx.ringModEnabled=bool(creative.getProperty("ringOn",false));
   fx.ringModFrequencyHz=std::clamp(float(double(creative.getProperty("ringFreq",34.))),.5f,5000.f);
   fx.ringModMix=std::clamp(float(double(creative.getProperty("ringMix",.55))),0.f,1.f);
   fx.tapeEnabled=bool(creative.getProperty("tapeOn",false));
   fx.tapeDrive=std::clamp(float(double(creative.getProperty("tapeDrive",2.5))),1.f,12.f);
   fx.tapeMix=std::clamp(float(double(creative.getProperty("tapeMix",.65))),0.f,1.f);
   fx.phaserEnabled=bool(creative.getProperty("phaserOn",false));
   fx.phaserRateHz=std::clamp(float(double(creative.getProperty("phaserRate",.35))),.02f,8.f);
   fx.phaserDepth=std::clamp(float(double(creative.getProperty("phaserDepth",.7))),0.f,1.f);
   fx.phaserFeedback=std::clamp(float(double(creative.getProperty("phaserFeedback",.15))),-.85f,.85f);
   fx.phaserMix=std::clamp(float(double(creative.getProperty("phaserMix",.45))),0.f,1.f);
   fx.tiltEnabled=bool(creative.getProperty("tiltOn",false));
   fx.tiltDb=std::clamp(float(double(creative.getProperty("tiltDb",0.))),-12.f,12.f);
   fx.exciterEnabled=bool(creative.getProperty("exciterOn",false));
   fx.exciterDrive=std::clamp(float(double(creative.getProperty("exciterDrive",2.5))),1.f,10.f);
   fx.exciterMix=std::clamp(float(double(creative.getProperty("exciterMix",.3))),0.f,.85f);
   fx.resonatorEnabled=bool(creative.getProperty("resonatorOn",false));
   fx.resonatorHz=std::clamp(float(double(creative.getProperty("resonatorHz",220.))),45.f,2500.f);
   fx.resonatorFeedback=std::clamp(float(double(creative.getProperty("resonatorFeedback",.70))),0.f,.92f);
   fx.resonatorMix=std::clamp(float(double(creative.getProperty("resonatorMix",.35))),0.f,.8f);
   fx.wahEnabled=bool(creative.getProperty("wahOn",false));
   fx.wahRateHz=std::clamp(float(double(creative.getProperty("wahRate",.7))),.03f,9.f);
   fx.wahDepth=std::clamp(float(double(creative.getProperty("wahDepth",.7))),0.f,1.f);
   fx.wahMix=std::clamp(float(double(creative.getProperty("wahMix",.55))),0.f,.95f);
   fx.foldEnabled=bool(creative.getProperty("foldOn",false));
   fx.foldDrive=std::clamp(float(double(creative.getProperty("foldDrive",2.5))),1.f,8.f);
   fx.foldMix=std::clamp(float(double(creative.getProperty("foldMix",.5))),0.f,1.f);
   fx.diodeEnabled=bool(creative.getProperty("diodeOn",false));fx.diodeDrive=std::clamp(float(double(creative.getProperty("diodeDrive",3.))),1.f,12.f);fx.diodeMix=std::clamp(float(double(creative.getProperty("diodeMix",.5))),0.f,1.f);
   fx.octaveFuzzEnabled=bool(creative.getProperty("octaveFuzzOn",false));fx.octaveFuzzDrive=std::clamp(float(double(creative.getProperty("octaveFuzzDrive",2.5))),1.f,10.f);fx.octaveFuzzMix=std::clamp(float(double(creative.getProperty("octaveFuzzMix",.4))),0.f,1.f);
   fx.crossfeedEnabled=bool(creative.getProperty("crossfeedOn",false));fx.crossfeedAmount=std::clamp(float(double(creative.getProperty("crossfeedAmount",.16))),0.f,.45f);
   fx.pingPongEnabled=bool(creative.getProperty("pingOn",false));
   fx.pingPongDelayMs=std::clamp(float(double(creative.getProperty("pingMs",280.))),35.f,1800.f);
   fx.pingPongFeedback=std::clamp(float(double(creative.getProperty("pingFeedback",.42))),0.f,.85f);
   fx.pingPongMix=std::clamp(float(double(creative.getProperty("pingMix",.35))),0.f,.85f);
   // Missing proFx means a compatible older project with the new effects bypassed.
   const auto pro=tr.getProperty("proFx",{});
   t.room.enabled=bool(pro.getProperty("roomOn",false));
   t.room.size=std::clamp(float(double(pro.getProperty("roomSize",.58))),0.f,1.f);
   t.room.damping=std::clamp(float(double(pro.getProperty("roomDamping",.45))),0.f,.97f);
   t.room.mix=std::clamp(float(double(pro.getProperty("roomMix",.25))),0.f,1.f);
   t.deEsser.enabled=bool(pro.getProperty("deEssOn",false));
   t.deEsser.frequencyHz=std::clamp(float(double(pro.getProperty("deEssHz",5600))),1000.f,18000.f);
   t.deEsser.thresholdDb=std::clamp(float(double(pro.getProperty("deEssThreshold",-30))),-60.f,0.f);
   t.deEsser.reduction=std::clamp(float(double(pro.getProperty("deEssReduction",.6))),0.f,1.f);
   t.transient.enabled=bool(pro.getProperty("transientOn",false));
   t.transient.attack=std::clamp(float(double(pro.getProperty("transientAttack",.45))),-1.f,1.f);
   t.transient.sustain=std::clamp(float(double(pro.getProperty("transientSustain",0))),-1.f,1.f);
   t.automation=automationFromJson(tr.getProperty("automation",{}));
   const auto sendValue=tr.getProperty("sends",{});
   if(auto* sendList=sendValue.getArray())for(int idx=0;idx<std::min(sendList->size(),int(veysona::kMaxBuses));++idx)
    t.sends[size_t(idx)]=std::clamp(float(double(sendList->getReference(idx))),0.f,1.f);
   auto pluginChain=tr.getProperty("pluginChain",{});
   if(auto* insertList=pluginChain.getArray())for(const auto& entry:*insertList){
    auto path=entry.getProperty("path","").toString().toStdString();
    if(!path.empty())t.pluginChain.push_back({path,entry.getProperty("state","").toString().toStdString()});
   }
   if(t.pluginChain.empty()&&!t.pluginPath.empty())t.pluginChain.push_back({t.pluginPath,t.pluginStateBase64});
   t.synth.attackMs=std::clamp(float(double(tr.getProperty("synthAttackMs",8))),.1f,5000.f);
   t.synth.releaseMs=std::clamp(float(double(tr.getProperty("synthReleaseMs",170))),5.f,8000.f);
   t.synth.sustain=std::clamp(float(double(tr.getProperty("synthSustain",.85))),0.f,1.f);
   t.synth.sampleRootNote=std::clamp(int(tr.getProperty("samplerRoot",60)),0,127);
   t.synth.wavetableMorph=std::clamp(float(double(tr.getProperty("waveMorph",.35))),0.f,1.f);
   t.synth.wavetableDetuneCents=std::clamp(float(double(tr.getProperty("waveDetune",7))),-100.f,100.f);
   t.synth.wavetableSecondOscMix=std::clamp(float(double(tr.getProperty("waveSecondMix",.23))),0.f,1.f);
   auto samplePath=tr.getProperty("samplerPath","").toString();
   if(samplePath.isNotEmpty()){
    auto f=juce::File::isAbsolutePath(samplePath)?juce::File(samplePath):file.getParentDirectory().getChildFile(samplePath);
    if(!f.existsAsFile())throw std::runtime_error("Missing sampler instrument: "+f.getFileName().toStdString());
    auto c=veysona::readWav(f.getFullPathName().toStdString());
    veysona::assignSamplerSample(t,c,t.synth.sampleRootNote);t.instrumentSampleName=tr.getProperty("samplerName","Sampler").toString().toStdString();
   }
   const auto zonesValue=tr.getProperty("sampleZones",{});
   if(auto* zones=zonesValue.getArray())if(!zones->isEmpty()){
    if(zones->size()>32)throw std::runtime_error("Too many instrument sample zones");
    std::vector<veysona::SamplerZone> decoded;
    for(const auto& item:*zones){
     const auto path=item.getProperty("path","").toString();
     if(path.isEmpty())throw std::runtime_error("Missing sample zone path");
     const auto f=juce::File::isAbsolutePath(path)?juce::File(path):file.getParentDirectory().getChildFile(path);
     if(!f.existsAsFile())throw std::runtime_error("Missing multisample zone: "+f.getFileName().toStdString());
     const auto c=veysona::readWav(f.getFullPathName().toStdString());
     veysona::SamplerZone z;z.sampleRate=c.sampleRate;z.stereo=c.stereo;
     z.rootNote=std::clamp(int(item.getProperty("root",60)),0,127);
     z.lowNote=std::clamp(int(item.getProperty("low",0)),0,127);
     z.highNote=std::clamp(int(item.getProperty("high",127)),0,127);
     z.velocityLow=std::clamp(float(double(item.getProperty("velLow",0.))),0.f,1.f);
     z.velocityHigh=std::clamp(float(double(item.getProperty("velHigh",1.))),0.f,1.f);
     z.name=item.getProperty("name","Sample").toString().toStdString();
     decoded.push_back(std::move(z));
    }
    veysona::assignSamplerZones(t,std::move(decoded),tr.getProperty("samplerName","Instrument").toString().toStdString());
   }
   t.drumRackRecordedOnly=bool(tr.getProperty("drumRackRecordedOnly",false));
   auto drumsVar=tr.getProperty("drums",{});
   if(auto* drums=drumsVar.getArray())for(auto& d:*drums){
    auto slot=int(d.getProperty("slot",-1));if(slot<0||slot>=8)continue;
    auto path=d.getProperty("path","").toString();auto f=juce::File::isAbsolutePath(path)?juce::File(path):file.getParentDirectory().getChildFile(path);
    if(!f.existsAsFile())throw std::runtime_error("Missing drum sample: "+f.getFileName().toStdString());
    auto c=veysona::readWav(f.getFullPathName().toStdString());
    veysona::assignDrumSample(t,size_t(slot),int(d.getProperty("note",36)),c);
    t.drumRack[size_t(slot)].name=d.getProperty("name","Drum").toString().toStdString();
   }
   t.eq.enabled=bool(tr.getProperty("eqOn",true));const auto eqVar=tr.getProperty("eq",{});if(auto* bands=eqVar.getArray()){for(int j=0;j<std::min(8,bands->size());++j){auto& b=t.eq.bands[size_t(j)];b.frequency=float(double((*bands)[j].getProperty("freq",b.frequency)));b.gainDb=float(double((*bands)[j].getProperty("db",b.gainDb)));b.q=float(double((*bands)[j].getProperty("q",b.q)));b.enabled=bool((*bands)[j].getProperty("enabled",true));b.mode=veysona::EqMode(std::clamp(int((*bands)[j].getProperty("mode",int(b.mode))),0,2));}}
   veysona::eqtools::sanitize(t.eq);
   t.compressor.enabled=bool(tr.getProperty("compOn",false));t.compressor.thresholdDb=float(double(tr.getProperty("thresholdDb",-18)));t.compressor.ratio=float(double(tr.getProperty("ratio",3)));t.compressor.attackMs=float(double(tr.getProperty("attackMs",10)));t.compressor.releaseMs=float(double(tr.getProperty("releaseMs",120)));t.compressor.makeupDb=float(double(tr.getProperty("makeupDb",0)));
   const auto clipValues=tr.getProperty("clips",{});if(auto* clips=clipValues.getArray())for(auto& c:*clips){auto path=c.getProperty("path","").toString();auto candidate=juce::File::isAbsolutePath(path)?juce::File(path):file.getParentDirectory().getChildFile(path);if(!candidate.existsAsFile())throw std::runtime_error("Missing audio asset: "+candidate.getFileName().toStdString());auto clip=veysona::readWav(candidate.getFullPathName().toStdString());clip.name=c.getProperty("name","Clip").toString().toStdString();clip.startBeat=double(c.getProperty("beat",0));clip.gain=float(double(c.getProperty("gain",1)));clip.fadeInSeconds=float(double(c.getProperty("fadeIn",0)));clip.fadeOutSeconds=float(double(c.getProperty("fadeOut",0)));clip.equalPowerFades=bool(c.getProperty("fadeShapePower",false));t.clips.push_back(std::move(clip));}
   const auto midiValues=tr.getProperty("midi",{});if(auto* midi=midiValues.getArray())for(auto& m:*midi){veysona::MidiRegion r;r.name=m.getProperty("name","MIDI").toString().toStdString();r.startBeat=double(m.getProperty("beat",0));r.lengthBeats=double(m.getProperty("length",4));const auto noteValues=m.getProperty("notes",{});if(auto* notes=noteValues.getArray())for(auto& n:*notes)r.notes.push_back({int(n.getProperty("pitch",60)),double(n.getProperty("beat",0)),double(n.getProperty("length",1)),float(double(n.getProperty("velocity",.8))),std::clamp(int(n.getProperty("channel",1)),1,16)});
     const auto controlValue=m.getProperty("controllers",{});
     if(auto* controls=controlValue.getArray())for(const auto& vcc:*controls){
      r.controllers.push_back({veysona::MidiControlType(std::clamp(int(vcc.getProperty("type",0)),0,2)),
       std::clamp(int(vcc.getProperty("number",1)),0,127),std::clamp(int(vcc.getProperty("value",64)),0,16383),
       std::clamp(int(vcc.getProperty("channel",1)),1,16),std::max(0.,double(vcc.getProperty("beat",0)))});
     }
     t.midi.push_back(std::move(r));}
   newSession.tracks.push_back(std::move(t));
  }
  {const juce::ScopedLock lock(audioLock);playing=false;recording=false;plugins.clear();session=std::move(newSession);prepareEngine();playCursor=0;engine.reset();}
  currentProject=recovery?juce::File{}:file;
  if(!recovery){rememberRecentProject(file);savePreferences();}
  lastAutosaveTick=juce::Time::currentTimeMillis();
  midiUndo.clear();midiRedo.clear();audioUndo.clear();audioRedo.clear();eqHistory.clear();bpm.setValue(session.bpm,juce::dontSendNotification);selectTrack(0);masterGain.setValue(session.master.gain,juce::dontSendNotification);setStatus(recovery?"Recovered snapshot. Choose Save As to preserve your recovered work.":
   "Opened project. Plugin paths retained, but plugins must be reloaded manually.");
 }catch(const std::exception& e){alert("Open failed",e.what());}
}
void Studio::importMidi(juce::File file){
 auto stream=std::unique_ptr<juce::FileInputStream>(file.createInputStream());
 if(!stream){alert("MIDI import","Could not read the selected file");return;}
 juce::MidiFile midi;
 if(!midi.readFrom(*stream)){alert("MIDI import","Invalid Standard MIDI File");return;}
 const int ppq=midi.getTimeFormat();
 if(ppq<=0){alert("Unsupported MIDI","SMPTE-timed MIDI files are not supported by this version; use PPQ MIDI files.");return;}
 int imported=0;
 double firstTempo=0;bool additionalTempos=false;
 for(int ti=0;ti<midi.getNumTracks();++ti){auto* seq=midi.getTrack(ti);if(!seq)continue;
  for(int i=0;i<seq->getNumEvents();++i){const auto& event=seq->getEventPointer(i)->message;
   if(event.isTempoMetaEvent()){
    auto next=60./event.getTempoSecondsPerQuarterNote();
    if(!std::isfinite(next)||next<=0)continue;
    if(firstTempo<=0)firstTempo=next;else if(std::abs(firstTempo-next)>.01)additionalTempos=true;
   }
  }
 }
 // A single-tempo session: beats in the MIDI file are preserved even with tempo automation.
 // We can adopt the first tempo but cannot yet reproduce later tempo changes.
 if(firstTempo>=40&&firstTempo<=240&&session.tracks.empty()){
  session.bpm=firstTempo;bpm.setValue(firstTempo,juce::dontSendNotification);
 }
 for(int ti=0;ti<midi.getNumTracks();++ti){
  auto* seq=midi.getTrack(ti);if(!seq)continue;
  veysona::Track tr;tr.name=(file.getFileNameWithoutExtension()+" "+juce::String(ti+1)).toStdString();
  veysona::MidiRegion region;region.name="Imported MIDI";
  // Separate queues allow repeated notes of the same pitch/channel without losing note-offs.
  std::map<int,std::vector<std::pair<double,float>>> active;
  for(int i=0;i<seq->getNumEvents();++i){
   const auto& msg=seq->getEventPointer(i)->message;
   auto beat=std::max(0.,msg.getTimeStamp()/ppq);
   if(msg.isController()){
    region.controllers.push_back({veysona::MidiControlType::CC,msg.getControllerNumber(),msg.getControllerValue(),msg.getChannel(),beat});continue;
   }
   if(msg.isPitchWheel()){
    region.controllers.push_back({veysona::MidiControlType::PitchWheel,0,msg.getPitchWheelValue(),msg.getChannel(),beat});continue;
   }
   if(msg.isChannelPressure()){
    region.controllers.push_back({veysona::MidiControlType::ChannelPressure,0,msg.getChannelPressureValue(),msg.getChannel(),beat});continue;
   }
   if(!msg.isNoteOn()&&!msg.isNoteOff())continue;
   int key=msg.getChannel()*128+msg.getNoteNumber();
   if(msg.isNoteOn())active[key].push_back({beat,msg.getFloatVelocity()});
   else{
    auto it=active.find(key);if(it!=active.end()&&!it->second.empty()){
     const auto note=it->second.front();it->second.erase(it->second.begin());
     region.notes.push_back({msg.getNoteNumber(),note.first,std::max(.015625,beat-note.first),note.second,msg.getChannel()});
    }
   }
  }
  // Malformed/unfinished MIDI clips sometimes omit their final note-off.
  // Finish those notes at the sequence end so they are actually audible.
  const double sequenceEnd=seq->getNumEvents()?std::max(0.,seq->getEndTime()/ppq):0.;
  for(auto& pending:active)for(const auto& onset:pending.second){
   region.notes.push_back({pending.first%128,onset.first,
       std::max(.125,sequenceEnd-onset.first),onset.second,std::clamp(pending.first/128,1,16)});
  }
  if(region.notes.empty()&&region.controllers.empty())continue;
  veysona::midi::sanitize(region.notes);
  // A percussion-only Standard MIDI track is a drum track, not a pitched synth.
  // Mixed MIDI tracks retain their previous instrument and GM-channel-10 playback.
  if(!region.notes.empty() && std::all_of(region.notes.begin(),region.notes.end(),
        [](const veysona::Note& n){return n.channel==10;})){
   tr.instrument=veysona::Instrument::DrumRack;
   if(recordedDrumPack("Recorded Acoustic").getChildFile("RECORDED_SOURCE.json").existsAsFile())
    populateDrumKit(tr,"Recorded Acoustic");
  }
  double endBeat=4;
  for(const auto& n:region.notes)endBeat=std::max(endBeat,n.beat+n.length);for(const auto& c:region.controllers)endBeat=std::max(endBeat,c.beat+.01);
  region.lengthBeats=std::ceil(endBeat/session.beatsPerBar)*session.beatsPerBar;
  tr.midi.push_back(std::move(region));
  {const juce::ScopedLock lock(audioLock);session.tracks.push_back(std::move(tr));prepareEngine();}
  ++imported;
 }
 selectTrack(std::max(0,int(session.tracks.size())-1));
 setStatus("Imported "+juce::String(imported)+" MIDI tracks"+(additionalTempos?"  |  Note: tempo automation is not yet supported":""));
}
void Studio::exportMidi(juce::File file){
 juce::MidiFile mf;mf.setTicksPerQuarterNote(960);
 juce::MidiMessageSequence timing;
 auto tempo=juce::MidiMessage::tempoMetaEvent(int(std::lround(60000000./std::clamp(session.bpm,40.,240.))));
 tempo.setTimeStamp(0);timing.addEvent(tempo);
 auto timeSig=juce::MidiMessage::timeSignatureMetaEvent(session.beatsPerBar,4);
 timeSig.setTimeStamp(0);timing.addEvent(timeSig);
 mf.addTrack(timing);
 for(const auto& t:session.tracks){
  juce::MidiMessageSequence seq;
  for(const auto& region:t.midi)for(const auto& note:region.notes){
   auto start=std::max(0.,region.startBeat+note.beat)*960;
   auto end=std::max(0.,region.startBeat+note.beat+note.length)*960;
   auto on=juce::MidiMessage::noteOn(std::clamp(note.channel,1,16),std::clamp(note.pitch,0,127),(juce::uint8)std::clamp(int(note.velocity*127),1,127));
   on.setTimeStamp(start);seq.addEvent(on);
   auto off=juce::MidiMessage::noteOff(std::clamp(note.channel,1,16),std::clamp(note.pitch,0,127));off.setTimeStamp(end);seq.addEvent(off);
  }
  for(const auto& region:t.midi)for(const auto& c:region.controllers){
   const int channel=std::clamp(c.channel,1,16);
   juce::MidiMessage msg;
   if(c.type==veysona::MidiControlType::CC)msg=juce::MidiMessage::controllerEvent(channel,std::clamp(c.number,0,127),std::clamp(c.value,0,127));
   else if(c.type==veysona::MidiControlType::PitchWheel)msg=juce::MidiMessage::pitchWheel(channel,std::clamp(c.value,0,16383));
   else msg=juce::MidiMessage::channelPressureChange(channel,std::clamp(c.value,0,127));
   msg.setTimeStamp(std::max(0.,region.startBeat+c.beat)*960);seq.addEvent(msg);
  }
  if(seq.getNumEvents()>0){seq.sort();mf.addTrack(seq);}
 }
 if(auto stream=std::unique_ptr<juce::FileOutputStream>(file.createOutputStream())){
  stream->setPosition(0);stream->truncate();if(!mf.writeTo(*stream))alert("MIDI export","Could not write MIDI");
 }else alert("MIDI export","Could not create MIDI output");
}
void Studio::loadPlugin(juce::File file){
 if(selectedTrack<0||selectedTrack>=int(session.tracks.size())){alert("Choose track","Select a track before loading a plugin.");return;}
 juce::OwnedArray<juce::PluginDescription> descriptions;
 juce::VST3PluginFormat vst;vst.findAllTypesForFile(descriptions,file.getFullPathName());
 if(descriptions.isEmpty()){alert("VST3 not recognised","No loadable VST3 was found at the selected path. Use a real installed .vst3 file or bundle.");return;}
 juce::String err;auto loaded=pluginFormats.createPluginInstance(*descriptions[0],audioRate,audioBlock,err);
 if(!loaded){alert("VST3 load failed",err);return;}
 if(loaded->isSynth())loaded->setPlayConfigDetails(0,2,audioRate,audioBlock);
 else loaded->setPlayConfigDetails(2,2,audioRate,audioBlock);
 loaded->prepareToPlay(audioRate,audioBlock);
 {
  const juce::ScopedLock lock(audioLock);
  auto& track=session.tracks[size_t(selectedTrack)];auto& chain=plugins[size_t(selectedTrack)];
  if(chain.size()>=8){alert("Plugin chain full","This development build supports up to eight plugins per track.");return;}
  const std::string path=file.getFullPathName().toStdString();
  const size_t insertIndex=chain.size();
  if(insertIndex<track.pluginChain.size()&&track.pluginChain[insertIndex].path==path){
   juce::MemoryBlock previousState;
   if(previousState.fromBase64Encoding(jstr(track.pluginChain[insertIndex].stateBase64))&&!previousState.isEmpty())
    loaded->setStateInformation(previousState.getData(),int(previousState.getSize()));
  }else{
   if(insertIndex<track.pluginChain.size())track.pluginChain.resize(insertIndex);
   track.pluginChain.push_back({path,""});
  }
  chain.push_back(std::move(loaded));track.pluginPath=path;
  if(size_t(selectedTrack)<pluginTrackFaults.size())pluginTrackFaults[size_t(selectedTrack)]=false;
  pluginFaultReported=false;
 }
 setStatus("Inserted VST3 #"+juce::String(int(plugins[size_t(selectedTrack)].size()))+" on "+jstr(session.tracks[size_t(selectedTrack)].name)+": "+descriptions[0]->name);
}
// Explicit restore avoids silently running unknown third-party plugin binaries
// just because a project was opened. Each slot retains its original order and
// preset state. Missing or broken plugins remain in the project for later repair.
void Studio::restoreSavedPlugins(){
 playing=false;
 int restored=0,missing=0,failed=0;
 juce::StringArray problems;
 for(size_t trackIndex=0;trackIndex<session.tracks.size()&&trackIndex<plugins.size();++trackIndex){
  const auto& track=session.tracks[trackIndex];
  const auto& saved=track.pluginChain;
  for(size_t slot=plugins[trackIndex].size();slot<saved.size()&&slot<8;++slot){
   // Never load slot N+1 if slot N is unavailable: this preserves insert order.
   if(plugins[trackIndex].size()!=slot)break;
   const juce::File path(jstr(saved[slot].path));
   if(!path.exists()){
    ++missing;problems.add(jstr(track.name)+" / "+juce::String(int(slot+1))+": missing "+path.getFullPathName());
    break;
   }
   try{
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile(descriptions,path.getFullPathName());
    if(descriptions.isEmpty()){
     ++failed;problems.add(jstr(track.name)+": VST3 format not recognised");break;
    }
    juce::String error;
    auto instance=pluginFormats.createPluginInstance(*descriptions[0],audioRate,audioBlock,error);
    if(!instance){
     ++failed;problems.add(jstr(track.name)+": "+error);break;
    }
    instance->setPlayConfigDetails(instance->isSynth()?0:2,2,audioRate,audioBlock);
    instance->prepareToPlay(audioRate,audioBlock);
    if(!saved[slot].stateBase64.empty()){
     juce::MemoryBlock state;
     if(state.fromBase64Encoding(jstr(saved[slot].stateBase64))&&!state.isEmpty())
      instance->setStateInformation(state.getData(),int(state.getSize()));
    }
    {const juce::ScopedLock lock(audioLock);
     if(plugins[trackIndex].size()==slot){plugins[trackIndex].push_back(std::move(instance));++restored;}
    }
   }catch(const std::exception& e){
    ++failed;problems.add(jstr(track.name)+": "+juce::String(e.what()));break;
   }
  }
 }
 updateControls();
 auto result="Restored "+juce::String(restored)+" VST3 inserts";
 if(missing||failed){
  result+=", "+juce::String(missing)+" missing, "+juce::String(failed)+" failed";
  alert("VST3 restore results",result+"\n\n"+problems.joinIntoString("\n"));
 }
 setStatus(result);
}
bool Studio::renderExport(juce::File output,veysona::WavFormat format){
 try{
  playing=false;const juce::ScopedLock lock(audioLock);
  // Reject invalid transport metadata before calculating render lengths or MIDI event timestamps.
  if(session.sampleRate<8000||session.sampleRate>384000||!std::isfinite(session.bpm)||session.bpm<20.||session.bpm>400.)
   throw std::runtime_error("Invalid sample rate or tempo; run Project Health before export");
  const double seconds=std::max(1.,session.duration()+.5);
  if(!std::isfinite(seconds)||seconds>24.*3600.)throw std::runtime_error("Invalid or excessive render duration");
  const auto total=size_t(std::ceil(seconds*session.sampleRate));
  // File is only replaced after every render block and the WAV header succeed.
  juce::TemporaryFile tempOutput(output);
  veysona::WavStreamWriter writer(tempOutput.getFile().getFullPathName().toStdString(),session.sampleRate,format,total);
  std::array<float,1024> interleaved{};veysona::EngineState offline;offline.prepare(session.tracks.size(),session.sampleRate);std::array<float,512> left{},right{};
  std::array<float,512> masterL{},masterR{};
  std::array<std::array<float,512>,veysona::kMaxBuses> busBufL{},busBufR{};
  std::array<float*,veysona::kMaxBuses> busL{},busR{};
  for(size_t b=0;b<veysona::kMaxBuses;++b){busL[b]=busBufL[b].data();busR[b]=busBufR[b].data();}
  for(size_t offset=0;offset<total;offset+=512){int n=int(std::min(size_t(512),total-offset));
   std::fill_n(masterL.data(),n,0.f);std::fill_n(masterR.data(),n,0.f);
   for(size_t b=0;b<session.buses.size()&&b<veysona::kMaxBuses;++b){std::fill_n(busL[b],n,0.f);std::fill_n(busR[b],n,0.f);}
   for(size_t tr=0;tr<session.tracks.size();++tr){
    bool vst=tr<plugins.size()&&!plugins[tr].empty();
    bool hasInstrument=false;if(vst)for(auto& p:plugins[tr])if(p&&p->isSynth())hasInstrument=true;
    veysona::renderTrackBlock(session,tr,int64_t(offset),n,left.data(),right.data(),offline,!hasInstrument,false);
    if(vst){
     if(offset==0)for(auto& p:plugins[tr])if(p)p->reset();
     float* p[]={left.data(),right.data()};juce::AudioBuffer<float> block(p,2,n);juce::MidiBuffer midi;
     for(const auto& region:session.tracks[tr].midi)for(const auto& note:region.notes){for(int edge=0;edge<2;++edge){
      auto at=int64_t(std::llround((region.startBeat+note.beat+(edge?note.length:0))*60./session.bpm*session.sampleRate));
      if(at>=int64_t(offset)&&at<int64_t(offset+n)){auto m=edge?juce::MidiMessage::noteOff(std::clamp(note.channel,1,16),note.pitch):juce::MidiMessage::noteOn(std::clamp(note.channel,1,16),note.pitch,(juce::uint8)std::clamp(int(note.velocity*127),1,127));midi.addEvent(m,int(at-offset));}
     }}
     for(const auto& region:session.tracks[tr].midi)for(const auto& control:region.controllers){
      const auto at=int64_t(std::llround((region.startBeat+control.beat)*60./session.bpm*session.sampleRate));
      if(at<int64_t(offset)||at>=int64_t(offset+n))continue;
      int channel=std::clamp(control.channel,1,16);juce::MidiMessage msg;
      if(control.type==veysona::MidiControlType::CC)msg=juce::MidiMessage::controllerEvent(channel,std::clamp(control.number,0,127),std::clamp(control.value,0,127));
      else if(control.type==veysona::MidiControlType::PitchWheel)msg=juce::MidiMessage::pitchWheel(channel,std::clamp(control.value,0,16383));
      else msg=juce::MidiMessage::channelPressureChange(channel,std::clamp(control.value,0,127));
      midi.addEvent(msg,int(at-offset));
     }
     if(tr<pluginTrackFaults.size() && pluginTrackFaults[tr].load(std::memory_order_acquire))
      throw std::runtime_error("Track "+std::to_string(tr+1)+" contains a failed VST3. Restore or bypass the plugin before export.");
     try {for(auto& p:plugins[tr])if(p)p->processBlock(block,midi);}
     catch(const std::exception& ex){throw std::runtime_error("VST3 on track "+std::to_string(tr+1)+" failed during export: "+ex.what());}
     catch(...){throw std::runtime_error("VST3 on track "+std::to_string(tr+1)+" failed during export");}
     for(int k=0;k<n;++k)if(!std::isfinite(left[k])||!std::isfinite(right[k]))
      throw std::runtime_error("VST3 on track "+std::to_string(tr+1)+" produced invalid audio; no WAV was committed");
    }
    veysona::processTrackEffects(session,tr,n,left.data(),right.data(),offline,int64_t(offset));
    veysona::routeTrack(session.tracks[tr],n,left.data(),right.data(),masterL.data(),masterR.data(),busL.data(),busR.data(),session.buses.size());
   }
   for(size_t b=0;b<session.buses.size()&&b<veysona::kMaxBuses;++b){
    veysona::applyBus(session.buses[b],session,int64_t(offset),n,busL[b],busR[b]);
    for(int i=0;i<n;++i){masterL[i]+=busL[b][i];masterR[i]+=busR[b][i];}
   }
   veysona::processMasterBlock(session.master,n,masterL.data(),masterR.data());
   for(int i=0;i<n;++i){interleaved[2*i]=masterL[i];interleaved[2*i+1]=masterR[i];}
   writer.append(interleaved.data(),size_t(n));
  }
  writer.finish();
  if(!tempOutput.overwriteTargetFileWithTemporary())throw std::runtime_error("Could not commit completed WAV export");
  setStatus("Exported "+output.getFileName());
  return true;
 }catch(const std::exception& e){alert("Export failed",e.what());return false;}
}
void Studio::exportStems(juce::File folder){
 if(!folder.isDirectory()){alert("Stems export","Select an existing folder for your stems.");return;}
 const bool wasPlaying=playing.exchange(false);
 std::vector<bool> oldMutes,oldSolos;
 for(const auto& t:session.tracks){oldMutes.push_back(t.mute);oldSolos.push_back(t.solo);}
 const auto restore=[&]{for(size_t i=0;i<session.tracks.size()&&i<oldMutes.size();++i){
  session.tracks[i].mute=oldMutes[i];session.tracks[i].solo=oldSolos[i];
 }};
 int rendered=0;
 for(size_t i=0;i<session.tracks.size();++i){
  for(size_t j=0;j<session.tracks.size();++j){session.tracks[j].mute=j!=i;session.tracks[j].solo=false;}
  juce::String base=jstr(session.tracks[i].name).retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
  if(base.isEmpty())base="Track";
  const auto f=folder.getChildFile(juce::String::formatted("%02d_",int(i+1))+base+".wav");
  if(!renderExport(f)){
   restore();playing=wasPlaying;
   setStatus("Stem export stopped after "+juce::String(rendered)+" successful stems");
   return;
  }
  ++rendered;
 }
 restore();playing=wasPlaying;
 setStatus("Exported "+juce::String(rendered)+" WAV stems to "+folder.getFileName()+" (with buses / effects)");
}
int PerformanceGrid::contentWidth() const{return 98+std::max(1,int(host.session.tracks.size()))*164;}
int PerformanceGrid::contentHeight() const{return 60+12*66+18;}
void PerformanceGrid::paint(juce::Graphics& g){
 g.fillAll(C::bg);
 const auto& session=host.session;
 g.setColour(C::top);g.fillRect(0,0,getWidth(),60);
 caption(g,"SCENES",{12,17,83,25},C::ink,12);
 for(int t=0;t<int(session.tracks.size());++t){
  int x=98+t*164;
  g.setColour(C::panel);g.fillRect(x,0,163,59);
  g.setColour(C::outline);g.drawRect(x,0,163,60);
  caption(g,jstr(session.tracks[size_t(t)].name),{x+10,8,145,22},trackColour(t),12);
  caption(g,"CLICK CLIPS TO LAUNCH",{x+10,34,148,18},C::muted,10);
 }
 for(int scene=0;scene<12;++scene){
  const int y=60+scene*66;
  g.setColour(scene%2?C::bg:C::top);g.fillRect(0,y,getWidth(),66);
  g.setColour(C::raised);g.fillRoundedRectangle(11.f,float(y+13),76.f,38.f,6.f);
  caption(g,"> "+juce::String(scene+1),{22,y+19,60,25},C::ink,13);
  for(int t=0;t<int(session.tracks.size());++t){
   const auto& tr=session.tracks[size_t(t)];
   const bool audio=scene<int(tr.clips.size());
   const int midiIndex=scene-int(tr.clips.size());
   const bool midi=!audio && midiIndex>=0 && midiIndex<int(tr.midi.size());
   if(!audio&&!midi)continue;
   int x=102+t*164;
   const auto& state=host.launcher.state(size_t(t));
   const bool active=state.active.kind==(audio?veysona::LaunchKind::Audio:veysona::LaunchKind::Midi)&&state.active.index==(audio?scene:midiIndex);
   const bool queued=state.hasQueue&&state.queued.kind==(audio?veysona::LaunchKind::Audio:veysona::LaunchKind::Midi)&&state.queued.index==(audio?scene:midiIndex);
   auto col=trackColour(t);
   g.setColour(col.withAlpha(active?.55f:(queued?.4f:.22f)));
   g.fillRoundedRectangle(float(x),float(y+7),156.f,52.f,6.f);
   g.setColour(active?C::green:(queued?C::gold:col));g.drawRoundedRectangle(float(x),float(y+7),156.f,52.f,6.f,active?2.f:1.f);
   caption(g,jstr(audio?tr.clips[size_t(scene)].name:tr.midi[size_t(midiIndex)].name),{x+8,y+10,139,21},C::ink,12);
   caption(g,active?"ACTIVE PLAYING":(queued?"WAITING QUEUED":(audio?"AUDIO LOOP":"MIDI LOOP")),{x+8,y+32,137,17},active?C::green:(queued?C::gold:C::muted),10);
  }
 }
 caption(g,"Independent loop clips  |  Select launch quantisation in Settings  |  Ctrl+L switches here",{104,60+12*66,720,18},C::muted,11);
}
void PerformanceGrid::mouseDown(const juce::MouseEvent& e){
 const int x=e.x,y=e.y;
 if(y<60){if(e.mods.isRightButtonDown()&&x>=98){int t=(x-98)/164;
   const juce::ScopedLock lock(host.audioLock);
   if(t>=0&&t<int(host.session.tracks.size()))host.launcher.queue(size_t(t),veysona::LaunchKind::Stop,-1,double(host.playCursor.load())/host.session.sampleRate*host.session.bpm/60.);
  }return;}
 const int scene=(y-60)/66;
 if(scene<0||scene>=12)return;
 const juce::ScopedLock lock(host.audioLock);
 const double now=double(host.playCursor.load())/host.session.sampleRate*host.session.bpm/60.;
 if(x<98){host.launcher.queueScene(host.session,size_t(scene),now);}
 else {
  const int track=(x-98)/164;
  if(track<0||track>=int(host.session.tracks.size()))return;
  const auto& tr=host.session.tracks[size_t(track)];
  if(e.mods.isRightButtonDown()){host.launcher.queue(size_t(track),veysona::LaunchKind::Stop,-1,now);}
  else if(scene<int(tr.clips.size()))host.launcher.queue(size_t(track),veysona::LaunchKind::Audio,scene,now);
  else if(scene-int(tr.clips.size())<int(tr.midi.size()))host.launcher.queue(size_t(track),veysona::LaunchKind::Midi,scene-int(tr.clips.size()),now);
 }
 host.playing=true;repaint();
}
int Arrangement::contentWidth() const{return int(180+host.bars*host.pixelsPerBar)+8;}
void Arrangement::paint(juce::Graphics& g){
 g.fillAll(C::bg);const int ruler=36,rowH=73,header=180;auto& s=host.session;auto pxBar=host.pixelsPerBar;
 g.setColour(C::top);g.fillRect(0,0,getWidth(),ruler);g.setColour(C::panel);g.fillRect(0,0,header,getHeight());
 g.setColour(C::outline);g.drawHorizontalLine(ruler-1,0,float(getWidth()));g.drawVerticalLine(header-1,0,float(getHeight()));
 caption(g,"TRACKS",{15,4,145,27},C::muted,11);
 // Soft four-bar phrases give the arrangement a legible structure when zoomed out.
 for(int bar=0;bar<host.bars;bar+=8){
  const int x=veysona::ui::measureX(bar,header,pxBar);
  const int xEnd=veysona::ui::measureX(std::min(host.bars,bar+4),header,pxBar);
  g.setColour(C::blue.withAlpha(.035f));g.fillRect(x,ruler,std::max(0,xEnd-x),std::max(0,getHeight()-ruler));
 }
 auto clipArea=juce::Rectangle<int>(header,ruler,getWidth()-header,getHeight()-ruler);
 // Ruler markers are painted above track lanes so they remain readable.
 for(int bar=0;bar<host.bars;++bar){
  const int x=veysona::ui::measureX(bar,header,pxBar);
  g.setColour(bar%4==0?C::blue.withAlpha(.6f):C::muted.withAlpha(.5f));
  g.drawVerticalLine(x,0,float(ruler));
  caption(g,juce::String(bar+1),{x+7,5,44,22},bar%4==0?C::ink:C::muted,12);
  for(int division=1;division<s.beatsPerBar;++division){
   const int bx=veysona::ui::subdivisionX(bar,division,s.beatsPerBar,header,pxBar);
   g.setColour(C::outline.withAlpha(.7f));g.drawVerticalLine(bx,26,float(ruler));
  }
 }
 for(int i=0;i<int(s.tracks.size());++i){auto y=ruler+i*rowH;if(y>=getHeight())break;
  g.setColour(i%2?juce::Colour(0xff0e1723):juce::Colour(0xff111c29));g.fillRect(0,y,getWidth(),rowH-1);
  g.setColour(i%2?C::top:C::panel);g.fillRect(0,y,header,rowH-1);
  if(i==host.selectedTrack){
   g.setColour(C::purple.withAlpha(.10f));g.fillRect(header,y,getWidth()-header,rowH-1);
   g.setColour(C::purple.withAlpha(.13f));g.fillRect(0,y,header,rowH-1);
   g.setColour(C::purple);g.fillRect(0,y,3,rowH-1);
  }
  // Persistent timeline grid: paint AFTER alternating track backgrounds.
  // No grid crosses the pinned track information column.
  for(int bar=0;bar<host.bars;++bar){
   const int x=veysona::ui::measureX(bar,header,pxBar);
   g.setColour(bar%4==0?C::blue.withAlpha(.33f):C::muted.withAlpha(.21f));
   g.drawVerticalLine(x,float(y),float(y+rowH-1));
   for(int division=1;division<s.beatsPerBar;++division){
    const int bx=veysona::ui::subdivisionX(bar,division,s.beatsPerBar,header,pxBar);
    g.setColour(C::muted.withAlpha(.105f));g.drawVerticalLine(bx,float(y),float(y+rowH-1));
   }
  }
  auto color=trackColour(i);
  g.setColour(color);g.fillRoundedRectangle(4.f,float(y+6),4.f,float(rowH-12),2);
  roundRect(g,{13.f,float(y+11),23.f,23.f},color.withAlpha(.23f),5);
  caption(g,juce::String(i+1),{17,y+11,21,23},color.brighter(.35f),12);
  caption(g,jstr(s.tracks[size_t(i)].name),{43,y+7,133,23},C::ink,13);
  const auto& currentTrack=s.tracks[size_t(i)];
  const bool isMidi=!currentTrack.midi.empty();
  caption(g,isMidi?"MIDI TRACK":"AUDIO TRACK",{44,y+27,98,16},C::muted,9);
  roundRect(g,{44.f,float(y+48),28.f,20.f},currentTrack.mute?C::pink.withAlpha(.8f):C::raised,3);
  roundRect(g,{78.f,float(y+48),28.f,20.f},currentTrack.solo?C::gold.withAlpha(.85f):C::raised,3);
  caption(g,"M",{53,y+48,15,20},C::ink,10);caption(g,"S",{87,y+48,15,20},C::ink,10);
  roundRect(g,{112.f,float(y+54),28.f,6.f},C::raised,2);
  const float knobX=159.f,knobY=float(y+55),knobR=11.f;
  const float knob=veysona::ui::trackGainToKnob(currentTrack.gain);
  g.setColour(C::outline);g.fillEllipse(knobX-knobR,knobY-knobR,knobR*2,knobR*2);
  g.setColour(C::muted.withAlpha(.42f));g.drawEllipse(knobX-knobR,knobY-knobR,knobR*2,knobR*2,1.f);
  juce::Path arc;arc.addCentredArc(knobX,knobY,knobR-3,knobR-3,0,-juce::MathConstants<float>::pi*.75f,-juce::MathConstants<float>::pi*.75f+knob*juce::MathConstants<float>::pi*1.5f,true);
  g.setColour(C::aqua);g.strokePath(arc,juce::PathStrokeType(2.2f));
  const float angle=-juce::MathConstants<float>::pi*.75f+knob*juce::MathConstants<float>::pi*1.5f;
  g.drawLine(knobX,knobY,knobX+std::sin(angle)*6.5f,knobY-std::cos(angle)*6.5f,1.8f);
  caption(g,"GAIN",{145,y+30,34,12},C::muted,8);
  float trackLevel=std::clamp(host.trackPeaks[size_t(i)%host.trackPeaks.size()].load(),0.f,1.f);
  if(trackLevel>.001f)roundRect(g,{112.f,float(y+54),28.f*trackLevel,6.f},trackLevel>.92f?C::pink:C::green,2);
  g.setColour(C::outline);g.drawHorizontalLine(y+rowH-1,0,getWidth());
  const auto& track=s.tracks[size_t(i)];
  for(int j=0;j<int(track.clips.size());++j){const auto& clip=track.clips[size_t(j)];double lengthSec=clip.sampleRate?double(clip.stereo.size()/2)/clip.sampleRate:0;
   float x=float(header+clip.startBeat/s.beatsPerBar*pxBar),w=std::max(14.f,float(lengthSec*s.bpm/60/s.beatsPerBar*pxBar));
   auto box=juce::Rectangle<float>(x,float(y+5),w,float(rowH-11));if(box.getRight()<header||box.getX()>getWidth())continue;
   roundRect(g,box,color.withAlpha(host.selectedTrack==i&&host.selectedClip==j?.58f:.34f),6);
   g.setColour(color.withAlpha(.92f));g.fillRoundedRectangle(x+2,float(y+7),std::max(1.f,w-4),3.f,1.5f);
   g.setColour(color.brighter(.32f).withAlpha(.55f));g.drawRoundedRectangle(box.reduced(.5f),6.f,1.f);
   caption(g,jstr(clip.name),{int(x)+9,y+7,std::max(16,int(w)-18),20},C::ink,12);
   auto baseline=float(y+48);auto available=std::max(1,int(w)-9);g.setColour(color.brighter(.8f));
   const auto& peaks=clip.waveformPeaks;
   const size_t frames=clip.stereo.size()/2;
   for(int dx=0;dx<available;dx+=2){
    float mx=0;
    if(!peaks.empty()){
     const size_t a=std::min(peaks.size()-1,size_t(double(dx)/available*peaks.size()));
     const size_t b=std::min(peaks.size(),std::max(a+1,size_t(double(dx+2)/available*peaks.size())));
     for(size_t k=a;k<b;++k)mx=std::max(mx,peaks[k]);
    }else if(frames>0){
     const size_t a=std::min(frames-1,size_t(double(dx)/available*frames));
     mx=std::max(std::abs(clip.stereo[a*2]),std::abs(clip.stereo[a*2+1]));
    }
    auto h=std::min(21.f,mx*24);g.drawVerticalLine(int(x)+5+dx,baseline-h,baseline+h);
   }
   if(clip.fadeInSeconds>0||clip.fadeOutSeconds>0){
    auto duration=std::max(.001,lengthSec);
    g.setColour(C::ink.withAlpha(.45f));
    if(clip.fadeInSeconds>0){auto right=x+float(std::min(double(clip.fadeInSeconds)/duration,1.)*w);
     g.drawLine(x,box.getBottom()-7,right,box.getY()+22,1.f);}
    if(clip.fadeOutSeconds>0){auto left=x+w-float(std::min(double(clip.fadeOutSeconds)/duration,1.)*w);
     g.drawLine(left,box.getY()+22,x+w,box.getBottom()-7,1.f);}
   }
   if(host.selectedTrack==i&&host.selectedClip==j){g.setColour(C::ink);g.drawRoundedRectangle(box,6.f,1.5f);}
  }
  if(host.automationMode&&host.selectedTrack==i){
   const auto lane=std::find_if(track.automation.begin(),track.automation.end(),[](const auto& a){return a.target==veysona::AutomationTarget::TrackGain;});
   const float top=float(y+44),bot=float(y+69);
   g.setColour(C::bg.withAlpha(.72f));g.fillRect(header,y+41,getWidth()-header,31);
   g.setColour(C::gold.withAlpha(.32f));g.drawHorizontalLine(int(bot),header,getWidth());
   if(lane!=track.automation.end()&&!lane->points.empty()){
    juce::Path curve;bool first=true;
    for(const auto& pt:lane->points){float x=float(header+pt.beat/s.beatsPerBar*pxBar);
     float py=bot-std::clamp(pt.value,0.f,1.5f)/1.5f*(bot-top);
     if(first){curve.startNewSubPath(x,py);first=false;}else curve.lineTo(x,py);
     g.setColour(C::gold);g.fillEllipse(x-4,py-4,8,8);
    }
    g.setColour(C::gold);g.strokePath(curve,juce::PathStrokeType(1.7f));
   }else{caption(g,"Click to add gain automation",{header+12,y+45,210,18},C::gold,11);}
  }
  for(const auto& midi:track.midi){auto x=float(header+midi.startBeat/s.beatsPerBar*pxBar);auto w=float(midi.lengthBeats/s.beatsPerBar*pxBar);
   auto box=juce::Rectangle<float>(x,float(y+5),w,float(rowH-11));roundRect(g,box,color.withAlpha(.40f),6);
   g.setColour(color.withAlpha(.9f));g.fillRoundedRectangle(x+2,float(y+7),std::max(1.f,w-4),3.f,1.5f);
   caption(g,jstr(midi.name),{int(x)+9,y+7,std::max(16,int(w)-18),19},C::ink,12);
   for(const auto& n:midi.notes){auto bx=float(x+n.beat/s.beatsPerBar*pxBar),bw=float(n.length/s.beatsPerBar*pxBar);auto ny=y+57-std::clamp(n.pitch-42,0,36);g.setColour(color.brighter(.7f));g.fillRoundedRectangle(bx,ny,std::max(2.f,bw-1),3.f,1);}
  }
  // A subtle foreground beat guide keeps full bars visible through coloured clips.
  for(int bar=0;bar<host.bars;++bar){
   const int x=veysona::ui::measureX(bar,header,pxBar);
   g.setColour(bar%4==0?C::ink.withAlpha(.19f):C::ink.withAlpha(.10f));
   g.drawVerticalLine(x,float(y),float(y+rowH-1));
  }
 }
 // Continue musical measures below the final track, including empty arrangement space.
 const int emptyTop=ruler+int(s.tracks.size())*rowH;
 if(emptyTop<getHeight())for(int bar=0;bar<host.bars;++bar){
  const int x=veysona::ui::measureX(bar,header,pxBar);
  g.setColour(bar%4==0?C::blue.withAlpha(.23f):C::muted.withAlpha(.13f));
  g.drawVerticalLine(x,float(emptyTop),float(getHeight()));
  for(int division=1;division<s.beatsPerBar;++division){
   const int bx=veysona::ui::subdivisionX(bar,division,s.beatsPerBar,header,pxBar);
   g.setColour(C::muted.withAlpha(.065f));g.drawVerticalLine(bx,float(emptyTop),float(getHeight()));
  }
 }
 auto cursor=host.playCursor.load();auto beats=double(cursor)/std::max(1u,s.sampleRate)*s.bpm/60;
 auto playX=float(header+beats/s.beatsPerBar*pxBar);g.setColour(C::blue);g.drawVerticalLine(int(playX),0,float(getHeight()));
 juce::Path triangle;triangle.addTriangle(playX-6,18,playX+6,18,playX,29);g.fillPath(triangle);
}
void Arrangement::mouseDown(const juce::MouseEvent& e){
 const int ruler=36,rowH=73,header=180;
 if(e.y<ruler){
  if(e.x>=header){const juce::ScopedLock lock(host.audioLock);auto beat=std::max(0.,(e.x-header)/host.pixelsPerBar*host.session.beatsPerBar);
   host.playCursor.store(int64_t(beat*60./host.session.bpm*host.session.sampleRate));host.engine.reset();repaint();}
  return;
 }
 const int row=(e.y-ruler)/rowH;
 if(row<0||row>=int(host.session.tracks.size()))return;
 if(e.x<header){
  if(e.mods.isRightButtonDown()){
   juce::PopupMenu menu;menu.addSectionHeader("Track actions");
   menu.addItem("Duplicate track (audio + MIDI)",host.session.tracks.size()<128,false,[this,row]{
    const juce::ScopedLock lock(host.audioLock);if(host.session.tracks.size()>=128)return;auto copy=host.session.tracks[size_t(row)];
    copy.name+=" Copy";copy.pluginPath.clear();copy.pluginStateBase64.clear();copy.pluginChain.clear();
    host.session.tracks.insert(host.session.tracks.begin()+row+1,std::move(copy));
    host.plugins.emplace(host.plugins.begin()+row+1);
    host.engine.prepare(host.session.tracks.size(),host.session.sampleRate);host.launcher.prepare(host.session.tracks.size());host.eqHistory.clear();
    host.selectTrack(row+1);repaint();
   });
   menu.addItem("Mute / unmute track",[this,row]{const juce::ScopedLock lock(host.audioLock);auto& t=host.session.tracks[size_t(row)];t.mute=!t.mute;repaint();});
   menu.addItem("Solo / unsolo track",[this,row]{const juce::ScopedLock lock(host.audioLock);auto& t=host.session.tracks[size_t(row)];t.solo=!t.solo;repaint();});
   menu.addSeparator();
   juce::PopupMenu outputMenu;
   outputMenu.addItem("Master",[this,row]{const juce::ScopedLock lock(host.audioLock);host.session.tracks[size_t(row)].outputBus=-1;});
   for(int b=0;b<int(host.session.buses.size());++b){
    outputMenu.addItem(jstr(host.session.buses[size_t(b)].name),[this,row,b]{const juce::ScopedLock lock(host.audioLock);host.session.tracks[size_t(row)].outputBus=b;repaint();});
   }
   menu.addSubMenu("Output routing",outputMenu);
   menu.addItem("Add volume automation point at playhead",[this,row]{
    const juce::ScopedLock lock(host.audioLock);
    double beat=double(host.playCursor.load())/host.session.sampleRate*host.session.bpm/60.;
    auto& track=host.session.tracks[size_t(row)];
    veysona::setAutomationPoint(track.automation,veysona::AutomationTarget::TrackGain,beat,track.gain);
    host.automationMode=true;host.automationButton.setButtonText("Automation ON");repaint();
   });
   menu.addSeparator();
   menu.addItem("Remove track",host.session.tracks.size()>1,false,[this,row]{
    const juce::ScopedLock lock(host.audioLock);
    if(host.session.tracks.size()<=1||row>=int(host.session.tracks.size()))return;
    host.session.tracks.erase(host.session.tracks.begin()+row);
    if(row<int(host.plugins.size()))host.plugins.erase(host.plugins.begin()+row);
    host.engine.prepare(host.session.tracks.size(),host.session.sampleRate);host.launcher.prepare(host.session.tracks.size());host.eqHistory.clear();
    host.selectTrack(std::max(0,row-1));repaint();
   });
   menu.showMenuAsync(juce::PopupMenu::Options());return;
  }
  const int relativeY=e.y-ruler-row*rowH;
  if(e.x>=144&&e.x<=178&&relativeY>=40&&relativeY<=72 && !e.mods.isRightButtonDown()){
   gainDragTrack=row;gainDragScreenY=e.getScreenY();gainDragOriginal=host.session.tracks[size_t(row)].gain;
   host.selectTrack(row);repaint();return;
  }
  if(relativeY>=46&&relativeY<=69){
   if(e.x>=44&&e.x<74){const juce::ScopedLock lock(host.audioLock);host.session.tracks[size_t(row)].mute=!host.session.tracks[size_t(row)].mute;}
   else if(e.x>=78&&e.x<108){const juce::ScopedLock lock(host.audioLock);host.session.tracks[size_t(row)].solo=!host.session.tracks[size_t(row)].solo;}
  }
  host.selectTrack(row);repaint();return;
 }
 auto& track=host.session.tracks[size_t(row)];
 double b=(e.x-header)/host.pixelsPerBar*host.session.beatsPerBar;
 if(host.automationMode&&e.y>=(ruler+row*rowH+41)){
  const juce::ScopedLock lock(host.audioLock);
  const double beat=std::max(0.,veysona::midi::snap(b,.25));
  const float value=std::clamp(float((ruler+row*rowH+69-e.y)/25.f)*1.5f,0.f,1.5f);
  auto it=std::find_if(track.automation.begin(),track.automation.end(),[](const auto& a){return a.target==veysona::AutomationTarget::TrackGain;});
  if(e.mods.isRightButtonDown()){
   if(it!=track.automation.end()&&!it->points.empty()){
    auto nearest=std::min_element(it->points.begin(),it->points.end(),[beat](auto x,auto y){return std::abs(x.beat-beat)<std::abs(y.beat-beat);});
    if(nearest!=it->points.end()&&std::abs(nearest->beat-beat)<.5)it->points.erase(nearest);
   }
  }else{
   veysona::setAutomationPoint(track.automation,veysona::AutomationTarget::TrackGain,beat,value);
   it=std::find_if(track.automation.begin(),track.automation.end(),[](const auto& a){return a.target==veysona::AutomationTarget::TrackGain;});
   if(it!=track.automation.end()){
    auto point=std::lower_bound(it->points.begin(),it->points.end(),beat,[](auto a,double x){return a.beat<x;});
    pickedAutoPoint=int(std::distance(it->points.begin(),point));draggingAuto=true;pickedTrack=row;
   }
  }
  host.selectTrack(row);repaint();return;
 }
 pickedTrack=row;pickedClip=-1;pickedMidi=-1;dragging=draggingMidi=false;
 for(int c=0;c<int(track.clips.size());++c){
  const auto& clip=track.clips[size_t(c)];
  double duration=clip.sampleRate?double(clip.stereo.size()/2)/clip.sampleRate*host.session.bpm/60:0;
  if(b>=clip.startBeat&&b<clip.startBeat+std::max(.2,duration)){
   pickedClip=c;dragging=true;originalBeat=clip.startBeat;dragBeat=b;break;
  }
 }
 if(pickedClip<0){
  for(int c=0;c<int(track.midi.size());++c){const auto& clip=track.midi[size_t(c)];
   if(b>=clip.startBeat&&b<=clip.startBeat+clip.lengthBeats){pickedMidi=c;draggingMidi=true;dragBeat=b;originalBeat=clip.startBeat;break;}
  }
 }
 host.selectTrack(row,pickedClip);
 if(pickedMidi>=0){host.selectedMidiRegion=pickedMidi;host.selectedNote=-1;host.piano.focusOnNotes();host.piano.repaint();}
 if(e.mods.isRightButtonDown()){
  dragging=draggingMidi=false;
  juce::PopupMenu menu;
  if(pickedClip>=0){int index=pickedClip;
   // Offline operations are performed outside the real-time audio lock, then
   // installed atomically after validation, with the previous clip in Audio Undo.
   auto editClip=[this,row,index](const std::function<void(veysona::Clip&)>& operation,const juce::String& message){
    veysona::Clip working;
    {const juce::ScopedLock lock(host.audioLock);
     if(row<0||row>=int(host.session.tracks.size())||index<0||index>=int(host.session.tracks[size_t(row)].clips.size()))return;
     working=host.session.tracks[size_t(row)].clips[size_t(index)];
    }
    try{
     operation(working);
     host.playing=false;host.selectedTrack=row;host.selectedClip=index;host.rememberAudioEdit();
     {const juce::ScopedLock lock(host.audioLock);
      if(index>=int(host.session.tracks[size_t(row)].clips.size()))return;
      host.session.tracks[size_t(row)].clips[size_t(index)]=std::move(working);
     }
     host.setStatus(message+" (Edit > Undo Audio Edit)");host.arrangement.repaint();host.piano.repaint();
    }catch(const std::exception& error){host.setStatus("Audio edit failed: "+juce::String(error.what()));}
   };
   auto setFades=[editClip](float in,float out,bool equalPower){
    editClip([in,out,equalPower](veysona::Clip& c){
      const float seconds=float(c.stereo.size()/2)/std::max(uint32_t(1),c.sampleRate);
      if(in>=0)c.fadeInSeconds=std::min(in,seconds);
      if(out>=0)c.fadeOutSeconds=std::min(out,seconds);
      c.equalPowerFades=equalPower;
    },equalPower?"Equal-power fade updated":"Linear fade updated");
   };
   juce::PopupMenu fades;
   fades.addItem("Fade in - 10 ms",[setFades]{setFades(.01f,-1.f,false);});
   fades.addItem("Fade in - 100 ms",[setFades]{setFades(.1f,-1.f,false);});
   fades.addItem("Fade out - 100 ms",[setFades]{setFades(-1.f,.1f,false);});
   fades.addItem("Fade out - 500 ms",[setFades]{setFades(-1.f,.5f,false);});
   fades.addSeparator();
   fades.addItem("Equal-power fades (100 ms)",[setFades]{setFades(.1f,.1f,true);});
   fades.addItem("Clear fades",[setFades]{setFades(0.f,0.f,false);});
   menu.addSubMenu("Fades and envelopes",fades);
   juce::PopupMenu stretch;
   for(const double speed:{.25,.5,.75,1.25,1.5,2.,4.}){
    auto label=juce::String(speed*100.,0)+"% speed  (pitch preserved)";
    stretch.addItem(label,[editClip,speed]{editClip([speed](veysona::Clip& c){veysona::warp::stretchClip(c,speed);},"Stretched clip to "+juce::String(speed*100.,0)+"% speed");});
   }
   menu.addSubMenu("Time stretch  /  speed",stretch);
   juce::PopupMenu stretchHQ;
   for(const double speed:{.5,.75,1.25,1.5,2.}){
    const auto label=juce::String(speed*100.,0)+"% speed  |  High fidelity";
    stretchHQ.addItem(label,[editClip,speed]{editClip([speed](veysona::Clip& c){
      veysona::warp::stretchClip(c,speed,{veysona::warp::Quality::HighFidelity,14.});
     },"High-fidelity stretched clip to "+juce::String(speed*100.,0)+"% speed");});
   }
   menu.addSubMenu("Time stretch  /  high fidelity (offline)",stretchHQ);
   juce::PopupMenu stretchPercussive;
   for(const double speed:{.5,.75,1.25,1.5,2.}){
    stretchPercussive.addItem(juce::String(speed*100.,0)+"% speed  |  Drums / transients",
     [editClip,speed]{editClip([speed](veysona::Clip& c){
      veysona::warp::stretchClip(c,speed,{veysona::warp::Quality::Percussive,6.});
     },"Percussive-stretched clip");});
   }
   menu.addSubMenu("Time stretch  /  drums",stretchPercussive);
   juce::PopupMenu fit;
   for(const int bars:{1,2,4,8,16}){
    const double bpm=host.session.bpm;const int beatsPerBar=host.session.beatsPerBar;
    fit.addItem(juce::String(bars)+" bar"+(bars==1?"":"s")+" at project BPM",
      [editClip,bars,bpm,beatsPerBar]{editClip([bars,bpm,beatsPerBar](veysona::Clip& c){
       veysona::warp::fitClipToBeats(c,double(bars*beatsPerBar),bpm);
      },"Fitted clip to "+juce::String(bars)+" bars");});
   }
   menu.addSubMenu("Fit audio to tempo",fit);
   menu.addItem("Match detected source BPM to project tempo (experimental)",[editClip,bpm=host.session.bpm]{
    editClip([bpm](veysona::Clip& c){
     const auto analysis=veysona::production::analyseLoop(c.stereo,c.sampleRate);
     if(analysis.estimatedBpm<=0||analysis.confidence<.1)
      throw std::runtime_error("BPM detection uncertain; use Fit to bars instead");
     veysona::warp::stretchClip(c,bpm/analysis.estimatedBpm);
    },"Matched detected loop tempo to project BPM");
   });
   juce::PopupMenu pitch;
   for(const int semitones:{-12,-7,-5,-2,2,5,7,12}){
    pitch.addItem((semitones>0?"+":"")+juce::String(semitones)+" semitones  (duration preserved)",
      [editClip,semitones]{editClip([semitones](veysona::Clip& c){veysona::warp::pitchShiftClip(c,semitones);},"Pitch-shifted clip "+juce::String(semitones)+" semitones");});
   }
   menu.addSubMenu("Pitch shift",pitch);
   menu.addItem("Warp transients to 1/2 beat grid (experimental)",[editClip,bpm=host.session.bpm]{
    editClip([bpm](veysona::Clip& c){
     auto markers=veysona::warp::suggestBeatMarkers(c,bpm,.5,32);
     if(markers.size()<3)throw std::runtime_error("No reliable transients to warp");
     veysona::warp::warpClipToMarkers(c,markers,{veysona::warp::Quality::Percussive,6.});
    },"Warped transients toward 1/2 beat grid");
   });
   menu.addSeparator();
   menu.addItem("Split audio here",[this,row,index,b]{
    host.playing=false;host.selectedTrack=row;host.rememberAudioEdit();
    const juce::ScopedLock lock(host.audioLock);auto& clips=host.session.tracks[size_t(row)].clips;
    if(index>=int(clips.size()))return;
    auto seconds=(b-clips[size_t(index)].startBeat)*60./host.session.bpm;
    try{auto tail=veysona::splitAudioClip(clips[size_t(index)],seconds,host.session.bpm);
     clips.insert(clips.begin()+index+1,std::move(tail));host.selectedClip=index;repaint();
    }catch(const std::exception&){host.setStatus("Move the cursor inside the clip to split it.");}
   });
   menu.addItem("Duplicate audio clip",[this,row,index]{host.playing=false;host.selectedTrack=row;host.rememberAudioEdit();
    const juce::ScopedLock lock(host.audioLock);auto& clips=host.session.tracks[size_t(row)].clips;if(index>=int(clips.size()))return;
    auto copy=clips[size_t(index)];copy.startBeat+=double(copy.stereo.size()/2)/copy.sampleRate*host.session.bpm/60.;
    clips.push_back(std::move(copy));repaint();});
   menu.addItem("Reverse audio",[editClip]{editClip([](veysona::Clip& c){veysona::reverseAudioClip(c);c.sourcePath.clear();},"Reversed audio");});
   menu.addItem("Normalise clip to -1 dB peak",[editClip]{editClip([](veysona::Clip& c){veysona::normaliseAudioClip(c);},"Normalised clip");});
   juce::PopupMenu crossfade;
   for(const double ms:{25.,100.,250.,500.}){
    crossfade.addItem(juce::String(ms,0)+" ms equal-power crossfade with next clip",[this,row,index,ms]{
     host.playing=false;host.selectedTrack=row;
     {const juce::ScopedLock lock(host.audioLock);if(index+1>=int(host.session.tracks[size_t(row)].clips.size())){host.setStatus("Place another clip immediately after this one");return;}}
     host.rememberAudioEdit();
     try{const juce::ScopedLock lock(host.audioLock);auto& clips=host.session.tracks[size_t(row)].clips;
      const auto secs=veysona::warp::crossfadeAdjacent(clips[size_t(index)],clips[size_t(index+1)],host.session.bpm,ms/1000.);
      host.setStatus("Created "+juce::String(secs*1000.,0)+" ms equal-power crossfade");repaint();
     }catch(const std::exception& error){host.setStatus("Crossfade: "+juce::String(error.what()));}
    });
   }
   menu.addSubMenu("Crossfade into next clip",crossfade);
   menu.addSeparator();
   menu.addItem("Delete audio clip",[this,row,index]{const juce::ScopedLock lock(host.audioLock);auto& t=host.session.tracks[size_t(row)];if(index<int(t.clips.size()))t.clips.erase(t.clips.begin()+index);host.selectTrack(row);});
  }else if(pickedMidi>=0){int index=pickedMidi;
   menu.addItem("Duplicate MIDI clip",[this,row,index]{const juce::ScopedLock lock(host.audioLock);auto& t=host.session.tracks[size_t(row)];if(index>=int(t.midi.size()))return;auto dup=t.midi[size_t(index)];dup.startBeat+=dup.lengthBeats;t.midi.push_back(std::move(dup));repaint();});
   menu.addItem("Delete MIDI clip",[this,row,index]{const juce::ScopedLock lock(host.audioLock);auto& t=host.session.tracks[size_t(row)];if(index>=int(t.midi.size()))return;t.midi.erase(t.midi.begin()+index);host.selectedMidiRegion=0;host.piano.repaint();repaint();});
  }
  if(menu.getNumItems()>0)menu.showMenuAsync(juce::PopupMenu::Options());
 }
}
void Arrangement::mouseDrag(const juce::MouseEvent& e){
 if(gainDragTrack>=0 && gainDragTrack<int(host.session.tracks.size())){
  const float initial=veysona::ui::trackGainToKnob(gainDragOriginal);
  const float scale=e.mods.isShiftDown()?900.f:220.f;
  const auto position=std::clamp(initial-float(e.getScreenY()-gainDragScreenY)/scale,0.f,1.f);
  {const juce::ScopedLock lock(host.audioLock);
   host.session.tracks[size_t(gainDragTrack)].gain=veysona::ui::trackGainFromKnob(position);
  }
  host.updateControls();host.mixer.repaint();repaint();return;
 }

 if(pickedTrack<0)return;
 const juce::ScopedLock lock(host.audioLock);
 if(draggingAuto){
  auto& lanes=host.session.tracks[size_t(pickedTrack)].automation;
  auto it=std::find_if(lanes.begin(),lanes.end(),[](const auto& a){return a.target==veysona::AutomationTarget::TrackGain;});
  if(it==lanes.end()||pickedAutoPoint<0||pickedAutoPoint>=int(it->points.size()))return;
  it->points.erase(it->points.begin()+pickedAutoPoint);
  const double beat=std::max(0.,veysona::midi::snap((e.x-180)/host.pixelsPerBar*host.session.beatsPerBar,.25));
  const float value=std::clamp(float((36+pickedTrack*73+69-e.y)/25.f)*1.5f,0.f,1.5f);
  veysona::setAutomationPoint(lanes,veysona::AutomationTarget::TrackGain,beat,value);
  it=std::find_if(lanes.begin(),lanes.end(),[](const auto& a){return a.target==veysona::AutomationTarget::TrackGain;});
  if(it!=lanes.end())pickedAutoPoint=int(std::distance(it->points.begin(),std::lower_bound(it->points.begin(),it->points.end(),beat,[](auto p,double x){return p.beat<x;})));
  repaint();return;
 }
 auto at=(e.x-180)/host.pixelsPerBar*host.session.beatsPerBar;
 auto raw=originalBeat+at-dragBeat;
 auto snapped=veysona::ui::snapBeat(raw,host.snapEnabled);
 if(dragging&&pickedClip>=0&&pickedClip<int(host.session.tracks[size_t(pickedTrack)].clips.size()))
  host.session.tracks[size_t(pickedTrack)].clips[size_t(pickedClip)].startBeat=snapped;
 else if(draggingMidi&&pickedMidi>=0&&pickedMidi<int(host.session.tracks[size_t(pickedTrack)].midi.size()))
  host.session.tracks[size_t(pickedTrack)].midi[size_t(pickedMidi)].startBeat=snapped;
 repaint();
}
void Arrangement::mouseDoubleClick(const juce::MouseEvent& e){
 int row=(e.y-36)/73;
 if(row>=0 && row<int(host.session.tracks.size()) && e.x>=144&&e.x<=178 && e.y>=36+row*73+40 && e.y<=36+row*73+72){
  {const juce::ScopedLock lock(host.audioLock);host.session.tracks[size_t(row)].gain=1.f;}
  host.selectTrack(row);host.mixer.repaint();repaint();return;
 }
 if(row<0||row>=int(host.session.tracks.size())||e.x<180)return;
 double beat=std::max(0.,std::floor((e.x-180)/host.pixelsPerBar*host.session.beatsPerBar));
 const juce::ScopedLock lock(host.audioLock);auto& track=host.session.tracks[size_t(row)];
 for(int i=0;i<int(track.midi.size());++i){const auto& m=track.midi[size_t(i)];if(beat>=m.startBeat&&beat<m.startBeat+m.lengthBeats){host.selectTrack(row);host.selectedMidiRegion=i;host.piano.focusOnNotes();return;}}
 veysona::MidiRegion region;region.name="MIDI Pattern";region.startBeat=beat;region.lengthBeats=16;
 track.midi.push_back(std::move(region));host.selectTrack(row);host.selectedMidiRegion=int(track.midi.size())-1;host.piano.focusOnNotes();repaint();
}

bool PianoRoll::isDrumTrack() const{
 return host.selectedTrack>=0 && host.selectedTrack<int(host.session.tracks.size()) &&
  host.session.tracks[size_t(host.selectedTrack)].instrument==veysona::Instrument::DrumRack;
}
void PianoRoll::paintDrumGrid(juce::Graphics& g){
 g.fillAll(C::bg);
 const int w=getWidth(),h=getHeight();
 constexpr int left=142,head=43,velocityH=55;
 const float bottom=float(h-velocityH),cell=std::max(7.f,float(w-left-13)/float(drumGridSteps));
 const float rowH=std::max(18.f,(bottom-head)/8.f);
 g.setColour(C::panel);g.fillRect(0,0,w,head);
 caption(g,"DRUM SEQUENCER",{12,5,215,28},C::ink,14);
 if(w>540)caption(g,"Click: toggle   •   Shift: accent   •   Right-click: erase   •   Scroll: bars",{222,7,w-370,25},C::muted,10);
 const auto switchBox=juce::Rectangle<float>(float(w-105),8.f,93.f,27.f);
 roundRect(g,switchBox,C::raised,6.f);
 caption(g,drumGridSteps==16?"16 STEPS":"32 STEPS",{w-99,10,85,22},C::blue,11);
 g.setColour(C::outline);g.drawHorizontalLine(head,0.f,float(w));
 const auto* region=host.activeMidi();
 const auto& track=host.session.tracks[size_t(host.selectedTrack)];
 const std::array<const char*,8> fallback{{"Kick","Snare","Closed Hat","Open Hat","Rim","Low Tom","Crash","High Tom"}};
 const std::array<int,8> gm{{36,38,42,46,37,41,49,43}};
 for(int lane=0;lane<8;++lane){
  const float y=head+lane*rowH;
  if(y>=bottom)break;
  g.setColour(lane==drumSelectedLane?C::purple.withAlpha(.14f):(lane%2?C::raised.withAlpha(.23f):C::panel.withAlpha(.25f)));
  g.fillRect(0,int(y),w,int(std::ceil(rowH)));
  const auto& slot=track.drumRack[size_t(lane)];
  const auto padName=slot.name.empty()?juce::String(fallback[size_t(lane)]):jstr(slot.name);
  const int pitch=slot.stereo.empty()?gm[size_t(lane)]:slot.midiNote;
  roundRect(g,{8.f,y+3.f,11.f,std::max(8.f,rowH-6.f)},lane==drumSelectedLane?C::gold:C::purple,3);
  caption(g,padName,{25,int(y+2.f),109,int(rowH-3.f)},C::ink,11);
  for(int column=0;column<drumGridSteps;++column){
   const int step=drumGridStart+column;
   const float x=left+cell*float(column);
   if(x>=w)break;
   if(column%16==0){g.setColour(C::outline.brighter(.3f));g.fillRect(int(x),int(y),1,int(rowH));}
   else if(column%4==0){g.setColour(C::outline.withAlpha(.45f));g.fillRect(int(x),int(y),1,int(rowH));}
   const int note=region?veysona::drums::findNote(*region,pitch,step):-1;
   const float value=note>=0?std::clamp(region->notes[size_t(note)].velocity,0.f,1.f):0.f;
   const auto fill=note>=0?(value>.88f?C::gold:C::blue.withMultipliedBrightness(.48f+.52f*value)):
     (column%4==0?C::raised.brighter(.15f):C::raised);
   roundRect(g,{x+2.f,y+4.f,std::max(3.f,cell-4.f),std::max(7.f,rowH-8.f)},fill,4);
   if(note>=0){g.setColour(C::ink.withAlpha(value*.27f));g.fillRoundedRectangle(x+4.f,y+5.f,std::max(1.f,cell-8.f),2.f,1.f);}
  }
  g.setColour(C::outline.withAlpha(.29f));g.drawHorizontalLine(int(y+rowH),0.f,float(w));
 }
 if(region && host.playing.load()){
  const double beat=(double(host.playCursor.load())/std::max(1u,host.session.sampleRate))*host.session.bpm/60.-region->startBeat;
  if(beat>=0 && region->lengthBeats>0){
   const int step=veysona::drums::stepAt(std::fmod(beat,region->lengthBeats));
   if(step>=drumGridStart && step<drumGridStart+drumGridSteps){
    const float x=left+(step-drumGridStart)*cell;
    g.setColour(C::ink.withAlpha(.88f));g.fillRect(int(x),head,2,int(bottom-head));
   }
  }
 }
 g.setColour(C::panel);g.fillRect(0,int(bottom),w,velocityH);
 g.setColour(C::outline);g.drawHorizontalLine(int(bottom),0.f,float(w));
 caption(g,"VELOCITY",{12,int(bottom+3),109,22},C::muted,11);
 caption(g,"Drag bars to edit",{12,int(bottom+23),125,20},C::muted,9);
 const int velocityPitch=track.drumRack[size_t(drumSelectedLane)].stereo.empty()?gm[size_t(drumSelectedLane)]:track.drumRack[size_t(drumSelectedLane)].midiNote;
 if(region)for(int column=0;column<drumGridSteps;++column){
  const int index=veysona::drums::findNote(*region,velocityPitch,drumGridStart+column);
  if(index<0)continue;
  const float value=std::clamp(region->notes[size_t(index)].velocity,0.f,1.f);
  const float x=left+cell*column+cell*.25f;
  roundRect(g,{x,float(h-5)-value*45.f,std::max(3.f,cell*.5f),value*45.f},value>.88f?C::gold:C::blue,2.f);
 }
}
void PianoRoll::drumMouseDown(const juce::MouseEvent& e){
 if(e.y<43){
  if(e.x>=getWidth()-112){drumGridSteps=drumGridSteps==16?32:16;repaint();}
  return;
 }
 if(!isDrumTrack())return;
 const int left=142,h=getHeight(),bottom=h-55;
 const float rowH=std::max(18.f,float(bottom-43)/8.f);
 const int lane=std::clamp(int((e.y-43)/rowH),0,7);
 auto* region=host.activeMidi();if(!region)return;
 const auto& slot=host.session.tracks[size_t(host.selectedTrack)].drumRack[size_t(lane)];
 constexpr std::array<int,8> gm{{36,38,42,46,37,41,49,43}};
 const int pitch=slot.stereo.empty()?gm[size_t(lane)]:slot.midiNote;
 if(e.x<left && e.y<bottom){
  drumSelectedLane=lane;
  if(slot.stereo.size()>2){
   const juce::ScopedLock lock(host.audioLock);
   host.previewAudio.stereo=slot.stereo;host.previewAudio.sampleRate=slot.sampleRate;
   host.previewAudio.name=slot.name;host.previewFrame=0;host.previewPlaying=true;
  }
  repaint();return;
 }
 const float cell=std::max(7.f,float(getWidth()-left-13)/float(drumGridSteps));
 const int column=std::clamp(int((e.x-left)/cell),0,drumGridSteps-1);
 const int step=drumGridStart+column;
 const juce::ScopedLock lock(host.audioLock);
 if(e.y>=bottom){
  const auto& selected=host.session.tracks[size_t(host.selectedTrack)].drumRack[size_t(drumSelectedLane)];
  const int selectedPitch=selected.stereo.empty()?gm[size_t(drumSelectedLane)]:selected.midiNote;
  if(veysona::drums::findNote(*region,selectedPitch,step)<0)return;
  host.saveMidiUndo();drumVelocityEditing=true;drumLastStep=step;
  veysona::drums::setVelocity(*region,selectedPitch,step,std::clamp(float(h-5-e.y)/45.f,.01f,1.f));
 }else if(e.x>=left){
  drumSelectedLane=lane;
  const bool on=veysona::drums::findNote(*region,pitch,step)>=0;
  host.saveMidiUndo();drumPainting=true;drumLastStep=step;
  drumPaintAdd=e.mods.isShiftDown()||(!e.mods.isRightButtonDown()&&!on);
  veysona::drums::setStep(*region,pitch,step,drumPaintAdd,e.mods.isShiftDown()?1.f:.8f);
 }
 host.arrangement.repaint();repaint();
}
void PianoRoll::drumMouseDrag(const juce::MouseEvent& e){
 if(!drumPainting&&!drumVelocityEditing)return;
 auto* region=host.activeMidi();if(!region)return;
 const int left=142,h=getHeight();
 const float cell=std::max(7.f,float(getWidth()-left-13)/float(drumGridSteps));
 const int step=drumGridStart+std::clamp(int((e.x-left)/cell),0,drumGridSteps-1);
 constexpr std::array<int,8> gm{{36,38,42,46,37,41,49,43}};
 const auto& slot=host.session.tracks[size_t(host.selectedTrack)].drumRack[size_t(drumSelectedLane)];
 const int pitch=slot.stereo.empty()?gm[size_t(drumSelectedLane)]:slot.midiNote;
 const juce::ScopedLock lock(host.audioLock);
 if(drumVelocityEditing){
  veysona::drums::setVelocity(*region,pitch,step,std::clamp(float(h-5-e.y)/45.f,.01f,1.f));
 }else if(step!=drumLastStep){
  veysona::drums::setStep(*region,pitch,step,drumPaintAdd,e.mods.isShiftDown()?1.f:.8f);
 }
 drumLastStep=step;host.arrangement.repaint();repaint();
}
void PianoRoll::focusOnNotes(){
 if(isDrumTrack()){drumGridStart=0;drumSelectedLane=0;repaint();return;}
 auto* region=host.activeMidi();if(!region||region->notes.empty()){topPitch=72;visibleStartBeat=0;return;}
 int high=0,low=127;double start=1024;
 for(const auto& n:region->notes){high=std::max(high,n.pitch);low=std::min(low,n.pitch);start=std::min(start,n.beat);}
 topPitch=std::clamp(high+2,14,127);
 visibleStartBeat=std::max(0.,std::floor(start/4.)*4.);
 if(high-low>13)topPitch=std::clamp(high+1,14,127);
 repaint();
}
double PianoRoll::snapGrid() const {return std::pow(2.,1-host.midiGrid.getSelectedId());}
juce::Rectangle<float> PianoRoll::noteRect(const veysona::Note& n) const {
 const float x=54.f+float((n.beat-visibleStartBeat)*beatPixels);
 const float y=29.f+float(topPitch-n.pitch)*noteHeight;
 return {x+1.f,y+1.f,std::max(5.f,float(n.length*beatPixels)-2.f),noteHeight-1.f};
}
void PianoRoll::paint(juce::Graphics& g){
 if(isDrumTrack()){paintDrumGrid(g);return;}
 g.fillAll(C::bg);
 const int w=getWidth(),h=getHeight(),left=54,head=29,velocityH=43,gridBottom=h-velocityH;
 g.setColour(C::panel);g.fillRect(0,0,w,head);
 caption(g,"PIANO ROLL",{11,2,140,25},C::ink,11);
 if(w>490)caption(g,"Click to add  /  Drag to move  /  Edge to resize  /  Right-click to delete",{143,2,w-155,25},C::muted,10);
 {juce::Graphics::ScopedSaveState state(g);
 g.reduceClipRegion(0,head,w,gridBottom-head);
 const int rows=std::max(1,int((gridBottom-head)/noteHeight)+1);
 for(int row=0;row<rows;++row){
  int pitch=topPitch-row;if(pitch<0)break;
  int y=head+int(row*noteHeight);
  bool dark=(pitch%12==1||pitch%12==3||pitch%12==6||pitch%12==8||pitch%12==10);
  g.setColour(dark?juce::Colour(0xff43546b):juce::Colour(0xffb9c7d8));g.fillRect(0,y,left-1,int(noteHeight)-1);
  if(pitch%12==0)caption(g,"C"+juce::String(pitch/12-1),{4,y,44,int(noteHeight)},C::bg,9);
  if(pitch%12==0){g.setColour(C::purple.withAlpha(.045f));g.fillRect(left,y,w-left,int(noteHeight));}
  if(scaleMode!=0){
   const bool inScale=scalePitch(pitch)==pitch;
   const bool root=(pitch-scaleRoot+132)%12==0;
   if(inScale){g.setColour((root?C::aqua:C::blue).withAlpha(root?.13f:.055f));g.fillRect(left,y,w-left,int(noteHeight));}
   else {g.setColour(C::bg.withAlpha(.40f));g.fillRect(left,y,w-left,int(noteHeight));}
  }
  g.setColour((pitch%12==0)?C::outline:C::outline.withAlpha(.27f));g.drawHorizontalLine(y,left,float(w));
 }
 const double grid=snapGrid();const int columns=std::max(2,int((w-left)/beatPixels/grid)+2);
 for(int i=0;i<columns;++i){
  double beat=veysona::midi::snap(visibleStartBeat,grid)+i*grid;
  float x=left+float((beat-visibleStartBeat)*beatPixels);
  if(x<left||x>w)continue;
  bool bar=std::fmod(beat,double(host.session.beatsPerBar))<.00001;
  g.setColour(bar?C::outline.brighter(.2f):C::outline.withAlpha(.29f));g.drawVerticalLine(int(x),head,float(gridBottom));
 }
 if(auto* region=host.activeMidi()){
  for(size_t i=0;i<region->notes.size();++i){
   const auto& n=region->notes[i];auto r=noteRect(n);
   if(r.getBottom()<head||r.getY()>gridBottom||r.getRight()<left||r.getX()>w)continue;
   auto selected=host.selectedNote==int(i);
   const auto noteColour=selected?C::gold:C::purple.brighter(.28f);
   roundRect(g,r,noteColour,3.f);
   g.setColour(selected?C::ink.withAlpha(.85f):C::ink.withAlpha(.23f));
   g.drawRoundedRectangle(r.reduced(.65f),2.5f,selected?1.45f:.9f);
   g.setColour(C::ink.withAlpha(selected?.35f:.16f));
   g.fillRoundedRectangle(r.getX()+2.f,r.getY()+1.5f,std::max(1.f,r.getWidth()-4.f),std::min(2.f,r.getHeight()*.18f),1.f);
   if(selected&&r.getWidth()>28.f)caption(g,juce::String(int(n.velocity*127.f)),{int(r.getX()+5),int(r.getY()),int(r.getWidth()-10),int(r.getHeight())},C::bg,9);
   g.setColour(selected?C::bg:C::purple.brighter(.8f));g.fillRect(r.getRight()-3,r.getY()+2,2.f,std::max(1.f,r.getHeight()-4));
  }
 }else caption(g,"Select a MIDI clip in Arrangement View or add a MIDI track to start composing.",{left+16,65,w-left-26,55},C::muted,12);
}
 // bottom lane: velocity values are editable by dragging their stems.
 g.setColour(C::panel);g.fillRect(0,gridBottom,w,velocityH);
 caption(g,"VELOCITY",{5,gridBottom+6,left-7,20},C::muted,10);
 g.setColour(C::outline);g.drawHorizontalLine(gridBottom,left,float(w));
 if(auto* region=host.activeMidi())for(size_t i=0;i<region->notes.size();++i){
  const auto& n=region->notes[i];float x=54.f+float((n.beat-visibleStartBeat)*beatPixels)+5.f;
  if(x<left||x>w)continue;
  float height=std::clamp(n.velocity,.01f,1.f)*float(velocityH-8);
  g.setColour(host.selectedNote==int(i)?C::gold:C::purple);g.fillRoundedRectangle(x,float(h-4)-height,5.f,height,2.f);
  if(host.selectedNote==int(i)&&x+24.f<w)caption(g,juce::String(int(n.velocity*127.f)),{int(x+8),h-20,28,16},C::ink,9);
 }
}
void PianoRoll::mouseDown(const juce::MouseEvent& e){
 if(isDrumTrack()){drumMouseDown(e);return;}
 auto* region=host.activeMidi();if(!region||e.x<54)return;
 const int bottom=getHeight()-43;
 double beat=visibleStartBeat+(e.x-54)/beatPixels;
 const int pitch=scalePitch(std::clamp(topPitch-int((e.y-29)/noteHeight),0,127));
 const juce::ScopedLock lock(host.audioLock);
 dragNote=-1;dragging=resizing=velocityDrag=false;
 if(e.y>=bottom){
  double distance=1e12;int index=-1;
  for(int i=0;i<int(region->notes.size());++i){double d=std::abs(region->notes[size_t(i)].beat-beat);if(d<distance){distance=d;index=i;}}
  if(index>=0&&distance<std::max(.35,8./beatPixels)){
   host.saveMidiUndo();dragNote=index;dragging=velocityDrag=true;
   host.selectedNote=index;startingVelocity=region->notes[size_t(index)].velocity;
  }
  repaint();return;
 }
 if(e.y<29||e.y>=bottom)return;
 for(int i=int(region->notes.size())-1;i>=0;--i){
  auto r=noteRect(region->notes[size_t(i)]);
  if(r.contains(float(e.x),float(e.y))){dragNote=i;break;}
 }
 if(e.mods.isRightButtonDown()){
  if(dragNote>=0){host.saveMidiUndo();region->notes.erase(region->notes.begin()+dragNote);host.selectedNote=-1;}
  repaint();host.arrangement.repaint();return;
 }
 host.saveMidiUndo();
 if(dragNote<0){
  const double snapped=veysona::midi::snap(beat,snapGrid());
  region->notes.push_back({pitch,snapped,std::max(.125,snapGrid()),.8f});
  dragNote=int(region->notes.size())-1;
  region->lengthBeats=std::max(region->lengthBeats,std::ceil((snapped+snapGrid())/host.session.beatsPerBar)*host.session.beatsPerBar);
 }else{
  auto r=noteRect(region->notes[size_t(dragNote)]);
  resizing=float(e.x)>r.getRight()-8.f;
 }
 host.selectedNote=dragNote;
 const auto& n=region->notes[size_t(dragNote)];startingPitch=n.pitch;startingBeat=n.beat;startingLength=n.length;startingVelocity=n.velocity;
 dragging=true;repaint();host.arrangement.repaint();
}
void PianoRoll::mouseDrag(const juce::MouseEvent& e){
 if(isDrumTrack()){drumMouseDrag(e);return;}
 if(!dragging||dragNote<0)return;
 auto* region=host.activeMidi();if(!region||dragNote>=int(region->notes.size()))return;
 const juce::ScopedLock lock(host.audioLock);
 auto& n=region->notes[size_t(dragNote)];
 if(velocityDrag){n.velocity=std::clamp(float((getHeight()-4-e.y)/float(39)),.01f,1.f);}
 else if(resizing){
  double end=visibleStartBeat+(e.x-54)/beatPixels;
  n.length=std::max(snapGrid(),veysona::midi::snap(end,snapGrid())-startingBeat);
 }else{
  double dx=(e.x-e.getMouseDownX())/beatPixels;
  double move=veysona::midi::snap(startingBeat+dx,snapGrid());
  int dy=int(std::round((e.y-e.getMouseDownY())/noteHeight));
  n.beat=std::max(0.,move);n.pitch=scalePitch(std::clamp(startingPitch-dy,0,127));
 }
 region->lengthBeats=std::max(region->lengthBeats,std::ceil((n.beat+n.length)/host.session.beatsPerBar)*host.session.beatsPerBar);
 repaint();host.arrangement.repaint();
}
void PianoRoll::mouseUp(const juce::MouseEvent&){
 if(isDrumTrack()){drumPainting=drumVelocityEditing=false;drumLastStep=-1;repaint();return;}
 if(dragging){
  if(auto* region=host.activeMidi()){
   const juce::ScopedLock lock(host.audioLock);
   const int trackedPitch=(dragNote>=0&&dragNote<int(region->notes.size()))?region->notes[size_t(dragNote)].pitch:-1;
   const double trackedBeat=(dragNote>=0&&dragNote<int(region->notes.size()))?region->notes[size_t(dragNote)].beat:-1.;
   veysona::midi::sanitize(region->notes);
   host.selectedNote=-1;
   for(int i=0;i<int(region->notes.size());++i)if(region->notes[size_t(i)].pitch==trackedPitch&&std::abs(region->notes[size_t(i)].beat-trackedBeat)<.00001){host.selectedNote=i;break;}
  }
 }
 dragNote=-1;dragging=resizing=velocityDrag=false;repaint();
}
void PianoRoll::mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& wheel){
 if(isDrumTrack()){
  drumGridStart=std::clamp(drumGridStart+(wheel.deltaY>0?-16:16),0,240);
  repaint();return;
 }
 if(e.mods.isShiftDown())visibleStartBeat=std::clamp(visibleStartBeat-wheel.deltaY*8.,0.,960.);
 else if(e.mods.isCtrlDown())beatPixels=std::clamp(beatPixels+wheel.deltaY*60.,20.,150.);
 else topPitch=std::clamp(topPitch+int(std::round(wheel.deltaY*7.)),36,127);
 repaint();
}
void Studio::showExpandedEQ(){
 if(expandedEQ!=nullptr){expandedEQ->getTopLevelComponent()->toFront(true);return;}
 auto* editor=new EQView(*this);
 editor->setSize(980,560);
 expandedEQ=editor;
 juce::DialogWindow::LaunchOptions opts;
 opts.content.setOwned(editor);
 opts.dialogTitle="Veysona Studio  |  Spectrum EQ 8";
 opts.dialogBackgroundColour=C::panel;
 opts.resizable=true;
 opts.useNativeTitleBar=true;
 opts.launchAsync();
}
static float eqFrequencyToX(float hz,juce::Rectangle<float> area){
 return area.getX()+float(std::log(std::clamp(double(hz),30.,20000.)/30.)/std::log(20000./30.))*area.getWidth();
}
static float eqXToFrequency(float x,juce::Rectangle<float> area){
 return float(30.*std::exp(std::clamp(double((x-area.getX())/std::max(1.f,area.getWidth())),0.,1.)*std::log(20000./30.)));
}
// The analyzer is computed on the message thread from a fixed atomic audio ring.
// No FFT allocations, locks, or transforms run in the realtime callback.
void Studio::refreshAnalyser(){
 constexpr size_t N=4096;
 std::array<std::complex<float>,N> fft{};
 const uint32_t tail=eqInputWrite.load(std::memory_order_acquire);
 for(size_t i=0;i<N;++i){
  const float wave=eqInputRing[size_t((tail-uint32_t(N)+uint32_t(i)) & 4095u)].load(std::memory_order_relaxed);
  const float hann=.5f-.5f*std::cos(2.f*juce::MathConstants<float>::pi*float(i)/float(N-1));
  fft[i]={wave*hann,0.f};
 }
 for(size_t i=1,j=0;i<N;++i){
  size_t mask=N>>1;
  for(;j&mask;mask>>=1)j^=mask;
  j^=mask;if(i<j)std::swap(fft[i],fft[j]);
 }
 for(size_t width=2;width<=N;width*=2){
  const float phi=-2.f*juce::MathConstants<float>::pi/float(width);
  const std::complex<float> phase(std::cos(phi),std::sin(phi));
  for(size_t i=0;i<N;i+=width){
   std::complex<float> omega{1.f,0.f};
   for(size_t j=0;j<width/2;++j){
    auto a=fft[i+j],b=omega*fft[i+j+width/2];
    fft[i+j]=a+b;fft[i+j+width/2]=a-b;omega*=phase;
   }
  }
 }
 for(size_t i=0;i<analyserBins.size();++i){
  const float amplitude=std::abs(fft[i+1])/1024.f;
  const float db=std::clamp(20.f*std::log10(std::max(1.e-7f,amplitude)),-90.f,0.f);
  analyserBins[i]=std::clamp(.82f*analyserBins[i]+.18f*db,-90.f,0.f);
 }
}
static juce::String eqFreqLabel(float value){
 return value>=1000.f?juce::String(value/1000.f,value<10000.f?2:1)+" kHz":juce::String(int(std::round(value)))+" Hz";
}
// Spectrum EQ 8: compact dock and expanded view share the same eight-band DSP
// state. Every preset, edit and bypass change goes through the audio lock.
void EQView::paint(juce::Graphics& g){
 g.fillAll(C::panel);
 const int w=getWidth(),h=getHeight();
 const bool compact=h<265;
 const auto area=graphArea();
 const float center=area.getCentreY(),half=area.getHeight()*.46f;
 const auto& eqValid=host.session.tracks;
 const bool valid=host.selectedTrack>=0&&host.selectedTrack<int(eqValid.size());
 const bool eqOn=valid&&eqValid[size_t(host.selectedTrack)].eq.enabled;
 // Header, selectable controls, and clear on/off indication.
 caption(g,"SPECTRUM EQ 8",{12,8,std::max(65,w-185),23},C::ink,13);
 if(w>410){
  roundRect(g,{float(w-282),11.f,60.f,18.f},eqOn?C::green.withAlpha(.14f):C::pink.withAlpha(.10f),4.f);
  caption(g,eqOn?"ACTIVE":"BYPASS",{w-276,12,56,16},eqOn?C::green:C::muted,10);
 }
 auto control=[&](juce::Rectangle<int> r,const juce::String& title,bool active){
  roundRect(g,r.toFloat(),active?C::purple.withAlpha(.24f):C::raised.withAlpha(.72f),5.f);
  g.setColour(active?C::purple.withAlpha(.76f):C::outline.withAlpha(.8f));
  g.drawRoundedRectangle(r.toFloat().reduced(.5f),5.f,1.f);
  g.setFont(juce::Font(10.f).boldened());g.setColour(active?C::ink:C::muted);
  g.drawFittedText(title,r,juce::Justification::centred,1);
 };
 control(liveButton(),"LIVE",analyserVisible);
 control(presetsButton(),"PRESETS",false);
 control(expandButton(),"+",false);
 // Plot surface with subtle vertical shading; clipping is essential for short docks.
 juce::ColourGradient surface(C::bg.brighter(.09f),area.getX(),area.getY(),C::bg,area.getX(),area.getBottom(),false);
 g.setGradientFill(surface);g.fillRoundedRectangle(area,6.f);
 g.setColour(C::outline.withAlpha(.85f));g.drawRoundedRectangle(area,6.f,1.f);
 g.saveState();g.reduceClipRegion(area.toNearestInt().reduced(1));
 // A visible unity reference helps producers avoid accidental gain bias.
 g.setColour(C::aqua.withAlpha(.045f));
 g.fillRect(area.getX(),center-9.f,area.getWidth(),18.f);
 for(int d=-18;d<=18;d+=6){
  const float y=center-float(d)/18.f*half;
  g.setColour((d==0?C::muted:C::outline).withAlpha(d==0?.66f:.36f));
  g.drawHorizontalLine(int(y),area.getX(),area.getRight());
 }
 for(int hz:{30,60,100,200,500,1000,2000,5000,10000,20000}){
  const float x=eqFrequencyToX(float(hz),area);
  g.setColour(C::outline.withAlpha(hz==1000?.45f:.24f));
  g.drawVerticalLine(int(x),area.getY(),area.getBottom());
 }
 if(valid){
  const auto& eq=eqValid[size_t(host.selectedTrack)].eq;
  const uint32_t rate=std::max(8000u,host.session.sampleRate);
  if(analyserVisible){
   juce::Path spectrum;
   const int steps=std::max(90,int(area.getWidth()));
   for(int i=0;i<=steps;++i){
    const float x=area.getX()+area.getWidth()*float(i)/float(steps);
    const float hz=eqXToFrequency(x,area);
    // Smooth in linear frequency between neighbouring bins. This removes
    // the blocky, one-bin-at-a-time steps in the live spectrum display.
    const float fractional=std::clamp(hz*4096.f/float(rate)-1.f,0.f,float(host.analyserBins.size()-1));
    const int bin=int(fractional);
    const float blend=fractional-bin;
    const float db0=host.analyserBins[size_t(bin)];
    const float db1=host.analyserBins[size_t(std::min(bin+1,int(host.analyserBins.size())-1))];
    const float db=std::clamp(db0+(db1-db0)*blend,-90.f,0.f);
    const float y=area.getBottom()-(db+90.f)/90.f*area.getHeight();
    if(i==0)spectrum.startNewSubPath(x,y);else spectrum.lineTo(x,y);
   }
   juce::Path fill=spectrum;
   fill.lineTo(area.getRight(),area.getBottom());fill.lineTo(area.getX(),area.getBottom());fill.closeSubPath();
   juce::ColourGradient spectral(C::blue.withAlpha(.28f),area.getX(),area.getY(),C::blue.withAlpha(.015f),area.getX(),area.getBottom(),false);
   g.setGradientFill(spectral);g.fillPath(fill);
   g.setColour(C::blue.withAlpha(.60f));g.strokePath(spectrum,juce::PathStrokeType(1.35f));
  }
  juce::Path response;
  const int steps=std::max(128,int(area.getWidth()));
  for(int i=0;i<=steps;++i){
   const float x=area.getX()+float(i)/float(steps)*area.getWidth();
   const float db=float(std::clamp(veysona::eqMagnitudeDb(eq,eqXToFrequency(x,area),rate),-18.,18.));
   const float y=center-db/18.f*half;
   if(i==0)response.startNewSubPath(x,y);else response.lineTo(x,y);
  }
  if(eq.enabled){
   juce::Path under=response;under.lineTo(area.getRight(),center);under.lineTo(area.getX(),center);under.closeSubPath();
   g.setColour(C::aqua.withAlpha(.11f));g.fillPath(under);
   g.setColour(C::aqua.withAlpha(.13f));g.strokePath(response,juce::PathStrokeType(8.f,juce::PathStrokeType::curved));
   g.setColour(C::aqua.withAlpha(.25f));g.strokePath(response,juce::PathStrokeType(4.f,juce::PathStrokeType::curved));
   g.setColour(C::aqua);g.strokePath(response,juce::PathStrokeType(2.f,juce::PathStrokeType::curved));
  }else{
   g.setColour(C::muted.withAlpha(.45f));g.strokePath(response,juce::PathStrokeType(1.6f));
  }
  const std::array<juce::Colour,8> colours{C::gold,C::purple,C::blue,C::green,C::pink,C::aqua,C::gold,C::purple};
  for(int i=0;i<8;++i){
   const auto& band=eq.bands[size_t(i)];
   const float x=eqFrequencyToX(band.frequency,area);
   const float y=center-std::clamp(band.gainDb,-18.f,18.f)/18.f*half;
   const bool chosen=i==selectedBand;
   auto col=eq.enabled&&band.enabled?colours[size_t(i)]:C::muted;
   if(chosen){
    g.setColour(col.withAlpha(.10f));g.fillEllipse(x-22.f,y-22.f,44.f,44.f);
    g.setColour(col.withAlpha(.78f));g.drawEllipse(x-15.f,y-15.f,30.f,30.f,1.3f);
    g.setColour(col.withAlpha(.15f));g.drawVerticalLine(int(x),area.getY(),area.getBottom());
   }
   const float rad=chosen?9.2f:7.2f;
   g.setColour(C::bg.withAlpha(.85f));g.fillEllipse(x-rad-1.7f,y-rad-1.7f,2*rad+3.4f,2*rad+3.4f);
   g.setColour(col);g.fillEllipse(x-rad,y-rad,2*rad,2*rad);
   g.setColour(C::bg);g.setFont(juce::Font(10.f).boldened());
   g.drawFittedText(juce::String(i+1),{int(x-rad),int(y-rad),int(2*rad),int(2*rad)},juce::Justification::centred,1);
   if(!band.enabled){g.setColour(C::pink);g.drawLine(x-rad,y+rad,x+rad,y-rad,1.4f);}
  }
 }
 g.restoreState();
 // Frequency and gain scales stay outside the clipped plot area.
 for(int d:{-18,-12,-6,0,6,12,18}){
  const float y=center-float(d)/18.f*half;
  caption(g,(d>0?"+":"")+juce::String(d),{5,int(y-7),34,14},d==0?C::ink:C::muted,10);
 }
 for(int hz:{30,100,500,1000,5000,20000}){
  const float x=eqFrequencyToX(float(hz),area);
  caption(g,hz>=1000?juce::String(hz/1000)+"k":juce::String(hz),{int(x-13),int(area.getBottom()+3),33,15},C::muted,10);
 }
 if(!valid){caption(g,"Select a track to edit its EQ",area.toNearestInt().reduced(15),C::muted,12);return;}
 const auto& eq=eqValid[size_t(host.selectedTrack)].eq;
 const std::array<juce::Colour,8> colours{C::gold,C::purple,C::blue,C::green,C::pink,C::aqua,C::gold,C::purple};
 const int y=bandChipY();
 const float pitch=std::max(8.f,float(w-24)/8.f);
 // Extremely short docks reserve space for the graph and numerical readout.
 // Band chips remain available once the editor is tall enough to fit them.
 if(h>=170)for(int i=0;i<8;++i){
  const float x=12.f+pitch*i;
  const float cw=std::max(5.f,pitch-3.f);
  const bool selected=i==selectedBand;
  const auto& b=eq.bands[size_t(i)];
  auto col=b.enabled?colours[size_t(i)]:C::muted;
  roundRect(g,{x,float(y),cw,compact?25.f:35.f},selected?C::raised:C::bg,5.f);
  g.setColour(selected?col:C::outline);g.drawRoundedRectangle(x+.5f,float(y)+.5f,cw-1.f,compact?24.f:34.f,5.f,selected?1.7f:.8f);
  g.setColour(col);g.fillEllipse(x+5.f,float(y)+7.f,6.f,6.f);
  caption(g,juce::String(i+1),{int(x+14),y+3,int(cw-17),17},selected?C::ink:C::muted,11);
  if(!compact&&cw>54.f)caption(g,eqFreqLabel(b.frequency),{int(x+4),y+18,int(cw-8),15},C::muted,10);
 }
 const auto& band=eq.bands[size_t(std::clamp(selectedBand,0,7))];
 const auto type=band.mode==veysona::EqMode::Bell?"BELL":(band.mode==veysona::EqMode::LowShelf?"LOW SHELF":"HIGH SHELF");
 const int readoutY=h-(compact?36:53);
 roundRect(g,{12.f,float(readoutY),float(std::max(50,w-24)),26.f},C::raised.withAlpha(.66f),5.f);
 const auto readout="B"+juce::String(selectedBand+1)+"  "+juce::String(type)+"     "+eqFreqLabel(band.frequency)+"     "+
  (band.gainDb>0?"+":"")+juce::String(band.gainDb,1)+" dB     Q "+juce::String(band.q,2);
 caption(g,readout,{19,readoutY+3,w-36,20},eqOn&&band.enabled?C::ink:C::muted,11);
 if(!compact)caption(g,"DRAG  freq / gain     SHIFT  fine     WHEEL  Q     RIGHT-CLICK  options",{13,h-21,w-26,18},C::muted,10);
}
void EQView::showPresetMenu(){
 juce::Component::SafePointer<EQView> self(this);
 juce::PopupMenu menu;
 using P=veysona::eqtools::Preset;
 menu.addSectionHeader("Eight-band EQ: selected track");
 for(auto p:{P::Flat,P::VocalPresence,P::BassWeight,P::DrumPunch,P::WarmKeys,P::AiryPads,P::DeMud,P::Telephone}){
  menu.addItem(veysona::eqtools::presetName(p),[self,p]{
   if(self==nullptr)return;
   auto& h=self->host;const int track=h.selectedTrack;
   if(track<0||track>=int(h.session.tracks.size()))return;
   const juce::ScopedLock lock(h.audioLock);
   auto& eq=h.session.tracks[size_t(track)].eq;const auto before=eq;
   eq=veysona::eqtools::makePreset(p);h.eqHistory.commit(track,before,eq);
   h.updateControls();h.eqView.repaint();if(h.expandedEQ!=nullptr)h.expandedEQ->repaint();
   h.setStatus("EQ preset: "+juce::String(veysona::eqtools::presetName(p)));
  });
 }
 menu.addSeparator();menu.addSectionHeader("Edit / compare");
 menu.addItem("Bypass eight-band EQ",true,
  host.selectedTrack>=0&&host.selectedTrack<int(host.session.tracks.size())&&!host.session.tracks[size_t(host.selectedTrack)].eq.enabled,
  [self]{
   if(self==nullptr)return;auto& h=self->host;const int track=h.selectedTrack;
   if(track<0||track>=int(h.session.tracks.size()))return;
   const juce::ScopedLock lock(h.audioLock);auto& eq=h.session.tracks[size_t(track)].eq;const auto before=eq;
   eq.enabled=!eq.enabled;h.eqHistory.commit(track,before,eq);
   h.updateControls();h.eqView.repaint();if(h.expandedEQ!=nullptr)h.expandedEQ->repaint();
  });
 menu.addItem("Undo EQ change",host.eqHistory.canUndo(host.selectedTrack),false,[self]{
  if(self==nullptr)return;auto& h=self->host;const int track=h.selectedTrack;
  if(track<0||track>=int(h.session.tracks.size()))return;
  const juce::ScopedLock lock(h.audioLock);
  h.eqHistory.undo(track,h.session.tracks[size_t(track)].eq);
  h.updateControls();h.eqView.repaint();if(h.expandedEQ!=nullptr)h.expandedEQ->repaint();
 });
 menu.addItem("Redo EQ change",host.eqHistory.canRedo(host.selectedTrack),false,[self]{
  if(self==nullptr)return;auto& h=self->host;const int track=h.selectedTrack;
  if(track<0||track>=int(h.session.tracks.size()))return;
  const juce::ScopedLock lock(h.audioLock);
  h.eqHistory.redo(track,h.session.tracks[size_t(track)].eq);
  h.updateControls();h.eqView.repaint();if(h.expandedEQ!=nullptr)h.expandedEQ->repaint();
 });
 menu.addItem("Copy EQ from this track",[self]{
  if(self==nullptr)return;auto& h=self->host;
  if(h.selectedTrack<0||h.selectedTrack>=int(h.session.tracks.size()))return;
  const juce::ScopedLock lock(h.audioLock);
  h.eqClipboard=h.session.tracks[size_t(h.selectedTrack)].eq;
  h.setStatus("Copied eight-band EQ. Select another track and choose Paste EQ.");
 });
 menu.addItem("Paste EQ to selected track",host.eqClipboard.has_value(),false,[self]{
  if(self==nullptr)return;auto& h=self->host;const int track=h.selectedTrack;
  if(!h.eqClipboard||track<0||track>=int(h.session.tracks.size()))return;
  const juce::ScopedLock lock(h.audioLock);
  auto& eq=h.session.tracks[size_t(track)].eq;const auto before=eq;
  eq=*h.eqClipboard;h.eqHistory.commit(track,before,eq);
  h.updateControls();h.eqView.repaint();if(h.expandedEQ!=nullptr)h.expandedEQ->repaint();
  h.setStatus("Pasted eight-band EQ to selected track.");
 });
 menu.addSeparator();
 menu.addItem("Show live analyzer",true,analyserVisible,[self]{if(self!=nullptr){self->analyserVisible=!self->analyserVisible;self->repaint();}});
 menu.addItem("Open expanded EQ editor",[self]{if(self!=nullptr)self->host.showExpandedEQ();});
 menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
}
void EQView::mouseDown(const juce::MouseEvent& e){
 draggingBand=-1;dragBefore.reset();editedTrack=-1;
 if(expandButton().contains(e.getPosition())){host.showExpandedEQ();return;}
 if(presetsButton().contains(e.getPosition())){showPresetMenu();return;}
 if(liveButton().contains(e.getPosition())){analyserVisible=!analyserVisible;repaint();return;}
 const int track=host.selectedTrack;
 if(track<0||track>=int(host.session.tracks.size()))return;
 const auto area=graphArea();
 const auto& eq=host.session.tracks[size_t(track)].eq;
 const int chipY=bandChipY();
 if(getHeight()>=170&&e.y>=chipY&&e.y<chipY+(getHeight()<265?25:35)){
  const float pitch=float(getWidth()-24)/8.f;
  if(e.x>=12&&e.x<getWidth()-12&&pitch>0){selectBand(int((e.x-12)/pitch));return;}
 }
 int closest=-1;float best=1.e6f;
 for(int i=0;i<8;++i){
  const auto& b=eq.bands[size_t(i)];
  const float x=eqFrequencyToX(b.frequency,area),y=area.getCentreY()-std::clamp(b.gainDb,-18.f,18.f)*area.getHeight()*.46f/18.f;
  const float dist=std::hypot(float(e.x)-x,float(e.y)-y);
  if(dist<best){best=dist;closest=i;}
 }
 if(best<22.f&&closest>=0)selectBand(closest);
 if(e.mods.isRightButtonDown()){
  const int chosen=selectedBand;
  juce::Component::SafePointer<EQView> self(this);
  juce::PopupMenu menu;menu.addSectionHeader("Band "+juce::String(chosen+1));
  auto edit=[self,chosen](auto fn){
   if(self==nullptr)return;auto& h=self->host;int track=h.selectedTrack;
   if(track<0||track>=int(h.session.tracks.size()))return;
   const juce::ScopedLock lock(h.audioLock);
   auto& eq=h.session.tracks[size_t(track)].eq;const auto before=eq;
   fn(eq.bands[size_t(chosen)]);h.eqHistory.commit(track,before,eq);
   h.eqView.repaint();if(h.expandedEQ!=nullptr)h.expandedEQ->repaint();
  };
  menu.addItem("Band enabled",true,eq.bands[size_t(chosen)].enabled,[edit]{edit([](auto& b){b.enabled=!b.enabled;});});
  menu.addSeparator();
  menu.addItem("Bell",true,eq.bands[size_t(chosen)].mode==veysona::EqMode::Bell,[edit]{edit([](auto& b){b.mode=veysona::EqMode::Bell;});});
  menu.addItem("Low shelf",true,eq.bands[size_t(chosen)].mode==veysona::EqMode::LowShelf,[edit]{edit([](auto& b){b.mode=veysona::EqMode::LowShelf;});});
  menu.addItem("High shelf",true,eq.bands[size_t(chosen)].mode==veysona::EqMode::HighShelf,[edit]{edit([](auto& b){b.mode=veysona::EqMode::HighShelf;});});
  menu.addSeparator();
  menu.addItem("Reset band gain and Q",[edit]{edit([](auto& b){b.gainDb=0.f;b.q=1.f;});});
  menu.addItem("Reset all band gains",[self]{
   if(self==nullptr)return;auto& h=self->host;const int track=h.selectedTrack;
   if(track<0||track>=int(h.session.tracks.size()))return;
   const juce::ScopedLock lock(h.audioLock);auto& eq=h.session.tracks[size_t(track)].eq;
   const auto before=eq;for(auto& b:eq.bands)b.gainDb=0.f;
   h.eqHistory.commit(track,before,eq);h.eqView.repaint();if(h.expandedEQ!=nullptr)h.expandedEQ->repaint();
  });
  menu.addSeparator();menu.addItem("EQ presets / clipboard...",[self]{if(self!=nullptr)self->showPresetMenu();});
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
  return;
 }
 if(best>=22.f||closest<0)return;
 if(e.mods.isAltDown()){
  const juce::ScopedLock lock(host.audioLock);
  auto& current=host.session.tracks[size_t(track)].eq;const auto before=current;
  current.bands[size_t(closest)].enabled=!current.bands[size_t(closest)].enabled;
  host.eqHistory.commit(track,before,current);repaint();return;
 }
 dragBefore=eq;editedTrack=track;draggingBand=closest;
}
void EQView::mouseDrag(const juce::MouseEvent& e){
 if(draggingBand<0||!dragBefore||editedTrack<0||editedTrack!=host.selectedTrack||editedTrack>=int(host.session.tracks.size()))return;
 const auto area=graphArea();
 const auto& orig=dragBefore->bands[size_t(draggingBand)];
 const float fine=e.mods.isShiftDown() ?.18f:1.f;
 float x=eqFrequencyToX(orig.frequency,area)+float(e.getDistanceFromDragStartX())*fine;
 float y=area.getCentreY()-orig.gainDb/18.f*(area.getHeight()*.46f)+float(e.getDistanceFromDragStartY())*fine;
 const juce::ScopedLock lock(host.audioLock);
 auto& b=host.session.tracks[size_t(editedTrack)].eq.bands[size_t(draggingBand)];
 b.frequency=eqXToFrequency(x,area);
 b.gainDb=std::clamp((area.getCentreY()-y)/std::max(1.f,area.getHeight()*.46f)*18.f,-18.f,18.f);
 host.eqView.repaint();if(host.expandedEQ!=nullptr)host.expandedEQ->repaint();
}
void EQView::mouseUp(const juce::MouseEvent&){
 if(draggingBand>=0&&dragBefore&&editedTrack>=0&&editedTrack<int(host.session.tracks.size())){
  const juce::ScopedLock lock(host.audioLock);
  host.eqHistory.commit(editedTrack,*dragBefore,host.session.tracks[size_t(editedTrack)].eq);
 }
 draggingBand=-1;editedTrack=-1;dragBefore.reset();
}
void EQView::mouseDoubleClick(const juce::MouseEvent& e){
 const int track=host.selectedTrack;
 if(track<0||track>=int(host.session.tracks.size())||!graphArea().contains(float(e.x),float(e.y)))return;
 const juce::ScopedLock lock(host.audioLock);
 auto& eq=host.session.tracks[size_t(track)].eq;const auto before=eq;
 if(e.mods.isAltDown())for(auto& b:eq.bands)b.gainDb=0.f;
 else eq.bands[size_t(selectedBand)].gainDb=0.f;
 host.eqHistory.commit(track,before,eq);host.eqView.repaint();if(host.expandedEQ!=nullptr)host.expandedEQ->repaint();
}
void EQView::mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& wheel){
 const int track=host.selectedTrack;
 if(track<0||track>=int(host.session.tracks.size())||!graphArea().contains(float(e.x),float(e.y)))return;
 const auto area=graphArea();
 int closest=selectedBand;float best=1.e6f;
 const auto& eq=host.session.tracks[size_t(track)].eq;
 for(int i=0;i<8;++i){
  const auto& b=eq.bands[size_t(i)];
  const float x=eqFrequencyToX(b.frequency,area),y=area.getCentreY()-b.gainDb/18.f*(area.getHeight()*.46f);
  const float dist=std::hypot(float(e.x)-x,float(e.y)-y);
  if(dist<best){best=dist;closest=i;}
 }
 if(best>34.f)return;
 const juce::ScopedLock lock(host.audioLock);
 selectedBand=closest;auto& current=host.session.tracks[size_t(track)].eq;const auto before=current;
 auto& b=current.bands[size_t(closest)];
 b.q=std::clamp(b.q*std::pow(2.f,wheel.deltaY*(e.mods.isShiftDown()?.18f:.8f)),.25f,12.f);
 host.eqHistory.commit(track,before,current);
 host.eqView.repaint();if(host.expandedEQ!=nullptr)host.expandedEQ->repaint();
}
// v1.15 channel strips: logarithmic dB faders, visible pan, bus routing,
// direct send controls and metering. All writes use the host audio lock.
void MixerView::paint(juce::Graphics& g){
 g.fillAll(C::bg);
 const int h=getHeight(),strip=150;
 const bool anySolo=std::any_of(host.session.tracks.begin(),host.session.tracks.end(),[](const auto& t){return t.solo;});
 for(int i=0;i<int(host.session.tracks.size());++i){
  const int x=9+i*strip;if(x>=getWidth())break;
  const auto& t=host.session.tracks[size_t(i)];const auto colour=trackColour(i);
  const bool audible=veysona::mix::audible(t,anySolo);
  roundRect(g,{float(x),4.f,142.f,float(std::max(20,h-9))},C::panel,7);
  g.setColour(C::outline.withAlpha(.34f));
  g.drawRoundedRectangle(float(x)+.6f,4.6f,140.8f,float(std::max(19,h-11)),6.4f,.9f);
  g.setColour(colour.withAlpha(.16f));g.fillRoundedRectangle(float(x+10),7.f,122.f,29.f,4.f);
  if(i==host.selectedTrack){g.setColour(C::blue.withAlpha(.88f));g.drawRoundedRectangle(float(x)+.75f,4.75f,140.5f,float(std::max(19,h-11)),5.f,1.8f);}
  g.setColour(colour);g.fillRoundedRectangle(float(x+4),9.f,4.f,23.f,2.f);
  caption(g,jstr(t.name),{x+14,9,116,25},audible?C::ink:C::muted,11);
  const bool bus=t.outputBus>=0&&t.outputBus<int(host.session.buses.size());
  juce::String destination=bus?jstr(host.session.buses[size_t(t.outputBus)].name):"MASTER";
  caption(g,"OUT  "+destination,{x+13,37,121,19},C::aqua,10);
  roundRect(g,{float(x+12),65.f,49.f,25.f},t.mute?C::pink:C::raised,4);
  roundRect(g,{float(x+65),65.f,49.f,25.f},t.solo?C::gold:C::raised,4);
  caption(g,"MUTE",{x+18,68,41,19},t.mute?C::bg:C::ink,10);
  caption(g,"SOLO",{x+70,68,41,19},t.solo?C::bg:C::ink,10);
  caption(g,"PAN",{x+12,98,28,20},C::muted,10);
  g.setColour(C::outline);g.drawLine(float(x+45),108.f,float(x+126),108.f,2.f);
  g.setColour(C::muted);g.drawVerticalLine(x+85,103.f,113.f);
  g.setColour(C::aqua);g.fillEllipse(float(x+85)+t.pan*37.f-5.f,103.f,10.f,10.f);
  const int top=std::min(129,std::max(114,h-72)),base=std::max(top+15,h-39),travel=base-top;
  // -72 to +6 dB is mapped across the fader; zero gain is explicit -infinity.
  const float faderY=float(base)-veysona::mix::faderFromGain(t.gain)*float(travel);
  g.setColour(C::outline);g.fillRoundedRectangle(float(x+51),float(top),7.f,float(travel),3.f);
  g.setColour(C::blue.withAlpha(.9f));g.fillRoundedRectangle(float(x+51),faderY,7.f,std::max(0.f,float(base)-faderY),3.f);
  const auto unityY=float(base)-veysona::mix::faderFromGain(1.f)*float(travel);
  g.setColour(C::aqua.withAlpha(.72f));g.drawLine(float(x+41),unityY,float(x+68),unityY,1.2f);
  roundRect(g,{float(x+37),faderY-4.f,35.f,10.f},C::ink,3);
  g.setColour(C::bg.withAlpha(.68f));g.fillRoundedRectangle(float(x+43),faderY-.4f,23.f,2.f,1.f);
  for(const float tick:{-48.f,-24.f,-12.f,0.f,6.f}){
   const float y=float(base)-veysona::mix::faderFromGain(veysona::mix::amplitude(tick))*float(travel);
   g.setColour(C::outline);g.drawLine(float(x+24),y,float(x+33),y,1.f);
  }
  caption(g,"dB",{x+17,top-6,30,15},C::muted,9);
  const float raw=std::clamp(host.trackPeaks[size_t(i)].load(std::memory_order_relaxed),0.f,2.f);
  g.setColour(C::raised);g.fillRoundedRectangle(float(x+100),float(top),12.f,float(travel),3.f);
  const float meter=veysona::mix::peakMeterFraction(raw);
  g.setColour(veysona::mix::peakClipping(raw)?C::pink:raw>.72f?C::gold:C::green);
  g.fillRoundedRectangle(float(x+100),float(base)-meter*float(travel),12.f,meter*float(travel),3.f);
  // dBFS readout above the meter helps diagnose gain staging without
  // having to infer levels from the height of an unlabelled bar.
  const juce::String peakLabel=raw<=0.f?"-inf":juce::String::formatted("%.0f",20.f*std::log10(raw));
  caption(g,peakLabel,{x+97,top-18,36,16},veysona::mix::peakClipping(raw)?C::pink:C::muted,9);
  if(veysona::mix::peakClipping(raw)){
   g.setColour(C::pink);g.fillRoundedRectangle(float(x+100),float(top),12.f,3.f,1.5f);
  }
  const float db=veysona::mix::decibels(t.gain);
  caption(g,t.gain<=0.f?"-inf dB":juce::String::formatted("%+.1f dB",db),{x+22,h-35,100,21},C::ink,11);
  int activeSends=0;for(size_t bi=0;bi<host.session.buses.size()&&bi<veysona::kMaxBuses;++bi)activeSends+=t.sends[bi]>.0001f;
  if(activeSends>0)caption(g,juce::String(activeSends)+" sends",{x+74,top-17,62,16},C::aqua,9);
 }
 const int busX=9+int(host.session.tracks.size())*strip;
 for(int b=0;b<int(host.session.buses.size())&&b<int(veysona::kMaxBuses);++b){
  const int x=busX+b*strip;const auto& bus=host.session.buses[size_t(b)];
  const auto colour=C::aqua;
  roundRect(g,{float(x),4.f,142.f,float(std::max(20,h-9))},C::panel,7);
  g.setColour(C::outline);g.drawRoundedRectangle(float(x)+.6f,4.6f,140.8f,float(std::max(19,h-11)),6.4f,1.f);
  g.setColour(colour);g.fillRoundedRectangle(float(x+4),9.f,4.f,23.f,2.f);
  caption(g,jstr(bus.name),{x+14,9,116,25},bus.muted?C::muted:C::ink,11);
  caption(g,"RETURN  "+juce::String(b+1),{x+13,37,121,19},C::aqua,10);
  roundRect(g,{float(x+12),65.f,49.f,25.f},bus.muted?C::pink:C::raised,4);
  caption(g,"MUTE",{x+18,68,41,19},bus.muted?C::bg:C::ink,10);
  caption(g,"PAN",{x+12,98,28,20},C::muted,10);
  g.setColour(C::outline);g.drawLine(float(x+45),108.f,float(x+126),108.f,2.f);
  g.setColour(C::aqua);g.fillEllipse(float(x+85)+bus.pan*37.f-5.f,103.f,10.f,10.f);
  const int top=std::min(129,std::max(114,h-72)),base=std::max(top+15,h-39);
  const float faderY=float(base)-veysona::mix::faderFromGain(bus.gain)*float(base-top);
  g.setColour(C::outline);g.fillRoundedRectangle(float(x+51),float(top),7.f,float(base-top),3.f);
  g.setColour(C::blue);g.fillRoundedRectangle(float(x+51),faderY,7.f,float(base)-faderY,3.f);
  roundRect(g,{float(x+37),faderY-4.f,35.f,10.f},C::ink,3);
  const float peak=std::clamp(host.busPeaks[size_t(b)].load(std::memory_order_relaxed),0.f,2.f);
  const float meter=veysona::mix::peakMeterFraction(peak);
  g.setColour(C::raised);g.fillRoundedRectangle(float(x+100),float(top),12.f,float(base-top),3.f);
  g.setColour(veysona::mix::peakClipping(peak)?C::pink:C::green);
  g.fillRoundedRectangle(float(x+100),float(base)-meter*(base-top),12.f,meter*(base-top),3.f);
  caption(g,juce::String::formatted("%+.1f dB",veysona::mix::decibels(bus.gain)),{x+22,h-35,100,21},C::ink,11);
 }
 {
  const int x=busX+int(host.session.buses.size())*strip;
  roundRect(g,{float(x),4.f,142.f,float(std::max(20,h-9))},C::top,7);
  g.setColour(C::blue);g.drawRoundedRectangle(float(x)+1,5.f,140.f,float(std::max(19,h-11)),6.f,1.5f);
  caption(g,"MASTER",{x+14,9,118,25},C::ink,13);
  caption(g,"MAIN OUTPUT",{x+13,37,124,19},C::aqua,10);
  const int top=std::min(129,std::max(114,h-72)),base=std::max(top+15,h-39);
  const auto gain=host.session.master.gain;
  const float fy=float(base)-veysona::mix::faderFromGain(gain)*(base-top);
  g.setColour(C::outline);g.fillRoundedRectangle(float(x+51),float(top),7.f,float(base-top),3.f);
  g.setColour(C::blue);g.fillRoundedRectangle(float(x+51),fy,7.f,float(base)-fy,3.f);
  roundRect(g,{float(x+37),fy-4.f,35.f,10.f},C::ink,3);
  const float peak=std::max(host.peakL.load(),host.peakR.load());
  const float meter=veysona::mix::peakMeterFraction(peak);
  g.setColour(C::raised);g.fillRoundedRectangle(float(x+100),float(top),12.f,float(base-top),3.f);
  g.setColour(peak>=.999f?C::pink:C::green);
  g.fillRoundedRectangle(float(x+100),float(base)-meter*(base-top),12.f,meter*(base-top),3.f);
  caption(g,juce::String::formatted("%+.1f dB",veysona::mix::decibels(gain)),{x+22,h-35,100,21},C::ink,11);
 }
}
void MixerView::mouseDown(const juce::MouseEvent& e){
 const int i=(e.x-9)/150;
 draggingFader=-1;draggingBus=-1;draggingMaster=false;draggingPan=false;
 if(e.x<9||i<0)return;
 const int tracks=int(host.session.tracks.size());
 if(i>=tracks){
  const int busIdx=i-tracks,localX=e.x-(9+i*150);
  if(e.mods.isPopupMenu()){
   juce::PopupMenu menu;
   if(busIdx<int(host.session.buses.size())){
    const auto& bus=host.session.buses[size_t(busIdx)];
    menu.addSectionHeader("RETURN  /  "+jstr(bus.name));
    juce::PopupMenu width;
    for(float w:{0.f,.5f,1.f,1.5f,2.f})
     width.addItem(juce::String::formatted("%.0f%%",w*100.f),[this,busIdx,w]{
      const juce::ScopedLock lock(host.audioLock);
      host.session.buses[size_t(busIdx)].width=w;repaint();
     });
    menu.addSubMenu("Stereo width",width);
    menu.addItem("Reset return pan and gain",[this,busIdx]{
     const juce::ScopedLock lock(host.audioLock);
     auto& b=host.session.buses[size_t(busIdx)];b.pan=0.f;b.gain=1.f;repaint();
    });
    menu.addItem("Mute return",true,bus.muted,[this,busIdx]{
     const juce::ScopedLock lock(host.audioLock);
     auto& b=host.session.buses[size_t(busIdx)];b.muted=!b.muted;repaint();
    });
   }else if(busIdx==int(host.session.buses.size())){
    menu.addSectionHeader("MASTER  /  OUTPUT");
    menu.addItem("Reset master to 0 dB",[this]{
     const juce::ScopedLock lock(host.audioLock);
     host.session.master.gain=1.f;host.masterGain.setValue(1.,juce::dontSendNotification);repaint();
    });
    menu.addItem("Peak limiter",true,host.session.master.limiterEnabled,[this]{
     const juce::ScopedLock lock(host.audioLock);
     host.session.master.limiterEnabled=!host.session.master.limiterEnabled;repaint();
    });
    for(float db:{-6.f,-3.f,-1.f,-.3f})
     menu.addItem("Limiter ceiling: "+juce::String(db,1)+" dBFS",[this,db]{
      const juce::ScopedLock lock(host.audioLock);
      host.session.master.ceilingDb=db;repaint();
     });
   }
   if(menu.getNumItems()>0)menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
   return;
  }
  const int top=std::min(129,std::max(114,getHeight()-72)),base=std::max(top+15,getHeight()-39);
  const juce::ScopedLock lock(host.audioLock);
  if(busIdx<int(host.session.buses.size())){
   auto& bus=host.session.buses[size_t(busIdx)];
   if(e.y>=65&&e.y<=90&&localX>=12&&localX<=61)bus.muted=!bus.muted;
   else if(e.y>=94&&e.y<=118){draggingBus=busIdx;draggingPan=true;startingPan=bus.pan;
    bus.pan=std::clamp(float(localX-85)/37.f,-1.f,1.f);}
   else if(e.y>=119&&e.y<getHeight()-28){draggingBus=busIdx;
    startingDb=veysona::mix::decibels(bus.gain);
    if(!e.mods.isShiftDown())bus.gain=veysona::mix::gainFromFader(float(base-e.y)/std::max(1,base-top));}
  }else if(busIdx==int(host.session.buses.size())&&e.y>=119&&e.y<getHeight()-28){
   draggingMaster=true;startingDb=veysona::mix::decibels(host.session.master.gain);
   if(!e.mods.isShiftDown())host.session.master.gain=veysona::mix::gainFromFader(float(base-e.y)/std::max(1,base-top));
  }
  host.updateControls();repaint();return;
 }
 const int x=9+i*150,localX=e.x-x;
 host.selectedTrack=i;host.selectedMidiRegion=0;host.selectedNote=-1;
 if(e.mods.isPopupMenu()){
  juce::PopupMenu menu;menu.addSectionHeader("CHANNEL  /  "+jstr(host.session.tracks[size_t(i)].name));
  juce::PopupMenu output;output.addItem("Master",[this,i]{const juce::ScopedLock l(host.audioLock);veysona::mix::setOutput(host.session.tracks[size_t(i)],-1,host.session.buses.size());repaint();});
  for(int b=0;b<int(host.session.buses.size());++b)
   output.addItem(jstr(host.session.buses[size_t(b)].name),[this,i,b]{const juce::ScopedLock l(host.audioLock);veysona::mix::setOutput(host.session.tracks[size_t(i)],b,host.session.buses.size());repaint();});
  menu.addSubMenu("Output routing",output);
  juce::PopupMenu sends;
  for(size_t bi=0;bi<host.session.buses.size()&&bi<veysona::kMaxBuses;++bi){
   juce::PopupMenu levels;
   for(float amount:{0.f,.10f,.25f,.5f,.75f,1.f})
    levels.addItem(juce::String(int(amount*100.f))+"%",[this,i,bi,amount]{const juce::ScopedLock l(host.audioLock);veysona::mix::setSend(host.session.tracks[size_t(i)],bi,amount,host.session.buses.size());repaint();});
   sends.addSubMenu(jstr(host.session.buses[bi].name)+"  ("+juce::String(int(host.session.tracks[size_t(i)].sends[bi]*100.f))+"%)",levels);
  }
  if(!host.session.buses.empty())menu.addSubMenu("Auxiliary sends",sends);
  menu.addItem("Create new bus",host.session.buses.size()<veysona::kMaxBuses,false,[this]{
   {
    const juce::ScopedLock l(host.audioLock);
    if(host.session.buses.size()>=veysona::kMaxBuses)return;
    veysona::Bus b;b.name="Bus "+std::to_string(host.session.buses.size()+1);
    host.session.buses.push_back(std::move(b));
   }
   host.mixer.setSize(std::max(600,int(host.session.tracks.size()+host.session.buses.size()+1)*150+16),host.mixer.getHeight());
   repaint();
  });
  juce::PopupMenu gainMenu;
  for(float db:{-24.f,-18.f,-12.f,-6.f,-3.f,0.f,3.f,6.f})
   gainMenu.addItem(juce::String::formatted("%+.0f dB",db),[this,i,db]{const juce::ScopedLock l(host.audioLock);veysona::mix::setGain(host.session.tracks[size_t(i)],veysona::mix::amplitude(db));host.updateControls();repaint();});
  menu.addSubMenu("Set channel fader",gainMenu);
  menu.addSeparator();
  menu.addItem("Copy channel mix + effects",[this,i]{
   const juce::ScopedLock l(host.audioLock);host.channelClipboard=veysona::mix::copyChannel(host.session.tracks[size_t(i)]);
   host.setStatus("Copied channel mix and effects (not clips or instruments)");
  });
  menu.addItem("Paste channel mix + effects",host.channelClipboard.has_value(),false,[this,i]{
   const juce::ScopedLock l(host.audioLock);if(!host.channelClipboard)return;
   veysona::mix::pasteChannel(host.session.tracks[size_t(i)],*host.channelClipboard,host.session.buses.size());
   host.updateControls();host.eqView.repaint();repaint();
  });
  menu.addItem("Reset pan to centre",[this,i]{const juce::ScopedLock l(host.audioLock);veysona::mix::setPan(host.session.tracks[size_t(i)],0.f);host.updateControls();repaint();});
  menu.addSeparator();menu.addItem("Shift-click SOLO: additive solo",false,false,[]{});
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
  host.updateControls();return;
 }
 {
  const juce::ScopedLock lock(host.audioLock);
  auto& t=host.session.tracks[size_t(i)];
  if(e.y>=65&&e.y<=90){
   if(localX>=12&&localX<=61)t.mute=!t.mute;
   else if(localX>=65&&localX<=114)veysona::mix::toggleSolo(host.session.tracks,size_t(i),!e.mods.isShiftDown());
  } else if(e.y>=94&&e.y<=118){
   draggingFader=i;draggingPan=true;startingPan=t.pan;
   veysona::mix::setPan(t,std::clamp(float(localX-85)/37.f,-1.f,1.f));
  } else if(e.y>=119&&e.y<getHeight()-28){
   draggingFader=i;startingDb=veysona::mix::decibels(t.gain);
   const int top=std::min(129,std::max(114,getHeight()-72)),base=std::max(top+15,getHeight()-39);
   if(!e.mods.isShiftDown())veysona::mix::setGain(t,veysona::mix::gainFromFader(float(base-e.y)/std::max(1,base-top)));
  }
 }
 host.updateControls();host.eqView.repaint();repaint();host.arrangement.repaint();
}
void MixerView::mouseDrag(const juce::MouseEvent& e){
 if(draggingBus>=0||draggingMaster){
  const juce::ScopedLock lock(host.audioLock);
  const int top=std::min(129,std::max(114,getHeight()-72)),base=std::max(top+15,getHeight()-39);
  if(draggingMaster){
   host.session.master.gain=e.mods.isShiftDown()?veysona::mix::amplitude(startingDb+float(e.getMouseDownY()-e.y)*.08f):
    veysona::mix::gainFromFader(float(base-e.y)/std::max(1,base-top));
   host.masterGain.setValue(host.session.master.gain,juce::dontSendNotification);
  }else if(draggingBus<int(host.session.buses.size())){
   auto& bus=host.session.buses[size_t(draggingBus)];
   if(draggingPan)bus.pan=std::clamp(e.mods.isShiftDown()?startingPan+float(e.getMouseDownY()-e.y)*-.003f:
    float(e.x-(9+(int(host.session.tracks.size())+draggingBus)*150+85))/37.f,-1.f,1.f);
   else bus.gain=std::clamp(e.mods.isShiftDown()?veysona::mix::amplitude(startingDb+float(e.getMouseDownY()-e.y)*.08f):
    veysona::mix::gainFromFader(float(base-e.y)/std::max(1,base-top)),0.f,2.f);
  }
  repaint();return;
 }
 if(draggingFader<0||draggingFader>=int(host.session.tracks.size()))return;
 const juce::ScopedLock lock(host.audioLock);
 auto& track=host.session.tracks[size_t(draggingFader)];
 if(draggingPan){
  const float value=e.mods.isShiftDown()?startingPan+float(e.y-e.getMouseDownY())*.003f:float(e.x-(9+draggingFader*150+85))/37.f;
  veysona::mix::setPan(track,value);
 }else{
  const int top=std::min(129,std::max(114,getHeight()-72)),base=std::max(top+15,getHeight()-39);
  if(e.mods.isShiftDown())veysona::mix::setGain(track,veysona::mix::amplitude(startingDb+float(e.getMouseDownY()-e.y)*.08f));
  else veysona::mix::setGain(track,veysona::mix::gainFromFader(float(base-e.y)/std::max(1,base-top)));
 }
 host.updateControls();repaint();
}
void MixerView::mouseDoubleClick(const juce::MouseEvent& e){
 const int i=(e.x-9)/150;if(e.x<9||i<0)return;
 if(i>=int(host.session.tracks.size())){
  const juce::ScopedLock lock(host.audioLock);
  const int b=i-int(host.session.tracks.size());
  if(b<int(host.session.buses.size())){
   if(e.y>=94&&e.y<=118)host.session.buses[size_t(b)].pan=0.f;
   else if(e.y>=119&&e.y<getHeight()-28)host.session.buses[size_t(b)].gain=1.f;
  }else if(b==int(host.session.buses.size())){
   host.session.master.gain=1.f;host.masterGain.setValue(1.,juce::dontSendNotification);
  }
  repaint();return;
 }
 const juce::ScopedLock lock(host.audioLock);
 auto& t=host.session.tracks[size_t(i)];
 if(e.y>=94&&e.y<=118)veysona::mix::setPan(t,0.f);
 else if(e.y>=119&&e.y<getHeight()-28)veysona::mix::setGain(t,1.f);
 host.updateControls();repaint();
}
void Studio::refreshWorkspaceButtons(){
 browserSamplesTab.setToggleState(browserCategory!=8,juce::dontSendNotification);
 browserPluginsTab.setToggleState(browserCategory==8,juce::dontSendNotification);
 browserToggle.setButtonText(browserVisible?"Hide Browser":"Show Browser");
 tabArrangement.setToggleState(!sessionMode,juce::dontSendNotification);
 tabSession.setToggleState(sessionMode,juce::dontSendNotification);
 tabPiano.setToggleState(!mixerMode,juce::dontSendNotification);
 tabMixer.setToggleState(mixerMode,juce::dontSendNotification);
 stopScenes.setVisible(sessionMode);
}
void Studio::fitSongToView(){
 // Fit the real content, not the nominal 64-bar empty session.
 double finalBeat=4.*session.beatsPerBar;
 for(const auto& track:session.tracks){
  for(const auto& m:track.midi)finalBeat=std::max(finalBeat,m.startBeat+m.lengthBeats);
  for(const auto& c:track.clips){
   const double duration=c.sampleRate?double(c.stereo.size()/2)/c.sampleRate*session.bpm/60.:0.;
   finalBeat=std::max(finalBeat,c.startBeat+duration);
  }
 }
 const int available=std::max(320,timelineViewport.getViewWidth()-185);
 zoom.setValue(veysona::ui::fitPixelsPerBar(finalBeat,session.beatsPerBar,available));
 timelineViewport.setViewPosition(0,timelineViewport.getViewPositionY());
 setStatus("Arrangement fitted to project content");
}
void Arrangement::mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& wheel){
 const int row=(e.y-36)/73;
 if(row>=0 && row<int(host.session.tracks.size()) && e.x>=144&&e.x<=178 && e.y>=36+row*73+40 && e.y<=36+row*73+72){
  const juce::ScopedLock lock(host.audioLock);
  auto& track=host.session.tracks[size_t(row)];
  track.gain=veysona::ui::trackGainFromKnob(veysona::ui::trackGainToKnob(track.gain)+wheel.deltaY*.035f);
  host.updateControls();host.mixer.repaint();repaint();return;
 }

 if(e.mods.isCommandDown()){
  host.zoom.setValue(std::clamp(host.zoom.getValue()+wheel.deltaY*28.,65.,215.));
  return;
 }
 juce::Component::mouseWheelMove(e,wheel);
}
void Studio::paint(juce::Graphics& g){
 g.fillAll(C::bg);
 const int w=getWidth(),h=getHeight();
 const auto workspace=veysona::ui::makeWorkspaceLayout(w,h,browserVisible,detailsVisible,browserDockWidth,editorDockHeight);
 const int browserW=workspace.browserWidth, right=workspace.rightEdge;
 const int head=workspace.arrangementTop,footer=27,bottomH=workspace.editorHeight;
 const int lowerY=workspace.editorTop;
 // Three restrained layers: system menu, transport, arrangement controls.
 g.setColour(C::top);g.fillRect(0,0,w,30);
 juce::ColourGradient transport(C::top.brighter(.09f),0.f,30.f,C::bg.brighter(.055f),0.f,93.f,false);
 g.setGradientFill(transport);g.fillRect(0,30,w,63);
 g.setColour(C::purple.withAlpha(.8f));g.fillRect(0,30,w,2);
 g.setColour(C::top);g.fillRect(0,93,w,44);
 // The main controls are grouped by purpose rather than framed by every individual button.
 g.setColour(C::outline.withAlpha(.48f));
 g.drawVerticalLine(183,39,85);
 g.drawVerticalLine(557,39,85);
 g.setColour(C::outline.withAlpha(.78f));
 for(const auto y:{30,93,137,h-footer})g.drawHorizontalLine(y,0,w);
 if(brandIcon.isValid())g.drawImageWithin(brandIcon,12,45,34,34,juce::RectanglePlacement::centred|juce::RectanglePlacement::onlyReduceInSize);
 else {roundRect(g,{12.f,45.f,34.f,34.f},C::purple,9);
  g.setColour(C::ink);g.setFont(juce::Font(21.f).boldened());g.drawFittedText("V",12,45,34,34,juce::Justification::centred,1);
 }
 caption(g,"VEYSONA",{50,44,115,26},C::ink,16);
 caption(g,"STUDIO",{51,66,105,15},C::muted,10);
 // Active playback / recording is conveyed at a glance without flashing labels.
 const auto transportColour=recording.load()?C::pink:(playing.load()?C::green:C::muted);
 g.setColour(transportColour);g.fillEllipse(168.f,57.f,7.f,7.f);
 caption(g,"TEMPO",{569,35,64,12},C::muted,10);
 if(w>=1450)caption(g,"ZOOM",{721,35,64,12},C::muted,10);
 // Small unobtrusive master peak indication, leaving the main workspace uncluttered.
 if(w>=1490){
  caption(g,"MASTER",{w-118,36,87,16},C::muted,10);
  for(int ch=0;ch<2;++ch){
   auto peak=std::clamp(ch==0?peakL.load():peakR.load(),0.f,1.f);
   auto bx=w-110+ch*49;
   roundRect(g,{float(bx),60.f,43.f,7.f},C::raised,2);
   if(peak>0.002f)roundRect(g,{float(bx),60.f,43.f*peak,7.f},peak>.91?C::pink:C::green,2);
  }
 }
 const auto projectName=currentProject.existsAsFile()?currentProject.getFileNameWithoutExtension():juce::String("Untitled Project");
 if(right>=1150){
  caption(g,"PROJECT",{right-238,100,56,23},C::muted,9);
  caption(g,projectName,{right-178,100,165,24},C::ink,12);
 }
 if(browserVisible){
  g.setColour(C::panel);g.fillRect(right,head,browserW,h-head-footer);
  g.setColour(C::outline);g.drawVerticalLine(right,head,h-footer);
  g.setColour(C::purple.withAlpha(.8f));g.fillRoundedRectangle(float(right+13),148.f,3.f,11.f,1.5f);
  caption(g,"LIBRARY",{right+23,138,140,27},C::ink,12);
  caption(g,juce::String(browserItems.size())+" ITEMS",{right+browserW-91,138,79,27},C::muted,10);
  g.setColour(C::outline.withAlpha(.58f));g.drawHorizontalLine(161,float(right+10),float(w-10));
  caption(g,"Double-click to load  |  Ctrl+F to search",{right+13,h-55,browserW-23,20},C::muted,10);
 }
 if(detailsVisible){
  g.setColour(C::panel);g.fillRect(0,lowerY,right,bottomH);
  g.setColour(C::top);g.fillRect(0,lowerY,right,45);
  g.setColour(C::outline);g.drawHorizontalLine(lowerY,0,right);
  g.setColour(C::outline.withAlpha(.52f));g.drawHorizontalLine(lowerY+44,0,right);
  const auto cols=veysona::ui::makeDockColumns(right,pianoDockFraction,instrumentDockFraction);
  const int first=cols.editor, second=cols.instrument, third=cols.equalizer;
  g.setColour(C::outline.withAlpha(.8f));
  g.drawVerticalLine(first,lowerY+45,h-footer);
  g.drawVerticalLine(first+second,lowerY+45,h-footer);
  // Splitter grab handles are visible only where they are draggable.
  g.setColour(C::blue.withAlpha(.50f));
  g.fillRoundedRectangle(float(first-2),float(lowerY+64),4.f,42.f,2.f);
  g.fillRoundedRectangle(float(first+second-2),float(lowerY+64),4.f,42.f,2.f);
  caption(g,"INSTRUMENT / CHANNEL",{first+12,lowerY+49,second-20,23},C::ink,11);
  caption(g,"Gain",{first+12,lowerY+165,51,20},C::muted,11);
  caption(g,"Pan",{first+12,lowerY+192,51,20},C::muted,11);
  caption(g,"Filter",{first+12,lowerY+219,51,20},C::muted,11);
  caption(g,"Drive",{first+12,lowerY+246,51,20},C::muted,11);
  caption(g,"Delay",{first+12,lowerY+273,51,20},C::muted,11);
  caption(g,"SPECTRUM EQ 8",{first+second+12,lowerY+49,third-20,20},C::ink,11);
  caption(g,"Thres",{first+second+12,lowerY+bottomH-66,55,19},C::muted,10);
  caption(g,"Ratio",{first+second+12,lowerY+bottomH-36,55,19},C::muted,10);
 }
 if(browserVisible){
  g.setColour(C::blue.withAlpha(.45f));g.fillRoundedRectangle(float(right-3),float(head+100),6.f,48.f,3.f);
 }
 if(detailsVisible){
  g.setColour(C::blue.withAlpha(.48f));g.fillRoundedRectangle(float(right/2-27),float(lowerY-3),54.f,6.f,3.f);
 }
 g.setColour(C::top);g.fillRect(0,h-footer,w,footer);
 g.setColour(C::outline);g.drawHorizontalLine(h-footer,0,w);
 g.setColour(playing.load()?C::green:C::muted);g.fillEllipse(7.f,float(h-18),5.f,5.f);
}
void Studio::resized(){
 const int w=getWidth(),h=getHeight();if(w<1200||h<720)return;
 auto at=[&](juce::Component& c,int x,int y,int width,int height){c.setBounds(x,y,std::max(1,width),std::max(1,height));};
 menuBar.setBounds(0,0,w,29);
 masterArea={w-126,32,115,54};
 int x=192;
 at(playButton,x,49,62,31);x+=68;at(stopButton,x,49,62,31);x+=68;
 at(recordButton,x,49,76,31);x+=82;at(loopButton,x,49,84,31);
 at(bpm,569,51,w>=1450?142:126,27);
 zoom.setVisible(w>=1450);
 if(w>=1450)at(zoom,721,51,113,27);
 const bool roomy=w>=1490;
 metronomeButton.setVisible(roomy);masterGain.setVisible(false);
 if(roomy)at(metronomeButton,849,49,85,31);
 at(audioSettings,w-493,49,113,31);
 at(openButton,w-375,49,60,31);at(saveButton,w-310,49,60,31);
 at(exportButton,w-245,49,112,31);
 const auto workspace=veysona::ui::makeWorkspaceLayout(w,h,browserVisible,detailsVisible,browserDockWidth,editorDockHeight);
 const int right=workspace.rightEdge;
 // The main workflow is visible; creation, recovery and advanced tools remain in the menus / Ctrl+K.
 at(tabArrangement,12,99,109,31);at(tabSession,127,99,87,31);
 at(stopScenes,220,99,89,31);
 at(detailsButton,315,99,100,31);
 at(snapButton,422,99,104,31);at(fitButton,532,99,86,31);
 at(followButton,624,99,106,31);
 at(browserToggle,738,99,118,31);
 moreButton.setVisible(right>=985);
 if(right>=985)at(moreButton,865,99,112,31);
 moreButton.setButtonText("Quick Actions");
 refreshWorkspaceButtons();
 for(juce::Component* extra:std::initializer_list<juce::Component*>{&importButton,&midiButton,&demoButton,
   &previewButton,&automationButton,&preferencesButton,&samplerButton,&drumButton,&midiToolsButton,&midiArmButton})extra->setVisible(false);
 // The sound and VST3 browser docks on the right, like a modern single-window DAW.
 for(juce::Component* browserPart:std::initializer_list<juce::Component*>{&browserSamplesTab,&browserPluginsTab,&category,&search,&browser})
  browserPart->setVisible(browserVisible);
 if(browserVisible){
  at(browserSamplesTab,right+12,167,std::max(100,(browserDockWidth-30)/2),28);at(browserPluginsTab,right+18+(browserDockWidth-30)/2,167,std::max(100,(browserDockWidth-30)/2),28);
  at(category,right+12,202,browserDockWidth-25,28);at(search,right+12,237,browserDockWidth-25,30);
  at(browser,right+8,274,browserDockWidth-17,std::max(100,h-274-65));
 }
 const int top=workspace.arrangementTop,footer=27,bottomH=workspace.editorHeight,bottom=workspace.editorTop;
 timelineViewport.setBounds(0,top,right,std::max(100,bottom-top));
 performanceViewport.setBounds(0,top,right,std::max(100,bottom-top));
 performanceGrid.setSize(performanceGrid.contentWidth(),std::max(bottom-top,performanceGrid.contentHeight()));
 arrangement.setSize(arrangement.contentWidth(),std::max(bottom-top,36+int(session.tracks.size())*73));
 for(juce::Component* child:std::initializer_list<juce::Component*>{&tabPiano,&tabMixer,&midiImportButton,&midiExportButton,
  &midiGrid,&pianoScaleButton,&quantizeButton,&transposeUp,&transposeDown,&duplicateNotesButton,&undoMidiButton,&redoMidiButton,
  &addNoteButton,&pluginButton,&pluginEditorButton,&trackName,&instrument,&volume,&pan,&cutoff,&drive,&delay,
  &eqToggle,&compressorToggle,&fxPresetsButton,&eqView,&compThreshold,&compRatio})child->setVisible(detailsVisible);
 piano.setVisible(detailsVisible&&!mixerMode);mixerViewport.setVisible(detailsVisible&&mixerMode);
 for(juce::Component* extra:std::initializer_list<juce::Component*>{&midiImportButton,&midiExportButton,
   &transposeUp,&transposeDown,&duplicateNotesButton,&addNoteButton})extra->setVisible(false);
 if(detailsVisible){
  int tx=10;
  auto toolbar=[&](juce::Component& c,int width){at(c,tx,bottom+7,width,30);tx+=width+7;};
  midiToolsButton.setVisible(true);
  toolbar(tabPiano,104);toolbar(tabMixer,79);toolbar(midiGrid,78);toolbar(pianoScaleButton,113);
  toolbar(quantizeButton,91);toolbar(undoMidiButton,88);toolbar(redoMidiButton,88);toolbar(midiToolsButton,102);
  toolbar(pluginButton,104);toolbar(pluginEditorButton,117);
  const auto cols=veysona::ui::makeDockColumns(right,pianoDockFraction,instrumentDockFraction);
  const int first=cols.editor,second=cols.instrument,third=cols.equalizer;
  editorArea={0,bottom+45,first,bottomH-45};
  effectArea={first+second,bottom+45,third,bottomH-45};

  at(piano,6,bottom+70,first-12,std::max(75,bottomH-78));
  at(mixerViewport,6,bottom+70,first-12,std::max(75,bottomH-78));
  mixer.setSize(std::max(first-12,int(session.tracks.size()+session.buses.size()+1)*150+16),std::max(75,bottomH-78));
  const int x1=first+9,x2=first+second+9;
  at(trackName,x1,bottom+79,second-18,23);at(instrument,x1,bottom+123,second-18,29);
  at(volume,x1+51,bottom+165,second-66,24);at(pan,x1+51,bottom+192,second-66,24);
  at(cutoff,x1+51,bottom+219,second-66,24);at(drive,x1+51,bottom+246,second-66,24);
  at(delay,x1+51,bottom+273,second-66,24);
  at(eqToggle,x2,bottom+74,78,25);at(compressorToggle,x2+84,bottom+74,92,25);
  fxPresetsButton.setVisible(third>=295);
  if(third>=295)at(fxPresetsButton,x2+182,bottom+74,third-199,25);
  at(eqView,x2,bottom+102,third-18,std::max(105,bottomH-173));
  at(compThreshold,x2+52,bottom+bottomH-66,third-65,23);
  at(compRatio,x2+52,bottom+bottomH-36,third-65,23);
 }
 browserDivider.setVisible(browserVisible);
 editorDivider.setVisible(detailsVisible);
 deviceDivider.setVisible(detailsVisible);
 effectDivider.setVisible(detailsVisible);
 if(browserVisible)browserDivider.setBounds(right-5,top,10,h-27-top);
 if(detailsVisible){
  const auto columns=veysona::ui::makeDockColumns(right,pianoDockFraction,instrumentDockFraction);
  editorDivider.setBounds(0,bottom-5,right,10);
  deviceDivider.setBounds(columns.editor-5,bottom+46,10,std::max(10,bottomH-46));
  effectDivider.setBounds(columns.editor+columns.instrument-5,bottom+46,10,std::max(10,bottomH-46));
 }
 for(auto* split:{&browserDivider,&editorDivider,&deviceDivider,&effectDivider})split->toFront(false);
 status.setBounds(12,h-26,std::max(130,w-495),25);
 transportStatus.setBounds(w-470,h-26,458,25);
}
class MainWindow final : public juce::DocumentWindow {
public:
 MainWindow(const juce::String& name,juce::Colour col,int buttons):juce::DocumentWindow(name,col,buttons){}
 void closeButtonPressed() override{if(auto* a=juce::JUCEApplication::getInstance())a->systemRequestedQuit();}
};
class App : public juce::JUCEApplication {
 std::unique_ptr<MainWindow> mainWindow;
public:
 const juce::String getApplicationName() override{return "Veysona Studio";}
 const juce::String getApplicationVersion() override{return "1.31.0";}
 void initialise(const juce::String&) override{
  mainWindow=std::make_unique<MainWindow>("Veysona Studio",C::bg,juce::DocumentWindow::allButtons);
  mainWindow->setUsingNativeTitleBar(true);mainWindow->setResizable(true,true);mainWindow->setResizeLimits(1200,720,3840,2160);
  mainWindow->setContentOwned(new Studio(),true);mainWindow->centreWithSize(1680,960);mainWindow->setVisible(true);
 }
 void shutdown() override{mainWindow.reset();}
 void systemRequestedQuit() override{quit();}
};
START_JUCE_APPLICATION(App)
