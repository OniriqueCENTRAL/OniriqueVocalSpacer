#include "PluginEditor.h"
#include "OniriqueKnob.h"
#include "OniriqueWindowSize.h"
#include <BinaryData.h>
#include <array>
#include <map>

using namespace onirique;

namespace
{
constexpr float kBaseW = 1480.0f, kBaseH = 900.0f;
constexpr int kMinW = 980, kMaxW = 2600;
constexpr float kPi = juce::MathConstants<float>::pi;

juce::Point<float> onCircle (juce::Point<float> c, float r, float ang)
{
    return { c.x + std::sin (ang) * r, c.y - std::cos (ang) * r };
}

// pastille de valeur (boutons, curseurs, champs) : fond sombre, liseré, point braise
void drawValuePill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, float glow, float u)
{
    const float rad = r.getHeight() * 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillRoundedRectangle (r, rad);
    g.setColour (ui::ember.withAlpha (0.14f * glow));
    g.fillRoundedRectangle (r, rad);
    g.setColour (ui::line.interpolatedWith (ui::ember.withAlpha (0.8f), glow));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, std::max (1.0f, 1.2f * u));

    const float dot = r.getHeight() * 0.22f;
    const auto dotR = juce::Rectangle<float> (dot, dot).withCentre ({ r.getX() + rad * 0.95f, r.getCentreY() });
    g.setColour (ui::ember.withAlpha (0.25f + 0.35f * glow));
    g.fillEllipse (dotR.expanded (dot * 0.6f * glow));
    g.setColour (ui::ember.interpolatedWith (ui::bone, 0.35f * glow));
    g.fillEllipse (dotR);

    const auto textR = r.withTrimmedLeft (rad * 1.5f).withTrimmedRight (rad * 0.6f);
    g.setColour (ui::mist);
    g.setFont (ui::fitFont (ui::Face::mono, r.getHeight() * 0.56f, textR.getWidth(), text));
    g.drawText (text, textR, juce::Justification::centred, false);
}

// fond d'afficheur commun (analyseur, éventail stéréo, historique) : creux sombre arrondi + liseré
void drawScreen (juce::Graphics& g, juce::Rectangle<float> area, float u)
{
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, area.getY(),
                                             ui::night.withAlpha (0.85f), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (area, 12.0f * u);
    g.setColour (ui::line);
    g.drawRoundedRectangle (area.reduced (0.5f), 12.0f * u, std::max (1.0f, u));
}
} // namespace

//==============================================================================
juce::Font ui::font (Face f, float h, bool bold, float kerning)
{
    static const char* names[] { "Bahnschrift", "Segoe UI", "Cascadia Mono" };
    juce::Font fnt (juce::FontOptions (names[(int) f], std::max (4.0f, h), bold ? juce::Font::bold : juce::Font::plain));
    if (kerning != 0.0f) fnt.setExtraKerningFactor (kerning);
    return fnt;
}

float ui::editorScale (const juce::Component& c)
{
    if (auto* ed = c.findParentComponentOfClass<juce::AudioProcessorEditor>()) return (float) ed->getWidth() / kBaseW;
    if (auto* ed = dynamic_cast<const juce::AudioProcessorEditor*> (&c)) return (float) ed->getWidth() / kBaseW;
    return 1.0f;
}

float ui::textWidth (const juce::Font& f, const juce::String& t)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, t, 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

juce::Font ui::fitFont (Face face, float maxH, float maxW, const juce::String& text, bool bold, float kerning)
{
    auto f = font (face, maxH, bold, kerning);
    const float w = textWidth (f, text);
    if (w > maxW && w > 0.0f) f.setHeight (std::max (4.0f, f.getHeight() * maxW / w));
    return f;
}

void ui::strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float intensity)
{
    if (intensity <= 0.002f) return;
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (c.withAlpha (juce::jlimit (0.0f, 1.0f, intensity * 0.17f / (float) i)));
        g.strokePath (p, juce::PathStrokeType (width * (1.0f + 1.15f * (float) i), juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }
}

void ui::drawCachedShadow (juce::Graphics& g, ShadowCache& shadowCache, const juce::DropShadow& ds, juce::Rectangle<float> shape, float corner)
{
    // clé : taille et position dans le pixel de la forme, coin, flou, décalage (au 1/64 de pixel près)
    using Key = std::array<int, 8>;
    auto& cache = shadowCache.images;   // thread de l'interface uniquement

    const auto ib = shape.getSmallestIntegerContainer();
    const auto q = [] (float v) { return juce::roundToInt (v * 64.0f); };
    const Key key { q (shape.getWidth()), q (shape.getHeight()), q (shape.getX() - (float) ib.getX()), q (shape.getY() - (float) ib.getY()),
                    corner < 0.0f ? -1 : q (corner), ds.radius, ds.offset.x, ds.offset.y };

    // zone de l'ombre relative au coin de la forme, comme DropShadow::drawForPath (sans le découpage par la zone à redessiner)
    const auto area = (ib.withZeroOrigin() + ds.offset).expanded (ds.radius + 1);
    auto it = cache.find (key);
    if (it == cache.end())
    {
        if (cache.size() > 512) cache.clear();
        // image en mémoire ordinaire : rien qui dépende des ressources graphiques du système (Direct2D, Core Graphics)
        juce::Image pathImage (juce::Image::SingleChannel, area.getWidth(), area.getHeight(), true, juce::SoftwareImageType());
        {
            juce::Graphics g2 (pathImage);
            g2.setColour (juce::Colours::white);
            const auto local = shape.translated ((float) (ds.offset.x - ib.getX() - area.getX()), (float) (ds.offset.y - ib.getY() - area.getY()));
            if (corner < 0.0f) g2.fillEllipse (local);
            else               g2.fillRoundedRectangle (local, corner);
        }
        juce::Image blurred;
        juce::ImageEffects::applySingleChannelBoxBlurEffect (ds.radius, pathImage, blurred);
        it = cache.emplace (key, blurred).first;
    }
    g.setColour (ds.colour);
    g.drawImageAt (it->second, ib.getX() + area.getX(), ib.getY() + area.getY(), true);
}

//==============================================================================
OniriqueLookAndFeel::OniriqueLookAndFeel()
{
    setDefaultSansSerifTypefaceName ("Segoe UI");
    setColour (juce::TooltipWindow::backgroundColourId, ui::deep2);
    setColour (juce::TooltipWindow::textColourId, ui::mist);
    setColour (juce::TooltipWindow::outlineColourId, ui::line);
    setColour (juce::PopupMenu::backgroundColourId, ui::deep2.withAlpha (0.99f));   // non opaque : coins arrondis transparents
    setColour (juce::PopupMenu::textColourId, ui::mist);
    setColour (juce::PopupMenu::headerTextColourId, ui::haze);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ui::ember.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, ui::bone);
}

void OniriqueLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float a0, float a1, juce::Slider& s)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float D = juce::jmin (b.getWidth(), b.getHeight());
    if (D < 8.0f) return;

    const auto c = b.getCentre();
    const float R = D * 0.5f, u = D / 150.0f;
    const auto& props = s.getProperties();
    const float shown = (float) props.getWithDefault ("shown", pos);
    const float glow = (float) props.getWithDefault ("glow", 0.0f);
    const bool bipolar = (bool) props.getWithDefault ("bipolar", false);

    // ---- le potard ONIRIQUE, le même dans tous les plugins de la marque (OniriqueKnob.h) : graduations allumées
    //      jusqu'à la valeur (depuis le centre si bipolaire), corps biseauté, aiguille à l'étoile
    onirique::ui::drawKnob (g, b, shown, a0, a1, ui::ember, bipolar, s.isMouseOverOrDragging() || glow > 0.5f);

    // ---- bornes de l'échelle
    const auto minL = props["minLabel"].toString(), maxL = props["maxLabel"].toString();
    if (minL.isNotEmpty() || maxL.isNotEmpty())
    {
        g.setColour (ui::haze.withAlpha (0.8f));
        for (int side = 0; side < 2; ++side)
        {
            const auto& txt = side == 0 ? minL : maxL;
            const float es = ui::editorScale (s);   // plancher lisible même sur un petit bouton
            const auto f = ui::fitFont (ui::Face::mono, std::max (11.0f * u, 10.0f * es), std::max (0.34f * D, 30.0f * es), txt);
            const float tw = ui::textWidth (f, txt) + 2.0f;
            const auto pt = onCircle (c, R * 0.9f, side == 0 ? a0 : a1);
            const float cx = juce::jlimit (b.getX() + tw * 0.5f, b.getRight() - tw * 0.5f, pt.x);
            g.setFont (f);
            g.drawText (txt, juce::Rectangle<float> (tw, f.getHeight() * 1.3f).withCentre ({ cx, pt.y + 0.17f * R }),
                        juce::Justification::centred, false);
        }
    }
}

//==============================================================================
void OniriqueLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setGradientFill (juce::ColourGradient (ui::deep2, 0.0f, 0.0f, ui::deep, 0.0f, (float) height, false));
    g.fillRoundedRectangle (r, 12.0f);
    g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.10f), (float) width * 0.8f, 0.0f,
                                             ui::ember.withAlpha (0.0f), (float) width * 0.8f, (float) height * 0.6f, true));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r, 12.0f, 1.0f);
}

void OniriqueLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& w, int& h)
{
    if (isSeparator) { w = 50; h = 11; return; }
    w = juce::roundToInt (ui::textWidth (getPopupMenuFont(), text)) + 56;
    h = 30;
}

void OniriqueLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    const auto r = area.toFloat().withTrimmedLeft (16.0f).withTrimmedTop (4.0f);
    const auto f = ui::font (ui::Face::mono, 10.5f, true, 0.18f);
    g.setFont (f);
    g.setColour (ui::haze);
    g.drawText (name, r, juce::Justification::centredLeft, false);
    const float tw = ui::textWidth (f, name);
    g.setColour (ui::line);
    g.fillRect (juce::Rectangle<float> (r.getX() + tw + 10.0f, r.getCentreY(), r.getWidth() - tw - 26.0f, 1.0f));
}

void OniriqueLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                             bool isHighlighted, bool isTicked, bool, const juce::String& text,
                                             const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (ui::line);
        g.fillRect (juce::Rectangle<float> (14.0f, (float) area.getCentreY(), (float) area.getWidth() - 28.0f, 1.0f));
        return;
    }

    const auto r = area.toFloat().reduced (5.0f, 2.0f);
    if (isHighlighted && isActive)
    {
        juce::Path p;
        p.addRoundedRectangle (r, r.getHeight() * 0.5f);
        ui::strokeGlow (g, p, ui::ember, 1.0f, 0.45f);
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.38f), r.getX(), r.getY(),
                                                 ui::blood.withAlpha (0.35f), r.getRight(), r.getBottom(), false));
        g.fillPath (p);
        g.setColour (ui::ember.withAlpha (0.7f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    // élément actif (preset chargé) : point braise lumineux
    const juce::Point<float> dot { r.getX() + 14.0f, r.getCentreY() };
    if (isTicked)
    {
        g.setColour (ui::ember.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (dot));
        g.setColour (ui::ember.interpolatedWith (ui::bone, 0.25f));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
    }

    g.setColour (! isActive ? ui::haze.withAlpha (0.45f)
                            : (isHighlighted || isTicked ? ui::bone : ui::mist.withAlpha (0.88f)));
    g.setFont (getPopupMenuFont().withStyle (isTicked ? juce::Font::bold : juce::Font::plain));
    g.drawText (text, r.withTrimmedLeft (28.0f).withTrimmedRight (10.0f), juce::Justification::centredLeft, true);
}

// curseur vertical : la hauteur du curseur vaut D (même unité que les boutons rotatifs)
int OniriqueLookAndFeel::getSliderThumbRadius (juce::Slider& s)
{
    if (s.getSliderStyle() == juce::Slider::LinearVertical)
        return juce::roundToInt (s.getHeight() / 150.0f * 9.0f);
    if (s.getSliderStyle() == juce::Slider::LinearHorizontal)
        return juce::roundToInt (s.getHeight() * 0.32f);
    return LookAndFeel_V4::getSliderThumbRadius (s);
}

void OniriqueLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style == juce::Slider::LinearHorizontal)
    {
        // curseur horizontal : rail fin, remplissage braise lumineux depuis la gauche, pastille claire
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float u = b.getHeight() / 30.0f, cy = b.getCentreY();
        const float r = (float) getSliderThumbRadius (s);
        const float left = b.getX() + r, right = b.getRight() - r;
        const float shown = (float) s.getProperties().getWithDefault ("shown", s.valueToProportionOfLength (s.getValue()));
        const float glow = (float) s.getProperties().getWithDefault ("glow", 0.0f);
        const float tx = left + shown * (right - left);
        const float trackH = std::max (3.0f, 4.0f * u);

        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (juce::Rectangle<float> (left, cy - trackH * 0.5f, right - left, trackH), trackH * 0.5f);
        if (tx - left > 0.5f)
        {
            juce::Path fill;
            fill.startNewSubPath (left, cy);
            fill.lineTo (tx, cy);
            ui::strokeGlow (g, fill, ui::ember, trackH, 0.5f + 0.5f * glow);
            g.setGradientFill (juce::ColourGradient (ui::ember, left, cy, ui::bone, tx, cy, false));
            g.strokePath (fill, juce::PathStrokeType (trackH, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const auto thumb = juce::Rectangle<float> (1.3f * r, 2.0f * r).withCentre ({ tx, cy });
        g.setColour (ui::ember.withAlpha (0.22f + 0.25f * glow));
        g.fillRoundedRectangle (thumb.expanded (3.0f * u), thumb.getWidth() * 0.5f + 3.0f * u);
        g.setGradientFill (juce::ColourGradient (ui::mist, thumb.getX(), thumb.getY(), juce::Colour (0xffd9c9cb), thumb.getX(), thumb.getBottom(), false));
        g.fillRoundedRectangle (thumb, thumb.getWidth() * 0.5f);
        g.setColour (ui::ember);
        g.drawLine (tx, thumb.getY() + 4.0f * u, tx, thumb.getBottom() - 4.0f * u, std::max (1.2f, 1.8f * u));
        return;
    }

    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }

    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float u = b.getHeight() / 150.0f, cx = b.getCentreX();
    const float thumbR = (float) getSliderThumbRadius (s);
    const float top = b.getY() + thumbR, bottom = b.getBottom() - thumbR;
    const auto& props = s.getProperties();
    const float shown = (float) props.getWithDefault ("shown", s.valueToProportionOfLength (s.getValue()));
    const float glow = (float) props.getWithDefault ("glow", 0.0f);
    const float ty = bottom - shown * (bottom - top);

    // ---- graduations : points discrets, ceux atteints s'allument
    for (int i = 0; i <= 10; ++i)
    {
        const float ti = (float) i / 10.0f;
        const float yy = bottom - ti * (bottom - top);
        const float d = (i % 5 == 0 ? 3.4f : 2.2f) * u;
        g.setColour (ti <= shown + 1.0e-3f ? ui::ember.withAlpha (0.75f) : juce::Colours::white.withAlpha (0.16f));
        for (float dx : { -13.0f * u, 13.0f * u })
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ cx + dx, yy }));
    }

    // ---- rail + remplissage lumineux du bas jusqu'au curseur
    const float trackW = std::max (3.0f, 5.0f * u);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRoundedRectangle (juce::Rectangle<float> (cx - trackW * 0.5f, top, trackW, bottom - top), trackW * 0.5f);
    if (bottom - ty > 0.5f)
    {
        juce::Path fill;
        fill.startNewSubPath (cx, bottom);
        fill.lineTo (cx, ty);
        ui::strokeGlow (g, fill, ui::ember, trackW, 0.5f + 0.5f * glow);
        g.setGradientFill (juce::ColourGradient (ui::ember, cx, bottom, ui::bone, cx, ty, false));
        g.strokePath (fill, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- curseur : pastille claire avec halo braise (comme les curseurs du site)
    const float tw = std::min (b.getWidth() * 0.56f, 34.0f * u), th = 1.5f * thumbR;
    const auto thumb = juce::Rectangle<float> (tw, th).withCentre ({ cx, ty });
    g.setColour (ui::ember.withAlpha (0.22f + 0.25f * glow));
    g.fillRoundedRectangle (thumb.expanded (4.0f * u), th * 0.5f + 4.0f * u);
    juce::Path tp;
    tp.addRoundedRectangle (thumb, th * 0.5f);
    ui::drawCachedShadow (g, shadows, juce::DropShadow (juce::Colours::black.withAlpha (0.5f), juce::roundToInt (8.0f * u), { 0, juce::roundToInt (3.0f * u) }),
                          thumb, th * 0.5f);
    g.setGradientFill (juce::ColourGradient (ui::mist, thumb.getX(), thumb.getY(), juce::Colour (0xffd9c9cb),
                                             thumb.getX(), thumb.getBottom(), false));
    g.fillPath (tp);
    g.setColour (ui::ember);
    g.drawLine (thumb.getX() + 7.0f * u, ty, thumb.getRight() - 7.0f * u, ty, std::max (1.5f, 2.4f * u));
}

//==============================================================================
KnobComponent::KnobComponent (juce::AudioProcessorValueTreeState& apvts, const char* paramId,
                              const juce::String& t, const juce::String& minLabel, const juce::String& maxLabel,
                              Layout l, const juce::String& tooltip, juce::LookAndFeel& lnf, bool bipolar)
    : title (t), layout (l)
{
    slider.setSliderStyle (l == Layout::fader ? juce::Slider::LinearVertical : l == Layout::horizontal ? juce::Slider::LinearHorizontal
                                              : juce::Slider::RotaryHorizontalVerticalDrag);   // glisser vertical OU horizontal
    slider.setRotaryParameters (1.25f * kPi, 2.75f * kPi, true);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setMouseDragSensitivity (220);
    slider.setLookAndFeel (&lnf);
    slider.setTooltip (tooltip);
    auto& props = slider.getProperties();
    props.set ("minLabel", minLabel);
    props.set ("maxLabel", maxLabel);
    props.set ("bipolar", bipolar);
    props.set ("shown", 0.0f);     // à l'ouverture, les arcs se déploient jusqu'à leur valeur
    props.set ("glow", 0.0f);

    if (auto* param = apvts.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));   // double-clic = reset

    slider.onDragStart = [this] { setFocus (hovering, true); };
    slider.onDragEnd   = [this] { setFocus (hovering, false); };
    slider.onValueChange = [this] { repaint (valueArea.getSmallestIntegerContainer().expanded (2)); };
    slider.addMouseListener (this, false);

    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramId, slider);
}

KnobComponent::~KnobComponent()
{
    slider.removeMouseListener (this);
    slider.setLookAndFeel (nullptr);
}

void KnobComponent::setFocus (bool hover, bool drag)
{
    const bool was = hovering || dragging;
    hovering = hover;
    dragging = drag;
    if ((hovering || dragging) != was && onFocusChange) onFocusChange (hovering || dragging);
}

void KnobComponent::mouseEnter (const juce::MouseEvent&) { setFocus (true, dragging); }
void KnobComponent::mouseExit (const juce::MouseEvent&)  { setFocus (false, dragging); }

void KnobComponent::tick()
{
    const float target = (float) slider.valueToProportionOfLength (slider.getValue());
    bool changed = ui::approach (shown, target, 0.30f);
    if (activity) changed |= ui::approach (act, activity() > 0.5f ? 1.0f : 0.0f, 0.3f);
    changed |= ui::approach (glow, dragging ? 1.0f : (hovering ? 0.6f : 0.0f), 0.18f);
    if (changed)
    {
        slider.getProperties().set ("shown", shown);
        slider.getProperties().set ("glow", glow);
        slider.repaint();
        repaint (nameArea.getUnion (valueArea).getSmallestIntegerContainer().expanded (2));
    }
}

void KnobComponent::resized()
{
    const auto area = getLocalBounds().toFloat();
    juce::Rectangle<float> knob;

    if (layout == Layout::horizontal)
    {
        // une seule ligne : nom (36 %) | curseur | pastille de valeur (24 %)
        const float h = area.getHeight();
        unit = h / 30.0f;
        nameArea = area.withWidth (area.getWidth() * 0.36f);
        valueArea = area.withLeft (area.getRight() - area.getWidth() * 0.24f).reduced (0.0f, h * 0.08f);
        knob = area.withLeft (nameArea.getRight() + 4.0f * unit).withRight (valueArea.getX() - 6.0f * unit);
    }
    else if (layout != Layout::textRight)
    {
        // nom + bouton ou curseur (D) + pastille de valeur. Le nom et la pastille ont une taille MINIMALE liée à
        // la fenêtre : sur les petits boutons (multibande, 1176...) les textes restent lisibles, c'est le bouton
        // qui rétrécit un peu pour leur laisser la place.
        const bool fader = (layout == Layout::fader);
        const float es = ui::editorScale (*this);
        float D = std::max (10.0f, juce::jmin (maxD, fader ? area.getHeight() / 1.47f
                                                           : juce::jmin (area.getWidth() - 2.0f, area.getHeight() / 1.47f)));
        const float nameH = std::max (0.15f * D, 17.0f * es), pillH = std::max (0.21f * D, 23.0f * es);
        D = std::max (10.0f, std::min (D, (area.getHeight() - nameH - pillH) / 1.11f));
        unit = D / 150.0f;
        const float total = nameH + 0.05f * D + D + 0.06f * D + pillH;
        const float top = area.getY() + (area.getHeight() - total) * 0.5f;
        nameArea = { area.getX(), top, area.getWidth(), nameH };
        const float knobTop = top + nameH + 0.05f * D;
        knob = fader ? juce::Rectangle<float> (area.getX(), knobTop, area.getWidth(), D)
                     : juce::Rectangle<float> (D, D).withCentre ({ area.getCentreX(), knobTop + D * 0.5f });
        const float pillW = juce::jmin (area.getWidth() * (fader ? 0.98f : 0.92f), std::max (D * 0.95f, 92.0f * es));
        valueArea = juce::Rectangle<float> (pillW, pillH).withCentre ({ area.getCentreX(), knob.getBottom() + 0.06f * D + pillH * 0.5f });
    }
    else
    {
        const float D = std::max (10.0f, juce::jmin (area.getHeight() - 2.0f, area.getWidth() * 0.50f));
        unit = D / 150.0f;
        knob = juce::Rectangle<float> (D, D).withCentre ({ area.getX() + D * 0.5f + 2.0f, area.getCentreY() });
        const float tx = knob.getRight() + 0.12f * D;
        const float tw = std::max (10.0f, area.getRight() - tx - 2.0f);
        const float blockH = 0.17f * D + 0.08f * D + 0.28f * D;
        const float ty = area.getCentreY() - blockH * 0.5f;
        nameArea  = { tx, ty, tw, 0.17f * D };
        valueArea = { tx, ty + 0.25f * D, std::min (tw, D * 1.3f), 0.28f * D };
    }

    slider.setBounds (knob.toNearestInt());
}

void KnobComponent::paint (juce::Graphics& g)
{
    const float u = unit;
    auto nameR = nameArea;
    if (activity)
    {
        // LED d'activité devant le nom (s'allume quand le module agit, ex. plosive détectée)
        const float d = nameR.getHeight() * 0.36f;
        const auto led = juce::Rectangle<float> (d, d).withCentre ({ nameR.getX() + d, nameR.getCentreY() });
        g.setColour (ui::ember.withAlpha (0.45f * act));
        g.fillEllipse (led.expanded (d * 0.8f));
        g.setColour (ui::haze.withAlpha (0.35f).interpolatedWith (ui::ember, act));
        g.fillEllipse (led);
        nameR.removeFromLeft (d * 2.4f);
    }
    g.setColour (ui::haze.interpolatedWith (ui::mist, glow));
    g.setFont (ui::fitFont (ui::Face::body, layout == Layout::horizontal ? nameArea.getHeight() * 0.48f
                                          : std::max ({ 8.0f, (layout == Layout::textRight ? 18.0f : 15.0f) * u, nameArea.getHeight() * 0.72f }),
                            nameR.getWidth() - 2.0f, title, true, 0.14f));
    g.drawText (title, nameR, (layout == Layout::textRight || layout == Layout::horizontal) ? juce::Justification::centredLeft
                                                                                           : juce::Justification::centred, false);

    drawValuePill (g, valueArea, slider.getTextFromValue (slider.getValue()), glow, u);
}

//==============================================================================
DragValue::DragValue (juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& tooltip)
{
    setTooltip (tooltip);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    param = apvts.getParameter (paramId);
    if (param != nullptr)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); });
        attachment->sendInitialUpdate();
    }
}

void DragValue::tick()
{
    if (ui::approach (glow, dragging ? 1.0f : (hovering ? 0.6f : 0.0f), 0.18f)) repaint();
}

void DragValue::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    if (! dragging && onFocusChange) onFocusChange (true);
}

void DragValue::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    if (! dragging && onFocusChange) onFocusChange (false);
}

void DragValue::mouseDown (const juce::MouseEvent&)
{
    if (param == nullptr) return;
    dragging = true;
    startNorm = param->getValue();
    attachment->beginGesture();
}

void DragValue::mouseDrag (const juce::MouseEvent& e)
{
    if (param == nullptr) return;
    const float range = e.mods.isShiftDown() ? 900.0f : 220.0f;   // Maj = réglage fin
    const float norm = juce::jlimit (0.0f, 1.0f, startNorm - (float) e.getDistanceFromDragStartY() / range);
    attachment->setValueAsPartOfGesture (param->convertFrom0to1 (norm));
}

void DragValue::mouseUp (const juce::MouseEvent&)
{
    if (param == nullptr) return;
    dragging = false;
    attachment->endGesture();
    if (! hovering && onFocusChange) onFocusChange (false);
}

void DragValue::mouseDoubleClick (const juce::MouseEvent&)
{
    if (param != nullptr) attachment->setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue()));
}

void DragValue::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (param == nullptr) return;
    const float norm = juce::jlimit (0.0f, 1.0f, param->getValue() + w.deltaY * 0.06f);
    attachment->setValueAsCompleteGesture (param->convertFrom0to1 (norm));
}

void DragValue::paint (juce::Graphics& g)
{
    if (param == nullptr) return;
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float u = r.getHeight() / 30.0f;
    drawValuePill (g, r, prefix + param->getCurrentValueAsText(), glow, u);
}

//==============================================================================
void PowerButton::tick()
{
    if (ui::approach (on, getToggleState() ? 1.0f : 0.0f, 0.2f)) repaint();
}

void PowerButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto b = getLocalBounds().toFloat();
    const float d = std::min (b.getWidth(), b.getHeight()) * 0.72f;
    if (d < 6.0f) return;
    const auto c = b.getCentre();
    const auto disk = juce::Rectangle<float> (d, d).withCentre (c);
    const float u = d / 24.0f;

    juce::Path ring;
    ring.addEllipse (disk);
    ui::strokeGlow (g, ring, ui::ember, 1.4f * u, on * (highlighted ? 1.0f : 0.7f));
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillEllipse (disk);
    g.setColour (ui::ember.withAlpha (0.16f * on));
    g.fillEllipse (disk);
    g.setColour (ui::line.interpolatedWith (ui::ember, on).withMultipliedAlpha (highlighted ? 1.0f : 0.9f));
    g.drawEllipse (disk, std::max (1.0f, 1.4f * u));

    // pictogramme power : arc ouvert en haut + barre verticale, extrémités arrondies
    const float r = 0.26f * d, stroke = std::max (1.3f, 0.09f * d);
    juce::Path p;
    p.addCentredArc (c.x, c.y + 0.02f * d, r, r, 0.0f, 0.23f * kPi, 1.77f * kPi, true);
    p.startNewSubPath (c.x, c.y - 0.32f * d);
    p.lineTo (c.x, c.y - 0.04f * d);
    g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (on, highlighted ? 0.5f : 0.0f)));
    g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
