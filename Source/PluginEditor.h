#pragma once

#include "License/LicenseOverlay.h"
#include "PluginProcessor.h"

#include <array>
#include <map>

namespace onirique::ui
{
// ---- DA du site ONIRIQUE (identique à VOCALBOX) : nuit / sang, accent braise, lignes fines, lueurs
inline const juce::Colour night  { 0xff0D0809 };
inline const juce::Colour blood  { 0xff5C0008 };
inline const juce::Colour deep   { 0xff1A080A };
inline const juce::Colour deep2  { 0xff2A0A0E };
inline const juce::Colour mist   { 0xffF6EEEE };
inline const juce::Colour haze   { 0xffA88F92 };
inline const juce::Colour ember  { 0xffE23444 };
inline const juce::Colour bone   { 0xffFFECEE };
inline const juce::Colour rose   { 0xffFF9AA4 };   // la voix (sidechain) dans les afficheurs
inline const juce::Colour line   { (juce::uint8) 255, (juce::uint8) 210, (juce::uint8) 214, 0.13f };

// Polices : display (titres), body (libellés), mono (valeurs). Tout passe par font().
enum class Face { display, body, mono };
juce::Font font (Face, float height, bool bold = false, float kerning = 0.0f);
float textWidth (const juce::Font&, const juce::String&);
// police de hauteur maxH, réduite si nécessaire pour que le texte tienne dans maxW (jamais coupé)
juce::Font fitFont (Face, float maxH, float maxW, const juce::String&, bool bold = false, float kerning = 0.0f);

// échelle de la fenêtre (1 = largeur de référence) : sert de PLANCHER aux tailles de texte
float editorScale (const juce::Component&);

// trait lumineux : plusieurs passes de plus en plus larges et transparentes sous le trait net
void strokeGlow (juce::Graphics&, const juce::Path&, juce::Colour, float width, float intensity);

// ombre portée d'un rectangle arrondi (corner >= 0) ou d'une ellipse (corner < 0) : même rendu que
// juce::DropShadow::drawForPath, mais le flou est calculé une seule fois par forme puis gardé en cache
// (sur Mac, Core Graphics redessine de grandes zones à chaque image : recalculer les flous y faisait ramer l'interface).
// Le cache appartient à l'interface (jamais statique) : ses images disparaissent avec elle, avant que l'hôte
// décharge le plugin (un cache statique libéré au déchargement de la DLL bloquait FL Studio à la suppression).
struct ShadowCache
{
    std::map<std::array<int, 8>, juce::Image> images;
};
void drawCachedShadow (juce::Graphics&, ShadowCache&, const juce::DropShadow&, juce::Rectangle<float> shape, float corner);

// approche exponentielle d'une valeur animée ; renvoie true tant qu'elle bouge
inline bool approach (float& v, float target, float k) noexcept
{
    if (std::abs (target - v) < 1.0e-3f) { const bool moved = v != target; v = target; return moved; }
    v += k * (target - v);
    return true;
}
} // namespace onirique::ui

// Tout ce qui s'anime reçoit tick() à 60 Hz depuis l'éditeur (une seule horloge pour toute l'interface).
struct Animated
{
    virtual ~Animated() = default;
    virtual void tick() = 0;
};

class OniriqueLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OniriqueLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;
    juce::Font getPopupMenuFont() override { return onirique::ui::font (onirique::ui::Face::body, 15.0f); }

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& name) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;
    int getPopupMenuBorderSize() override { return 7; }

private:
    onirique::ui::ShadowCache shadows;   // ombres des boutons et des curseurs
};

// Bouton rotatif complet : nom + contrôle + valeur dans une pastille. L'arc lumineux suit la valeur en glissant.
class KnobComponent : public juce::Component, public Animated
{
public:
    enum class Layout { textBelow, textRight, fader, horizontal };

    KnobComponent (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& title,
                   const juce::String& minLabel, const juce::String& maxLabel, Layout layout,
                   const juce::String& tooltip, juce::LookAndFeel& lnf, bool bipolar = false);
    ~KnobComponent() override;

