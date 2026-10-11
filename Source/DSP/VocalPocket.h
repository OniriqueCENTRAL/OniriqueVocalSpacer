#pragma once

#include <array>
#include <atomic>
#include <complex>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  VOCAL POCKET : égalisation dynamique pilotée par la voix, pour ne creuser dans l'instrumental QUE les
    fréquences qui masquent la voix, et seulement pendant qu'elle chante. Latence nulle.

    Objectif : transparence totale, AUCUN pompage. La poche suit les PHRASES, pas les syllabes ni la batterie :

      - Instrumental : niveau par bande moyenné sur ~200 ms (symétrique). Un coup de kick ou de snare ne fait
        pas bouger l'estimation du masquage, donc ne module jamais la poche.
      - Voix, activité : niveau large bande rapide -> activité 0..1 (courbe douce entre -62 et -46 dBFS),
        avec MAINTIEN : une fois la voix détectée, la poche reste ouverte pendant les creux entre syllabes
        (HOLD selon SPEED) ; elle ne se referme qu'après la fin de la phrase.
      - Voix, forme spectrale : mémorisée pendant que la voix chante (attaque 30 ms, relâchement 600 ms) et
        GELÉE dans les creux : la forme de la poche ne scintille pas d'un phonème à l'autre.
      - Présence par bande (près de la bande la plus forte de la voix -> 1, 30 dB dessous -> 0) et masquage
        (instrumental 9 dB sous la voix -> 0, 9 dB au-dessus -> 1, décalé de +/-15 dB par SENSITIVITY),
        tous deux en courbe douce (smoothstep), sans coude.
      - Lissage spectral large (noyau 1-2-3-2-1) : une poche ronde et continue, jamais un peigne.
      - Dynamique du gain en dB : deux lissages en cascade (courbe en S, pas de départ sec), attaque à
        l'ouverture, relâchement lent à la fermeture.

    Mode STATIC (sans sidechain) : poche fixe, en forme de spectre vocal moyen (corps ~330 Hz, présence
    ~2.5 kHz), dosée par DEPTH et limitée à la zone RANGE.

    Application : 20 cloches (Q 2.3) sur le Mid ; en mode STEREO, les mêmes cloches aussi sur le Side.
    La réponse cumulée des cloches est normalisée : la réduction affichée est la réduction réelle.
*/
class VocalPocket
{
public:
    static constexpr int kBands = 20;
    static constexpr float kMaxDepth = 12.0f;
    static constexpr double kBellQ = 2.3, kDetQ = 2.6;

    struct Params
    {
        bool staticMode = false, stereo = false;
        float depthDb = 6.0f, sensitivity = 50.0f;   // dB, %
        int speed = 1;                               // 0 FAST, 1 NORMAL, 2 SMOOTH
        float loHz = 200.0f, hiHz = 7000.0f;
    };

    // dynamique par vitesse : ouverture, maintien entre les syllabes, fermeture (secondes)
    struct Timing { float attack, hold, release; };
    static Timing timing (int speed) noexcept
    {
        static constexpr Timing t[3] { { 0.015f, 0.12f, 0.25f }, { 0.030f, 0.25f, 0.50f }, { 0.060f, 0.45f, 0.90f } };
        return t[juce::jlimit (0, 2, speed)];
    }

    // lu par l'interface (aucun verrou) ; niveaux en dB, réduction en dB positifs
    struct Meter
    {
        std::array<std::atomic<float>, kBands> gr {}, inst {}, voc {};
        std::atomic<bool> scActive { false };
    };

    static float bandHz (int b) noexcept { return 100.0f * std::pow (120.0f, (float) b / (float) (kBands - 1)); }

    static float staticProfile (float hz) noexcept
    {
        const auto bump = [hz] (float centre, float octWidth)
        {
            const float d = std::log2 (hz / centre) / octWidth;
            return std::exp (-0.5f * d * d);
        };
        return std::max (bump (2500.0f, 1.1f), 0.5f * bump (330.0f, 0.8f));
    }

    static float rangeWeight (float hz, float lo, float hi) noexcept
    {
        const float d = hz < lo ? std::log2 (lo / hz) : (hz > hi ? std::log2 (hz / hi) : 0.0f);
        return smoothstep (1.0f - d / 0.33f);
    }

    // cloche de la bande b (gain en dB, déjà normalisé) : la même pour l'audio et pour la courbe affichée
    static BiquadCoeffs bell (double fs, int b, float gainDb) noexcept
    {
        return BiquadCoeffs::peaking (fs, bandHz (b), kBellQ, gainDb);
    }

