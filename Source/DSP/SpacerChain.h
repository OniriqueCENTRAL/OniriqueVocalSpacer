#pragma once

#include <array>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Common.h"
#include "SpaceModule.h"
#include "VocalPocket.h"
#include "RestoreModule.h"
#include "AutoGain.h"

namespace onirique
{
/*  Chaîne complète : INPUT -> (Mid / Side) SPACE -> VOCAL POCKET -> RESTORE -> (L / R) AUTO GAIN -> OUTPUT
    + interrupteur par module (fondu 20 ms entre l'entrée et la sortie du module, qui tourne en permanence)
    + LISTEN de la poche (on n'entend que ce qu'elle retire)
    + BYPASS global (fondu 20 ms avec le signal d'entrée d'origine).

    Latence nulle partout (filtres IIR, analyse de la poche faite sur la tranche avant de la traiter).
    Temps réel : aucune allocation dans process(), traitement par tranches de 32 échantillons (rythme des
    réglages dynamiques), NaN/Inf neutralisés à l'entrée et à la sortie.
*/
class SpacerChain
{
public:
    static constexpr int kChunk = 32;

    struct Params
    {
        float inputDb = 0.0f, outputDb = 0.0f;
        bool autoGain = false, bypass = false;
        bool spaceOn = true, pocketOn = true, restoreOn = true, listen = false;
        SpaceModule::Params space;
        VocalPocket::Params pocket;
        float kick = 0.0f, energy = 0.0f;
    };

    void prepare (double sampleRate, const Params& initial)
    {
        fs = sampleRate;
        space.prepare (fs);
        pocket.prepare (fs);
        restore.prepare (fs);
        autoGain.prepare (fs);
        inGain.reset (fs, 0.03);
        outGain.reset (fs, 0.03);
        for (auto* s : { &spaceMix, &pocketMix, &restoreMix, &listenMix, &wetMix }) s->reset (fs, 0.02);
        widthEnvM = widthEnvS = 0.0f;
        params = initial;
        applyParams (true);
    }

    void setParams (const Params& p) noexcept { params = p; }

    // ch : 1 ou 2 canaux traités sur place ; sc : sidechain (scCh = 0, 1 ou 2 canaux)
    void process (float* const* ch, int numCh, const float* const* sc, int scCh, int n) noexcept
    {
        applyParams (false);
        for (int pos = 0; pos < n; pos += kChunk)
            processChunk (ch, numCh, sc, scCh, pos, std::min (kChunk, n - pos));
    }

    // ---- lus par l'interface
    const VocalPocket::Meter& getPocketMeter() const noexcept { return pocket.getMeter(); }
    float getPocketNorm() const noexcept { return pocket.getNorm(); }
    float getAutoGainDb() const noexcept { return autoGain.getGainDb(); }
    float consumeInputLevel (int c) noexcept  { return inLevel[(size_t) (c & 1)].exchange (0.0f, std::memory_order_relaxed); }
    float consumeOutputLevel (int c) noexcept { return outLevel[(size_t) (c & 1)].exchange (0.0f, std::memory_order_relaxed); }
    float consumeScLevel (int c) noexcept     { return scLevel[(size_t) (c & 1)].exchange (0.0f, std::memory_order_relaxed); }
    float consumeKickBoostDb() noexcept       { return kickShown.exchange (0.0f, std::memory_order_relaxed); }
    float getEnergyDb() const noexcept        { return energyShown.load (std::memory_order_relaxed); }
    float getWidthRatio() const noexcept      { return widthShown.load (std::memory_order_relaxed); }   // S / M en sortie

private:
    using Smooth = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    void applyParams (bool snap) noexcept
    {
        const auto& p = params;
        auto set = [snap] (Smooth& s, float v) { if (snap) s.setCurrentAndTargetValue (v); else s.setTargetValue (v); };
        set (inGain, dbToGain (p.inputDb));
        set (outGain, dbToGain (p.outputDb));
        set (spaceMix, p.spaceOn ? 1.0f : 0.0f);
        set (pocketMix, p.pocketOn ? 1.0f : 0.0f);
        set (listenMix, p.listen && p.pocketOn ? 1.0f : 0.0f);
        set (restoreMix, p.restoreOn && ! p.listen ? 1.0f : 0.0f);
        set (wetMix, p.bypass ? 0.0f : 1.0f);
        space.setParams (p.space, snap);
        restore.setParams ({ p.kick, p.energy, duckSmooth }, snap);
    }

