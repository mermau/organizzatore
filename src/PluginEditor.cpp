#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;
using namespace org;
using namespace org::ui;

namespace
{
const Colour kPanel   (0xff25272c);   // pannello grigio antracite
const Colour kLine    (0xff8c3a2c);   // filetti rosso mattone delle sezioni
const Colour kPrint   (0xffece8dc);   // serigrafia
const Colour kLedge   (0xff0d0d0e);
const Colour kIvory   (0xfff3efe3);
const Colour kCream   (0xffe9e3d2);
const Colour kRed     (0xffd9261c);

float degToRad (float d) { return d * MathConstants<float>::pi / 180.0f; }
Point<float> polar (Point<float> c, float r, float deg)
{
    const float a = degToRad (deg);
    return { c.x + r * std::sin (a), c.y - r * std::cos (a) };
}

void text (Graphics& g, const String& s, Point<float> c, float h, Justification j = Justification::centred, float w = 140.0f)
{
    h *= 1.12f;   // un po' più grande della serigrafia vera, per leggibilità sullo schermo
    g.setFont (Fonts::get().label (h));
    Rectangle<float> r (c.x - w * 0.5f, c.y - h * 0.6f, w, h * 1.2f);
    if (j == Justification::centredLeft) r.setX (c.x);
    if (j == Justification::centredRight) r.setX (c.x - w);
    g.drawText (s, r, j, false);
}

// "5⅓'" ecc. con la frazione piccola, come sulle linguette originali
void footText (Graphics& g, const String& whole, const String& num, const String& den, Point<float> c, float h)
{
    auto f = Fonts::get().label (h);
    auto fs = Fonts::get().label (h * 0.62f);
    const float ww = GlyphArrangement::getStringWidth (f, whole);
    const float wf = num.isEmpty() ? 0.0f : jmax (GlyphArrangement::getStringWidth (fs, num), GlyphArrangement::getStringWidth (fs, den)) + 0.6f;
    float x = c.x - (ww + wf) * 0.5f;
    g.setFont (f);
    g.drawText (whole, Rectangle<float> (x, c.y - h * 0.6f, ww + 1.0f, h * 1.2f), Justification::centredLeft, false);
    if (num.isNotEmpty())
    {
        x += ww + 0.3f;
        g.setFont (fs);
        g.drawText (num, Rectangle<float> (x, c.y - h * 0.62f, wf, h * 0.62f), Justification::centred, false);
        g.drawText (den, Rectangle<float> (x, c.y + h * 0.02f, wf, h * 0.62f), Justification::centred, false);
        g.drawLine (x, c.y + 0.02f * h, x + wf, c.y - 0.1f * h, 0.35f);
    }
}

struct FootLabel { const char* whole; const char* num; const char* den; };
const FootLabel feetLbl[9] = { { "16'", "", "" }, { "5", "1", "3" }, { "8'", "", "" }, { "4'", "", "" }, { "2", "2", "3" },
                               { "2'", "", "" }, { "1", "3", "5" }, { "1", "1", "3" }, { "1'", "", "" } };

Path fitText (const Font& f, const String& s, Rectangle<float> target)
{
    GlyphArrangement ga;
    ga.addLineOfText (f, s, 0.0f, 0.0f);
    Path p; ga.createPath (p);
    auto b = p.getBounds();
    if (b.isEmpty()) return p;
    p.applyTransform (AffineTransform::translation (-b.getX(), -b.getY())
                          .scaled (target.getWidth() / b.getWidth(), target.getHeight() / b.getHeight())
                          .translated (target.getX(), target.getY()));
    return p;
}

void learnRing (Graphics& g, Rectangle<float> r)
{
    g.setColour (Colour (0xffff3b30));
    g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.2f);
}
} // namespace

//==============================================================================
Fonts& Fonts::get() { static Fonts f; return f; }
Fonts::Fonts()
{
    narrowBold = Typeface::createSystemTypefaceFor (BinaryData::PTSansNarrowBold_ttf, BinaryData::PTSansNarrowBold_ttfSize);
    sansBold   = Typeface::createSystemTypefaceFor (BinaryData::PTSansBold_ttf, BinaryData::PTSansBold_ttfSize);
    slabBold   = Typeface::createSystemTypefaceFor (BinaryData::ZillaSlabBold_ttf, BinaryData::ZillaSlabBold_ttfSize);
    slabSemi   = Typeface::createSystemTypefaceFor (BinaryData::ZillaSlabSemiBold_ttf, BinaryData::ZillaSlabSemiBold_ttfSize);
}
Font Fonts::label (float h) const { return Font (FontOptions (narrowBold).withHeight (h)).withExtraKerningFactor (0.05f); }
Font Fonts::sans (float h) const  { return Font (FontOptions (sansBold).withHeight (h)); }
Font Fonts::slab (float h) const  { return Font (FontOptions (slabBold).withHeight (h)); }
Font Fonts::model (float h) const { return Font (FontOptions (slabSemi).withHeight (h)); }

//==============================================================================
ParamControl::ParamControl (LearnHost& h, RangedAudioParameter& p)
    : host (h), param (p),
      attachment (p, [this] (float v) { current = v; repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
}

void ParamControl::setReal (float v)
{
    attachment.setValueAsCompleteGesture (param.getNormalisableRange().snapToLegalValue (v));
}

void ParamControl::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { host.showControlMenu (*this, param.paramID); return; }
    if (draggable())
    {
        dragStart = norm();
        dragging = true;
        attachment.beginGesture();
    }
    else clicked (e);
}

void ParamControl::mouseDrag (const MouseEvent& e)
{
    if (! dragging) return;
    float d = (dragVertical ? -(float) e.getDistanceFromDragStartY() : (float) e.getDistanceFromDragStartX()) / dragPixels;
    if (dragInverted) d = -d;
    if (e.mods.isShiftDown()) d *= 0.25f;
    const float n = jlimit (0.0f, 1.0f, dragStart + d);
    attachment.setValueAsPartOfGesture (param.getNormalisableRange().snapToLegalValue (param.convertFrom0to1 (n)));
}

void ParamControl::mouseUp (const MouseEvent&)
{
    if (dragging) { attachment.endGesture(); dragging = false; }
}

void ParamControl::mouseDoubleClick (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    if (onDoubleClick) { onDoubleClick(); return; }
    if (draggable()) setReal (param.convertFrom0to1 (param.getDefaultValue()));
}

void ParamControl::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    const float dir = (w.deltaY + w.deltaX) > 0 ? 1.0f : -1.0f;
    setReal (param.convertFrom0to1 (jlimit (0.0f, 1.0f, norm() + (dragInverted ? -dir : dir) * wheelStep)));
}