void PillToggle::tick()
{
    if (ui::approach (on, getToggleState() ? 1.0f : 0.0f, 0.16f)) repaint();
}

void PillToggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto b = getLocalBounds().toFloat();
    const bool big = (style == Style::big);
    const float u = big ? std::min (b.getWidth() / 220.0f, b.getHeight() / 100.0f) : b.getHeight() / 36.0f;
    const auto r = b.reduced (big ? 10.0f * u : (style == Style::mini ? 2.0f * u : 6.0f * u));
    const float radius = big ? 18.0f * u : r.getHeight() * 0.5f;

    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    ui::strokeGlow (g, shape, ui::ember, 1.5f * u, on * 0.8f);
    g.setColour (juce::Colours::black.withAlpha (highlighted ? 0.28f : 0.36f));
    g.fillPath (shape);
    if (on > 0.001f)
    {
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.30f * on), r.getX(), r.getY(),
                                                 ui::blood.withAlpha (0.25f * on), r.getRight(), r.getBottom(), false));
        g.fillPath (shape);
    }
    g.setColour (ui::line.interpolatedWith (ui::ember, on));
    g.strokePath (shape, juce::PathStrokeType (std::max (1.0f, 1.4f * u)));

    if (big)
    {
        const bool hasCaption = caption.isNotEmpty() && on > 0.5f;
        auto top = r.reduced (12.0f * u, 8.0f * u);
        auto capArea = hasCaption ? top.removeFromBottom (top.getHeight() * 0.28f) : juce::Rectangle<float>();

        const juce::String main = getToggleState() ? "ON" : "OFF";
        g.setColour (ui::haze.interpolatedWith (ui::bone, on));
        g.setFont (ui::fitFont (ui::Face::display, top.getHeight() * 0.56f, top.getWidth(), main, true, 0.12f));
        g.drawText (main, top, juce::Justification::centred, false);

        if (hasCaption)
        {
            g.setColour (ui::mist.withAlpha (0.8f));
            g.setFont (ui::fitFont (ui::Face::mono, std::max (8.0f, 15.0f * u), capArea.getWidth(), caption));
            g.drawText (caption, capArea, juce::Justification::centred, false);
        }
    }
    else
    {
        const float d = r.getHeight() * 0.26f;
        const float pad = r.getHeight() * 0.40f;
        const auto ledR = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + pad + d * 0.5f, r.getCentreY() });
        g.setColour (ui::ember.withAlpha (0.35f * on));
        g.fillEllipse (ledR.expanded (d * 0.8f));
        g.setColour (ui::haze.withAlpha (0.35f).interpolatedWith (ui::ember, on));
        g.fillEllipse (ledR);

        const auto textR = r.withTrimmedLeft (pad * 2.0f + d).withTrimmedRight (pad * 0.8f);
        g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (on, highlighted ? 0.6f : 0.0f)));
        g.setFont (ui::fitFont (ui::Face::body, r.getHeight() * (style == Style::mini ? 0.52f : 0.40f), textR.getWidth(), getButtonText(), true, 0.10f));
        g.drawText (getButtonText(), textR, juce::Justification::centred, false);
    }
}

//==============================================================================
SegmentSelector::SegmentSelector (juce::AudioProcessorValueTreeState& apvts, const char* paramId,
                                  const juce::StringArray& l, const juce::StringArray& t)
    : labels (l), tips (t)
{
    if (auto* param = apvts.getParameter (paramId))
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
        {
            const int idx = juce::jlimit (0, labels.size() - 1, juce::roundToInt (v));
            if (idx != selected) { selected = idx; repaint(); }
        });
        attachment->sendInitialUpdate();
    }
}

juce::Rectangle<float> SegmentSelector::segmentArea() const noexcept
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    if (title.isNotEmpty())
    {
        const auto f = ui::font (ui::Face::mono, r.getHeight() * 0.40f, true, 0.08f);
        r.removeFromLeft (ui::textWidth (f, title) + r.getHeight() * 0.9f);
    }
    return r;
}

int SegmentSelector::segmentAt (float x) const noexcept
{
    const auto a = segmentArea();
    if (labels.isEmpty() || a.getWidth() <= 0.0f || x < a.getX()) return -1;
    return juce::jlimit (0, labels.size() - 1, (int) ((x - a.getX()) * (float) labels.size() / a.getWidth()));
}

void SegmentSelector::setLocked (bool shouldLock, const juce::String& text, const juce::String& tip)
{
    if (shouldLock == locked && text == lockedText) return;
    locked = shouldLock;
    lockedText = text;
    lockedTip = tip;
    setMouseCursor (locked ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    repaint();
}

void SegmentSelector::tick()
{
    bool changed = false;
    if (slide < 0.0f) { slide = (float) selected; changed = true; }
    changed |= ui::approach (slide, (float) selected, 0.22f);
    changed |= ui::approach (lockAmt, locked ? 1.0f : 0.0f, 0.18f);
    if (changed) repaint();
}

void SegmentSelector::mouseDown (const juce::MouseEvent& e)
{
    if (locked) return;
    const int idx = segmentAt (e.position.x);
    if (idx >= 0 && idx != selected && attachment != nullptr)
        attachment->setValueAsCompleteGesture ((float) idx);
}

void SegmentSelector::mouseMove (const juce::MouseEvent& e)
{
    const int h = locked ? -1 : segmentAt (e.position.x);
    if (h != hover) { hover = h; repaint(); }
}

void SegmentSelector::mouseExit (const juce::MouseEvent&)
{
    if (hover != -1) { hover = -1; repaint(); }
}

juce::String SegmentSelector::getTooltip()
{
    if (locked && lockedTip.isNotEmpty()) return lockedTip;
    const int idx = segmentAt ((float) getMouseXYRelative().x);
    return juce::isPositiveAndBelow (idx, tips.size()) ? tips[idx] : titleTip;
}

void SegmentSelector::paint (juce::Graphics& g)
{
    const int n = labels.size();
    if (n == 0) return;

    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float u = r.getHeight() / 26.0f;
    const float radius = r.getHeight() * 0.5f;
    const auto a = segmentArea();
    const float segW = a.getWidth() / (float) n;
    const float pos = slide < 0.0f ? (float) selected : slide;
    const float dim = 1.0f - 0.65f * lockAmt;   // verrouillé : tout s'estompe

    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillRoundedRectangle (r, radius);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, std::max (1.0f, 1.1f * u));

    if (title.isNotEmpty())
    {
        g.setColour (ui::haze.withMultipliedAlpha (dim));
        g.setFont (ui::font (ui::Face::mono, r.getHeight() * 0.40f, true, 0.08f));
        g.drawText (title, juce::Rectangle<float> (r.getX(), r.getY(), a.getX() - r.getX(), r.getHeight()), juce::Justification::centred, false);
    }

    // pastille braise qui glisse vers le choix
    auto pill = juce::Rectangle<float> (a.getX() + segW * pos, a.getY(), segW, a.getHeight()).reduced (2.5f * u);
    if (pill.getWidth() < pill.getHeight())   // segment étroit (ex. 2X / 4X / 8X) : pastille ronde, jamais un ovale vertical
        pill = pill.withSizeKeepingCentre (pill.getWidth(), pill.getWidth());
    juce::Path pp;
    pp.addRoundedRectangle (pill, pill.getHeight() * 0.5f);
    ui::strokeGlow (g, pp, ui::ember, 1.0f * u, 0.6f * dim);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffEE4555).withMultipliedAlpha (dim), pill.getX(), pill.getY(),
                                             juce::Colour (0xffB81F30).withMultipliedAlpha (dim), pill.getX(), pill.getBottom(), false));
    g.fillPath (pp);

    // une seule taille de texte pour tous les choix, qui tient dans la pastille (ronde si le segment est étroit)
    const bool round = pill.getWidth() <= pill.getHeight() + 0.5f && segW < a.getHeight();
    const float fitW = round ? pill.getWidth() * 0.74f : segW - 6.0f * u;
    auto labelFont = ui::font (ui::Face::body, r.getHeight() * 0.52f, true, 0.06f);
    for (const auto& l : labels)
    {
        const float tw = ui::textWidth (labelFont, l);
        if (tw > fitW && tw > 0.0f) labelFont.setHeight (std::max (4.0f, labelFont.getHeight() * fitW / tw));
    }

    for (int i = 0; i < n; ++i)
    {
        const juce::Rectangle<float> seg (a.getX() + segW * (float) i, a.getY(), segW, a.getHeight());
        const float sel = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (pos - (float) i));
        const auto base = i == hover ? ui::mist : ui::haze;
        g.setColour (base.interpolatedWith (ui::night, sel).withMultipliedAlpha (dim));
        g.setFont (labelFont);
        g.drawText (labels[i], seg, juce::Justification::centred, false);
    }

    // verrouillé : message par-dessus (ex. "MIX MODE ONLY")
    if (lockAmt > 0.01f && lockedText.isNotEmpty())
    {
        const auto f = ui::fitFont (ui::Face::mono, r.getHeight() * 0.42f, a.getWidth() - 10.0f * u, lockedText, true, 0.08f);
        const float tw = ui::textWidth (f, lockedText) + 16.0f * u;
        const auto box = juce::Rectangle<float> (tw, r.getHeight() - 6.0f * u).withCentre (a.getCentre());
        g.setColour (ui::night.withAlpha (0.92f * lockAmt));
        g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
        g.setColour (ui::line.withMultipliedAlpha (2.0f * lockAmt));
        g.drawRoundedRectangle (box, box.getHeight() * 0.5f, std::max (1.0f, u));
        g.setColour (ui::mist.withAlpha (lockAmt));
        g.setFont (f);
        g.drawText (lockedText, box, juce::Justification::centred, false);
    }
}

//==============================================================================
PresetBar::PresetBar (onirique::PresetManager& p) : pm (p)
{
    addChildComponent (editor);
    editor.setJustification (juce::Justification::centred);
    editor.setColour (juce::TextEditor::backgroundColourId, ui::night.withAlpha (0.9f));
    editor.setColour (juce::TextEditor::textColourId, ui::mist);
    editor.setColour (juce::TextEditor::outlineColourId, ui::ember);
    editor.setColour (juce::TextEditor::focusedOutlineColourId, ui::ember);
    editor.setColour (juce::TextEditor::highlightColourId, ui::ember.withAlpha (0.4f));
    editor.setColour (juce::CaretComponent::caretColourId, ui::bone);
    editor.setTextToShowWhenEmpty ("Preset name", ui::haze);
    editor.onReturnKey = [this] { endSave (true); };
    editor.onEscapeKey = [this] { endSave (false); };
    editor.onFocusLost = [this] { endSave (false); };
    shownName = pm.getCurrentName();
}

void PresetBar::resized()
{
    const auto r = getLocalBounds().toFloat();
    const float h = r.getHeight();
    auto rest = r;
    zones[save] = rest.removeFromRight (h * 2.1f);
    rest.removeFromRight (h * 0.2f);
    zones[prev] = rest.removeFromLeft (h);
    zones[next] = rest.removeFromRight (h);
    zones[name] = rest;
    editor.setBounds (zones[name].reduced (2.0f, h * 0.14f).toNearestInt());
    editor.setFont (ui::font (ui::Face::body, h * 0.40f, true));
}

PresetBar::Zone PresetBar::zoneAt (juce::Point<float> p) const noexcept
{
    for (int z = 0; z < 4; ++z) if (zones[z].contains (p)) return (Zone) z;
    return none;
}

void PresetBar::tick()
{
    bool changed = false;
    for (int z = 0; z < 4; ++z) changed |= ui::approach (glow[z], hover == z ? 1.0f : 0.0f, 0.2f);
    if ((++frame % 10) == 0)
    {
        const auto n = pm.getCurrentName();
        const bool mod = pm.isModified();
        if (n != shownName || mod != modified) { shownName = n; modified = mod; changed = true; }
    }
    if (changed) repaint();
}

void PresetBar::mouseMove (const juce::MouseEvent& e)
{
    const auto z = zoneAt (e.position);
    if (z != hover) { hover = z; repaint(); }
}

void PresetBar::mouseExit (const juce::MouseEvent&) { hover = none; }

void PresetBar::mouseDown (const juce::MouseEvent& e)
{
    switch (zoneAt (e.position))
    {
        case prev: pm.step (-1); break;
        case next: pm.step (1); break;
        case name: showMenu(); break;
        case save: startSave(); break;
        default: break;
    }
    shownName = pm.getCurrentName();
    modified = pm.isModified();
    repaint();
}

