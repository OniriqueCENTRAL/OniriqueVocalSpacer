#pragma once

#include "Parameters.h"
#include "ui/OniriquePresets.h"

namespace onirique
{
/*  Presets d'usine de VOCAL SPACER (sur l'instrumental, la voix en sidechain), rangés par usage pour le navigateur de la
    barre (les tags). Valeurs en unités réelles ; un réglage non cité reprend sa valeur par défaut.
    width 0..200 %, pocketDepth en dB (0..12), pocketSpeed : 0 FAST, 1 NORMAL, 2 SMOOTH ; pocketTarget : 0 MID, 1 STEREO ;
    pocketMode : 0 SIDECHAIN, 1 STATIC ; pocketRangeLo / Hi en Hz. */
namespace spacer
{
enum Tags { rap = 1, pop = 2, dense = 4, gentle = 8, wide = 16, punch = 32, staticPocket = 64 };

inline juce::StringArray tagNames() { return { "Rap", "Pop & R&B", "Dense Mix", "Gentle", "Wide", "Punch", "Static" }; }

/** Les réglages de session (ce qu'on écoute) : un preset les laisse. */
inline juce::StringArray notStored() { return { ids::bypass, ids::pocketListen }; }

inline std::vector<PresetBank::Stock> stockPresets()
{
    return {
        { "Init", 0, {} },
        { "Rap Vocal Pocket", rap | punch,
          { { ids::width, 115 }, { ids::lowFocus, 60 }, { ids::sideDetail, 30 }, { ids::depth, 7 }, { ids::sensitivity, 60 },
            { ids::speed, 0 }, { ids::rangeLo, 180 }, { ids::rangeHi, 6000 }, { ids::kickFocus, 45 }, { ids::midEnergy, 25 },
            { ids::autoGain, 1 } } },
        { "Drill Beat Pocket", rap | punch | dense,
          { { ids::width, 110 }, { ids::lowFocus, 65 }, { ids::sideDetail, 25 }, { ids::depth, 8 }, { ids::sensitivity, 65 },
            { ids::speed, 0 }, { ids::rangeLo, 160 }, { ids::rangeHi, 6500 }, { ids::kickFocus, 60 }, { ids::midEnergy, 30 },
            { ids::autoGain, 1 } } },
        { "Trap Hook Space", rap | wide,
          { { ids::width, 130 }, { ids::lowFocus, 60 }, { ids::sideDetail, 45 }, { ids::depth, 6.5f }, { ids::sensitivity, 55 },
            { ids::speed, 1 }, { ids::rangeLo, 200 }, { ids::rangeHi, 7500 }, { ids::kickFocus, 40 }, { ids::midEnergy, 30 },
            { ids::autoGain, 1 } } },
        { "Pop Lead Space", pop | wide,
          { { ids::width, 125 }, { ids::lowFocus, 40 }, { ids::sideDetail, 45 }, { ids::depth, 6 }, { ids::sensitivity, 50 },
            { ids::speed, 1 }, { ids::rangeLo, 250 }, { ids::rangeHi, 8000 }, { ids::kickFocus, 30 }, { ids::midEnergy, 35 },
            { ids::autoGain, 1 } } },
        { "R&B Smooth Pocket", pop | gentle,
          { { ids::width, 115 }, { ids::lowFocus, 35 }, { ids::sideDetail, 35 }, { ids::depth, 5 }, { ids::sensitivity, 45 },
            { ids::speed, 2 }, { ids::rangeLo, 220 }, { ids::rangeHi, 7000 }, { ids::kickFocus, 20 }, { ids::midEnergy, 25 },
            { ids::autoGain, 1 } } },
        { "Ballad Air", pop | gentle | wide,
          { { ids::width, 125 }, { ids::lowFocus, 30 }, { ids::sideDetail, 50 }, { ids::depth, 4 }, { ids::sensitivity, 40 },
            { ids::speed, 2 }, { ids::rangeLo, 300 }, { ids::rangeHi, 9000 }, { ids::kickFocus, 10 }, { ids::midEnergy, 15 },
            { ids::autoGain, 1 } } },
        { "Gentle Carve", gentle,
          { { ids::width, 105 }, { ids::lowFocus, 25 }, { ids::depth, 3.5f }, { ids::sensitivity, 40 }, { ids::speed, 2 },
            { ids::rangeLo, 300 }, { ids::rangeHi, 5000 }, { ids::kickFocus, 15 }, { ids::midEnergy, 15 }, { ids::autoGain, 1 } } },
        { "Wide & Clear", wide | dense,
          { { ids::width, 140 }, { ids::lowFocus, 70 }, { ids::sideDetail, 60 }, { ids::depth, 5 }, { ids::sensitivity, 50 },
            { ids::target, 1 }, { ids::kickFocus, 25 }, { ids::midEnergy, 30 }, { ids::autoGain, 1 } } },
        { "Punchy Beat", punch | rap,
          { { ids::width, 110 }, { ids::lowFocus, 55 }, { ids::sideDetail, 20 }, { ids::depth, 6 }, { ids::sensitivity, 55 },
            { ids::speed, 0 }, { ids::kickFocus, 70 }, { ids::midEnergy, 45 }, { ids::autoGain, 1 } } },
        { "Mono Focus Low", punch,
          { { ids::width, 100 }, { ids::lowFocus, 90 }, { ids::sideDetail, 10 }, { ids::depth, 5 }, { ids::kickFocus, 35 },
            { ids::midEnergy, 20 }, { ids::autoGain, 1 } } },
        { "Dense Mix Rescue", dense,
          { { ids::width, 120 }, { ids::lowFocus, 50 }, { ids::sideDetail, 40 }, { ids::depth, 9 }, { ids::sensitivity, 70 },
            { ids::target, 1 }, { ids::rangeLo, 150 }, { ids::rangeHi, 9000 }, { ids::kickFocus, 40 }, { ids::midEnergy, 40 },
            { ids::autoGain, 1 } } },
        { "Rock Wall Pocket", dense | punch,
          { { ids::width, 110 }, { ids::lowFocus, 45 }, { ids::sideDetail, 30 }, { ids::depth, 9 }, { ids::sensitivity, 65 },
            { ids::speed, 0 }, { ids::target, 1 }, { ids::rangeLo, 200 }, { ids::rangeHi, 5000 }, { ids::kickFocus, 50 },
            { ids::midEnergy, 35 }, { ids::autoGain, 1 } } },
        { "Wide Stereo Only", wide,
          { { ids::pocketOn, 0 }, { ids::restoreOn, 0 }, { ids::width, 140 }, { ids::lowFocus, 70 }, { ids::sideDetail, 55 },
            { ids::autoGain, 1 } } },
        { "Static Pocket", staticPocket,
          { { ids::pocketMode, 1 }, { ids::width, 115 }, { ids::lowFocus, 40 }, { ids::sideDetail, 30 }, { ids::depth, 4 },
            { ids::rangeLo, 250 }, { ids::rangeHi, 6000 }, { ids::kickFocus, 25 }, { ids::midEnergy, 20 }, { ids::autoGain, 1 } } },
        { "Static Gentle", staticPocket | gentle,
          { { ids::pocketMode, 1 }, { ids::width, 105 }, { ids::lowFocus, 25 }, { ids::depth, 2.5f }, { ids::rangeLo, 300 },
            { ids::rangeHi, 5000 }, { ids::kickFocus, 10 }, { ids::midEnergy, 10 }, { ids::autoGain, 1 } } },
    };
}

/** Les presets enregistrés avec l'ancienne barre (fichiers .ovspreset du même dossier) passent dans le navigateur,
    une fois : chacun est recopié au format de la nouvelle barre, l'original reste en place. */
inline void importOldUserPresets (const PresetBank& bank)
{
    for (const auto& file : bank.getFolder().findChildFiles (juce::File::findFiles, false, "*.ovspreset"))
    {
        const auto target = file.withFileExtension (bank.getExtension());
        if (target.existsAsFile())
            continue;

        if (const auto xml = juce::XmlDocument::parse (file))
        {
            juce::ValueTree snapshot ("Snapshot");
            for (auto* p : xml->getChildWithTagNameIterator ("PARAM"))
                if (const auto id = p->getStringAttribute ("id"); ! notStored().contains (id))
                    snapshot.setProperty (id, (float) p->getDoubleAttribute ("value"), nullptr);

            if (const auto out = snapshot.createXml())
                out->writeTo (target);
        }
    }
}
} // namespace spacer
} // namespace onirique