    // facteur appliqué à la réduction de chaque bande pour que la courbe cumulée corresponde à la demande
    static float computeNorm (double fs)
    {
        BiquadCoeffs c[kBands];
        for (int b = 0; b < kBands; ++b) c[b] = bell (fs, b, -1.0f);
        double sum = 0.0;
        int count = 0;
        for (int b = 2; b < kBands - 2; ++b)
        {
            if (bandHz (b) > 0.4 * fs) break;
            std::complex<double> h (1.0, 0.0);
            for (int k = 0; k < kBands; ++k) h *= c[k].response (fs, bandHz (b));
            sum += -20.0 * std::log10 (std::max (1.0e-9, std::abs (h)));
            ++count;
        }
        return count > 0 && sum > 0.0 ? (float) ((double) count / sum) : 1.0f;
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (int b = 0; b < kBands; ++b)
        {
            const auto bp = BiquadCoeffs::bandpass (fs, bandHz (b), kDetQ);
            for (auto* f : { &detI1[(size_t) b], &detI2[(size_t) b], &detV1[(size_t) b], &detV2[(size_t) b] })
                f->setCoeffs (bp);
        }
        norm = computeNorm (fs);
        reset();
    }

    void reset() noexcept
    {
        for (int b = 0; b < kBands; ++b)
        {
            const auto bi = (size_t) b;
            for (auto* f : { &detI1[bi], &detI2[bi], &detV1[bi], &detV2[bi], &bellM[bi], &bellS[bi] })
                f->reset();
            bellM[bi].setCoeffs ({});
            bellS[bi].setCoeffs ({});
            envI[bi] = envVf[bi] = envVs[bi] = 1.0e-12f;
            gr1[bi] = gr[bi] = appliedM[bi] = appliedS[bi] = 0.0f;
            meter.gr[bi].store (0.0f);
            meter.inst[bi].store (-120.0f);
            meter.voc[bi].store (-120.0f);
        }
        activity = 0.0f;
        holdLeft = 0;
        sMix = 0.0f;
        maxGr = 0.0f;
        meter.scActive.store (false);
    }

    const Meter& getMeter() const noexcept { return meter; }
    float getNorm() const noexcept { return norm; }
    float getMaxReductionDb() const noexcept { return maxGr; }

