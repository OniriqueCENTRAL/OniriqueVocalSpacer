#pragma once

#include <cmath>

namespace onirique
{
/*  Garde du sidechain : certains DAW (FL Studio notamment) activent le bus sidechain par défaut et, quand
    rien n'y est routé, y recopient l'entrée principale. La poche prendrait alors l'instrumental pour la voix.

    À chaque bloc : corrélation normalisée entre le sidechain et l'entrée principale (mono). Au-dessus de 0.98,
    le bloc est une copie (même atténuée) de l'instrumental. Une vraie voix n'est jamais corrélée à ce point.

    Hystérésis : au départ, le sidechain est présumé être une copie (aucune poche tant que rien ne prouve le
    contraire). Un seul bloc non corrélé suffit à le reconnaître comme une vraie voix ; il faut 0.4 s de blocs
    corrélés d'affilée pour repasser en "copie". Les creux entre syllables (où le sidechain ne contient que la
    repisse de l'instrumental) ne font donc jamais basculer la garde. Bloc silencieux : décision conservée.
*/
class SidechainGuard
{
public:
    void prepare (double sampleRate) noexcept { fs = sampleRate; reset(); }
    void reset() noexcept { mirror = true; mirroredSamples = 0; }

    // renvoie true si le sidechain doit être ignoré (copie de l'entrée principale)
    bool isMirror (const float* mL, const float* mR, const float* sA, const float* sB, int n) noexcept
    {
        double mm = 0.0, ss = 0.0, ms = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double m = mR != nullptr ? 0.5 * ((double) mL[i] + (double) mR[i]) : (double) mL[i];
            const double s = sB != nullptr ? 0.5 * ((double) sA[i] + (double) sB[i]) : (double) sA[i];
            mm += m * m;
            ss += s * s;
            ms += m * s;
        }
        const double floor = 1.0e-9 * (double) n;   // ~ -90 dBFS
        if (mm > floor && ss > floor)
        {
            if (std::abs (ms) / std::sqrt (mm * ss) > 0.98)
            {
                mirroredSamples += n;
                if (mirroredSamples > (long long) (0.4 * fs)) mirror = true;
            }
            else
            {
                mirroredSamples = 0;
                mirror = false;
            }
        }
        return mirror;
    }

private:
    double fs = 44100.0;
    bool mirror = true;
    long long mirroredSamples = 0;
};
} // namespace onirique