//==============================================================================
void Knob::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float deg = -135.0f + 270.0f * norm();
    const float rs = 11.2f, rc = 8.0f;
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillEllipse (c.x - rs + 0.8f, c.y - rs + 1.6f, rs * 2.0f, rs * 2.0f);
    // gonna zigrinata
    g.setGradientFill (ColourGradient (Colour (0xff3a3a3a), c.x - rs, c.y - rs, Colour (0xff060606), c.x + rs, c.y + rs, false));
    g.fillEllipse (c.x - rs, c.y - rs, rs * 2.0f, rs * 2.0f);
    g.setColour (Colours::black.withAlpha (0.7f));
    for (int i = 0; i < 40; ++i)
        g.drawLine (Line<float> (polar (c, rc + 0.6f, (float) i * 9.0f), polar (c, rs - 0.2f, (float) i * 9.0f)), 0.45f);
    g.setColour (Colour (0xff4a4a4a).withAlpha (0.5f));
    g.drawEllipse (c.x - rs, c.y - rs, rs * 2.0f, rs * 2.0f, 0.5f);
    // calotta
    g.setGradientFill (ColourGradient (Colour (0xff474747), c.x - rc * 0.7f, c.y - rc * 0.8f, Colour (0xff0b0b0b), c.x + rc * 0.6f, c.y + rc, true));
    g.fillEllipse (c.x - rc, c.y - rc, rc * 2.0f, rc * 2.0f);
    g.setColour (Colours::black);
    g.drawEllipse (c.x - rc, c.y - rc, rc * 2.0f, rc * 2.0f, 0.6f);
    // indice bianco
    g.setColour (kPrint);
    g.drawLine (Line<float> (polar (c, rc * 0.2f, deg), polar (c, rs - 0.4f, deg)), 1.3f);
    if (learning()) { g.setColour (Colour (0xffff3b30)); g.drawEllipse (b.reduced (0.6f), 1.2f); }
}

void Pointer::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre().translated (0.0f, 2.0f);
    const int idx = jlimit (0, positions - 1, (int) std::lround (value()));
    const float deg = angleFor (idx);
    const float r = 10.5f;
    // corpo ad aletta, come i selettori originali
    Path wing;
    wing.addCentredArc (0.0f, 0.0f, r, r, 0.0f, degToRad (55.0f), degToRad (305.0f), true);
    wing.lineTo (0.0f, -r * 1.75f);
    wing.closeSubPath();
    wing.applyTransform (AffineTransform::rotation (degToRad (deg)).translated (c));
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillPath (wing, AffineTransform::translation (0.9f, 1.8f));
    g.setGradientFill (ColourGradient (Colour (0xff4a4a4a), c.x - r, c.y - r, Colour (0xff080808), c.x + r, c.y + r, false));
    g.fillPath (wing);
    g.setColour (Colours::black);
    g.strokePath (wing, PathStrokeType (0.6f));
    // spigolo illuminato
    g.setColour (Colours::white.withAlpha (0.12f));
    g.drawLine (Line<float> (c, polar (c, r * 1.6f, deg)), 2.2f);
    g.setColour (kPrint);
    g.drawLine (Line<float> (polar (c, r * 0.9f, deg), polar (c, r * 1.68f, deg)), 1.0f);
    if (learning()) learnRing (g, b);
}

void Pointer::mouseUp (const MouseEvent& e)
{
    const bool wasClick = dragging && e.getDistanceFromDragStart() < 3 && ! e.mods.isPopupMenu();
    ParamControl::mouseUp (e);
    if (wasClick)
    {
        const int idx = (int) std::lround (value());
        select (idx + 1 >= positions ? 0 : idx + 1);
    }
}

void Drawbar::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float v = jlimit (0.0f, 8.0f, value());
    const float top = v / 8.0f * travel;
    const float cx = b.getCentreX();
    // asta scoperta con la scala incisa
    if (top > 0.5f)
    {
        Rectangle<float> shaft (cx - 3.6f, 0.0f, 7.2f, top + 2.0f);
        g.setGradientFill (ColourGradient (Colour (0xff1d1d1d), shaft.getX(), 0, Colour (0xff3a3a3a), shaft.getRight(), 0, false));
        g.fillRect (shaft);
        g.setColour (kPrint.withAlpha (0.85f));
        g.setFont (Fonts::get().label (3.6f));
        for (int i = 1; i <= 8; ++i)
        {
            const float y = top - (float) i * travel / 8.0f + travel / 16.0f;
            if (y < 0.0f) break;
            g.drawText (String (i), Rectangle<float> (cx - 3.5f, y - 1.8f, 7.0f, 3.6f), Justification::centred, false);
        }
    }
    // linguetta
    auto tab = Rectangle<float> (b.getX() + 0.5f, top, b.getWidth() - 1.0f, tabH);
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (tab.translated (0.6f, 1.4f), 1.6f);
    g.setGradientFill (ColourGradient (colour.brighter (0.18f), tab.getX(), tab.getY(), colour.darker (0.25f), tab.getX(), tab.getBottom(), false));
    g.fillRoundedRectangle (tab, 1.6f);
    g.setColour (Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (tab, 1.6f, 0.5f);
    g.setColour (Colours::white.withAlpha (0.22f));
    g.drawLine (tab.getX() + 1.5f, tab.getY() + 0.6f, tab.getRight() - 1.5f, tab.getY() + 0.6f, 0.6f);
    // scritta del piede
    if (text.isNotEmpty())
    {
        g.setColour (colour.getPerceivedBrightness() > 0.5f ? Colour (0xff141414) : kPrint);
        const int f = text.getIntValue();
        const auto& fl = feetLbl[jlimit (0, 8, f)];
        footText (g, fl.whole, fl.num, fl.den, tab.getCentre().translated (0.0f, -2.2f), 5.4f);
    }
    if (learning()) learnRing (g, b);
}

void Tab::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const bool on = value() > 0.5f;
    g.setColour (Colours::black);
    g.fillRoundedRectangle (b, 1.0f);
    auto face = b.reduced (0.9f);
    if (on) face = face.withTrimmedTop (2.2f);
    else face = face.withTrimmedBottom (1.2f);
    const Colour top = on ? colour.darker (0.35f) : colour.brighter (0.12f);
    const Colour bot = on ? colour.darker (0.1f) : colour.darker (0.25f);
    g.setGradientFill (ColourGradient (top, face.getX(), face.getY(), bot, face.getX(), face.getBottom(), false));
    g.fillRoundedRectangle (face, 0.8f);
    g.setColour (on ? Colours::black.withAlpha (0.35f) : Colours::white.withAlpha (0.55f));
    g.drawLine (face.getX() + 0.8f, face.getY() + 0.4f, face.getRight() - 0.8f, face.getY() + 0.4f, 0.6f);
    if (learning()) learnRing (g, b);
}

