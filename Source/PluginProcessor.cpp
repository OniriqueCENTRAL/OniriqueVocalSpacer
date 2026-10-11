#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace onirique;

OniriqueVocalSpacerAudioProcessor::OniriqueVocalSpacerAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)),   // la voix (VOCAL POCKET)
      apvts (*this, nullptr, "STATE", createLayout())
{
}

SpacerChain::Params OniriqueVocalSpacerAudioProcessor::readParams() const noexcept
{
    SpacerChain::Params p;
    p.inputDb   = value (ids::input);
    p.outputDb  = value (ids::output);
    p.autoGain  = value (ids::autoGain) > 0.5f;
    p.bypass    = value (ids::bypass) > 0.5f;
    p.spaceOn   = value (ids::spaceOn) > 0.5f;
    p.pocketOn  = value (ids::pocketOn) > 0.5f;
    p.restoreOn = value (ids::restoreOn) > 0.5f;
    p.listen    = value (ids::pocketListen) > 0.5f;

    p.space.width      = value (ids::width);
    p.space.lowFocus   = value (ids::lowFocus);
    p.space.sideDetail = value (ids::sideDetail);

    p.pocket.staticMode  = juce::roundToInt (value (ids::pocketMode)) == 1;
    p.pocket.stereo      = juce::roundToInt (value (ids::target)) == 1;
    p.pocket.depthDb     = value (ids::depth);
    p.pocket.sensitivity = value (ids::sensitivity);
    p.pocket.speed       = juce::roundToInt (value (ids::speed));
    p.pocket.loHz        = value (ids::rangeLo);
    p.pocket.hiHz        = std::max (value (ids::rangeHi), p.pocket.loHz * 1.25f);

    p.kick   = value (ids::kickFocus);
    p.energy = value (ids::midEnergy);
    return p;
}

bool OniriqueVocalSpacerAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& in = l.getMainInputChannelSet();
    const auto& out = l.getMainOutputChannelSet();
    if (in != out) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
    if (l.inputBuses.size() > 1)
    {
        const auto& sc = l.inputBuses.getReference (1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void OniriqueVocalSpacerAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    // Toute l'allocation se fait ici ; processBlock n'alloue rien. Latence nulle.
    for (auto& v : scCopy) v.assign ((size_t) std::max (samplesPerBlock, 8192), 0.0f);
    scGuard.prepare (sr);
    chain.prepare (sr, readParams());
    setLatencySamples (0);
}

void OniriqueVocalSpacerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int total = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, total);

    auto main = getBusBuffer (buffer, true, 0);
    const int numCh = juce::jmin (main.getNumChannels(), 2);
    if (numCh < 1 || total < 1)
        return;

    // sidechain (voix) : seulement si le DAW a activé le bus, copié AVANT tout traitement (certains hôtes le font
    // pointer sur la mémoire de l'entrée, qui va être modifiée sur place)
    const float* sc[2] {};
    int scCh = 0;
    if (getBusCount (true) > 1 && total <= (int) scCopy[0].size())
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0)
        {
            auto scBuf = getBusBuffer (buffer, true, 1);
            scCh = juce::jmin (2, scBuf.getNumChannels());
            for (int c = 0; c < scCh; ++c)
            {
                std::copy (scBuf.getReadPointer (c), scBuf.getReadPointer (c) + total, scCopy[c].begin());
                sc[c] = scCopy[c].data();
            }
            // rien de routé : le DAW recopie l'instrumental sur le sidechain -> ignoré, la poche reste fermée
            if (scGuard.isMirror (main.getReadPointer (0), numCh > 1 ? main.getReadPointer (1) : nullptr,
                                  sc[0], scCh > 1 ? sc[1] : nullptr, total))
                scCh = 0;
        }
    scConnected.store (scCh > 0, std::memory_order_relaxed);

    chain.setParams (readParams());

    float* ch[2] { main.getWritePointer (0), numCh > 1 ? main.getWritePointer (1) : nullptr };
    inputTap.push (ch[0], ch[1], total);                                   // afficheur : instrumental d'entrée
    if (scCh > 0) vocalTap.push (sc[0], scCh > 1 ? sc[1] : nullptr, total);   // afficheur : voix
    chain.process (ch, numCh, sc, scCh, total);
    outputTap.push (ch[0], ch[1], total);                                  // afficheur : instrumental de sortie

    // crête de sortie + correction d'Auto Gain pour l'interface
    float pk = 0.0f;
    for (int c = 0; c < numCh; ++c)
        pk = juce::jmax (pk, main.getMagnitude (c, 0, total));
    if (pk > outPeak.load (std::memory_order_relaxed))
        outPeak.store (pk, std::memory_order_relaxed);
    autoGainDbShown.store (value (ids::bypass) > 0.5f ? 0.0f : chain.getAutoGainDb(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* OniriqueVocalSpacerAudioProcessor::createEditor()
{
    return new OniriqueVocalSpacerAudioProcessorEditor (*this);
}

void OniriqueVocalSpacerAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void OniriqueVocalSpacerAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            presets.takeSnapshot();   // l'état rechargé sert de référence pour l'indicateur "modifié"
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OniriqueVocalSpacerAudioProcessor();
}
