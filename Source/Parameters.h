#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace onirique
{
namespace ids
{
    inline constexpr const char* input        = "input";
    inline constexpr const char* output       = "output";
    inline constexpr const char* autoGain     = "autoGain";
    inline constexpr const char* bypass       = "bypass";
    // SPACE
    inline constexpr const char* spaceOn      = "spaceOn";
    inline constexpr const char* width        = "width";
    inline constexpr const char* lowFocus     = "lowFocus";
    inline constexpr const char* sideDetail   = "sideDetail";
    // VOCAL POCKET
    inline constexpr const char* pocketOn     = "pocketOn";
    inline constexpr const char* pocketMode   = "pocketMode";
    inline constexpr const char* depth        = "pocketDepth";
    inline constexpr const char* sensitivity  = "pocketSensitivity";
    inline constexpr const char* speed        = "pocketSpeed";
    inline constexpr const char* target       = "pocketTarget";
    inline constexpr const char* rangeLo      = "pocketRangeLo";
    inline constexpr const char* rangeHi      = "pocketRangeHi";
    inline constexpr const char* pocketListen = "pocketListen";
    // RESTORE
    inline constexpr const char* restoreOn    = "restoreOn";
    inline constexpr const char* kickFocus    = "kickFocus";
    inline constexpr const char* midEnergy    = "midEnergy";
}

inline juce::String hzText (float v)
{
    return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    using Attr = AudioParameterFloatAttributes;

    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto dbText = [] (float v, int)
    {
        v = std::round (v * 10.0f) / 10.0f + 0.0f;   // évite "-0.0"
        return String (v > 0.0f ? "+" : "") + String (v, 1) + " dB";
    };
    auto pctText = [] (float v, int) { return String (juce::roundToInt (v)) + " %"; };
    auto pct = [&] (const char* id, const char* name, float lo, float hi, float def)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.1f), def,
                                                            Attr().withLabel ("%").withStringFromValueFunction (pctText)));
    };

    // ---- INPUT / OUTPUT / AUTO GAIN / BYPASS
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::input, 1 }, "Input",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, Attr().withLabel ("dB").withStringFromValueFunction (dbText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::output, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, Attr().withLabel ("dB").withStringFromValueFunction (dbText)));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::autoGain, 1 }, "Auto Gain", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));

    // ---- SPACE : image stéréo de l'instrumental (neutre par défaut)
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::spaceOn, 1 }, "Space On", true));
    pct (ids::width, "Width", 0.0f, 200.0f, 100.0f);
    pct (ids::lowFocus, "Low Focus", 0.0f, 100.0f, 0.0f);
    pct (ids::sideDetail, "Side Detail", 0.0f, 100.0f, 0.0f);

    // ---- VOCAL POCKET : EQ dynamique pilotée par la voix (sidechain) ou poche fixe (STATIC)
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::pocketOn, 1 }, "Vocal Pocket On", true));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::pocketMode, 1 }, "Pocket Mode",
                                                         StringArray { "Sidechain", "Static" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::depth, 1 }, "Pocket Depth",
        NormalisableRange<float> (0.0f, 12.0f, 0.1f), 6.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return v < 0.05f ? String ("0.0 dB") : "-" + String (v, 1) + " dB"; })));
    pct (ids::sensitivity, "Pocket Sensitivity", 0.0f, 100.0f, 50.0f);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::speed, 1 }, "Pocket Speed",
                                                         StringArray { "Fast", "Normal", "Smooth" }, 1));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::target, 1 }, "Pocket Target",
                                                         StringArray { "Mid", "Stereo" }, 0));
    {
        NormalisableRange<float> lo (80.0f, 2000.0f, 1.0f), hi (1000.0f, 16000.0f, 1.0f);
        lo.setSkewForCentre (300.0f);
        hi.setSkewForCentre (5000.0f);
        auto hz = [] (float v, int) { return hzText (v); };
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::rangeLo, 1 }, "Pocket Range Low", lo, 200.0f,
                                                            Attr().withLabel ("Hz").withStringFromValueFunction (hz)));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::rangeHi, 1 }, "Pocket Range High", hi, 7000.0f,
                                                            Attr().withLabel ("Hz").withStringFromValueFunction (hz)));
    }
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::pocketListen, 1 }, "Pocket Listen", false));

    // ---- RESTORE : punch et énergie (neutre par défaut)
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::restoreOn, 1 }, "Restore On", true));
    pct (ids::kickFocus, "Kick Focus", 0.0f, 100.0f, 0.0f);
    pct (ids::midEnergy, "Mid Energy", 0.0f, 100.0f, 0.0f);

    return { p.begin(), p.end() };
}
} // namespace onirique