void PresetButton::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const bool on = (int) std::lround (value()) == index;
    g.setColour (Colours::black);
    g.fillRoundedRectangle (b, 1.2f);
    auto face = b.reduced (1.0f);
    face = on ? face.withTrimmedTop (1.6f) : face.withTrimmedBottom (1.0f);
    g.setGradientFill (ColourGradient (Colour (on ? 0xff1a1a1a : 0xff3a3a3a), face.getX(), face.getY(),
                                       Colour (0xff111111), face.getX(), face.getBottom(), false));
    g.fillRoundedRectangle (face, 1.0f);
    g.setColour (Colours::white.withAlpha (on ? 0.05f : 0.16f));
    g.drawLine (face.getX() + 1.0f, face.getY() + 0.5f, face.getRight() - 1.0f, face.getY() + 0.5f, 0.6f);
    // LED rosso nella parte alta del tasto
    const auto c = Point<float> (face.getCentreX(), face.getY() + face.getHeight() * 0.3f);
    const float r = 1.7f;
    if (on)
    {
        g.setGradientFill (ColourGradient (kRed.withAlpha (0.5f), c.x, c.y, kRed.withAlpha (0.0f), c.x + r * 3.5f, c.y, true));
        g.fillEllipse (c.x - r * 3.5f, c.y - r * 3.5f, r * 7.0f, r * 7.0f);
    }
    g.setColour (on ? Colour (0xffff3a24) : Colour (0xff4a0f0b));
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour (Colours::white.withAlpha (0.35f));
    g.fillEllipse (c.x - r * 0.45f, c.y - r * 0.6f, r * 0.6f, r * 0.45f);
    if (learning()) learnRing (g, b);
}

void ToggleLever::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const bool ped = value() > 0.5f;
    // ghiera
    g.setGradientFill (ColourGradient (Colour (0xffd0d0d0), c.x - 4, c.y - 4, Colour (0xff6a6a6a), c.x + 4, c.y + 4, false));
    g.fillEllipse (c.x - 4.2f, c.y - 4.2f, 8.4f, 8.4f);
    g.setColour (Colour (0xff2a2a2a)); g.fillEllipse (c.x - 2.2f, c.y - 2.2f, 4.4f, 4.4f);
    // levetta cromata
    const float deg = ped ? 55.0f : -55.0f;
    const auto tip = polar (c, 9.0f, deg);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawLine (Line<float> (c.translated (0.8f, 1.2f), tip.translated (0.8f, 1.2f)), 2.6f);
    g.setGradientFill (ColourGradient (Colour (0xffffffff), c.x, c.y - 3, Colour (0xff8a8a8a), c.x, c.y + 3, false));
    g.drawLine (Line<float> (c, tip), 2.2f);
    g.fillEllipse (tip.x - 1.8f, tip.y - 1.8f, 3.6f, 3.6f);
    if (learning()) learnRing (g, b);
}

void BendLever::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float v = jlimit (-1.0f, 1.0f, value() + proc.wheelBend());
    const auto pivot = Point<float> (b.getCentreX(), b.getBottom() - 9.0f);
    // fessura curva
    Path slot;
    slot.addCentredArc (pivot.x, pivot.y, 40.0f, 40.0f, 0.0f, degToRad (-26.0f), degToRad (26.0f), true);
    g.setColour (Colours::black);
    g.strokePath (slot, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
    const float deg = v * 22.0f;
    const auto tip = polar (pivot, 50.0f, deg);
    // leva nera con pomello
    Path lev;
    lev.startNewSubPath (polar (pivot, 16.0f, deg - 6.0f));
    lev.lineTo (polar (pivot, 16.0f, deg + 6.0f));
    lev.lineTo (polar (pivot, 46.0f, deg + 2.4f));
    lev.lineTo (polar (pivot, 46.0f, deg - 2.4f));
    lev.closeSubPath();
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillPath (lev, AffineTransform::translation (1.0f, 1.5f));
    g.setGradientFill (ColourGradient (Colour (0xff3c3c3c), pivot.x - 4, 0, Colour (0xff070707), pivot.x + 4, 0, false));
    g.fillPath (lev);
    const auto knob = Rectangle<float> (9.0f, 13.0f).withCentre (tip);
    g.setGradientFill (ColourGradient (Colour (0xff4a4a4a), knob.getX(), knob.getY(), Colour (0xff050505), knob.getRight(), knob.getBottom(), false));
    g.fillRoundedRectangle (knob, 3.0f);
    g.setColour (Colours::black); g.drawRoundedRectangle (knob, 3.0f, 0.5f);
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (Colours::white.withAlpha (0.10f));
        g.drawLine (knob.getX() + 1.5f, knob.getY() + 3.0f + (float) i * 2.4f, knob.getRight() - 1.5f, knob.getY() + 3.0f + (float) i * 2.4f, 0.5f);
    }
    if (learning()) learnRing (g, b);
}

//==============================================================================
namespace
{
const int whiteOf[12] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
bool isBlack (int k) { return whiteOf[k % 12] < 0; }
}

Rectangle<float> Manual::whiteRect (int w) const
{
    const float kw = (float) getWidth() / 36.0f;
    return { (float) w * kw, 0.0f, kw, (float) getHeight() };
}

Rectangle<float> Manual::blackRect (int key) const
{
    const float kw = (float) getWidth() / 36.0f;
    const int oct = key / 12, n = key % 12;
    // posizioni dei tasti neri dentro l'ottava (in larghezze di tasto bianco), come su una tastiera vera
    static const float off[12] = { 0, 0.62f, 0, 1.76f, 0, 0, 3.58f, 0, 4.68f, 0, 5.80f, 0 };
    const float x = ((float) oct * 7.0f + off[n]) * kw;
    return { x, 0.0f, kw * 0.6f, (float) getHeight() * 0.62f };
}

int Manual::keyAt (Point<float> p) const
{
    for (int k = 0; k < t2::kKeys; ++k)
        if (isBlack (k) && blackRect (k).contains (p)) return k;
    const float kw = (float) getWidth() / 36.0f;
    const int w = jlimit (0, 35, (int) (p.x / kw));
    const int oct = w / 7, i = w % 7;
    static const int keyOfWhite[7] = { 0, 2, 4, 5, 7, 9, 11 };
    return jlimit (0, t2::kKeys - 1, oct * 12 + keyOfWhite[i]);
}