    void setMaxDiameter (float d) { maxD = d; resized(); }
    std::function<void (bool)> onFocusChange;
    // activité (ex. boost en dB) : une LED braise s'allume à côté du nom quand elle dépasse 0.5
    std::function<float()> activity;

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void setFocus (bool hover, bool drag);

    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::String title;
    Layout layout;
    bool dragging = false, hovering = false;
    float shown = 0.0f, glow = 0.0f, act = 0.0f;
    juce::Rectangle<float> nameArea, valueArea;
    float unit = 1.0f, maxD = 1.0e6f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobComponent)
};

// Valeur réglable dans une pastille (glisser verticalement, molette, double-clic = défaut).
class DragValue : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    DragValue (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& tooltip);
    std::function<void (bool)> onFocusChange;
    void setPrefix (const juce::String& p) { prefix = p; repaint(); }

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    float startNorm = 0.0f, glow = 0.0f;
    juce::String prefix;
    bool hovering = false, dragging = false;
};

// Bouton power d'un module : anneau braise lumineux quand le module est actif.
class PowerButton : public juce::ToggleButton, public Animated
{
public:
    PowerButton() { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;

private:
    float on = 1.0f;
};

// Interrupteur : grand (AUTO GAIN, ON/OFF + correction en dB) ou petit (BYPASS, LISTEN avec LED)
class PillToggle : public juce::ToggleButton, public Animated
{
public:
    enum class Style { big, small, mini };
    PillToggle (const juce::String& label, Style s) : juce::ToggleButton (label), style (s) { setClickingTogglesState (true); }

    void setCaption (const juce::String& c) { if (c != caption) { caption = c; repaint(); } }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;

private:
    Style style;
    juce::String caption;
    float on = 0.0f;
};

// Sélecteur segmenté relié à un paramètre à choix ; la pastille braise glisse vers le choix.
class SegmentSelector : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    SegmentSelector (juce::AudioProcessorValueTreeState&, const char* paramId,
                     const juce::StringArray& labels, const juce::StringArray& tooltips);

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    void setTitle (const juce::String& t, const juce::String& tip = {}) { title = t; titleTip = tip; repaint(); }
    // verrouillé : estompé, clics ignorés, message par-dessus et info-bulle d'explication
    void setLocked (bool locked, const juce::String& text, const juce::String& tip);

private:
    int segmentAt (float x) const noexcept;
    juce::Rectangle<float> segmentArea() const noexcept;

    juce::StringArray labels, tips;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int selected = 0, hover = -1;
    float slide = -1.0f, lockAmt = 0.0f;
    juce::String title, titleTip, lockedText, lockedTip;
    bool locked = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentSelector)
};

// Barre de presets (en-tête) : précédent / nom (menu) / suivant + SAVE (champ de nom intégré).
class PresetBar : public juce::Component, public Animated
{
public:
    explicit PresetBar (onirique::PresetManager&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    enum Zone { none = -1, prev, name, next, save };
    Zone zoneAt (juce::Point<float>) const noexcept;
    void showMenu();
    void startSave();
    void endSave (bool commit);

    onirique::PresetManager& pm;
    juce::TextEditor editor;
    juce::Rectangle<float> zones[4];
    juce::String shownName;
    bool modified = false;
    Zone hover = none;
    float glow[4] {};
    int frame = 0;
};

// Mètre de niveau vertical stéréo (L / R) : crête, retombée douce, maintien, valeur crête (clic = remise à zéro).
class LevelMeter : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit LevelMeter (std::function<float (int)> consumePeak, const juce::String& tip = {}) : consume (std::move (consumePeak))
    {
        setTooltip (tip.isNotEmpty() ? tip : "Peak level (dBFS), left / right. The number is the highest recent peak: click to reset it.");
    }
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override { maxDb = -100.0f; repaint(); }

private:
    static float norm (float db) noexcept { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 62.0f); }   // -60 .. +2 dBFS

    std::function<float (int)> consume;
    float levelDb[2] { -100.0f, -100.0f }, holdDb[2] { -100.0f, -100.0f }, maxDb = -100.0f;
    int holdFrames[2] {};
    int maxFrames = 0;
};

