#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"

namespace onirique
{
/*  Presets : usine (définis ici) + utilisateur (fichiers .ovspreset dans
    %APPDATA%/Onirique/VocalSpacer/Presets). Thread de l'interface uniquement.

    - Un preset contient tous les paramètres SAUF Bypass et LISTEN (réglages de session).
    - Un paramètre absent d'un fichier (preset d'une version plus ancienne) reprend sa valeur par défaut.
    - Le nom du preset courant est rangé dans l'état du plugin : il revient avec le projet.
    - "Modifié" : comparaison des valeurs actuelles avec celles du chargement.
*/
class PresetManager
{
public:
    struct Entry { juce::String name; bool factory = false; juce::File file; int factoryIndex = -1; };

    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s)
    {
        folder().createDirectory();
        takeSnapshot();
    }

    static juce::File folder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Onirique").getChildFile ("VocalSpacer").getChildFile ("Presets");
    }

    std::vector<Entry> list() const
    {
        std::vector<Entry> v;
        for (int i = 0; i < (int) factory().size(); ++i)
            v.push_back ({ factory()[(size_t) i].name, true, {}, i });
        auto files = folder().findChildFiles (juce::File::findFiles, false, "*.ovspreset");
        files.sort();
        for (auto& f : files) v.push_back ({ f.getFileNameWithoutExtension(), false, f, -1 });
        return v;
    }

    juce::String getCurrentName() const { return apvts.state.getProperty ("presetName", "Init").toString(); }

    bool isModified() const
    {
        if (snapshot.empty()) return false;
        size_t i = 0;
        for (auto* p : params())
            if (i < snapshot.size() && std::abs (p->getValue() - snapshot[i++]) > 1.0e-4f) return true;
        return false;
    }

    void load (const Entry& e)
    {
        std::map<juce::String, float> values;
        if (e.factory)
            values = factory()[(size_t) e.factoryIndex].values;
        else if (auto xml = juce::XmlDocument::parse (e.file))
            for (auto* p : xml->getChildWithTagNameIterator ("PARAM"))
                values[p->getStringAttribute ("id")] = (float) p->getDoubleAttribute ("value");

        for (auto* p : params())
        {
            const auto it = values.find (p->getParameterID());
            const float norm = it != values.end() ? p->convertTo0to1 (it->second) : p->getDefaultValue();
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        }
        apvts.state.setProperty ("presetName", e.name, nullptr);
        takeSnapshot();
    }

    // renvoie false si le nom est vide ; un nom d'usine est suffixé pour ne jamais l'écraser
    bool save (juce::String name)
    {
        name = juce::File::createLegalFileName (name.trim());
        if (name.isEmpty()) return false;
        for (const auto& f : factory()) if (f.name.equalsIgnoreCase (name)) name << " (user)";

        juce::XmlElement xml ("OniriqueSpacerPreset");
        xml.setAttribute ("name", name);
        xml.setAttribute ("version", 1);
        for (auto* p : params())
        {
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", p->getParameterID());
            e->setAttribute ("value", (double) p->convertFrom0to1 (p->getValue()));
        }
        if (! xml.writeTo (folder().getChildFile (name + ".ovspreset"))) return false;
        apvts.state.setProperty ("presetName", name, nullptr);
        takeSnapshot();
        return true;
    }

    void remove (const Entry& e)
    {
        if (! e.factory) e.file.deleteFile();
    }

    // charge le preset précédent / suivant dans la liste (boucle)
    void step (int delta)
    {
        const auto v = list();
        if (v.empty()) return;
        int idx = 0;
        for (int i = 0; i < (int) v.size(); ++i) if (v[(size_t) i].name == getCurrentName()) idx = i;
        load (v[(size_t) ((idx + delta + (int) v.size()) % (int) v.size())]);
    }

    void takeSnapshot()
    {
        snapshot.clear();
        for (auto* p : params()) snapshot.push_back (p->getValue());
    }