    // m, s : instrumental en Mid / Side (une tranche de n <= 32 échantillons), traité sur place
    // scA / scB : voix (sidechain), scA nul si aucun sidechain ; scB nul si sidechain mono
    void process (float* m, float* s, const float* scA, const float* scB, int n, const Params& p) noexcept
    {
        if (n <= 0) return;

        // ---- 1) analyse de la tranche (avant de la traiter : jamais en retard)
        std::array<double, kBands> accI {}, accV {};
        const bool hasSc = scA != nullptr && ! p.staticMode;
        for (int i = 0; i < n; ++i)
        {
            const float xi = m[i];
            for (int b = 0; b < kBands; ++b)
            {
                const float y = detI2[(size_t) b].process (detI1[(size_t) b].process (xi, 0), 0);
                accI[(size_t) b] += (double) y * (double) y;
            }
            if (hasSc)
            {
                const float xv = scB != nullptr ? 0.5f * (scA[i] + scB[i]) : scA[i];
                for (int b = 0; b < kBands; ++b)
                {
                    const float y = detV2[(size_t) b].process (detV1[(size_t) b].process (xv, 0), 0);
                    accV[(size_t) b] += (double) y * (double) y;
                }
            }
        }

        auto coef = [this, n] (double tau) { return (float) (1.0 - std::exp (-(double) n / (tau * fs))); };
        const float inv = 1.0f / (float) n;
        const float cI = coef (0.20);                       // instrumental : moyenne ~200 ms, symétrique
        const float vfA = coef (0.005), vfR = coef (0.04);  // voix : enveloppe rapide (activité)
        const float vsA = coef (0.030), vsR = coef (0.60);  // voix : forme spectrale (mémoire)

        std::array<float, kBands> I {}, Vs {}, w {};
        double vPow = 0.0;
        for (int b = 0; b < kBands; ++b)
        {
            const auto bi = (size_t) b;
            const float ei = (float) accI[bi] * inv;
            envI[bi] += cI * (ei - envI[bi]);
            I[bi] = 10.0f * std::log10 (envI[bi] + 1.0e-12f);
            w[bi] = rangeWeight (bandHz (b), p.loHz, p.hiHz);

            const float ev = hasSc ? (float) accV[bi] * inv : 0.0f;
            envVf[bi] += (ev > envVf[bi] ? vfA : vfR) * (ev - envVf[bi]);
            if (w[bi] > 0.0f) vPow += envVf[bi];
        }

        // ---- 2) activité de la voix, avec maintien entre les syllabes
        const Timing tm = timing (p.speed);
        const float raw = hasSc ? smoothstep ((10.0f * std::log10 ((float) vPow + 1.0e-12f) + 62.0f) / 16.0f) : 0.0f;
        if (raw >= activity)
        {
            activity = raw;
            holdLeft = (int) (tm.hold * fs);
        }
        else if (holdLeft > 0)
            holdLeft -= n;
        else
            activity = raw;   // la fermeture elle-même est lissée par la dynamique du gain (étape 4)
        const bool singing = raw > 0.5f;

        // forme de la voix : mise à jour seulement pendant qu'elle chante, gelée dans les creux
        float vsMax = -200.0f;
        for (int b = 0; b < kBands; ++b)
        {
            const auto bi = (size_t) b;
            if (singing)
                envVs[bi] += (envVf[bi] > envVs[bi] ? vsA : vsR) * (envVf[bi] - envVs[bi]);
            Vs[bi] = 10.0f * std::log10 (envVs[bi] + 1.0e-12f);
            if (w[bi] > 0.0f) vsMax = std::max (vsMax, Vs[bi]);
        }

        // ---- 3) réduction visée par bande
        std::array<float, kBands> target {};
        const float depth = juce::jlimit (0.0f, kMaxDepth, p.depthDb);
        if (p.staticMode)
        {
            for (int b = 0; b < kBands; ++b)
                target[(size_t) b] = depth * staticProfile (bandHz (b)) * w[(size_t) b];
        }
        else if (activity > 0.0f)
        {
            const float sensOffset = (juce::jlimit (0.0f, 100.0f, p.sensitivity) - 50.0f) / 50.0f * 15.0f;
            for (int b = 0; b < kBands; ++b)
            {
                const auto bi = (size_t) b;
                const float presence = smoothstep ((Vs[bi] - (vsMax - 30.0f)) / 24.0f);
                const float masking  = smoothstep ((I[bi] - Vs[bi] + sensOffset + 9.0f) / 18.0f);
                target[bi] = depth * activity * w[bi] * presence * masking;
            }
        }

        // lissage spectral large : poche ronde et continue
        std::array<float, kBands> smooth {};
        for (int b = 0; b < kBands; ++b)
        {
            float acc = 0.0f, wsum = 0.0f;
            for (int k = -2; k <= 2; ++k)
            {
                const int j = b + k;
                if (j < 0 || j >= kBands) continue;
                const float kw = (float) (3 - std::abs (k));
                acc += kw * target[(size_t) j];
                wsum += kw;
            }
            smooth[(size_t) b] = std::max (0.75f * target[(size_t) b], acc / wsum);
        }

        // ---- 4) dynamique du gain (courbe en S : deux lissages en cascade) + coefficients des cloches
        // deux lissages en cascade de constante 0.35 x T : la cascade atteint ~90 % en T (ouverture / fermeture annoncées)
        const float gA = coef (p.staticMode ? 0.05 : 0.35 * tm.attack), gR = coef (p.staticMode ? 0.05 : 0.35 * tm.release);
        sMix += coef (0.05) * ((p.stereo ? 1.0f : 0.0f) - sMix);
        float mx = 0.0f;
        for (int b = 0; b < kBands; ++b)
        {
            const auto bi = (size_t) b;
            const float k = smooth[bi] > gr1[bi] ? gA : gR;
            gr1[bi] += k * (smooth[bi] - gr1[bi]);
            gr[bi]  += (gr1[bi] > gr[bi] ? gA : gR) * (gr1[bi] - gr[bi]);
            mx = std::max (mx, gr[bi]);
            updateBell (bellM[bi], appliedM[bi], b, gr[bi]);
            updateBell (bellS[bi], appliedS[bi], b, gr[bi] * sMix);
            meter.gr[bi].store (gr[bi], std::memory_order_relaxed);
            meter.inst[bi].store (I[bi], std::memory_order_relaxed);
            meter.voc[bi].store (Vs[bi], std::memory_order_relaxed);
        }
        maxGr = mx;
        meter.scActive.store (hasSc && activity > 0.5f, std::memory_order_relaxed);

        // ---- 5) application
        const bool sideActive = sMix > 1.0e-4f || std::any_of (appliedS.begin(), appliedS.end(), [] (float v) { return v > 0.0f; });
        for (int i = 0; i < n; ++i)
        {
            float xm = m[i];
            for (auto& f : bellM) xm = f.process (xm, 0);
            m[i] = xm;
            if (sideActive)
            {
                float xs = s[i];
                for (auto& f : bellS) xs = f.process (xs, 0);
                s[i] = xs;
            }
        }
    }

private:
    static float smoothstep (float x) noexcept
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        return x * x * (3.0f - 2.0f * x);
    }

    // recalcule une cloche seulement si sa réduction a bougé (identité exacte quand elle retombe à 0)
    void updateBell (Biquad& f, float& applied, int b, float red) noexcept
    {
        if (red < 0.005f) red = 0.0f;
        if (std::abs (red - applied) < 0.01f && ! (red == 0.0f && applied != 0.0f)) return;
        applied = red;
        f.setCoeffs (red == 0.0f ? BiquadCoeffs {} : bell (fs, b, -red * norm));
    }

    double fs = 44100.0;
    float norm = 1.0f;
    std::array<Biquad, kBands> detI1, detI2, detV1, detV2, bellM, bellS;
    std::array<float, kBands> envI {}, envVf {}, envVs {}, gr1 {}, gr {}, appliedM {}, appliedS {};
    float activity = 0.0f, sMix = 0.0f, maxGr = 0.0f;
    int holdLeft = 0;
    Meter meter;
};
} // namespace onirique
