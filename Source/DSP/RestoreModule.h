#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  RESTORE : rend le punch et l'énergie que la poche vocale a pu enlever à l'instrumental. Mid / Side, latence nulle.

    - KICK FOCUS (0..100 %) : transient designer limité au kick. Détection sur le Mid filtré (passe-bas 4e ordre
      140 Hz) : enveloppe rapide / enveloppe lente = attaque. Pendant l'attaque seulement, cloche dynamique
      ajoutée à 62 Hz (corps) + 30 % à 3.2 kHz (claquement du batteur), jusqu'à +7 dB. Le sustain et la basse
      tenue ne bougent pas : le kick ressort sans gonfler le bas.
    - MID ENERGY (0..100 %) : compression parallèle de la bande 400 Hz - 3.5 kHz (Mid + Side, détecteur commun) :
      les passages calmes de la bande remontent (jusqu'à +12 dB dans la voie parallèle), les crêtes sont tassées
      (4:1 au-dessus du niveau moyen), saturation douce pour la densité. Sur le Mid, la voie parallèle est
      réduite quand la poche vocale travaille fort : l'énergie revient sur les côtés, pas dans la poche.
*/
class RestoreModule
{
public:
    struct Params { float kick = 0.0f, energy = 0.0f, pocketDuck = 0.0f; };   // %, %, 0..1

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const auto lp = BiquadCoeffs::lowpass (fs, 140.0, 0.7071);
        detLp1.setCoeffs (lp);
        detLp2.setCoeffs (lp);
        kickBp.setCoeffs (BiquadCoeffs::bandpass (fs, 62.0, 0.8));
        beaterBp.setCoeffs (BiquadCoeffs::bandpass (fs, 3200.0, 1.0));
        energyBp.setCoeffs (BiquadCoeffs::bandpass (fs, 1100.0, 0.55));

        auto c = [this] (double tau) { return (float) (1.0 - std::exp (-1.0 / (tau * fs))); };
        fastA = c (0.0003); fastR = c (0.035);
        slowA = c (0.030);  slowR = c (0.300);
        kSmooth = c (0.0015);
        eA = c (0.020); eR = c (0.30); eLongC = c (1.5);   // lent : la voie parallèle ne respire pas avec la batterie
        reset();
    }

    void reset() noexcept
    {
        for (auto* f : { &detLp1, &detLp2, &kickBp, &beaterBp, &energyBp }) f->reset();
        fast = slow = 0.0f;
        kGain = 0.0f;
        eEnv = eLong = 0.0f;
        gcNow = 0.0f;
        kickPeakDb = energyDb = 0.0f;
    }

    // dernière tranche : crête du boost KICK (dB) et gain moyen ajouté par MID ENERGY (dB), pour l'interface
    float getKickBoostDb() const noexcept { return kickPeakDb; }
    float getEnergyDb() const noexcept { return energyDb; }

    void setParams (const Params& p, bool snap) noexcept
    {
        kickTarget = juce::jlimit (0.0f, 1.0f, p.kick / 100.0f);
        energyTarget = juce::jlimit (0.0f, 1.0f, p.energy / 100.0f);
        duck = juce::jlimit (0.0f, 1.0f, p.pocketDuck);
        if (snap) { kickNow = kickTarget; energyNow = energyTarget; }
    }

    // une tranche (<= 32 échantillons) en Mid / Side, traitée sur place
    void process (float* m, float* s, int n) noexcept
    {
        if (n <= 0) return;
        kickNow += 0.08f * (kickTarget - kickNow);
        energyNow += 0.08f * (energyTarget - energyNow);

        // ---- MID ENERGY : gain de la voie parallèle, calculé en début de tranche, rampe linéaire dans la tranche
        float gcTarget = 0.0f;
        if (eLong > 1.0e-9f)   // en dessous de -90 dB : rien à densifier (pas de souffle remonté)
        {
            const float level = 10.0f * std::log10 (eEnv + 1.0e-12f);
            const float ref = 10.0f * std::log10 (eLong + 1.0e-12f) - 4.0f;
            const float gDb = level > ref ? -(level - ref) * 0.75f : std::min ((ref - level) * 0.5f, 12.0f);
            gcTarget = dbToGain (gDb);
        }
        const float gcStep = (gcTarget - gcNow) / (float) n;
        const float eMix = 0.5f * energyNow;
        const float eMixM = eMix * (1.0f - 0.5f * duck);

        float kPeak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float xm = m[i], xs = s[i];

            // ---- KICK FOCUS
            const float a = std::abs (detLp2.process (detLp1.process (xm, 0), 0));
            fast += (a > fast ? fastA : fastR) * (a - fast);
            slow += (a > slow ? slowA : slowR) * (a - slow);
            const float tr = juce::jlimit (0.0f, 12.0f, 8.6858896f * std::log ((fast + 1.0e-5f) / (slow + 1.0e-5f)));
            const float boostDb = kickNow * std::min (tr * 0.9f, 7.0f);
            kGain += kSmooth * ((dbToGain (boostDb) - 1.0f) - kGain);
            kPeak = std::max (kPeak, boostDb);
            const float kick = kickBp.process (xm, 0) + 0.3f * beaterBp.process (xm, 0);

            // ---- MID ENERGY (voie parallèle, détecteur Mid + Side)
            const float bm = energyBp.process (xm, 0), bs = energyBp.process (xs, 1);
            const float pw = bm * bm + bs * bs;
            eEnv += (pw > eEnv ? eA : eR) * (pw - eEnv);
            eLong += eLongC * (eEnv - eLong);
            gcNow += gcStep;
            const float pm = std::tanh (1.5f * bm * gcNow) * (1.0f / 1.5f);
            const float ps = std::tanh (1.5f * bs * gcNow) * (1.0f / 1.5f);

            m[i] = xm + kGain * kick + eMixM * pm;
            s[i] = xs + eMix * ps;
        }
        gcNow = gcTarget;
        kickPeakDb = kPeak;
        energyDb = 20.0f * std::log10 (1.0f + eMix * gcTarget);
    }

private:
    double fs = 44100.0;
    Biquad detLp1, detLp2, kickBp, beaterBp, energyBp;
    float fastA = 0, fastR = 0, slowA = 0, slowR = 0, kSmooth = 0, eA = 0, eR = 0, eLongC = 0;
    float fast = 0, slow = 0, kGain = 0, eEnv = 0, eLong = 0, gcNow = 0;
    float kickTarget = 0, kickNow = 0, energyTarget = 0, energyNow = 0, duck = 0;
    float kickPeakDb = 0, energyDb = 0;
};
} // namespace onirique