void PresetBar::showMenu()
{
    const auto entries = pm.list();
    const auto current = pm.getCurrentName();
    juce::PopupMenu menu;
    bool userHeader = false;
    menu.addSectionHeader ("FACTORY");
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        if (! e.factory && ! userHeader) { menu.addSectionHeader ("USER"); userHeader = true; }
        menu.addItem (i + 1, e.name, true, e.name == current);
    }
    menu.addSeparator();
    menu.addItem (1000, "Save as new preset...");
    bool currentIsUser = false;
    for (const auto& e : entries) if (! e.factory && e.name == current) currentIsUser = true;
    menu.addItem (1001, "Delete \"" + current + "\"", currentIsUser);
    menu.addItem (1002, "Open presets folder");

    menu.setLookAndFeel (&getLookAndFeel());   // sinon JUCE juge la fenêtre opaque avec le style système (coins blancs)
    juce::Component::SafePointer<PresetBar> self (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth())
                            .withParentComponent (findParentComponentOfClass<juce::AudioProcessorEditor>()),
                        [self, entries] (int r)
                        {
                            if (self == nullptr || r == 0) return;
                            auto& bar = *self;
                            if (r >= 1 && r <= (int) entries.size()) bar.pm.load (entries[(size_t) (r - 1)]);
                            else if (r == 1000) bar.startSave();
                            else if (r == 1001)
                            {
                                for (const auto& e : entries)
                                    if (! e.factory && e.name == bar.pm.getCurrentName()) bar.pm.remove (e);
                                bar.pm.load (entries.front());   // retour sur Init
                            }
                            else if (r == 1002) onirique::PresetManager::folder().startAsProcess();
                            bar.repaint();
                        });
}

void PresetBar::startSave()
{
    editor.setText (shownName == "Init" ? juce::String() : shownName, false);
    editor.setVisible (true);
    editor.selectAll();
    editor.grabKeyboardFocus();
    repaint();
}

void PresetBar::endSave (bool commit)
{
    if (! editor.isVisible()) return;
    const auto text = editor.getText();
    editor.setVisible (false);
    if (commit) pm.save (text);
    shownName = pm.getCurrentName();
    modified = pm.isModified();
    repaint();
}

void PresetBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 40.0f;

    // conteneur : précédent | nom | suivant
    const auto box = zones[prev].getUnion (zones[next]).reduced (0.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.6f * glow[name]));
    g.drawRoundedRectangle (box.reduced (0.5f), box.getHeight() * 0.5f, std::max (1.0f, 1.2f * u));

    for (int z : { (int) prev, (int) next })
    {
        const auto c = zones[z].getCentre();
        const float d = (z == prev ? -1.0f : 1.0f) * 4.0f * u;
        juce::Path chev;
        chev.startNewSubPath (c.x - d, c.y - 6.0f * u);
        chev.lineTo (c.x + d, c.y);
        chev.lineTo (c.x - d, c.y + 6.0f * u);
        g.setColour (ui::haze.interpolatedWith (ui::bone, glow[z]));
        g.strokePath (chev, juce::PathStrokeType (std::max (1.3f, 2.0f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (! editor.isVisible())
    {
        auto nr = zones[name].reduced (4.0f * u, 0.0f);
        g.setColour (ui::haze.withAlpha (0.8f));
        g.setFont (ui::font (ui::Face::mono, 9.0f * u, false, 0.1f));
        g.drawText ("PRESET", nr.removeFromTop (h * 0.42f).withTrimmedTop (h * 0.10f), juce::Justification::centredBottom, false);
        const juce::String txt = shownName + (modified ? " *" : "");
        g.setColour (ui::mist.interpolatedWith (ui::bone, glow[name]));
        g.setFont (ui::fitFont (ui::Face::body, h * 0.36f, nr.getWidth() - 18.0f * u, txt, true, 0.04f));
        g.drawText (txt, nr.withTrimmedBottom (h * 0.08f), juce::Justification::centredTop, false);
        // petit triangle : la liste s'ouvre au clic
        const float tx = zones[name].getRight() - 8.0f * u, ty = zones[name].getCentreY();
        juce::Path tri;
        tri.addTriangle (tx - 4.0f * u, ty - 2.0f * u, tx + 4.0f * u, ty - 2.0f * u, tx, ty + 3.0f * u);
        g.setColour (ui::haze.interpolatedWith (ui::ember, glow[name]));
        g.fillPath (tri);
    }

    // SAVE
    const auto s = zones[save].reduced (0.0f, 1.0f);
    juce::Path sp;
    sp.addRoundedRectangle (s, s.getHeight() * 0.5f);
    ui::strokeGlow (g, sp, ui::ember, 1.2f * u, glow[save] * 0.8f);
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillPath (sp);
    g.setColour (ui::ember.withAlpha (0.18f * glow[save]));
    g.fillPath (sp);
    g.setColour (ui::line.interpolatedWith (ui::ember, glow[save]));
    g.strokePath (sp, juce::PathStrokeType (std::max (1.0f, 1.2f * u)));
    g.setColour (ui::haze.interpolatedWith (ui::bone, glow[save]));
    g.setFont (ui::fitFont (ui::Face::body, h * 0.36f, s.getWidth() - 10.0f * u, "SAVE", true, 0.16f));
    g.drawText ("SAVE", s, juce::Justification::centred, false);
}


//==============================================================================
void LevelMeter::tick()
{
    float peakNow = -100.0f;
    for (int c = 0; c < 2; ++c)
    {
        const float pk = consume ? consume (c) : 0.0f;
        const float db = pk > 1.0e-5f ? 20.0f * std::log10 (pk) : -100.0f;
        peakNow = std::max (peakNow, db);
        levelDb[c] = db > levelDb[c] ? db : std::max (-100.0f, levelDb[c] - 0.33f);   // retombée ~20 dB/s
        if (db >= holdDb[c]) { holdDb[c] = db; holdFrames[c] = 90; }                // maintien 1.5 s
        else if (--holdFrames[c] <= 0) holdDb[c] = std::max (levelDb[c], holdDb[c] - 0.5f);
    }
    if (peakNow >= maxDb) { maxDb = peakNow; maxFrames = 180; }                      // valeur affichée : 3 s
    else if (--maxFrames <= 0) maxDb = std::max (holdDb[0], holdDb[1]);
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float u = area.getWidth() / 46.0f;
    auto r = area;
    const auto readout = r.removeFromBottom (18.0f * u);
    r.removeFromBottom (4.0f * u);
    const auto scale = r.removeFromRight (18.0f * u);
    const float barW = (r.getWidth() - 3.0f * u) * 0.5f;
    auto yOf = [&] (float db) { return r.getBottom() - norm (db) * r.getHeight(); };

    // graduations
    g.setFont (ui::font (ui::Face::mono, std::max (8.0f * u, 9.0f * ui::editorScale (*this))));
    for (int db : { 0, -6, -12, -24, -48 })
    {
        const float y = yOf ((float) db);
        g.setColour (db == 0 ? ui::ember.withAlpha (0.8f) : ui::haze.withAlpha (0.75f));
        g.fillRect (juce::Rectangle<float> (scale.getX() + 1.0f * u, y, 3.0f * u, 1.0f));
        if (db != -6)   // -6 : simple repère, sinon les chiffres du haut se chevauchent
            g.drawText (juce::String (db), juce::Rectangle<float> (scale.getX() + 4.0f * u, y - 6.0f * u, scale.getWidth() - 4.0f * u, 12.0f * u),
                    juce::Justification::centredLeft, false);
    }

    for (int c = 0; c < 2; ++c)
    {
        const auto bar = juce::Rectangle<float> (r.getX() + (float) c * (barW + 3.0f * u), r.getY(), barW, r.getHeight());
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (bar, 2.5f * u);
        g.setColour (ui::line);
        g.drawRoundedRectangle (bar.reduced (0.5f), 2.5f * u, 1.0f);

        // remplissage : os -> braise près de 0 dBFS -> rouge au-delà
        const float top = yOf (levelDb[c]);
        if (top < bar.getBottom() - 0.5f)
        {
            juce::ColourGradient cg (ui::bone.withAlpha (0.30f), 0.0f, bar.getBottom(), juce::Colour (0xffff2a3a), 0.0f, yOf (2.0f), false);
            cg.addColour ((double) norm (-12.0f), ui::bone.withAlpha (0.85f));   // positions = niveau normalisé (bas = -60, haut = +2)
            cg.addColour ((double) norm (-3.0f), ui::ember);
            g.setGradientFill (cg);
            g.fillRoundedRectangle (bar.withTop (top).reduced (1.0f, 0.0f), 2.0f * u);
        }
        // maintien de crête
        if (holdDb[c] > -60.0f)
        {
            g.setColour (holdDb[c] > 0.0f ? juce::Colour (0xffff2a3a) : ui::bone);
            g.fillRect (juce::Rectangle<float> (bar.getX() + 1.0f, yOf (holdDb[c]) - 1.0f * u, bar.getWidth() - 2.0f, 2.0f * u));
        }
    }

    // valeur crête (3 s) en clair
    const juce::String txt = maxDb <= -99.0f ? juce::String ("-inf") : (maxDb > 0.0f ? "+" : "") + juce::String (maxDb, 1);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (readout, readout.getHeight() * 0.5f);
    g.setColour (maxDb > 0.0f ? juce::Colour (0xffff2a3a) : ui::line);
    g.drawRoundedRectangle (readout.reduced (0.5f), readout.getHeight() * 0.5f, 1.0f);
    g.setColour (maxDb > 0.0f ? ui::bone : ui::mist);
    g.setFont (ui::fitFont (ui::Face::mono, readout.getHeight() * 0.62f, readout.getWidth() - 6.0f * u, txt, true));
    g.drawText (txt, readout, juce::Justification::centred, false);
}


//==============================================================================
PocketDisplay::PocketDisplay (OniriqueVocalSpacerAudioProcessor& p) : processor (p)
{
    window.resize ((size_t) kN);
    double sum = 0.0;
    for (int i = 0; i < kN; ++i)
    {
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (6.283185307179586 * i / kN));
        sum += window[(size_t) i];
    }
    windowGain = (float) (2.0 / sum);          // un sinus d'amplitude A lit 20 log10 A (dBFS)
    for (auto* s : { &specIn, &specOut, &specVoc })
    {
        s->hist.assign ((size_t) kN, 0.0f);
        s->db.fill (-140.0f);
    }
    scratch.assign ((size_t) SpectrumTap::kSize, 0.0f);
    fftBuf.assign ((size_t) (2 * kN), 0.0f);
    curve.fill (0.0f);
    loP = processor.apvts.getParameter (ids::rangeLo);
    hiP = processor.apvts.getParameter (ids::rangeHi);
}

void PocketDisplay::analyse (SpectrumTap& tap, Spectrum& s)
{
    // récupère tout ce qui est arrivé depuis la dernière image, garde les kN derniers échantillons
    const int got = tap.pull (scratch.data(), (int) scratch.size());
    if (got == 0)
    {
        // plus rien n'arrive depuis ~0.3 s (sidechain débranché, lecture arrêtée) : le spectre retombe
        if (++s.idle > 10) std::fill (s.hist.begin(), s.hist.end(), 0.0f);
    }
    else
        s.idle = 0;
    for (int i = std::max (0, got - kN); i < got; ++i)
    {
        s.hist[(size_t) s.write] = scratch[(size_t) i];
        s.write = (s.write + 1) & (kN - 1);
    }

    for (int i = 0; i < kN; ++i)
        fftBuf[(size_t) i] = s.hist[(size_t) ((s.write + i) & (kN - 1))] * window[(size_t) i];
    std::fill (fftBuf.begin() + kN, fftBuf.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftBuf.data(), true);

    const double fs = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    const double binHz = fs / kN;
    for (int i = 0; i < kPts; ++i)
    {
        const double f0 = hzAt (std::max (0, i - 1)) * 0.5 + hzAt (i) * 0.5;
        const double f1 = hzAt (std::min (kPts - 1, i + 1)) * 0.5 + hzAt (i) * 0.5;
        int b0 = (int) std::ceil (f0 / binHz), b1 = (int) std::floor (f1 / binHz);
        if (b1 < b0) b0 = b1 = (int) std::lround (hzAt (i) / binHz);
        b0 = juce::jlimit (1, kN / 2, b0);
        b1 = juce::jlimit (1, kN / 2, b1);
        float mag = 0.0f;
        for (int b = b0; b <= b1; ++b) mag = std::max (mag, fftBuf[(size_t) b]);
        // pente de +3 dB/oct (autour de 1 kHz), comme sur l'analyseur de VOCALBOX
        const float db = 20.0f * std::log10 (mag * windowGain + 1.0e-9f) + 3.0f * std::log2 (hzAt (i) / 1000.0f);
        auto& d = s.db[(size_t) i];
        d = std::max (db, d - 1.2f);
    }
}

void PocketDisplay::tick()
{
    bool moved = false;
    for (int h = 0; h < 2; ++h) moved |= ui::approach (handleGlow[h], (h == hover || h == drag) ? 1.0f : 0.0f, 0.2f);
    if ((++frame & 1) != 0) { if (moved) repaint(); return; }   // spectres à 30 images/s

    analyse (processor.inputTap, specIn);
    analyse (processor.outputTap, specOut);
    analyse (processor.vocalTap, specVoc);

    const auto& m = processor.getPocketMeter();
    const bool isStatic = juce::roundToInt (processor.apvts.getRawParameterValue (ids::pocketMode)->load()) == 1;
    ui::approach (staticAmt, isStatic ? 1.0f : 0.0f, 0.2f);
    ui::approach (vocAmt, ! isStatic && processor.isSidechainConnected() ? 1.0f : 0.0f, 0.15f);
    ui::approach (noScAmt, ! isStatic && ! m.scActive.load (std::memory_order_relaxed) ? 1.0f : 0.0f, 0.08f);
    ui::approach (activeAmt, active ? 1.0f : 0.0f, 0.2f);
    ui::approach (lo, loP->convertFrom0to1 (loP->getValue()), 0.35f);
    ui::approach (hi, hiP->convertFrom0to1 (hiP->getValue()), 0.35f);

    for (int b = 0; b < kB; ++b)
    {
        const float target = active ? m.gr[(size_t) b].load (std::memory_order_relaxed) : 0.0f;
        gr[(size_t) b] += 0.5f * (target - gr[(size_t) b]);
    }

    // courbe EXACTE de la poche : les mêmes cloches que l'audio (Mid)
    const double fs = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    const float norm = processor.getPocketNorm();
    BiquadCoeffs c[kB];
    bool use[kB];
    for (int b = 0; b < kB; ++b)
    {
        use[b] = gr[(size_t) b] > 0.01f;
        if (use[b]) c[b] = VocalPocket::bell (fs, b, -gr[(size_t) b] * norm);
    }
    for (int i = 0; i < kPts; ++i)
    {
        const double f = std::min ((double) hzAt (i), 0.49 * fs);
        std::complex<double> h (1.0, 0.0);
        for (int b = 0; b < kB; ++b) if (use[b]) h *= c[b].response (fs, f);
        curve[(size_t) i] = (float) (20.0 * std::log10 (std::max (1.0e-9, std::abs (h))));
    }
    repaint();
}

juce::Rectangle<float> PocketDisplay::plotArea() const noexcept
{
    const float u = ui::editorScale (*this);
    return getLocalBounds().toFloat().reduced (14.0f * u, 10.0f * u).withTrimmedTop (22.0f * u)
                                     .withTrimmedBottom (16.0f * u).withTrimmedRight (34.0f * u);
}

float PocketDisplay::xOf (float hz) const noexcept
{
    const auto r = plotArea();
    return r.getX() + r.getWidth() * std::log (std::max (hz, 20.0f) / 20.0f) / std::log (1000.0f);
}

float PocketDisplay::hzOf (float x) const noexcept
{
    const auto r = plotArea();
    return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth()));
}