void Manual::paint (Graphics& g)
{
    const float h = (float) getHeight();
    g.setColour (Colour (0xff141210));
    g.fillRect (getLocalBounds());
    for (int k = 0; k < t2::kKeys; ++k)
    {
        if (isBlack (k)) continue;
        const int w = (k / 12) * 7 + whiteOf[k % 12];
        auto r = whiteRect (w).reduced (0.45f, 0.0f);
        const bool down = shown[(size_t) k];
        const Colour top = down ? Colour (0xffd2ccbd) : kIvory;
        const Colour bot = down ? Colour (0xffc0b9a8) : Colour (0xffdcd5c4);
        g.setGradientFill (ColourGradient (top, r.getX(), r.getY(), bot, r.getX(), r.getBottom(), false));
        g.fillRect (r.withTrimmedBottom (down ? 1.5f : 3.2f));
        // bordo anteriore del tasto
        g.setColour (down ? Colour (0xffa59f8f) : Colour (0xffc9c2b0));
        g.fillRect (r.withTop (r.getBottom() - (down ? 1.5f : 3.2f)));
        g.setColour (Colours::black.withAlpha (0.07f));
        g.fillRect (r.withWidth (1.2f));
    }
    for (int k = 0; k < t2::kKeys; ++k)
    {
        if (! isBlack (k)) continue;
        auto r = blackRect (k);
        const bool down = shown[(size_t) k];
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRect (r.translated (1.2f, 0.0f).withTrimmedTop (2.0f));
        g.setColour (Colour (0xff050505));
        g.fillRect (r);
        auto topFace = r.reduced (r.getWidth() * 0.12f, 0.0f).withTrimmedBottom (r.getHeight() * (down ? 0.04f : 0.10f));
        g.setGradientFill (ColourGradient (down ? Colour (0xff222222) : Colour (0xff353535), topFace.getX(), topFace.getY(),
                                           Colour (0xff0a0a0a), topFace.getX(), topFace.getBottom(), false));
        g.fillRect (topFace);
        g.setColour (Colours::white.withAlpha (down ? 0.04f : 0.12f));
        g.fillRect (topFace.withWidth (topFace.getWidth() * 0.22f).translated (topFace.getWidth() * 0.12f, 0.0f));
    }
    // ombra della mensola sopra i tasti
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.55f), 0, 0, Colours::transparentBlack, 0, h * 0.08f, false));
    g.fillRect (0.0f, 0.0f, (float) getWidth(), h * 0.08f);
}

void Manual::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    held = keyAt (e.position);
    proc.guiKey (manual, held, true);
}

void Manual::mouseDrag (const MouseEvent& e)
{
    if (held < 0) return;
    const int k = getLocalBounds().toFloat().contains (e.position) ? keyAt (e.position) : -1;
    if (k != held)
    {
        proc.guiKey (manual, held, false);
        held = k;
        if (k >= 0) proc.guiKey (manual, k, true);
    }
}

void Manual::mouseUp (const MouseEvent&)
{
    if (held >= 0) proc.guiKey (manual, held, false);
    held = -1;
}

void Manual::refresh()
{
    bool changed = false;
    for (int k = 0; k < t2::kKeys; ++k)
    {
        const bool d = proc.keyIsDown (manual, k);
        if (d != shown[(size_t) k]) { shown[(size_t) k] = d; changed = true; }
    }
    if (changed) repaint();
}

//==============================================================================
template <typename T, typename... Args>
T* Panel::add (const String& id, Rectangle<float> r, const char* tip, Args&&... args)
{
    auto* p = proc.apvts.getParameter (id);
    jassert (p != nullptr);
    auto* c = new T (*this, *p, std::forward<Args> (args)...);
    controls.add (c);
    addAndMakeVisible (c);
    c->setBounds (r.toNearestInt());
    c->setTooltip (String::fromUTF8 (tip));
    return c;
}

Panel::Panel (OrgProcessor& p) : proc (p), upper (p, t2::Upper), lower (p, t2::Lower)
{
    setSize (W, H);
    const float ky = 71.0f;

    // ------------------------------------------------------------ manopole
    knobSpecs = {
        { pid::lmVolume,   114, ky, "LOWER MANUAL", false },
        { pid::master,     159, ky, "MASTER", false },
        { pid::rate,       212, ky, "RATE", false },
        { pid::vibrato,    256, ky, "VIBRATO", false },
        { pid::tremolo,    302, ky, "TREMOLO", false },
        { pid::bassDecay,  901, ky, "DECAY", true },
        { pid::bassRange,  945, ky, "RANGE", false },
        { pid::bassRes,    988, ky, "RESONANCE", false },
        { pid::bassVolume, 1031, ky, "VOLUME", false },
    };
    const char* knobTips[] = {
        "Volume delle armoniche del manuale inferiore",
        "Volume generale (MASTER)",
        "Velocità della modulazione (circa 0,8 … 80 Hz). Doppio clic: accende/spegne la modulazione (pulsante nella manopola)",
        "Profondità del vibrato (modulazione di frequenza dell'oscillatore principale)",
        "Profondità del tremolo (modulazione d'ampiezza sul CA3080 d'uscita)",
        "Bassi: durata del suono (0 … 10 s)",
        "Bassi: apertura del filtro",
        "Bassi: risonanza, dal passa-basso al passa-banda",
        "Bassi: volume",
    };
    for (size_t i = 0; i < knobSpecs.size(); ++i)
    {
        auto& s = knobSpecs[i];
        auto* k = add<Knob> (s.id, Rectangle<float> (25.0f, 25.0f).withCentre ({ s.x, s.y }), knobTips[i]);
        if (s.id == pid::rate)
        {
            rateKnob = k;
            k->onDoubleClick = [this]
            {
                auto* pm = proc.apvts.getParameter (pid::modOn);
                pm->setValueNotifyingHost (pm->getValue() > 0.5f ? 0.0f : 1.0f);
            };
        }
    }
    modeSel = add<Pointer> (pid::bassMode, Rectangle<float> (34.0f, 34.0f).withCentre ({ 815.0f, ky }),
                            "Bassi: OFF / PERC. (suono percussivo, basso elettrico) / SUST. (suono tenuto, pedaliera d'organo) / OFF. Clic: posizione successiva; clic su una scritta: quella posizione", 4, 216.0f);
    footSel = add<Pointer> (pid::bassFoot, Rectangle<float> (34.0f, 34.0f).withCentre ({ 859.0f, ky }),
                            "Bassi: piede 16' / 8' / 4'. Clic: posizione successiva; clic su una scritta: quella posizione", 3, 110.0f);

    // ------------------------------------------------------------ preselettori
    for (int i = 0; i < 4; ++i)
    {
        add<PresetButton> (pid::lmPreset, Rectangle<float> (15.0f, 15.0f).withCentre ({ 347.0f + 20.4f * (float) i, 61.0f }),
                           i == 0 ? "Manuale inferiore: drawbar" : "Manuale inferiore: preselettore", i);
        add<PresetButton> (pid::umPreset, Rectangle<float> (15.0f, 15.0f).withCentre ({ 547.0f + 20.4f * (float) i, 61.0f }),
                           i == 0 ? "Manuale superiore: drawbar" : "Manuale superiore: preselettore (2 = 16' 5 1/3' 8', 3 = 16' 2', 4 = pieno)", i);
    }

    // ------------------------------------------------------------ percussione
    for (int i = 0; i < 11; ++i)
    {
        const auto r = Rectangle<float> (10.5f, 14.0f).withCentre ({ 640.0f + 12.5f * (float) i, 59.0f });
        if (i < 9) add<Tab> (pid::perc (i), r, "Percussione su questo piede (manuale superiore)", kCream);
        else add<Tab> (i == 9 ? String (pid::k1) : String (pid::k2), r,
                       i == 9 ? "K1: click d'attacco tipo ruota foníca" : "K2: click attenuato", kRed);
    }

    // ------------------------------------------------------------ drawbar (linguette)
    const Colour sub (0xff2b2a28), mut (0xff8d8b86);
    const Colour tabCol[9] = { sub, sub, kCream, kCream, mut, kCream, mut, mut, kCream };
    for (int f = 0; f < 9; ++f)
    {
        const float hb = Drawbar::tabH + Drawbar::travel;
        add<Drawbar> (pid::lmDraw (f), Rectangle<float> (346.0f + 20.45f * (float) f - 7.5f, 93.0f, 15.0f, hb),
                      "Drawbar del manuale inferiore: trascina in giù (0 … 8)", tabCol[f], String (f));
        add<Drawbar> (pid::umDraw (f), Rectangle<float> (545.0f + 19.86f * (float) f - 7.5f, 93.0f, 15.0f, hb),
                      "Drawbar del manuale superiore: trascina in giù (0 … 8)", tabCol[f], String (f));
    }
    add<Drawbar> (pid::percVol, Rectangle<float> (744.0f - 7.5f, 93.0f, 15.0f, Drawbar::tabH + Drawbar::travel),
                  "Volume della percussione", Colour (0xff9a9893), String());
    add<Drawbar> (pid::percDecay, Rectangle<float> (764.0f - 7.5f, 93.0f, 15.0f, Drawbar::tabH + Drawbar::travel),
                  "Durata della percussione", Colour (0xff9a9893), String());

    // ------------------------------------------------------------ blocchi a sinistra delle tastiere
    add<ToggleLever> (pid::bassPedal, Rectangle<float> (22.0f, 22.0f).withCentre ({ 103.0f, 168.0f }),
                      "Bassi sul manuale (MAN.: prime due ottave del manuale inferiore) o sulla pedaliera (PED., canale MIDI 3)");
    lever = add<BendLever> (pid::bend, Rectangle<float> (64.0f, 72.0f).withCentre ({ 103.0f, 233.0f }),
                            "PITCH BENDING: un tono su o giù (anche dalla rotella della tastiera). Doppio clic: al centro", proc);
    add<Knob> (pid::susLength, Rectangle<float> (25.0f, 25.0f).withCentre ({ 75.0f, 334.0f }),
               "SUSTAIN LENGTH: durata del sustain del manuale superiore");
    add<Tab> (pid::susAuto, Rectangle<float> (13.0f, 13.0f).withCentre ({ 60.0f, 392.0f }),
              "AUTO: sostiene solo l'ultima nota o l'ultimo accordo", kCream);
    add<Tab> (pid::susOn, Rectangle<float> (13.0f, 13.0f).withCentre ({ 82.0f, 392.0f }),
              "ON/OFF del sustain del manuale superiore (anche dal pedale di sustain). Col sustain la percussione è esclusa", kCream);

    // ------------------------------------------------------------ tastiere
    addAndMakeVisible (upper);
    addAndMakeVisible (lower);
    upper.setBounds (136, 158, 900, 112);
    lower.setBounds (136, 292, 900, 134);

    logoArea = { 806.0f, 114.0f, 142.0f, 32.0f };
    ledArea = Rectangle<float> (14.0f, 14.0f).withCentre ({ 233.0f, 49.0f });
    startTimerHz (30);
}

