#pragma once
#include "AudioCore.h"
#include <cstddef>

namespace veysona::workflow {
enum class Template { House, DrumAndBass, HipHop, Ambient };
// Original, editable project templates. No copyrighted or external audio assets.
Session createTemplate(Template kind,uint32_t sampleRate=48000);
// Experimental offline single-pitch note tracking for clean bass, whistle,
// synth or isolated melodies. NOT polyphonic transcription or vocal correction.
MidiRegion transcribeMonophonic(const Clip& clip,double projectBpm,
                                float sensitivity=.025f);
// Bounce an internal synth/instrument and its built-in FX to a self-contained
// audio clip. VST3 inserts must be rendered by the plugin host separately.
Clip bounceBuiltInTrack(const Session& session,size_t trackIndex,
                        double maximumSeconds=90.);
}
