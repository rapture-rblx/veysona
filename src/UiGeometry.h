#pragma once
// UI calculations without JUCE dependencies. Deterministic and testable on all platforms.
#include <algorithm>
#include <cmath>

namespace veysona::ui {
// Single source of truth for the three-window DAW workspace. The browser is
// right-docked; arrangement and editor share the remaining width.
struct WorkspaceLayout {
 int rightEdge = 0;
 int browserWidth = 0;
 int arrangementTop = 137;
 int arrangementBottom = 137;
 int editorTop = 137;
 int editorHeight = 0;
 int footerTop = 0;
};
// User-adjustable docking dimensions are clamped to preserve space for the
// arrangement, track headers, mixer and EQ at every supported window size.
inline WorkspaceLayout makeWorkspaceLayout(int width,int height,bool browserVisible,bool editorVisible,
                                            int requestedBrowserWidth=294,int requestedEditorHeight=360) {
 width=std::max(1200,width);height=std::max(720,height);
 WorkspaceLayout l;
 l.browserWidth=browserVisible?std::clamp(requestedBrowserWidth,250,std::min(560,width-900)):0;
 l.rightEdge=width-l.browserWidth;
 l.footerTop=height-27;
 const int maxEditor=std::max(310,l.footerTop-l.arrangementTop-230);
 l.editorHeight=editorVisible?std::clamp(requestedEditorHeight,310,maxEditor):0;
 l.editorTop=l.footerTop-l.editorHeight;
 l.arrangementBottom=l.editorTop;
 return l;
}
struct DockColumns {int editor=0, instrument=0, equalizer=0;};
inline DockColumns makeDockColumns(int usableWidth,float editorFraction=.46f,float instrumentFraction=.24f){
 usableWidth=std::max(900,usableWidth);
 const int left=std::clamp(int(std::lround(usableWidth*std::clamp(editorFraction,.33f,.60f))),310,usableWidth-465);
 const int middle=std::clamp(int(std::lround(usableWidth*std::clamp(instrumentFraction,.18f,.38f))),185,usableWidth-left-280);
 return {left,middle,usableWidth-left-middle};
}
// Audio gain fader and arrangement knob use the same piecewise dB law.
// 0.8 on the dial is unity (0 dB); the upper fifth adds up to +3.52 dB.
inline float trackGainFromKnob(float position){
 position=std::clamp(position,0.f,1.f);
 if(position<=0.f)return 0.f;
 const float db=position<=.8f? -60.f+75.f*position:(position-.8f)*float(20.*std::log10(1.5)/.2);
 return std::pow(10.f,db/20.f);
}
inline float trackGainToKnob(float gain){
 if(gain<=0.f)return 0.f;
 const float db=20.f*std::log10(gain);
 return std::clamp(db<=0.f?(db+60.f)/75.f:.8f+db*float(.2/(20.*std::log10(1.5))),0.f,1.f);
}
inline double snapBeat(double beat, bool enabled, double step = 0.25) {
  if (!std::isfinite(beat)) return 0.0;
  if (!enabled || step <= 0.0 || !std::isfinite(step)) return std::max(0.0, beat);
  return std::max(0.0, std::round(beat / step) * step);
}
inline int measureX(int barIndex, int origin, double pixelsPerBar) {
  return origin + int(std::lround(barIndex * pixelsPerBar));
}
inline int subdivisionX(int barIndex, int subIndex, int beatsPerBar,
                        int origin, double pixelsPerBar) {
  if (beatsPerBar <= 0) return origin;
  return origin + int(std::lround((barIndex + double(subIndex)/beatsPerBar) * pixelsPerBar));
}
inline double fitPixelsPerBar(double finalBeat, int beatsPerBar, int availableWidth) {
  if (!std::isfinite(finalBeat) || beatsPerBar <= 0) return 110.0;
  const double bars = std::max(4., finalBeat / beatsPerBar);
  return std::clamp(double(std::max(320, availableWidth)) / (bars + 1.0), 65.0, 215.0);
}
inline int nextFollowScroll(int playX, int left, int width) {
  if (width <= 0) return std::max(0, left);
  if (playX > left + width - 95 || playX < left + 180)
    return std::max(0, playX - width / 3);
  return std::max(0, left);
}
}