Panel::~Panel() { stopTimer(); }

void Panel::paint (Graphics& g)
{
    // ------------------------------------------------------------ mobile: tolex, guance in legno
    g.fillAll (Colour (0xff151515));
    {
        // piano superiore (leggio) con i fori delle viti
        Rectangle<float> deck (40.0f, 0.0f, 1090.0f, 40.0f);
        g.setGradientFill (ColourGradient (Colour (0xff1b1b1c), 0, 0, Colour (0xff0f0f10), 0, 40, false));
        g.fillRect (deck);
        g.setColour (Colour (0xff050505));
        g.fillRoundedRectangle (Rectangle<float> (400.0f, 14.0f, 260.0f, 6.0f), 2.0f);
        for (float x : { 310.0f, 450.0f, 550.0f, 620.0f, 700.0f })
        {
            g.setColour (Colour (0xff050505)); g.fillEllipse (x, 24.0f, 3.2f, 3.2f);
            g.setColour (Colour (0xff2c2c2c)); g.drawEllipse (x, 24.0f, 3.2f, 3.2f, 0.4f);
        }
    }
    // frontale del mobile (tolex grigio) con le chiusure
    {
        Rectangle<float> front (0.0f, 428.0f, (float) W, 56.0f);
        g.setGradientFill (ColourGradient (Colour (0xff2c2b2a), 0, front.getY(), Colour (0xff1a1a1a), 0, front.getBottom(), false));
        g.fillRect (front);
        Random rnd (7);
        g.setColour (Colours::white.withAlpha (0.025f));
        for (int i = 0; i < 1600; ++i)
            g.fillRect (rnd.nextFloat() * (float) W, front.getY() + rnd.nextFloat() * front.getHeight(), 1.0f, 1.0f);
        g.setColour (Colour (0xff070707));
        g.fillRect (front.withHeight (3.0f));
        for (float x : { 60.0f, 250.0f, 450.0f, 650.0f, 850.0f, 1050.0f })
        {
            g.setColour (Colour (0xff4a2a1c)); g.fillEllipse (x, 470.0f, 4.0f, 4.0f);
        }
        for (float x : { 6.0f, 1124.0f })
        {
            Rectangle<float> latch (x, 452.0f, 40.0f, 14.0f);
            g.setGradientFill (ColourGradient (Colour (0xff9a6a3c), latch.getX(), latch.getY(), Colour (0xff4a2c16), latch.getX(), latch.getBottom(), false));
            g.fillRoundedRectangle (latch, 2.0f);
        }
    }
    // guance in legno, inclinate come nella foto
    auto cheek = [&] (Point<float> a, Point<float> b, Point<float> c, Point<float> d)
    {
        Path p; p.startNewSubPath (a); p.lineTo (b); p.lineTo (c); p.lineTo (d); p.closeSubPath();
        g.setGradientFill (ColourGradient (Colour (0xff7a4024), a.x, a.y, Colour (0xff4e2814), c.x, c.y, false));
        g.fillPath (p);
        g.setColour (Colours::black.withAlpha (0.08f));
        for (int i = 1; i < 9; ++i)
        {
            const float t = (float) i / 9.0f;
            g.drawLine (Line<float> (a + (b - a) * t, d + (c - d) * t), 0.6f);
        }
        g.setColour (Colours::black.withAlpha (0.6f));
        g.strokePath (p, PathStrokeType (0.8f));
    };
    cheek ({ 38.0f, 0.0f }, { 66.0f, 0.0f }, { 22.0f, 428.0f }, { 0.0f, 428.0f });
    cheek ({ 1104.0f, 0.0f }, { 1132.0f, 0.0f }, { 1170.0f, 428.0f }, { 1148.0f, 428.0f });

    // ------------------------------------------------------------ pannello dei comandi
    Rectangle<float> panel (79.0f, 40.0f, 984.0f, 62.0f);
    g.setGradientFill (ColourGradient (kPanel.brighter (0.06f), 0, panel.getY(), kPanel.darker (0.2f), 0, panel.getBottom(), false));
    g.fillRect (panel);
    g.setColour (Colours::black); g.drawRect (panel, 0.8f);

    auto box = [&] (float x0, float x1, std::initializer_list<std::pair<const char*, float>> titles)
    {
        Rectangle<float> r (x0, 43.5f, x1 - x0, 56.0f);
        g.setColour (kLine);
        g.drawRoundedRectangle (r, 3.0f, 0.75f);
        for (auto& t : titles)
        {
            g.setFont (Fonts::get().label (6.2f));
            const float w = GlyphArrangement::getStringWidth (g.getCurrentFont(), t.first) + 5.0f;
            g.setColour (kPanel);
            g.fillRect (Rectangle<float> (t.second - w * 0.5f, 41.5f, w, 4.0f));
            g.setColour (kPrint);
            text (g, t.first, { t.second, 43.6f }, 6.2f);
        }
    };
    box (92.0f, 186.0f, { { "VOLUME", 139.0f } });
    box (189.0f, 328.0f, { { "MODULATION", 258.0f } });
    box (331.0f, 528.0f, { { "LOWER MANUAL", 448.0f } });
    box (531.0f, 783.0f, { { "UPPER MANUAL", 655.0f } });
    box (786.0f, 1059.0f, { { "BASS", 922.0f } });

    g.setColour (kPrint);
    // RANGE sopra vibrato e tremolo
    text (g, "RANGE", { 279.0f, 50.5f }, 5.4f);
    g.setColour (kPrint);
    g.drawLine (240.0f, 50.5f, 266.0f, 50.5f, 0.5f);
    g.drawLine (292.0f, 50.5f, 318.0f, 50.5f, 0.5f);
    text (g, "PRESETS", { 377.0f, 50.5f }, 5.6f);
    text (g, "PRESETS", { 577.0f, 50.5f }, 5.6f);
    text (g, "PERCUSSION", { 703.0f, 50.0f }, 5.6f);
    for (int s = 0; s < 2; ++s)
    {
        const float x0 = s == 0 ? 347.0f : 547.0f;
        text (g, "DRAWBARS", { x0, 72.5f }, 4.6f);
        for (int i = 1; i < 4; ++i) text (g, String (i + 1), { x0 + 20.4f * (float) i, 72.5f }, 4.8f);
    }
    for (int i = 0; i < 9; ++i)
        footText (g, feetLbl[i].whole, feetLbl[i].num, feetLbl[i].den, { 640.0f + 12.5f * (float) i, 72.0f }, 4.9f);
    g.setFont (Fonts::get().label (4.9f));
    text (g, "K", { 751.5f, 72.0f }, 4.9f); text (g, "1", { 754.0f, 73.6f }, 3.2f);
    text (g, "K", { 764.0f, 72.0f }, 4.9f); text (g, "2", { 766.5f, 73.6f }, 3.2f);
    text (g, "PERCUSSION", { 754.0f, 82.0f }, 4.8f);
    text (g, "VOL.", { 744.0f, 88.0f }, 4.5f);
    text (g, "DECAY", { 764.0f, 88.0f }, 4.5f);

    // scale delle manopole
    for (auto& s : knobSpecs)
    {
        const Point<float> c (s.x, s.y);
        g.setColour (kPrint);
        for (int i = 0; i <= 20; ++i)
        {
            const float a = -135.0f + 13.5f * (float) i;
            const bool major = i % 2 == 0;
            g.drawLine (Line<float> (polar (c, 13.0f, a), polar (c, major ? 15.6f : 14.5f, a)), major ? 0.6f : 0.4f);
        }
        for (int i = 0; i <= 10; ++i)
        {
            const float a = -135.0f + 27.0f * (float) i;
            text (g, String (i), polar (c, i == 10 ? 19.3f : 18.3f, a), 4.6f);
        }
        if (s.secScale) text (g, "sec.", c.translated (0.0f, 15.0f), 4.0f);
        text (g, s.label, c.translated (0.0f, 23.5f), 4.9f);
    }
    // selettori dei bassi
    {
        const Point<float> c (815.0f, ky()), cf (859.0f, ky());
        const char* modes[4] = { "OFF", "PERC.", "SUST.", "OFF" };
        for (int i = 0; i < 4; ++i)
        {
            const float a = modeSel->angleFor (i);
            g.drawLine (Line<float> (polar (c, 16.0f, a), polar (c, 19.0f, a)), 0.6f);
            text (g, modes[i], polar (c, 23.0f, a).translated (0.0f, -0.5f), 4.6f);
        }
        const char* feet[3] = { "16'", "8'", "4'" };
        for (int i = 0; i < 3; ++i)
        {
            const float a = footSel->angleFor (i);
            g.drawLine (Line<float> (polar (cf, 16.0f, a), polar (cf, 19.0f, a)), 0.6f);
            text (g, feet[i], polar (cf, 22.5f, a), 4.8f);
        }
        text (g, "MODE", c.translated (0.0f, 23.5f), 4.9f);
        text (g, "FOOTAGE", cf.translated (0.0f, 23.5f), 4.9f);
    }

    // ------------------------------------------------------------ mensola sotto il pannello (linguette e marchio)
    {
        Rectangle<float> ledge (60.0f, 102.0f, 1050.0f, 50.0f);
        g.setGradientFill (ColourGradient (Colour (0xff1a1a1b), 0, ledge.getY(), kLedge, 0, ledge.getBottom(), false));
        g.fillRect (ledge);
        g.setColour (Colours::white.withAlpha (0.05f));
        g.drawLine (ledge.getX(), ledge.getY() + 0.5f, ledge.getRight(), ledge.getY() + 0.5f, 0.6f);
        // fessure delle linguette
        for (int f = 0; f < 9; ++f)
            for (float x : { 346.0f + 20.45f * (float) f, 545.0f + 19.86f * (float) f })
            {
                g.setColour (Colours::black);
                g.fillRoundedRectangle (Rectangle<float> (x - 4.5f, 101.0f, 9.0f, 4.0f), 1.0f);
            }
        for (float x : { 744.0f, 764.0f })
        { g.setColour (Colours::black); g.fillRoundedRectangle (Rectangle<float> (x - 4.5f, 101.0f, 9.0f, 4.0f), 1.0f); }
        for (float x : { 92.0f, 970.0f, 1088.0f })
        {
            g.setColour (Colour (0xff050505)); g.fillEllipse (x, 128.0f, 3.0f, 3.0f);
        }
    }
    // marchio: lettere in riquadri (stile della targa originale) e nome del modello
    {
        const String brand = "COLLATERAL";
        auto row = Rectangle<float> (logoArea.getX(), logoArea.getY(), logoArea.getWidth(), 14.0f);
        const float bw = row.getWidth() / (float) brand.length();
        for (int i = 0; i < brand.length(); ++i)
        {
            auto cell = Rectangle<float> (row.getX() + bw * (float) i, row.getY(), bw, row.getHeight()).reduced (0.5f, 0.0f);
            g.setColour (Colour (0xffcfc6a6));
            g.drawRect (cell, 0.6f);
            g.drawRect (cell.reduced (1.1f), 0.35f);
            auto glyph = fitText (Fonts::get().slab (20.0f), brand.substring (i, i + 1), cell.reduced (2.4f, 2.4f));
            g.setColour (Colour (0xffd8cfaf));
            g.fillPath (glyph);
        }
        g.setColour (Colour (0xffd8cfaf));
        g.setFont (Fonts::get().label (5.0f).withExtraKerningFactor (0.45f));
        g.drawText ("EFFECTS", Rectangle<float> (row.getRight() - 40.0f, row.getBottom() + 1.0f, 40.0f, 5.5f), Justification::centredRight, false);
        g.setFont (Fonts::get().model (10.5f));
        g.drawText ("organizzatore T2", Rectangle<float> (logoArea.getX() + 14.0f, logoArea.getY() + 17.0f, logoArea.getWidth(), 13.0f),
                    Justification::centredLeft, false);
    }

    // ------------------------------------------------------------ blocchi laterali delle tastiere
    {
        // manuale superiore: MAN./PED. e PITCH BENDING
        Path up; up.startNewSubPath (70.0f, 156.0f); up.lineTo (136.0f, 156.0f); up.lineTo (136.0f, 272.0f); up.lineTo (60.0f, 272.0f); up.closeSubPath();
        g.setColour (Colour (0xff0c0c0c)); g.fillPath (up);
        g.setColour (kPanel);
        auto b1 = Rectangle<float> (76.0f, 159.0f, 56.0f, 22.0f);
        g.fillRoundedRectangle (b1, 2.0f);
        g.setColour (kLine); g.drawRoundedRectangle (b1, 2.0f, 0.6f);
        auto b2 = Rectangle<float> (70.0f, 184.0f, 62.0f, 84.0f);
        g.setColour (kPanel); g.fillRoundedRectangle (b2, 2.0f);
        g.setColour (kLine); g.drawRoundedRectangle (b2, 2.0f, 0.6f);
        g.setColour (kPrint);
        text (g, "MAN.", { 86.0f, 163.5f }, 4.6f);
        text (g, "PED.", { 121.0f, 163.5f }, 4.6f);
        text (g, "BASS", { 103.0f, 177.5f }, 4.6f);
        text (g, "PITCH", { 103.0f, 258.0f }, 4.8f);
        text (g, "BENDING", { 103.0f, 263.5f }, 4.8f);
        // arco − 0 +
        Path arc; arc.addCentredArc (103.0f, 260.0f, 22.0f, 22.0f, 0.0f, degToRad (-50.0f), degToRad (50.0f), true);
        g.strokePath (arc, PathStrokeType (0.5f));
        text (g, "-", { 83.0f, 245.0f }, 5.0f);
        text (g, "0", { 103.0f, 240.5f }, 4.4f);
        text (g, "+", { 123.0f, 245.0f }, 5.0f);

        // manuale inferiore: SUSTAIN
        Path lo; lo.startNewSubPath (32.0f, 290.0f); lo.lineTo (136.0f, 290.0f); lo.lineTo (136.0f, 426.0f); lo.lineTo (18.0f, 426.0f); lo.closeSubPath();
        g.setColour (Colour (0xff0c0c0c)); g.fillPath (lo);
        auto b3 = Rectangle<float> (40.0f, 300.0f, 70.0f, 116.0f);
        g.setColour (kPanel); g.fillRoundedRectangle (b3, 2.0f);
        g.setColour (kLine); g.drawRoundedRectangle (b3, 2.0f, 0.6f);
        g.setColour (kPrint);
        text (g, "SUSTAIN", { 75.0f, 307.0f }, 5.0f);
        g.drawLine (50.0f, 307.0f, 60.0f, 307.0f, 0.5f);
        g.drawLine (90.0f, 307.0f, 100.0f, 307.0f, 0.5f);
        const Point<float> c (75.0f, 334.0f);
        for (int i = 0; i <= 20; ++i)
        {
            const float a = -135.0f + 13.5f * (float) i;
            g.drawLine (Line<float> (polar (c, 13.0f, a), polar (c, i % 2 == 0 ? 15.6f : 14.5f, a)), 0.5f);
        }
        for (int i = 0; i <= 10; ++i) text (g, String (i), polar (c, i == 10 ? 19.3f : 18.3f, -135.0f + 27.0f * (float) i), 4.6f);
        text (g, "LENGTH", c.translated (0.0f, 23.0f), 4.9f);
        text (g, "AUTO", { 60.0f, 403.0f }, 4.5f);
        text (g, "ON/OFF", { 82.0f, 403.0f }, 4.5f);

        // blocchi neri a destra
        g.setColour (Colour (0xff0c0c0c));
        g.fillRect (Rectangle<float> (1036.0f, 156.0f, 62.0f, 116.0f));
        g.fillRect (Rectangle<float> (1036.0f, 290.0f, 76.0f, 136.0f));
        // mensola tra i due manuali
        g.setGradientFill (ColourGradient (Colour (0xff1c1c1c), 0, 270.0f, Colour (0xff060606), 0, 290.0f, false));
        g.fillRect (Rectangle<float> (50.0f, 270.0f, 1062.0f, 20.0f));
        g.setColour (Colours::white.withAlpha (0.07f));
        g.drawLine (60.0f, 270.5f, 1098.0f, 270.5f, 0.6f);
        // listello sopra il manuale superiore
        g.setGradientFill (ColourGradient (Colour (0xff1e1e1e), 0, 152.0f, Colour (0xff070707), 0, 158.0f, false));
        g.fillRect (Rectangle<float> (60.0f, 152.0f, 1050.0f, 6.0f));
    }
}