// Afficheur VOCAL POCKET :
//  - spectres en temps réel : instrumental d'entrée (trait), sortie (dégradé os), voix du sidechain (rose lumineux) ;
//  - la poche : courbe EXACTE des 20 cloches (mêmes filtres que l'audio), rideau braise depuis le haut ;
//  - zone RANGE : deux poignées à glisser (LO / HI), l'extérieur est assombri ; double-clic = valeur par défaut.
class PocketDisplay : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    explicit PocketDisplay (OniriqueVocalSpacerAudioProcessor&);

    void setActive (bool a) noexcept { active = a; }
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    static constexpr int kOrder = 12, kN = 1 << kOrder, kPts = 256, kB = onirique::VocalPocket::kBands;
    static constexpr float kGrRange = 15.0f;

    struct Spectrum
    {
        std::vector<float> hist;
        int write = 0, idle = 0;
        std::array<float, kPts> db {};
    };

    void analyse (onirique::SpectrumTap&, Spectrum&);
    static float hzAt (int i) noexcept { return 20.0f * std::pow (1000.0f, (float) i / (float) (kPts - 1)); }
    juce::Rectangle<float> plotArea() const noexcept;
    float xOf (float hz) const noexcept;
    float hzOf (float x) const noexcept;
    float yGr (float db) const noexcept;
    int handleAt (juce::Point<float>) const noexcept;
    juce::RangedAudioParameter* handleParam (int h) const noexcept { return h == 0 ? loP : hiP; }

    OniriqueVocalSpacerAudioProcessor& processor;
    juce::dsp::FFT fft { kOrder };
    std::vector<float> window, scratch, fftBuf;
    float windowGain = 1.0f;
    Spectrum specIn, specOut, specVoc;
    std::array<float, kB> gr {};
    std::array<float, kPts> curve {};
    juce::RangedAudioParameter *loP = nullptr, *hiP = nullptr;
    float lo = 200.0f, hi = 7000.0f, staticAmt = 0.0f, vocAmt = 0.0f, activeAmt = 1.0f, noScAmt = 0.0f;
    float handleGlow[2] {};
    bool active = true;
    int hover = -1, drag = -1, frame = 0;
};

// Afficheur SPACE : l'image stéréo en éventail (graves / médiums / aigus) d'après WIDTH, LOW FOCUS et SIDE DETAIL,
// la voix au centre, et la largeur réellement mesurée en sortie (deux points lumineux sur le bord).
class StereoFieldDisplay : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit StereoFieldDisplay (OniriqueVocalSpacerAudioProcessor&);
    void paint (juce::Graphics&) override;
    void tick() override;

private:
    OniriqueVocalSpacerAudioProcessor& processor;
    float width = 1.0f, focus = 0.0f, detail = 0.0f, measured = 0.0f, on = 1.0f;
};

// Afficheur RESTORE : historique défilant (~5 s) des coups de kick renforcés (barres braise) et de l'énergie
// ajoutée dans les médiums (courbe os).
class RestoreDisplay : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit RestoreDisplay (OniriqueVocalSpacerAudioProcessor&);
    // dernières valeurs relevées (LED d'activité des boutons KICK FOCUS / MID ENERGY)
    float lastKick() const noexcept   { return kick[(size_t) ((write + kHist - 1) % kHist)]; }
    float lastEnergy() const noexcept { return energy[(size_t) ((write + kHist - 1) % kHist)]; }
    void paint (juce::Graphics&) override;
    void tick() override;

private:
    static constexpr int kHist = 150;
    OniriqueVocalSpacerAudioProcessor& processor;
    std::array<float, kHist> kick {}, energy {};
    int write = 0, frame = 0;
};

class OniriqueVocalSpacerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                                private juce::Timer
{
public:
    explicit OniriqueVocalSpacerAudioProcessorEditor (OniriqueVocalSpacerAudioProcessor&);
    ~OniriqueVocalSpacerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintVeils (juce::Graphics&);
    void resized() override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using Layout = KnobComponent::Layout;

    struct Section
    {
        juce::String title;
        juce::Rectangle<float> bounds;
        PowerButton* power = nullptr;
        float veil = 0.0f;   // 0 = module actif, 1 = module coupé (voile animé)
    };

    void timerCallback() override;
    float scale() const noexcept;
    void paintHeader (juce::Graphics&);
    void paintStar (juce::Graphics&, juce::Point<float> c, float r, float alpha);
    void paintScStatus (juce::Graphics&);
    // fond et cartes ne bougent pas : dessinés une fois (à la résolution réelle de l'écran) dans deux images,
    // recopiées à chaque image ; seuls l'en-tête animé, les points des cartes et les témoins sont redessinés
    void paintBackdrop (juce::Graphics&);
    void paintCards (juce::Graphics&);
    void rebuildLayers (float pixelScale);

    OniriqueVocalSpacerAudioProcessor& processor;
    OniriqueLookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 600 };

    // ---- SPACE
    StereoFieldDisplay field { processor };
    KnobComponent widthKnob  { processor.apvts, onirique::ids::width, "WIDTH", "0", "200", Layout::textBelow,
                               "Stereo width of the instrumental (Side level). 100 % = untouched, above = wider: the sides open up "
                               "and leave the centre to the voice. Double-click to reset.", lnf, true };
    KnobComponent focusKnob  { processor.apvts, onirique::ids::lowFocus, "LOW FOCUS", "0", "100", Layout::textBelow,
                               "Pulls the low end (below ~160 Hz) back to mono: a tight, centred bass and kick that never smear "
                               "around the voice. Double-click to reset.", lnf };
    KnobComponent detailKnob { processor.apvts, onirique::ids::sideDetail, "SIDE DETAIL", "0", "100", Layout::textBelow,
                               "Brightens the sides only (up to +6 dB from 3 kHz on the Side): the detail moves out to the edges, "
                               "the centre stays free for the voice. Double-click to reset.", lnf };

    // ---- VOCAL POCKET
    PocketDisplay pocketDisplay { processor };
    SegmentSelector modeSel  { processor.apvts, onirique::ids::pocketMode, { "SIDECHAIN", "STATIC" },
                               { "SIDECHAIN: route your vocal to the plugin's sidechain input. Only the frequencies where the "
                                 "instrumental masks the voice are pulled down, and only while the voice is there.",
                                 "STATIC: no sidechain needed. A fixed pocket shaped like an average lead vocal "
                                 "(body ~330 Hz, presence ~2.5 kHz), limited to the RANGE zone." } };
    SegmentSelector targetSel { processor.apvts, onirique::ids::target, { "MID", "STEREO" },
                               { "MID: the pocket is carved in the centre only, where the lead vocal sits. The sides stay intact.",
                                 "STEREO: the pocket is carved in the whole stereo image (centre and sides)." } };
    SegmentSelector speedSel { processor.apvts, onirique::ids::speed, { "FAST", "NORMAL", "SMOOTH" },
                               { "FAST: opens in 15 ms, stays open 120 ms between syllables, closes in 250 ms. Rap, fast flows.",
                                 "NORMAL: opens in 30 ms, stays open 250 ms between syllables, closes in 500 ms. The all-rounder.",
                                 "SMOOTH: opens in 60 ms, stays open 450 ms between syllables, closes in 900 ms. Invisible movement, ballads." } };
    KnobComponent depthKnob  { processor.apvts, onirique::ids::depth, "DEPTH", "0", "-12", Layout::textBelow,
                               "Maximum cut in the masking frequencies (0 to -12 dB). Double-click to reset.", lnf };
    KnobComponent sensKnob   { processor.apvts, onirique::ids::sensitivity, "SENSITIVITY", "0", "100", Layout::textBelow,
                               "How easily the instrumental is judged to mask the voice. Higher = the pocket opens sooner and in "
                               "more bands. Double-click to reset.", lnf };
    DragValue rangeLoV { processor.apvts, onirique::ids::rangeLo,
                         "Lowest frequency the pocket may touch: drag up/down (or drag the LO handle in the display). Double-click to reset." };
    DragValue rangeHiV { processor.apvts, onirique::ids::rangeHi,
                         "Highest frequency the pocket may touch: drag up/down (or drag the HI handle in the display). Double-click to reset." };
    LevelMeter scMeter { [this] (int c) { return processor.consumeScLevel (c); },
                         "Sidechain (vocal) level, left / right. Route the vocal track to the plugin's sidechain input." };
    PillToggle listenButton { "LISTEN", PillToggle::Style::mini };
    std::unique_ptr<ButtonAttachment> listenAttachment;

    // ---- RESTORE
    RestoreDisplay restoreDisplay { processor };
    KnobComponent kickKnob   { processor.apvts, onirique::ids::kickFocus, "KICK FOCUS", "0", "100", Layout::textBelow,
                               "Brings the kick's punch back: only the kick attacks are reinforced (body 62 Hz + beater click), "
                               "the sustain and the bass stay as they are. The LED lights up on each reinforced hit. Double-click to reset.", lnf };
    KnobComponent energyKnob { processor.apvts, onirique::ids::midEnergy, "MID ENERGY", "0", "100", Layout::textBelow,
                               "Parallel compression of the 400 Hz - 3.5 kHz band: density and energy back in the mids. "
                               "In the centre, it backs off while the pocket works, so it never refills the vocal space. Double-click to reset.", lnf };

    // ---- INPUT / AUTO GAIN / OUTPUT / BYPASS / presets
    KnobComponent inKnob  { processor.apvts, onirique::ids::input,  "INPUT",  "-24", "+24", Layout::textBelow,
                            "Input gain in dB. Double-click to reset to 0 dB.", lnf, true };
    KnobComponent outKnob { processor.apvts, onirique::ids::output, "OUTPUT", "-24", "+24", Layout::textBelow,
                            "Output level in dB. Double-click to reset to 0 dB.", lnf, true };
    LevelMeter inMeter  { [this] (int c) { return processor.consumeInputLevel (c); } };
    LevelMeter outMeter { [this] (int c) { return processor.consumeOutputLevel (c); } };
    PillToggle autoButton   { "AUTO GAIN", PillToggle::Style::big };
    PillToggle bypassButton { "BYPASS", PillToggle::Style::small };
    std::unique_ptr<ButtonAttachment> autoAttachment, bypassAttachment;
    PresetBar presetBar { processor.presets };

    PowerButton spacePower, pocketPower, restorePower;
    std::unique_ptr<ButtonAttachment> spacePowerAtt, pocketPowerAtt, restorePowerAtt;

    std::vector<Animated*> animated;
    std::vector<Section> sections;
    juce::Image logo, logoScaled, logoGlow;
    juce::Image backdropLayer, cardsLayer;   // vidées au redimensionnement
    float layersScale = 0.0f;
    juce::Rectangle<float> logoRect, signalRect, overRect, scStatusRect, scMeterTitle;
    std::vector<std::pair<juce::Point<float>, float>> stars;
    double clock = 0.0;
    int overHold = 0, frame = 0;
    float overGlow = 0.0f, scGlow = 0.0f, staticAmt = 0.0f;
    bool restoringSize = true;

    // Couche transparente au-dessus des modules pour les voiles des modules coupés ; laisse passer les clics.
    struct Overlay : juce::Component
    {
        std::function<void (juce::Graphics&)> draw;
        Overlay() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { if (draw) draw (g); }
    } veils;

    std::unique_ptr<onirique::license::Overlay> licenseOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueVocalSpacerAudioProcessorEditor)
};