    static void peakInto (std::atomic<float>& dst, const float* x, int n) noexcept
    {
        float pk = 0.0f;
        for (int i = 0; i < n; ++i) pk = std::max (pk, std::abs (x[i]));
        if (pk > dst.load (std::memory_order_relaxed)) dst.store (pk, std::memory_order_relaxed);
    }

    // fondu entre l'entrée du module (pre) et sa sortie (M / S), selon mix
    void blend (Smooth& mix, int n) noexcept
    {
        if (! mix.isSmoothing())
        {
            if (mix.getTargetValue() >= 1.0f) return;
            std::copy (preM.begin(), preM.begin() + n, M.begin());
            std::copy (preS.begin(), preS.begin() + n, S.begin());
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            const float k = mix.getNextValue();
            M[(size_t) i] = preM[(size_t) i] + k * (M[(size_t) i] - preM[(size_t) i]);
            S[(size_t) i] = preS[(size_t) i] + k * (S[(size_t) i] - preS[(size_t) i]);
        }
    }

    void savePre (int n) noexcept
    {
        std::copy (M.begin(), M.begin() + n, preM.begin());
        std::copy (S.begin(), S.begin() + n, preS.begin());
    }

    void processChunk (float* const* ch, int numCh, const float* const* sc, int scCh, int pos, int n) noexcept
    {
        float* L = ch[0] + pos;
        float* R = numCh > 1 ? ch[1] + pos : nullptr;
        const float* scA = scCh > 0 ? sc[0] + pos : nullptr;
        const float* scB = scCh > 1 ? sc[1] + pos : nullptr;

        // ---- entrée : sécurité, mètres, copie sèche (BYPASS)
        for (int i = 0; i < n; ++i)
        {
            L[i] = sanitize (L[i]);
            dryL[(size_t) i] = L[i];
            if (R != nullptr) { R[i] = sanitize (R[i]); dryR[(size_t) i] = R[i]; }
        }
        peakInto (inLevel[0], L, n);
        peakInto (inLevel[1], R != nullptr ? R : L, n);
        if (scA != nullptr)
        {
            peakInto (scLevel[0], scA, n);
            peakInto (scLevel[1], scB != nullptr ? scB : scA, n);
        }

        // ---- INPUT, référence de l'Auto Gain, Mid / Side
        for (int i = 0; i < n; ++i)
        {
            const float g = inGain.getNextValue();
            L[i] *= g;
            if (R != nullptr) R[i] *= g;
        }
        {
            const float* ref[2] { L, R != nullptr ? R : L };
            autoGain.analyseReference (ref, R != nullptr ? 2 : 1, n);
        }
        for (int i = 0; i < n; ++i)
        {
            M[(size_t) i] = R != nullptr ? 0.5f * (L[i] + R[i]) : L[i];
            S[(size_t) i] = R != nullptr ? 0.5f * (L[i] - R[i]) : 0.0f;
        }

        // ---- SPACE (inutile en mono : pas de Side)
        savePre (n);
        if (R != nullptr) space.process (M.data(), S.data(), n);
        blend (spaceMix, n);

        // ---- VOCAL POCKET (+ LISTEN : ce que la poche retire)
        savePre (n);
        pocket.process (M.data(), S.data(), scA, scB, n, params.pocket);
        blend (pocketMix, n);
        for (int i = 0; i < n; ++i)   // ce que la poche a retiré (l'Auto Gain l'ignore, voir plus bas)
        {
            const auto si = (size_t) i;
            cutM[si] = preM[si] - M[si];
            cutS[si] = preS[si] - S[si];
        }
        if (listenMix.isSmoothing() || listenMix.getTargetValue() > 0.0f)
            for (int i = 0; i < n; ++i)
            {
                const float k = listenMix.getNextValue();
                const auto si = (size_t) i;
                M[si] += k * (cutM[si] - M[si]);
                S[si] += k * (cutS[si] - S[si]);
            }

        // ---- RESTORE (le recul de MID ENERGY suit la poche lentement, ~0.4 s : jamais au rythme de la voix)
        savePre (n);
        duckSmooth += (1.0f - std::exp (-(float) n / (0.4f * (float) fs))) * (pocket.getMaxReductionDb() / VocalPocket::kMaxDepth - duckSmooth);
        restore.setParams ({ params.kick, params.energy, duckSmooth }, false);
        restore.process (M.data(), S.data(), n);
        blend (restoreMix, n);
        {
            const float k = kickShown.load (std::memory_order_relaxed);
            if (restore.getKickBoostDb() > k) kickShown.store (restore.getKickBoostDb(), std::memory_order_relaxed);
            energyShown.store (params.restoreOn ? restore.getEnergyDb() : 0.0f, std::memory_order_relaxed);
        }

        // ---- retour L / R, largeur mesurée pour l'afficheur SPACE
        float sm = 0.0f, ss = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float m = M[(size_t) i], s = S[(size_t) i];
            sm += m * m;
            ss += s * s;
            if (R != nullptr) { L[i] = m + s; R[i] = m - s; }
            else              L[i] = m;
        }
        {
            const float c = 1.0f - std::exp (-(float) n / (0.25f * (float) fs));
            widthEnvM += c * (sm / (float) n - widthEnvM);
            widthEnvS += c * (ss / (float) n - widthEnvS);
            widthShown.store (widthEnvM > 1.0e-9f ? std::sqrt (widthEnvS / widthEnvM) : 0.0f, std::memory_order_relaxed);
        }