float Panel::ky() const { return 71.0f; }

void Panel::paintOverChildren (Graphics& g)
{
    // LED della modulazione (lampeggia con la quadra del trigger di Schmitt)
    const auto c = ledArea.getCentre();
    const float r = 2.6f;
    const bool on = led && modOn;
    g.setColour (Colour (0xff120000)); g.fillEllipse (c.x - r - 0.8f, c.y - r - 0.8f, (r + 0.8f) * 2.0f, (r + 0.8f) * 2.0f);
    if (on)
    {
        g.setGradientFill (ColourGradient (kRed.withAlpha (0.5f), c.x, c.y, kRed.withAlpha (0.0f), c.x + r * 3.5f, c.y, true));
        g.fillEllipse (c.x - r * 3.5f, c.y - r * 3.5f, r * 7.0f, r * 7.0f);
    }
    g.setColour (on ? Colour (0xffff3020) : (modOn ? Colour (0xff5a120c) : Colour (0xff2a0806)));
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour (Colours::white.withAlpha (0.35f)); g.fillEllipse (c.x - r * 0.45f, c.y - r * 0.6f, r * 0.6f, r * 0.45f);
}

void Panel::timerCallback()
{
    const bool l = proc.lfoLed();
    const bool mo = proc.apvts.getRawParameterValue (pid::modOn)->load() > 0.5f;
    if (l != led || mo != modOn) { led = l; modOn = mo; repaint (ledArea.expanded (6.0f).toNearestInt()); }
    upper.refresh();
    lower.refresh();
    if (lever != nullptr)
    {
        const float w = proc.wheelBend();
        if (std::abs (w - lever->shown) > 0.003f) { lever->shown = w; lever->repaint(); }
    }
    if (learnId.isNotEmpty() && proc.learningIndex() < 0) { learnId.clear(); repaintAllControls(); }
}