private:
    struct Factory { juce::String name; std::map<juce::String, float> values; };

    std::vector<juce::RangedAudioParameter*> params() const
    {
        std::vector<juce::RangedAudioParameter*> v;
        for (auto* p : apvts.processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (const auto id = r->getParameterID(); id != ids::bypass && id != ids::pocketListen)
                    v.push_back (r);   // réglages de session exclus : bypass, écoute de la poche
        return v;
    }

    // valeurs en unités réelles (dB, %, Hz, index de choix, 0/1)
    // pocketMode 0 SIDECHAIN / 1 STATIC ; speed 0 FAST / 1 NORMAL / 2 SMOOTH ; target 0 MID / 1 STEREO
    static const std::vector<Factory>& factory()
    {
        static const std::vector<Factory> f {
            { "Init", {} },
            { "Rap Vocal Pocket",   { { ids::width, 115.0f }, { ids::lowFocus, 60.0f }, { ids::sideDetail, 30.0f },
                                      { ids::depth, 7.0f }, { ids::sensitivity, 60.0f }, { ids::speed, 0.0f },
                                      { ids::rangeLo, 180.0f }, { ids::rangeHi, 6000.0f },
                                      { ids::kickFocus, 45.0f }, { ids::midEnergy, 25.0f }, { ids::autoGain, 1.0f } } },
            { "Pop Lead Space",     { { ids::width, 125.0f }, { ids::lowFocus, 40.0f }, { ids::sideDetail, 45.0f },
                                      { ids::depth, 6.0f }, { ids::sensitivity, 50.0f }, { ids::speed, 1.0f },
                                      { ids::rangeLo, 250.0f }, { ids::rangeHi, 8000.0f },
                                      { ids::kickFocus, 30.0f }, { ids::midEnergy, 35.0f }, { ids::autoGain, 1.0f } } },
            { "Gentle Carve",       { { ids::width, 105.0f }, { ids::lowFocus, 25.0f },
                                      { ids::depth, 3.5f }, { ids::sensitivity, 40.0f }, { ids::speed, 2.0f },
                                      { ids::rangeLo, 300.0f }, { ids::rangeHi, 5000.0f },
                                      { ids::kickFocus, 15.0f }, { ids::midEnergy, 15.0f }, { ids::autoGain, 1.0f } } },
            { "Wide & Clear",       { { ids::width, 140.0f }, { ids::lowFocus, 70.0f }, { ids::sideDetail, 60.0f },
                                      { ids::depth, 5.0f }, { ids::sensitivity, 50.0f }, { ids::target, 1.0f },
                                      { ids::kickFocus, 25.0f }, { ids::midEnergy, 30.0f }, { ids::autoGain, 1.0f } } },
            { "Punchy Beat",        { { ids::width, 110.0f }, { ids::lowFocus, 55.0f }, { ids::sideDetail, 20.0f },
                                      { ids::depth, 6.0f }, { ids::sensitivity, 55.0f }, { ids::speed, 0.0f },
                                      { ids::kickFocus, 70.0f }, { ids::midEnergy, 45.0f }, { ids::autoGain, 1.0f } } },
            { "Dense Mix Rescue",   { { ids::width, 120.0f }, { ids::lowFocus, 50.0f }, { ids::sideDetail, 40.0f },
                                      { ids::depth, 9.0f }, { ids::sensitivity, 70.0f }, { ids::target, 1.0f },
                                      { ids::rangeLo, 150.0f }, { ids::rangeHi, 9000.0f },
                                      { ids::kickFocus, 40.0f }, { ids::midEnergy, 40.0f }, { ids::autoGain, 1.0f } } },
            { "Static Pocket",      { { ids::pocketMode, 1.0f }, { ids::width, 115.0f }, { ids::lowFocus, 40.0f },
                                      { ids::sideDetail, 30.0f }, { ids::depth, 4.0f },
                                      { ids::rangeLo, 250.0f }, { ids::rangeHi, 6000.0f },
                                      { ids::kickFocus, 25.0f }, { ids::midEnergy, 20.0f }, { ids::autoGain, 1.0f } } },
        };
        return f;
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<float> snapshot;
};
} // namespace onirique
