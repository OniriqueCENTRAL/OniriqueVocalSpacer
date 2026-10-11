#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  SPACE : image stéréo de l'instrumental, traitée en Mid / Side, pour dégager le centre où se pose la voix.

    - WIDTH (0..200 %)       : gain du Side. 100 % = neutre, 0 % = mono, 200 % = Side x2.
    - LOW FOCUS (0..100 %)   : resserre les graves au centre. Le Side passe dans un passe-haut 12 dB/oct
                               (2 x 1er ordre, 160 Hz) dosé en fondu : courbe monotone, sans bosse, et à 0 %
                               le Side est rigoureusement intact (aucun déphasage).
    - SIDE DETAIL (0..100 %) : shelf aigu sur le Side seulement (jusqu'à +6 dB dès 3 kHz) : le détail part
                               sur les côtés, le centre reste libre pour la voix.

    Le Mid n'est jamais touché ici. Coefficients recalculés par tranche, gains lissés : automation sans clic.
*/
class SpaceModule
{
public:
    static constexpr double kFocusHz = 160.0, kDetailHz = 3000.0;
    static constexpr float kDetailMaxDb = 6.0f;

    struct Params { float width = 100.0f, lowFocus = 0.0f, sideDetail = 0.0f; };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const auto hp = BiquadCoeffs::highpass1 (fs, kFocusHz);
        hp1.setCoeffs (hp);
        hp2.setCoeffs (hp);
        widthG.reset (fs, 0.03);
        focus.reset (fs, 0.03);
        appliedDb = -1.0f;
        reset();
    }

    void reset() noexcept { hp1.reset(); hp2.reset(); shelf.reset(); }

    void setParams (const Params& p, bool snap) noexcept
    {
        const float w = juce::jlimit (0.0f, 2.0f, p.width / 100.0f);
        const float f = juce::jlimit (0.0f, 1.0f, p.lowFocus / 100.0f);
        detailTarget = juce::jlimit (0.0f, 1.0f, p.sideDetail / 100.0f);
        if (snap)
        {
            widthG.setCurrentAndTargetValue (w);
            focus.setCurrentAndTargetValue (f);
            detailNow = detailTarget;
        }
        else
        {
            widthG.setTargetValue (w);
            focus.setTargetValue (f);
        }
    }

    // m, s : une tranche (<= 32 échantillons) en Mid / Side, traitée sur place
    void process (float* /*m*/, float* s, int n) noexcept
    {
        detailNow += 0.08f * (detailTarget - detailNow);   // ~8 ms par tranches de 32 échantillons
        const float db = kDetailMaxDb * detailNow;
        if (std::abs (db - appliedDb) > 0.005f)
        {
            shelf.setCoeffs (BiquadCoeffs::highShelf (fs, kDetailHz, 0.707, db));
            appliedDb = db;
        }

        for (int i = 0; i < n; ++i)
        {
            float side = s[i];
            const float hp = hp2.process (hp1.process (side, 0), 0);
            side += focus.getNextValue() * (hp - side);      // LOW FOCUS
            side = shelf.process (side, 0);                  // SIDE DETAIL (identité à 0 dB)
            s[i] = side * widthG.getNextValue();             // WIDTH
        }
    }

private:
    double fs = 44100.0;
    Biquad hp1, hp2, shelf;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthG, focus;
    float detailTarget = 0.0f, detailNow = 0.0f, appliedDb = -1.0f;
};
} // namespace onirique