void Panel::repaintAllControls() { for (auto* c : controls) c->repaint(); }

bool Panel::isLearning (const String& id) const
{
    const int li = proc.learningIndex();
    return li >= 0 && li == proc.paramIndexOf (id);
}

void Panel::showControlMenu (Component& c, const String& id)
{
    const int idx = proc.paramIndexOf (id);
    const int cc = proc.ccForParam (idx);
    PopupMenu m;
    m.addSectionHeader (proc.getParameters()[idx]->getName (64) + (cc >= 0 ? "  (CC " + String (cc) + ")" : String()));
    m.addItem (1, "MIDI Learn: muovi un comando della tastiera MIDI");
    m.addItem (2, "Rimuovi assegnazione MIDI", cc >= 0);
    if (proc.learningIndex() >= 0) m.addItem (3, "Annulla MIDI Learn");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&c), [this, idx, id] (int r)
    {
        if (r == 1) { proc.startLearn (idx); learnId = id; }
        else if (r == 2) proc.clearMapping (idx);
        else if (r == 3) proc.cancelLearn();
        repaintAllControls();
    });
}

void Panel::mouseDown (const MouseEvent& e)
{
    // clic sulle scritte dei selettori dei bassi
    for (int i = 0; i < 4; ++i)
        if (e.position.getDistanceFrom (polar ({ 815.0f, ky() }, 23.0f, modeSel->angleFor (i))) < 6.5f) { modeSel->select (i); return; }
    for (int i = 0; i < 3; ++i)
        if (e.position.getDistanceFrom (polar ({ 859.0f, ky() }, 22.5f, footSel->angleFor (i))) < 5.5f) { footSel->select (i); return; }
    if (logoArea.contains (e.position)) showSettingsMenu();
    else if (ledArea.expanded (3.0f).contains (e.position))
    {
        auto* pm = proc.apvts.getParameter (pid::modOn);
        pm->setValueNotifyingHost (pm->getValue() > 0.5f ? 0.0f : 1.0f);
    }
}