        // ---- AUTO GAIN (figé pendant LISTEN) + OUTPUT
        // Mesure du signal traité SANS la poche : l'Auto Gain compense SPACE / RESTORE / INPUT mais jamais la poche
        // (sinon il remonterait toute la prod à chaque entrée de la voix : pompage).
        {
            for (int i = 0; i < n; ++i)
            {
                const auto si = (size_t) i;
                agL[si] = L[i] + cutM[si] + cutS[si];
                agR[si] = R != nullptr ? R[i] + cutM[si] - cutS[si] : agL[si];
            }
            const float* proc[2] { agL.data(), agR.data() };
            autoGain.analyseProcessed (proc, R != nullptr ? 2 : 1, n);
            autoGain.update (params.autoGain && ! params.listen, n);
        }
        for (int i = 0; i < n; ++i)
        {
            const float g = autoGain.nextGain() * outGain.getNextValue();
            L[i] *= g;
            if (R != nullptr) R[i] *= g;
        }

        // ---- BYPASS (fondu) + sécurité de sortie
        const bool fading = wetMix.isSmoothing() || wetMix.getTargetValue() < 1.0f;
        for (int i = 0; i < n; ++i)
        {
            const float k = fading ? wetMix.getNextValue() : 1.0f;
            const auto si = (size_t) i;
            L[i] = sanitize (dryL[si] + k * (L[i] - dryL[si]));
            if (R != nullptr) R[i] = sanitize (dryR[si] + k * (R[i] - dryR[si]));
        }
        peakInto (outLevel[0], L, n);
        peakInto (outLevel[1], R != nullptr ? R : L, n);
    }

    double fs = 44100.0;
    Params params;
    SpaceModule space;
    VocalPocket pocket;
    RestoreModule restore;
    AutoGain autoGain;
    Smooth inGain, outGain, spaceMix, pocketMix, restoreMix, listenMix, wetMix;
    std::array<float, kChunk> M {}, S {}, preM {}, preS {}, dryL {}, dryR {}, cutM {}, cutS {}, agL {}, agR {};
    float duckSmooth = 0.0f;
    float widthEnvM = 0.0f, widthEnvS = 0.0f;
    std::array<std::atomic<float>, 2> inLevel {}, outLevel {}, scLevel {};
    std::atomic<float> kickShown { 0.0f }, energyShown { 0.0f }, widthShown { 0.0f };
};
} // namespace onirique