// réduction : 0 dB en haut du tracé, -15 dB à 55 % de sa hauteur (rideau qui descend)
float PocketDisplay::yGr (float db) const noexcept
{
    const auto r = plotArea();
    return r.getY() + juce::jlimit (0.0f, 1.0f, -db / kGrRange) * r.getHeight() * 0.55f;
}

int PocketDisplay::handleAt (juce::Point<float> p) const noexcept
{
    const auto r = plotArea();
    const float u = ui::editorScale (*this);
    if (p.y < r.getY() - 6.0f * u || p.y > r.getBottom() + 16.0f * u) return -1;
    const float dLo = std::abs (p.x - xOf (lo)), dHi = std::abs (p.x - xOf (hi));
    const float tol = 9.0f * u;
    if (dLo > tol && dHi > tol) return -1;
    return dLo <= dHi ? 0 : 1;
}

void PocketDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = handleAt (e.position);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }
}

void PocketDisplay::mouseExit (const juce::MouseEvent&)
{
    if (drag < 0) hover = -1;
}

void PocketDisplay::mouseDown (const juce::MouseEvent& e)
{
    drag = handleAt (e.position);
    if (drag >= 0) handleParam (drag)->beginChangeGesture();
}

void PocketDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0) return;
    auto* p = handleParam (drag);
    const float loNow = loP->convertFrom0to1 (loP->getValue()), hiNow = hiP->convertFrom0to1 (hiP->getValue());
    float hz = hzOf (e.position.x);
    hz = drag == 0 ? juce::jlimit (80.0f, std::min (2000.0f, hiNow / 1.25f), hz)
                   : juce::jlimit (std::max (1000.0f, loNow * 1.25f), 16000.0f, hz);
    p->setValueNotifyingHost (p->convertTo0to1 (hz));
}

void PocketDisplay::mouseUp (const juce::MouseEvent& e)
{
    if (drag >= 0) handleParam (drag)->endChangeGesture();
    drag = -1;
    hover = handleAt (e.position);
}

void PocketDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = handleAt (e.position);
    if (h < 0) return;
    auto* p = handleParam (h);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->getDefaultValue());
    p->endChangeGesture();
}

juce::String PocketDisplay::getTooltip()
{
    if (hover == 0) return "RANGE LO: drag to set the lowest frequency the pocket may cut. Double-click to reset.";
    if (hover == 1) return "RANGE HI: drag to set the highest frequency the pocket may cut. Double-click to reset.";
    return "Vocal pocket: red curtain = what is cut from the instrumental right now (exact filter curve). "
           "Pink = the vocal (sidechain), white = instrumental output, grey = instrumental input. "
           "Drag the LO / HI handles to limit the zone.";
}

void PocketDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float u = ui::editorScale (*this);
    drawScreen (g, area, u);

    const auto plot = plotArea();
    auto ySpec = [&] (float db) { return plot.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 88.0f) * plot.getHeight(); };

    // ---- grille : fréquences en bas, échelle de réduction à droite
    g.setFont (ui::font (ui::Face::mono, 10.0f * u));
    for (auto [hz, txt] : { std::pair { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" },
                            { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" } })
    {
        const float x = xOf (hz);
        g.setColour (ui::line.withMultipliedAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> (x, plot.getY(), 1.0f, plot.getHeight()));
        g.setColour (ui::haze.withAlpha (0.8f));
        g.drawText (txt, juce::Rectangle<float> (x + 4.0f * u, plot.getBottom() + 3.0f * u, 40.0f * u, 12.0f * u),
                    juce::Justification::centredLeft, false);
    }
    for (float db : { 0.0f, -3.0f, -6.0f, -9.0f, -12.0f })
    {
        const float y = yGr (db);
        g.setColour (db == 0.0f ? ui::line.withMultipliedAlpha (1.7f) : ui::line.withMultipliedAlpha (0.45f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), y, plot.getWidth(), 1.0f));
        if (std::fmod (-db, 6.0f) < 0.01f)
        {
            g.setColour (ui::haze.withAlpha (0.6f));
            g.drawText (db == 0.0f ? juce::String ("0 dB") : juce::String ((int) db),
                        juce::Rectangle<float> (plot.getRight() + 4.0f * u, y - 6.0f * u, 32.0f * u, 12.0f * u),
                        juce::Justification::centredLeft, false);
        }
    }

    auto specPath = [&] (const std::array<float, kPts>& s, bool closed)
    {
        juce::Path p;
        if (closed) p.startNewSubPath (plot.getX(), plot.getBottom());
        for (int i = 0; i < kPts; ++i)
        {
            const float x = xOf (hzAt (i)), y = ySpec (s[(size_t) i]);
            if (i == 0 && ! closed) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        if (closed) { p.lineTo (plot.getRight(), plot.getBottom()); p.closeSubPath(); }
        return p;
    };

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (plot.toNearestInt());

        // ---- zone RANGE : légère teinte braise dedans, assombrie dehors
        const float xl = xOf (lo), xh = xOf (hi);
        g.setColour (ui::ember.withAlpha (0.045f * activeAmt));
        g.fillRect (juce::Rectangle<float> (xl, plot.getY(), xh - xl, plot.getHeight()));

        // ---- spectres : entrée (trait discret), sortie (dégradé os + trait), voix (rose lumineux)
        g.setColour (ui::haze.withAlpha (0.45f));
        g.strokePath (specPath (specIn.db, false), juce::PathStrokeType (std::max (1.0f, 1.0f * u)));
        g.setGradientFill (juce::ColourGradient (ui::bone.withAlpha (0.36f), 0.0f, plot.getY(), ui::bone.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
        g.fillPath (specPath (specOut.db, true));
        g.setColour (ui::bone.withAlpha (0.75f));
        g.strokePath (specPath (specOut.db, false), juce::PathStrokeType (std::max (1.0f, 1.2f * u)));

        const float va = vocAmt * (1.0f - staticAmt);
        if (va > 0.01f)
        {
            const auto vp = specPath (specVoc.db, false);
            ui::strokeGlow (g, vp, ui::rose, 1.6f * u, 0.55f * va);
            g.setColour (ui::rose.withAlpha (0.9f * va));
            g.strokePath (vp, juce::PathStrokeType (std::max (1.2f, 1.6f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour (ui::night.withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getY(), xl - plot.getX(), plot.getHeight()));
        g.fillRect (juce::Rectangle<float> (xh, plot.getY(), plot.getRight() - xh, plot.getHeight()));

        // ---- la poche : rideau braise qui descend depuis 0 dB, trait lumineux
        juce::Path pocket;
        for (int i = 0; i < kPts; ++i)
        {
            const float x = xOf (hzAt (i)), y = yGr (curve[(size_t) i]);
            if (i == 0) pocket.startNewSubPath (x, y); else pocket.lineTo (x, y);
        }
        juce::Path fill (pocket);
        fill.lineTo (plot.getRight(), yGr (0.0f));
        fill.lineTo (plot.getX(), yGr (0.0f));
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.05f), 0.0f, yGr (0.0f),
                                                 ui::ember.withAlpha (0.42f), 0.0f, yGr (-kGrRange), false));
        g.fillPath (fill);
        ui::strokeGlow (g, pocket, ui::ember, 2.5f * u, 0.35f + 0.65f * activeAmt);
        g.setColour (ui::ember.withAlpha (0.45f + 0.55f * activeAmt));
        g.strokePath (pocket, juce::PathStrokeType (std::max (1.5f, 2.5f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- poignées RANGE LO / HI : trait pointillé + pastille de valeur en bas
    for (int h = 0; h < 2; ++h)
    {
        const float x = xOf (h == 0 ? lo : hi);
        const float gl = handleGlow[h];
        const float dash[] { 4.0f * u, 3.0f * u };
        g.setColour (ui::haze.withAlpha (0.55f).interpolatedWith (ui::ember, 0.4f + 0.6f * gl));
        g.drawDashedLine ({ x, plot.getY(), x, plot.getBottom() }, dash, 2, std::max (1.0f, (1.0f + gl) * u));
        const juce::String txt = (h == 0 ? "LO " : "HI ") + hzText (h == 0 ? lo : hi);
        const auto f = ui::font (ui::Face::mono, 10.5f * u, true);
        const float tw = ui::textWidth (f, txt) + 14.0f * u;
        auto box = juce::Rectangle<float> (tw, 17.0f * u).withCentre ({ x, plot.getBottom() - 14.0f * u });
        box = box.withX (juce::jlimit (plot.getX(), plot.getRight() - tw, box.getX()));
        juce::Path bp;
        bp.addRoundedRectangle (box, box.getHeight() * 0.5f);
        ui::strokeGlow (g, bp, ui::ember, 1.0f * u, 0.6f * gl);
        g.setColour (ui::night.withAlpha (0.9f));
        g.fillPath (bp);
        g.setColour (ui::line.interpolatedWith (ui::ember, 0.3f + 0.7f * gl));
        g.strokePath (bp, juce::PathStrokeType (std::max (1.0f, u)));
        g.setColour (ui::mist.interpolatedWith (ui::bone, gl));
        g.setFont (f);
        g.drawText (txt, box, juce::Justification::centred, false);
    }

    // ---- légende + réduction maximale
    {
        float x = plot.getX();
        const float y = area.getY() + 9.0f * u;
        g.setFont (ui::font (ui::Face::mono, 10.0f * u));
        for (auto [c, txt] : { std::pair { ui::ember, "POCKET" }, { ui::rose, "VOCAL (SC)" }, { ui::bone, "OUTPUT" }, { ui::haze, "INPUT" } })
        {
            g.setColour (c);
            g.fillRoundedRectangle (juce::Rectangle<float> (x, y + 4.5f * u, 14.0f * u, 3.0f * u), 1.5f * u);
            g.setColour (ui::haze);
            const float tw = ui::textWidth (g.getCurrentFont(), txt) + 4.0f * u;
            g.drawText (txt, juce::Rectangle<float> (x + 20.0f * u, y, tw, 12.0f * u), juce::Justification::centredLeft, false);
            x += 20.0f * u + tw + 16.0f * u;
        }

        float mx = 0.0f;
        for (float v : curve) mx = std::min (mx, v);
        const juce::String txt = (staticAmt > 0.5f ? "STATIC  " : "MAX  ") + (mx > -0.05f ? juce::String ("0.0 dB") : juce::String (mx, 1) + " dB");
        const auto f = ui::font (ui::Face::mono, 10.5f * u, true);
        const float tw = ui::textWidth (f, txt) + 16.0f * u;
        const auto box = juce::Rectangle<float> (tw, 17.0f * u).withRightX (plot.getRight()).withY (y - 2.5f * u);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
        g.setColour (mx < -0.5f ? ui::ember.withAlpha (0.8f) : ui::line);
        g.drawRoundedRectangle (box.reduced (0.5f), box.getHeight() * 0.5f, 1.0f);
        g.setColour (ui::mist);
        g.setFont (f);
        g.drawText (txt, box, juce::Justification::centred, false);
    }

    // ---- mode SIDECHAIN sans voix : message au centre (la poche reste fermée, l'instrumental est intact)
    if (noScAmt > 0.01f)
    {
        const bool connected = processor.isSidechainConnected();
        const juce::String l1 = connected ? "WAITING FOR THE VOICE" : "NO SIDECHAIN CONNECTED";
        const juce::String l2 = connected ? "the pocket opens as soon as the vocal plays"
                                          : "route the vocal track to the sidechain input, or switch to STATIC";
        const auto f1 = ui::font (ui::Face::display, 15.0f * u, true, 0.08f);
        const auto f2 = ui::font (ui::Face::mono, 10.5f * u);
        const float w = std::max (ui::textWidth (f1, l1), ui::textWidth (f2, l2)) + 40.0f * u;
        const auto box = juce::Rectangle<float> (w, 50.0f * u).withCentre ({ plot.getCentreX(), plot.getY() + plot.getHeight() * 0.38f });
        const float a = noScAmt * (connected ? 0.75f : 1.0f);
        g.setColour (ui::night.withAlpha (0.85f * a));
        g.fillRoundedRectangle (box, 12.0f * u);
        g.setColour (ui::line.withMultipliedAlpha (2.0f * a));
        g.drawRoundedRectangle (box, 12.0f * u, std::max (1.0f, u));
        g.setColour (ui::mist.withAlpha (a));
        g.setFont (f1);
        g.drawText (l1, box.withTrimmedBottom (box.getHeight() * 0.45f).withTrimmedTop (5.0f * u), juce::Justification::centred, false);
        g.setColour (ui::haze.withAlpha (a));
        g.setFont (f2);
        g.drawText (l2, box.withTrimmedTop (box.getHeight() * 0.52f).withTrimmedBottom (6.0f * u), juce::Justification::centred, false);
    }
}

//==============================================================================
StereoFieldDisplay::StereoFieldDisplay (OniriqueVocalSpacerAudioProcessor& p) : processor (p)
{
    setTooltip ("Stereo image of the instrumental: inner arc = lows, middle = mids, outer = highs. "
                "The voice sits in the centre. The two glowing dots show the width actually measured at the output.");
}

void StereoFieldDisplay::tick()
{
    auto v = [this] (const char* id) { return processor.apvts.getRawParameterValue (id)->load(); };
    bool changed = false;
    changed |= ui::approach (width, v (ids::width) / 100.0f, 0.25f);
    changed |= ui::approach (focus, v (ids::lowFocus) / 100.0f, 0.25f);
    changed |= ui::approach (detail, v (ids::sideDetail) / 100.0f, 0.25f);
    changed |= ui::approach (on, v (ids::spaceOn) > 0.5f && v (ids::bypass) < 0.5f ? 1.0f : 0.0f, 0.2f);
    changed |= ui::approach (measured, juce::jlimit (0.0f, 3.0f, processor.getWidthRatio()), 0.12f);
    if (changed) repaint();
}

void StereoFieldDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float u = ui::editorScale (*this);
    drawScreen (g, area, u);

    // éventail elliptique (plus large que haut) : occupe tout l'afficheur
    const float Ry = area.getHeight() - 26.0f * u;
    const float Rx = std::min (area.getWidth() * 0.40f, Ry * 1.9f);
    const juce::Point<float> c { area.getCentreX(), area.getBottom() - 15.0f * u };
    const float halfPi = 0.5f * kPi;
    auto onEllipse = [c] (float rx, float ry, float ang) { return juce::Point<float> { c.x + std::sin (ang) * rx, c.y - std::cos (ang) * ry }; };

    // demi-ellipse de référence + axe central
    juce::Path base;
    base.addCentredArc (c.x, c.y, Rx, Ry, 0.0f, -halfPi, halfPi, true);
    g.setColour (ui::line);
    g.strokePath (base, juce::PathStrokeType (std::max (1.0f, u)));
    g.fillRect (juce::Rectangle<float> (area.getX() + 14.0f * u, c.y, area.getWidth() - 28.0f * u, 1.0f));
    const float dash[] { 3.0f * u, 4.0f * u };
    g.setColour (ui::line.withMultipliedAlpha (1.4f));
    g.drawDashedLine ({ c.x, c.y, c.x, c.y - Ry }, dash, 2, std::max (1.0f, u));

    // éventail : la largeur de chaque zone suit les réglages (ce que SPACE fait au signal)
    const float base45 = 0.25f * kPi * width;   // 100 % = +/-45 degrés
    const float spreads[3] { base45 * (1.0f - 0.9f * focus), base45, std::min (halfPi, base45 * (1.0f + 0.35f * detail)) };
    const float radii[3] { 0.40f, 0.65f, 0.90f };
    const char* names[3] { "LOW", "MID", "HIGH" };
    g.setFont (ui::font (ui::Face::mono, 8.5f * u, true, 0.06f));
    for (int k = 0; k < 3; ++k)
    {
        const float rx = Rx * radii[k], ry = Ry * radii[k], sp = std::max (0.015f, spreads[k]);
        const float w = (k == 2 ? 6.0f * (1.0f + 0.6f * detail) : 6.0f) * u;
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, rx, ry, 0.0f, -sp, sp, true);
        ui::strokeGlow (g, arc, ui::ember, w, (0.35f + 0.45f * on));
        g.setGradientFill (juce::ColourGradient (ui::bone.withAlpha (0.55f + 0.45f * on), c.x, c.y - ry,
                                                 ui::ember.withAlpha (0.55f + 0.45f * on), c.x + rx, c.y, true));
        g.strokePath (arc, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // nom de la zone sous la ligne de base, à gauche, à l'aplomb de son arc
        g.setColour (ui::haze.withAlpha (0.75f));
        g.drawText (names[k], juce::Rectangle<float> (36.0f * u, 11.0f * u).withCentre ({ c.x - rx, c.y + 7.0f * u }),
                    juce::Justification::centred, false);
    }

    // la voix, au centre
    {
        const auto vc = juce::Point<float> { c.x, c.y - Ry * 0.16f };
        const float d = 8.0f * u;
        g.setColour (ui::rose.withAlpha (0.3f));
        g.fillEllipse (juce::Rectangle<float> (d * 2.4f, d * 2.4f).withCentre (vc));
        g.setColour (ui::rose);
        g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (vc));
        g.setColour (ui::mist.withAlpha (0.85f));
        g.setFont (ui::font (ui::Face::mono, 8.5f * u, true, 0.1f));
        g.drawText ("VOICE", juce::Rectangle<float> (40.0f * u, 11.0f * u).withCentre ({ vc.x, c.y + 7.0f * u }), juce::Justification::centred, false);
    }

    // largeur mesurée en sortie : S / M = 1 -> 45 degrés
    {
        const float ang = std::min (halfPi, std::atan (measured));
        for (float side : { -1.0f, 1.0f })
        {
            const auto pt = onEllipse (Rx, Ry, side * ang);
            g.setColour (ui::bone.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (14.0f * u, 14.0f * u).withCentre (pt));
            g.setColour (ui::bone);
            g.fillEllipse (juce::Rectangle<float> (5.0f * u, 5.0f * u).withCentre (pt));
        }
    }

    g.setColour (ui::haze);
    g.setFont (ui::font (ui::Face::body, 11.0f * u, true, 0.1f));
    g.drawText ("L", juce::Rectangle<float> (20.0f * u, 14.0f * u).withCentre ({ c.x - Rx - 13.0f * u, c.y - 6.0f * u }), juce::Justification::centred, false);
    g.drawText ("R", juce::Rectangle<float> (20.0f * u, 14.0f * u).withCentre ({ c.x + Rx + 13.0f * u, c.y - 6.0f * u }), juce::Justification::centred, false);
}

//==============================================================================
RestoreDisplay::RestoreDisplay (OniriqueVocalSpacerAudioProcessor& p) : processor (p)
{
    setTooltip ("RESTORE activity over the last seconds: red bars = kick hits reinforced by KICK FOCUS, "
                "white curve = energy added in the mids by MID ENERGY.");
}

void RestoreDisplay::tick()
{
    if ((++frame & 1) != 0) return;   // 30 relevés par seconde, ~5 s d'historique
    const bool on = processor.apvts.getRawParameterValue (ids::restoreOn)->load() > 0.5f
                    && processor.apvts.getRawParameterValue (ids::bypass)->load() < 0.5f;
    const float k = processor.consumeKickBoostDb();
    kick[(size_t) write] = on ? k : 0.0f;
    energy[(size_t) write] = on ? processor.getEnergyDb() : 0.0f;
    write = (write + 1) % kHist;
    repaint();
}

void RestoreDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float u = ui::editorScale (*this);
    drawScreen (g, area, u);

    const auto plot = area.reduced (12.0f * u, 8.0f * u).withTrimmedTop (16.0f * u).withTrimmedRight (30.0f * u);
    constexpr float range = 8.0f;
    auto yOf = [&] (float db) { return plot.getBottom() - juce::jlimit (0.0f, 1.0f, db / range) * plot.getHeight(); };

    g.setFont (ui::font (ui::Face::mono, 9.5f * u));
    for (float db : { 0.0f, 3.0f, 6.0f })
    {
        const float y = yOf (db);
        g.setColour (db == 0.0f ? ui::line.withMultipliedAlpha (1.6f) : ui::line.withMultipliedAlpha (0.5f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), y, plot.getWidth(), 1.0f));
        g.setColour (ui::haze.withAlpha (0.6f));
        g.drawText ("+" + juce::String ((int) db), juce::Rectangle<float> (plot.getRight() + 4.0f * u, y - 6.0f * u, 26.0f * u, 12.0f * u),
                    juce::Justification::centredLeft, false);
    }

    const float step = plot.getWidth() / (float) (kHist - 1);
    // kick : barres braise
    for (int i = 0; i < kHist; ++i)
    {
        const float v = kick[(size_t) ((write + i) % kHist)];
        if (v < 0.05f) continue;
        const float x = plot.getX() + step * (float) i;
        const auto bar = juce::Rectangle<float> (x - step * 0.35f, yOf (v), step * 0.7f, plot.getBottom() - yOf (v));
        g.setGradientFill (juce::ColourGradient (ui::bone, 0.0f, bar.getY(), ui::ember.withAlpha (0.6f), 0.0f, plot.getBottom(), false));
        g.fillRect (bar);
    }
    // énergie : courbe os + remplissage léger
    juce::Path e;
    for (int i = 0; i < kHist; ++i)
    {
        const float x = plot.getX() + step * (float) i, y = yOf (energy[(size_t) ((write + i) % kHist)]);
        if (i == 0) e.startNewSubPath (x, y); else e.lineTo (x, y);
    }
    juce::Path fill (e);
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.lineTo (plot.getX(), plot.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (ui::bone.withAlpha (0.22f), 0.0f, plot.getY(), ui::bone.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
    g.fillPath (fill);
    g.setColour (ui::bone.withAlpha (0.85f));
    g.strokePath (e, juce::PathStrokeType (std::max (1.0f, 1.4f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    float x = plot.getX();
    g.setFont (ui::font (ui::Face::mono, 9.5f * u));
    for (auto [c, txt] : { std::pair { ui::ember, "KICK" }, { ui::bone, "ENERGY" } })
    {
        g.setColour (c);
        g.fillRoundedRectangle (juce::Rectangle<float> (x, area.getY() + 11.0f * u, 12.0f * u, 3.0f * u), 1.5f * u);
        g.setColour (ui::haze);
        const float tw = ui::textWidth (g.getCurrentFont(), txt) + 4.0f * u;
        g.drawText (txt, juce::Rectangle<float> (x + 17.0f * u, area.getY() + 6.0f * u, tw, 12.0f * u), juce::Justification::centredLeft, false);
        x += 17.0f * u + tw + 14.0f * u;
    }
}

//==============================================================================
OniriqueVocalSpacerAudioProcessorEditor::OniriqueVocalSpacerAudioProcessorEditor (OniriqueVocalSpacerAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    tooltipWindow.setLookAndFeel (&lnf);
    setLookAndFeel (&lnf);   // menus (presets) et champs de texte dans le style du plugin
    setOpaque (true);

    for (auto* k : { &widthKnob, &focusKnob, &detailKnob, &depthKnob, &sensKnob, &kickKnob, &energyKnob, &inKnob, &outKnob })
    {
        addAndMakeVisible (k);
        animated.push_back (k);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &field, &pocketDisplay, &modeSel, &targetSel, &speedSel, &rangeLoV, &rangeHiV,
                                                             &scMeter, &listenButton, &restoreDisplay, &inMeter, &outMeter,
                                                             &autoButton, &bypassButton, &presetBar })
        addAndMakeVisible (c);
    for (auto* a : std::initializer_list<Animated*> { &field, &pocketDisplay, &modeSel, &targetSel, &speedSel, &rangeLoV, &rangeHiV,
                                                      &scMeter, &listenButton, &restoreDisplay, &inMeter, &outMeter,
                                                      &autoButton, &bypassButton, &presetBar })
        animated.push_back (a);

    rangeLoV.setPrefix ("LO ");
    rangeHiV.setPrefix ("HI ");
    kickKnob.activity   = [this] { return restoreDisplay.lastKick(); };
    energyKnob.activity = [this] { return restoreDisplay.lastEnergy() * 2.0f; };

    // boutons power : un par module, actifs par défaut
    struct PowerDef { PowerButton& b; std::unique_ptr<ButtonAttachment>& att; const char* id; const char* name; };
    for (auto& d : { PowerDef { spacePower, spacePowerAtt, ids::spaceOn, "SPACE" },
                     PowerDef { pocketPower, pocketPowerAtt, ids::pocketOn, "VOCAL POCKET" },
                     PowerDef { restorePower, restorePowerAtt, ids::restoreOn, "RESTORE" } })
    {
        addAndMakeVisible (d.b);
        animated.push_back (&d.b);
        d.b.setTooltip (juce::String ("Turn ") + d.name + " on/off (click-free).");
        d.att = std::make_unique<ButtonAttachment> (processor.apvts, d.id, d.b);
    }

    autoButton.setTooltip ("Auto Gain: keeps the perceived loudness the same before and after the processing, "
                           "so you judge the space made for the voice, not a volume change.");
    bypassButton.setTooltip ("Bypass the whole plugin (click-free).");
    listenButton.setTooltip ("Listen: hear only what the pocket removes from the instrumental "
                             "(it should sound like a thin ghost of the vocal range, never like a whole instrument).");
    autoAttachment   = std::make_unique<ButtonAttachment> (processor.apvts, ids::autoGain, autoButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (processor.apvts, ids::bypass, bypassButton);
    listenAttachment = std::make_unique<ButtonAttachment> (processor.apvts, ids::pocketListen, listenButton);

    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);

    // redimensionnable, proportions verrouillées. La taille mémorisée est lue AVANT setResizeLimits(),
    // qui déclenche un resized() à la taille minimale.
    // taille d'ouverture : la règle commune des plugins ONIRIQUE (OniriqueWindowSize.h)
    const int w = onirique::window::openingSize (processor.apvts.state, "ONIRIQUE VOCAL SPACER",
                                                 { 1280, juce::roundToInt (1280.0f * kBaseH / kBaseW) },
                                                 { kMinW, juce::roundToInt ((float) kMinW * kBaseH / kBaseW) },
                                                 { kMaxW, juce::roundToInt ((float) kMaxW * kBaseH / kBaseW) }).x;
    setResizable (true, true);
    setResizeLimits (kMinW, juce::roundToInt (kMinW * kBaseH / kBaseW), kMaxW, juce::roundToInt (kMaxW * kBaseH / kBaseW));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) (kBaseW / kBaseH));
    setSize (w, juce::roundToInt ((float) w * kBaseH / kBaseW));
    restoringSize = false;
    veils.draw = [this] (juce::Graphics& g) { paintVeils (g); };
    addAndMakeVisible (veils);   // ajoutée en dernier : au-dessus des modules, sous les menus ouverts ensuite
    onirique::window::remember (processor.apvts.state, getWidth(), getHeight());

    // Licence : pastille et page d'activation, par-dessus tout le reste.
    licenseOverlay = std::make_unique<onirique::license::Overlay> ("ONIRIQUE VOCAL SPACER", processor.license);
    addAndMakeVisible (*licenseOverlay);
    resized();

    startTimerHz (60);
}

OniriqueVocalSpacerAudioProcessorEditor::~OniriqueVocalSpacerAudioProcessorEditor()
{
    stopTimer();
    onirique::window::rememberOnThisComputer ("ONIRIQUE VOCAL SPACER", getWidth(), getHeight());
    tooltipWindow.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

float OniriqueVocalSpacerAudioProcessorEditor::scale() const noexcept { return (float) getWidth() / kBaseW; }

void OniriqueVocalSpacerAudioProcessorEditor::timerCallback()
{
    clock += 1.0 / 60.0;
    ++frame;

    const bool bypassed = processor.apvts.getRawParameterValue (ids::bypass)->load() > 0.5f;
    pocketDisplay.setActive (pocketPower.getToggleState() && ! bypassed);

    for (auto* a : animated) a->tick();

    // voile des modules coupés (fondu), aussi quand l'automation les change
    for (auto& s : sections)
        if (s.power != nullptr && ui::approach (s.veil, s.power->getToggleState() ? 0.0f : 1.0f, 0.14f))
            repaint (s.bounds.getSmallestIntegerContainer());

    // mode STATIC : SENSITIVITY et SPEED n'ont plus d'objet (pas de sidechain), le mètre SC s'estompe
    const bool isStatic = juce::roundToInt (processor.apvts.getRawParameterValue (ids::pocketMode)->load()) == 1;
    if (ui::approach (staticAmt, isStatic ? 1.0f : 0.0f, 0.18f))
    {
        sensKnob.setAlpha (1.0f - 0.6f * staticAmt);
        scMeter.setAlpha (1.0f - 0.6f * staticAmt);
        repaint (scMeterTitle.getSmallestIntegerContainer());
    }
    sensKnob.setEnabled (! isStatic);
    speedSel.setLocked (isStatic, "SIDECHAIN ONLY", "Speed only applies in SIDECHAIN mode: the STATIC pocket does not move.");

    // témoin du sidechain (en-tête de la carte VOCAL POCKET)
    const bool scActive = ! isStatic && processor.getPocketMeter().scActive.load (std::memory_order_relaxed);
    ui::approach (scGlow, scActive ? 1.0f : 0.0f, 0.15f);
    if ((frame & 3) == 0) repaint (scStatusRect.expanded (6.0f).getSmallestIntegerContainer());

    // témoin OVER : sortie au-dessus de 0 dBFS, maintenu ~1.5 s, s'éteint en fondu
    if (processor.consumeOutputPeak() > 1.0f) overHold = 90;
    else if (overHold > 0) --overHold;
    if (ui::approach (overGlow, overHold > 0 ? 1.0f : 0.0f, overHold > 0 ? 0.5f : 0.08f))
        repaint (overRect.getSmallestIntegerContainer().expanded (8));

    // correction d'Auto Gain affichée sous ON
    juce::String cap;
    if (autoButton.getToggleState())
    {
        const float db = processor.getAutoGainDb();
        cap = juce::String (std::abs (db) < 0.05f ? 0.0f : db, 1);
        if (db > 0.05f) cap = "+" + cap;
        cap << " dB";
    }
    autoButton.setCaption (cap);

    // en-tête vivant : lueur du logo, signal qui parcourt la ligne, étoiles qui scintillent
    repaint (logoRect.expanded (logoRect.getHeight() * 0.4f).getSmallestIntegerContainer());
    repaint (signalRect.getSmallestIntegerContainer());
    if ((frame & 1) == 0)
        for (const auto& st : stars)
            repaint (juce::Rectangle<float> (st.second * 2.4f, st.second * 2.4f).withCentre (st.first).getSmallestIntegerContainer());
}

void OniriqueVocalSpacerAudioProcessorEditor::resized()
{
    backdropLayer = {};   // fond et cartes redessinés à la nouvelle taille au prochain paint()

    const float u = scale();
    const float W = (float) getWidth(), H = (float) getHeight();
    const float m = 22.0f * u, gap = 16.0f * u;

    // ---- en-tête : presets + BYPASS à droite
    logoRect = { 22.0f * u, 10.0f * u, 64.0f * u * 985.0f / 895.0f, 64.0f * u };
    signalRect = { 0.0f, 80.0f * u, W, 12.0f * u };
    bypassButton.setBounds (juce::Rectangle<float> (136.0f * u, 40.0f * u).withRightX (W - m).withY (22.0f * u).toNearestInt());
    // La pastille de licence : un bouton de l'en-tête, entre les presets et BYPASS, à leur hauteur ; sa place suit son texte.
    const float badgeW = licenseOverlay != nullptr ? (float) licenseOverlay->pillWidth() : 0.0f;
    const float badgeRight = (float) bypassButton.getX() / u - 10.0f;
    if (licenseOverlay != nullptr)
        licenseOverlay->fit (getLocalBounds(), u, juce::Rectangle<float> (badgeRight - badgeW, 22.0f, badgeW, 40.0f).toNearestInt());
    presetBar.setBounds (juce::Rectangle<float> (400.0f * u, 40.0f * u).withRightX ((badgeRight - badgeW - 12.0f) * u).withY (22.0f * u).toNearestInt());

    if (logo.isValid())
    {
        // logo pré-réduit (net) + version floutée pour la lueur, recalculés seulement au redimensionnement
        const int lh = juce::jlimit (16, 600, juce::roundToInt (logoRect.getHeight() * 2.0f));
        logoScaled = logo.rescaled (juce::roundToInt ((float) lh * 985.0f / 895.0f), lh, juce::Graphics::highResamplingQuality);
        const int pad = lh / 4;
        juce::Image src (juce::Image::ARGB, logoScaled.getWidth() + 2 * pad, logoScaled.getHeight() + 2 * pad, true);
        { juce::Graphics lg (src); lg.drawImageAt (logoScaled, pad, pad); }
        logoGlow = juce::Image (juce::Image::ARGB, src.getWidth(), src.getHeight(), true);
        juce::ImageConvolutionKernel k (15);
        k.createGaussianBlur ((float) lh / 22.0f);
        k.applyToImage (logoGlow, src, src.getBounds());
    }

    stars.clear();
    juce::Random rnd (11);
    for (int i = 0; i < 14; ++i)
    {
        const float x = W * 0.40f + ((float) presetBar.getX() - 16.0f * u - W * 0.40f) * rnd.nextFloat();
        const float y = (8.0f + 66.0f * rnd.nextFloat()) * u;
        stars.push_back ({ { x, y }, (2.5f + 4.5f * rnd.nextFloat() * rnd.nextFloat()) * u });
    }

    // ---- cartes : 0 VOCAL POCKET (rangée du haut, pleine largeur), puis 1 SPACE, 2 RESTORE, 3 INPUT, 4 AUTO GAIN, 5 OUTPUT
    const float topY = 96.0f * u, topH = 410.0f * u;
    const float botY = topY + topH + gap, botH = H - 20.0f * u - botY;
    const float inW = 196.0f * u, agW = 172.0f * u, outW = 196.0f * u, restW = 352.0f * u;
    const float spaceW = W - 2.0f * m - inW - agW - outW - restW - 4.0f * gap;

    const std::vector<Section> old = sections;
    sections.clear();
    float x = m;
    sections.push_back ({ "VOCAL POCKET", { m, topY, W - 2.0f * m, topH }, &pocketPower });
    sections.push_back ({ "SPACE",        { x, botY, spaceW, botH }, &spacePower });  x += spaceW + gap;
    sections.push_back ({ "RESTORE",      { x, botY, restW, botH }, &restorePower }); x += restW + gap;
    sections.push_back ({ "INPUT",        { x, botY, inW, botH } });                  x += inW + gap;
    sections.push_back ({ "AUTO GAIN",    { x, botY, agW, botH } });                  x += agW + gap;
    sections.push_back ({ "OUTPUT",       { x, botY, outW, botH } });

    for (auto& s : sections)
    {
        for (const auto& o : old) if (o.title == s.title) s.veil = o.veil;
        if (s.power == nullptr) continue;
        if (old.empty()) s.veil = s.power->getToggleState() ? 0.0f : 1.0f;
        s.power->setBounds (juce::Rectangle<float> (32.0f * u, 32.0f * u)
                                .withCentre ({ s.bounds.getRight() - 24.0f * u, s.bounds.getY() + 19.0f * u }).toNearestInt());
    }

    auto inner = [u] (juce::Rectangle<float> r) { return r.withTrimmedTop (38.0f * u).withTrimmedBottom (12.0f * u).reduced (12.0f * u, 0.0f); };
    auto place = [] (juce::Component& c, juce::Rectangle<float> r) { c.setBounds (r.toNearestInt()); };
    const float D = 128.0f * u;   // diamètre commun des boutons de la rangée du bas

    // ---- VOCAL POCKET : afficheur | mètre SC | réglages (mode, cible + vitesse, DEPTH / SENSITIVITY, RANGE)
    {
        const auto card = sections[0].bounds;
        auto pc = inner (card);
        auto ctrl = pc.removeFromRight (392.0f * u);
        pc.removeFromRight (14.0f * u);
        auto scCol = pc.removeFromRight (46.0f * u);
        pc.removeFromRight (14.0f * u);
        scMeterTitle = scCol.removeFromTop (18.0f * u);
        scCol.removeFromTop (4.0f * u);
        place (scMeter, scCol);
        place (pocketDisplay, pc);

        place (modeSel, ctrl.removeFromTop (28.0f * u));
        ctrl.removeFromTop (10.0f * u);
        auto row = ctrl.removeFromTop (28.0f * u);
        place (targetSel, row.removeFromLeft (row.getWidth() * 0.40f));
        row.removeFromLeft (8.0f * u);
        place (speedSel, row);
        ctrl.removeFromTop (10.0f * u);
        auto range = ctrl.removeFromBottom (26.0f * u);
        ctrl.removeFromBottom (10.0f * u);
        const float half = (range.getWidth() - 8.0f * u) * 0.5f;
        place (rangeLoV, range.removeFromLeft (half));
        range.removeFromLeft (8.0f * u);
        place (rangeHiV, range);
        depthKnob.setMaxDiameter (150.0f * u);
        sensKnob.setMaxDiameter (150.0f * u);
        place (depthKnob, ctrl.removeFromLeft (ctrl.getWidth() * 0.5f));
        place (sensKnob, ctrl);

        listenButton.setBounds (juce::Rectangle<float> (96.0f * u, 24.0f * u).withRightX (card.getRight() - 46.0f * u)
                                    .withY (card.getY() + 7.0f * u).toNearestInt());
        scStatusRect = juce::Rectangle<float> (214.0f * u, 24.0f * u).withRightX ((float) listenButton.getX() - 10.0f * u)
                           .withY (card.getY() + 7.0f * u);
    }

    // ---- SPACE : éventail stéréo, puis WIDTH | LOW FOCUS | SIDE DETAIL
    {
        auto sp = inner (sections[1].bounds);
        place (field, sp.removeFromTop (118.0f * u));
        sp.removeFromTop (8.0f * u);
        KnobComponent* k[] { &widthKnob, &focusKnob, &detailKnob };
        const float cw = sp.getWidth() / 3.0f;
        for (int i = 0; i < 3; ++i)
        {
            k[i]->setMaxDiameter (D);
            place (*k[i], { sp.getX() + cw * (float) i, sp.getY(), cw, sp.getHeight() });
        }
    }

    // ---- RESTORE : historique, puis KICK FOCUS | MID ENERGY
    {
        auto rs = inner (sections[2].bounds);
        place (restoreDisplay, rs.removeFromTop (118.0f * u));
        rs.removeFromTop (8.0f * u);
        kickKnob.setMaxDiameter (D);
        energyKnob.setMaxDiameter (D);
        place (kickKnob, rs.removeFromLeft (rs.getWidth() * 0.5f));
        place (energyKnob, rs);
    }

    // ---- INPUT / OUTPUT : bouton + mètre vertical stéréo ; AUTO GAIN au milieu
    {
        auto ia = inner (sections[3].bounds), oa = inner (sections[5].bounds);
        place (inMeter,  ia.removeFromRight (46.0f * u));
        place (outMeter, oa.removeFromRight (46.0f * u));
        ia.removeFromRight (6.0f * u);
        oa.removeFromRight (6.0f * u);
        inKnob.setMaxDiameter (D);
        outKnob.setMaxDiameter (D);
        place (inKnob, ia);
        place (outKnob, oa);
        const auto ag = inner (sections[4].bounds);
        place (autoButton, ag.withSizeKeepingCentre (ag.getWidth(), std::min (ag.getHeight(), ag.getWidth() * 0.62f)));
    }

    const auto& o = sections[5].bounds;
    overRect = { o.getRight() - 84.0f * u, o.getY() + 10.0f * u, 64.0f * u, 18.0f * u };

    if (! restoringSize)
        onirique::window::remember (processor.apvts.state, getWidth(), getHeight());   // taille mémorisée avec le projet
    veils.setBounds (getLocalBounds());
}

void OniriqueVocalSpacerAudioProcessorEditor::paintStar (juce::Graphics& g, juce::Point<float> c, float r, float alpha)
{
    // étoile à 4 branches, reprise du logo
    juce::Path s;
    const float k = 0.16f * r;
    s.startNewSubPath (c.x, c.y - r);
    s.lineTo (c.x + k, c.y - k); s.lineTo (c.x + r, c.y); s.lineTo (c.x + k, c.y + k);
    s.lineTo (c.x, c.y + r);     s.lineTo (c.x - k, c.y + k); s.lineTo (c.x - r, c.y); s.lineTo (c.x - k, c.y - k);
    s.closeSubPath();
    g.setColour (ui::ember.withAlpha (0.25f * alpha));
    g.fillEllipse (juce::Rectangle<float> (r * 1.1f, r * 1.1f).withCentre (c));
    g.setColour (ui::mist.withAlpha (alpha));
    g.fillPath (s);
}

void OniriqueVocalSpacerAudioProcessorEditor::paintHeader (juce::Graphics& g)
{
    const float u = scale();
    const float W = (float) getWidth();
    const float t = (float) clock;

    for (size_t i = 0; i < stars.size(); ++i)
    {
        const float a = 0.15f + 0.75f * (0.5f + 0.5f * std::sin (t * (0.9f + 0.37f * (float) (i % 5)) + (float) i * 1.7f));
        paintStar (g, stars[i].first, stars[i].second, a);
    }

    // logo : lueur braise qui respire + sigil net
    if (logoScaled.isValid())
    {
        const float breathe = 0.5f + 0.5f * std::sin (t * 2.0f * kPi / 5.0f);
        const float pad = logoRect.getHeight() / 4.0f;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setColour (ui::ember.withAlpha (0.55f + 0.35f * breathe));
        g.drawImage (logoGlow, logoRect.expanded (pad), juce::RectanglePlacement::stretchToFit, true);
        g.setColour (juce::Colours::white.withAlpha (0.08f + 0.10f * breathe));
        g.drawImage (logoGlow, logoRect.expanded (pad), juce::RectanglePlacement::stretchToFit, true);
        g.setOpacity (1.0f);
        g.drawImage (logoScaled, logoRect, juce::RectanglePlacement::stretchToFit);
    }

    // titre + chaîne de traitement
    {
        const float x = logoRect.getRight() + 14.0f * u;
        auto f1 = ui::font (ui::Face::display, 30.0f * u, true, 0.10f);
        auto f2 = ui::font (ui::Face::display, 30.0f * u, false, 0.10f);
        const float w1 = ui::textWidth (f1, "ONIRIQUE") + 2.0f;
        const float w2 = ui::textWidth (f2, "VOCAL SPACER") + 2.0f;
        const float y = 12.0f * u, hh = 36.0f * u;
        g.setFont (f1);
        g.setColour (ui::ember.withAlpha (0.35f));
        g.drawText ("ONIRIQUE", juce::Rectangle<float> (x, y + 1.0f * u, w1, hh), juce::Justification::centredLeft, false);
        g.setColour (ui::mist);
        g.drawText ("ONIRIQUE", juce::Rectangle<float> (x, y, w1, hh), juce::Justification::centredLeft, false);
        g.setFont (f2);
        g.setColour (ui::haze);
        g.drawText ("VOCAL SPACER", juce::Rectangle<float> (x + w1 + 10.0f * u, y, w2, hh), juce::Justification::centredLeft, false);

        g.setFont (ui::font (ui::Face::mono, 11.0f * u, false, 0.12f));
        g.setColour (ui::haze.withAlpha (0.85f));
        g.drawText ("SPACE  >  VOCAL POCKET  >  RESTORE   /   ROOM FOR THE VOICE",
                    juce::Rectangle<float> (x + 2.0f * u, 50.0f * u, 520.0f * u, 18.0f * u), juce::Justification::centredLeft, false);
    }

    // ligne de signal : filet qui s'estompe aux bords + impulsion lumineuse qui la parcourt
    {
        const float ly = 86.0f * u;
        juce::ColourGradient cg (juce::Colours::transparentBlack, 22.0f * u, ly, juce::Colours::transparentBlack, W - 22.0f * u, ly, false);
        cg.addColour (0.08, ui::line);
        cg.addColour (0.92, ui::line);
        g.setGradientFill (cg);
        g.fillRect (juce::Rectangle<float> (22.0f * u, ly, W - 44.0f * u, std::max (1.0f, u)));

        const float len = 130.0f * u;
        const float span = W - 44.0f * u + len;
        const float px = 22.0f * u - len + std::fmod (t * 0.24f, 1.0f) * span;
        const auto streak = juce::Rectangle<float> (px, ly - 1.5f * u, len, 3.5f * u);
        juce::ColourGradient sg (ui::bone.withAlpha (0.0f), streak.getX(), ly, ui::ember, streak.getRight(), ly, false);
        sg.addColour (0.6, ui::bone);
        g.reduceClipRegion (juce::Rectangle<float> (22.0f * u, signalRect.getY(), W - 44.0f * u, signalRect.getHeight()).toNearestInt());
        g.setColour (ui::ember.withAlpha (0.25f));
        g.fillRoundedRectangle (streak.expanded (0.0f, 2.5f * u).withTrimmedLeft (len * 0.5f), 3.0f * u);
        g.setGradientFill (sg);
        g.fillRoundedRectangle (streak, streak.getHeight() * 0.5f);
    }
}

// témoin du sidechain dans l'en-tête de la carte VOCAL POCKET
void OniriqueVocalSpacerAudioProcessorEditor::paintScStatus (juce::Graphics& g)
{
    const float u = scale();
    const auto r = scStatusRect;
    const bool isStatic = staticAmt > 0.5f;
    const bool connected = processor.isSidechainConnected();
    const juce::String txt = isStatic ? "STATIC MODE" : (scGlow > 0.5f ? "VOCAL DETECTED" : (connected ? "SIDECHAIN: NO SIGNAL" : "NO SIDECHAIN"));
    const float pulse = scGlow * (0.75f + 0.25f * std::sin ((float) clock * 5.0f));

    juce::Path p;
    p.addRoundedRectangle (r, r.getHeight() * 0.5f);
    ui::strokeGlow (g, p, ui::rose, 1.0f * u, 0.6f * scGlow);
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillPath (p);
    g.setColour (ui::line.interpolatedWith (ui::rose, 0.7f * scGlow));
    g.strokePath (p, juce::PathStrokeType (std::max (1.0f, u)));

    const float d = r.getHeight() * 0.30f;
    const auto led = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + r.getHeight() * 0.55f, r.getCentreY() });
    g.setColour (ui::rose.withAlpha (0.45f * pulse));
    g.fillEllipse (led.expanded (d * 0.8f));
    g.setColour (isStatic ? ui::haze.withAlpha (0.6f) : ui::haze.withAlpha (connected ? 0.55f : 0.3f).interpolatedWith (ui::rose, scGlow));
    g.fillEllipse (led);

    const auto tr = r.withTrimmedLeft (r.getHeight() * 1.05f).withTrimmedRight (r.getHeight() * 0.4f);
    g.setColour (ui::haze.interpolatedWith (ui::bone, scGlow));
    g.setFont (ui::fitFont (ui::Face::mono, r.getHeight() * 0.46f, tr.getWidth(), txt, true, 0.06f));
    g.drawText (txt, tr, juce::Justification::centredLeft, false);
}

void OniriqueVocalSpacerAudioProcessorEditor::paint (juce::Graphics& g)
{
    const float u = scale();

    // fond et cartes : images prêtes, à la résolution réelle de l'écran (Retina : x2)
    const float ps = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! backdropLayer.isValid() || std::abs (ps - layersScale) > 1.0e-3f)
        rebuildLayers (ps);
    const auto toLogical = juce::AffineTransform::scale (1.0f / layersScale);
    g.setOpacity (1.0f);
    g.drawImageTransformed (backdropLayer, toLogical);

    {
        juce::Graphics::ScopedSaveState ss (g);
        paintHeader (g);
    }

    g.setOpacity (1.0f);
    g.drawImageTransformed (cardsLayer, toLogical);

    // ---- point braise des cartes : suit le bouton power du module
    for (const auto& s : sections)
    {
        const auto r = s.bounds;
        const bool on = s.power == nullptr || s.power->getToggleState();
        const float dot = 6.0f * u;
        const auto dotR = juce::Rectangle<float> (dot, dot).withCentre ({ r.getX() + 20.0f * u, r.getY() + 19.0f * u });
        g.setColour (ui::ember.withAlpha (on ? 0.35f : 0.0f));
        g.fillEllipse (dotR.expanded (dot * 0.7f));
        g.setColour (on ? ui::ember : ui::haze.withAlpha (0.5f));
        g.fillEllipse (dotR);
    }

    // ---- carte VOCAL POCKET : témoin du sidechain + titre du mètre SC
    paintScStatus (g);
    g.setColour (ui::haze.withAlpha (1.0f - 0.6f * staticAmt));
    g.setFont (ui::font (ui::Face::body, 11.0f * u, true, 0.12f));
    g.drawText ("SC", scMeterTitle, juce::Justification::centred, false);

    // ---- témoin OVER (carte OUTPUT)
    {
        const float d = 8.0f * u;
        const auto dot = juce::Rectangle<float> (d, d).withCentre ({ overRect.getX() + d * 0.5f, overRect.getCentreY() });
        g.setColour (ui::ember.withAlpha (0.45f * overGlow));
        g.fillEllipse (dot.expanded (d * 0.9f));
        g.setColour (ui::haze.withAlpha (0.35f).interpolatedWith (ui::ember, overGlow));
        g.fillEllipse (dot);
        g.setColour (ui::haze.interpolatedWith (ui::bone, overGlow));
        g.setFont (ui::fitFont (ui::Face::mono, 11.0f * u, overRect.getWidth() - d - 8.0f * u, "OVER", true, 0.1f));
        g.drawText ("OVER", overRect.withTrimmedLeft (d + 8.0f * u), juce::Justification::centredLeft, false);
    }
}

void OniriqueVocalSpacerAudioProcessorEditor::rebuildLayers (float pixelScale)
{
    layersScale = pixelScale;
    const int w = juce::jmax (1, juce::roundToInt ((float) getWidth() * pixelScale));
    const int h = juce::jmax (1, juce::roundToInt ((float) getHeight() * pixelScale));
    const auto toPixels = juce::AffineTransform::scale (pixelScale);

    backdropLayer = juce::Image (juce::Image::RGB, w, h, false);
    {
        juce::Graphics bg (backdropLayer);
        bg.addTransform (toPixels);
        paintBackdrop (bg);
    }
    cardsLayer = juce::Image (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics cg (cardsLayer);
        cg.addTransform (toPixels);
        paintCards (cg);
    }
}

// ---- fond : sang -> nuit, halo braise en haut à droite
void OniriqueVocalSpacerAudioProcessorEditor::paintBackdrop (juce::Graphics& g)
{
    const float u = scale();
    const float W = (float) getWidth(), H = (float) getHeight();

    g.fillAll (ui::night);
    g.setGradientFill (juce::ColourGradient (ui::blood.withAlpha (0.85f), 0.0f, 0.0f, ui::night.withAlpha (0.0f), 0.0f, H * 0.62f, false));
    g.fillRect (getLocalBounds());
    g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.20f), W * 0.74f, -40.0f * u,
                                             ui::ember.withAlpha (0.0f), W * 0.74f + W * 0.42f, -40.0f * u, true));
    g.fillRect (getLocalBounds());
}

// ---- cartes : verre sombre, filet fin, ombre portée, titre (le point braise est dessiné à part, il suit le module)
void OniriqueVocalSpacerAudioProcessorEditor::paintCards (juce::Graphics& g)
{
    const float u = scale();

    for (const auto& s : sections)
    {
        const auto r = s.bounds;
        const float rad = 16.0f * u;
        juce::Path card;
        card.addRoundedRectangle (r, rad);
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), juce::roundToInt (28.0f * u), { 0, juce::roundToInt (12.0f * u) })
            .drawForPath (g, card);
        g.setGradientFill (juce::ColourGradient (ui::deep2.withAlpha (0.94f), 0.0f, r.getY(), ui::deep.withAlpha (0.96f), 0.0f, r.getBottom(), false));
        g.fillPath (card);
        g.setColour (ui::line);
        g.strokePath (card, juce::PathStrokeType (std::max (1.0f, 1.0f * u)));
        g.setColour (juce::Colours::white.withAlpha (0.045f));
        g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.5f * u), r.getX() + rad, r.getRight() - rad);

        const float maxW = std::min (r.getWidth() - (s.power != nullptr ? 90.0f : 60.0f) * u, 300.0f * u);
        g.setColour (ui::mist.withAlpha (0.92f));
        g.setFont (ui::fitFont (ui::Face::body, 13.0f * u, maxW, s.title, true, 0.18f));
        g.drawText (s.title, juce::Rectangle<float> (r.getX() + 32.0f * u, r.getY() + 8.0f * u, maxW, 22.0f * u),
                    juce::Justification::centredLeft, false);
    }
}

// module coupé : voile nuit sur le contenu de sa carte (titre et bouton power restent nets)
void OniriqueVocalSpacerAudioProcessorEditor::paintVeils (juce::Graphics& g)
{
    const float u = scale();
    for (const auto& s : sections)
    {
        if (s.power == nullptr || s.veil < 0.002f) continue;
        g.setColour (ui::deep.withAlpha (0.72f * s.veil));
        g.fillRoundedRectangle (s.bounds.reduced (1.0f).withTrimmedTop (36.0f * u), 14.0f * u);
    }
}