void Panel::showSettingsMenu()
{
    auto* mode = proc.apvts.getParameter (pid::midiMode);
    auto* split = proc.apvts.getParameter (pid::split);
    auto* tune = proc.apvts.getParameter (pid::tune);
    auto* quality = proc.apvts.getParameter (pid::quality);
    const bool splitMode = proc.apvts.getRawParameterValue (pid::midiMode)->load() > 0.5f;
    const int sp = (int) proc.apvts.getRawParameterValue (pid::split)->load();
    const float tn = proc.apvts.getRawParameterValue (pid::tune)->load();
    const bool hiQ = proc.apvts.getRawParameterValue (pid::quality)->load() > 0.5f;

    PopupMenu mm;
    mm.addItem (100, "Per canale: 1 = superiore, 2 = inferiore, 3 = pedaliera", true, ! splitMode);
    mm.addItem (101, "Split su un canale (sotto lo split il manuale inferiore)", true, splitMode);
    PopupMenu sm;
    const char* names[] = { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
    for (int n : { 48, 53, 55, 60, 65, 67, 72 })
        sm.addItem (200 + n, String (names[n % 12]) + String (n / 12 - 1) + " (nota " + String (n) + ")", true, sp == n);
    PopupMenu tm;
    for (int c : { -50, -25, -10, 0, 10, 25, 50 })
        tm.addItem (400 + c + 100, (c > 0 ? "+" : "") + String (c) + " cent", true, std::abs (tn - (float) c) < 0.5f);
    PopupMenu fm;
    fm.addItem (500, "Drawbar del manuale superiore", true, ! proc.fadersOnLower());
    fm.addItem (501, "Drawbar del manuale inferiore", true, proc.fadersOnLower());
    PopupMenu qm;
    qm.addItem (600, "Normale", true, ! hiQ);
    qm.addItem (601, "Alta (sovracampionamento doppio)", true, hiQ);

    PopupMenu m;
    m.addSectionHeader ("Organizzatore T2  -  Collateral Effects");
    m.addSubMenu ("Tastiera MIDI", mm);
    m.addSubMenu ("Punto di split", sm, splitMode);
    m.addSubMenu ("Fader della KeyLab", fm);
    m.addSubMenu ("Pitch (accordatura generale)", tm);
    m.addSubMenu ("Qualita audio", qm);
    m.addSeparator();
    m.addItem (300, "Ripristina le assegnazioni MIDI predefinite");
    m.addItem (301, "Annulla MIDI Learn", proc.learningIndex() >= 0);
    m.showMenuAsync (PopupMenu::Options(), [this, mode, split, tune, quality] (int r)
    {
        if (r == 100) mode->setValueNotifyingHost (0.0f);
        else if (r == 101) mode->setValueNotifyingHost (1.0f);
        else if (r >= 200 && r < 300) split->setValueNotifyingHost (split->convertTo0to1 ((float) (r - 200)));
        else if (r >= 400 && r < 500) tune->setValueNotifyingHost (tune->convertTo0to1 ((float) (r - 500)));
        else if (r == 500 || r == 501) proc.resetDefaultMapping (r == 501);
        else if (r == 600) quality->setValueNotifyingHost (0.0f);
        else if (r == 601) quality->setValueNotifyingHost (1.0f);
        else if (r == 300) proc.resetDefaultMapping (proc.fadersOnLower());
        else if (r == 301) proc.cancelLearn();
        repaintAllControls();
    });
}

//==============================================================================
OrgEditor::OrgEditor (OrgProcessor& p) : AudioProcessorEditor (p), proc (p), panel (p)
{
    addAndMakeVisible (panel);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) org::ui::Panel::W / (double) org::ui::Panel::H);
    setResizeLimits (936, 387, 2574, 1065);
    setSize (1462, 605);
}

void OrgEditor::resized()
{
    const float s = (float) getWidth() / (float) org::ui::Panel::W;
    panel.setBounds (0, 0, org::ui::Panel::W, org::ui::Panel::H);
    panel.setTransform (AffineTransform::scale (s));
}
