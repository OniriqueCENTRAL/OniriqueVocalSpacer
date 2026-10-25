#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "License/LicenseGuard.h"
#include "Parameters.h"
#include "DSP/SpacerChain.h"
#include "DSP/SpectrumTap.h"
#include "DSP/SidechainGuard.h"
#include "StockPresets.h"

class OniriqueVocalSpacerAudioProcessor : public juce::AudioProcessor
{
public:
    OniriqueVocalSpacerAudioProcessor();
    ~OniriqueVocalSpacerAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // Le bypass du DAW pilote directement le paramètre "Bypass" (fondu propre)
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (onirique::ids::bypass); }

    const juce::String getName() const override { return "ONIRIQUE VOCAL SPACER"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    /** Les presets d'usine et ceux de l'utilisateur, la comparaison A/B : ce que montre la barre du haut. */
    onirique::PresetBank presets { apvts, "VocalSpacer", onirique::spacer::stockPresets(), onirique::spacer::tagNames(),
                                   onirique::spacer::notStored() };
    onirique::SpectrumTap inputTap, outputTap, vocalTap;   // afficheur de la poche : instrumental entrée / sortie, voix (mono)

    // ---- lus par l'éditeur (thread GUI)
    float consumeOutputPeak() noexcept { return outPeak.exchange (0.0f); }
    float getAutoGainDb() const noexcept { return autoGainDbShown.load(); }
    bool isSidechainConnected() const noexcept { return scConnected.load(); }   // un vrai signal arrive sur le sidechain (pas une copie de l'entrée)
    const onirique::VocalPocket::Meter& getPocketMeter() const noexcept { return chain.getPocketMeter(); }
    float getPocketNorm() const noexcept { return chain.getPocketNorm(); }
    float consumeInputLevel (int c) noexcept  { return chain.consumeInputLevel (c); }
    float consumeOutputLevel (int c) noexcept { return chain.consumeOutputLevel (c); }
    float consumeScLevel (int c) noexcept     { return chain.consumeScLevel (c); }
    float consumeKickBoostDb() noexcept       { return chain.consumeKickBoostDb(); }
    float getEnergyDb() const noexcept        { return chain.getEnergyDb(); }
    float getWidthRatio() const noexcept      { return chain.getWidthRatio(); }

private:
    onirique::SpacerChain::Params readParams() const noexcept;
    float value (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }

    onirique::SpacerChain chain;
    onirique::SidechainGuard scGuard;
    std::vector<float> scCopy[2];   // copie du sidechain (le DAW peut le faire pointer sur la mémoire de l'entrée)
    std::atomic<float> outPeak { 0.0f }, autoGainDbShown { 0.0f };
    std::atomic<bool> scConnected { false };

public:
    /** Licence ONIRIQUE (acheté, essai, démo) : voir License/LicenseGuard.h. */
    onirique::license::Guard license { "onirique-vocal-spacer" };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueVocalSpacerAudioProcessor)
};
