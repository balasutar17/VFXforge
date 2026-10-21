#include "vfx/Metadata.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Utf8.h"

namespace vfx {

namespace {

// Small fluent builder so the tables below read like a specification.
struct P {
    PropertyDesc d;

    P(std::string key, std::string label, ValueKind kind, Value def) {
        d.key = std::move(key);
        d.label = std::move(label);
        d.kind = kind;
        d.defaultValue = std::move(def);
    }
    P& simple() { d.level = Level::Simple; return *this; }
    P& unit(std::string u) { d.unit = std::move(u); return *this; }
    P& help(std::string h) { d.help = std::move(h); return *this; }
    P& range(double lo, double hi) { d.min = lo; d.max = hi; return *this; }
    P& ui(double lo, double hi) { d.uiMin = lo; d.uiMax = hi; return *this; }
    P& options(std::vector<std::string> o) { d.options = std::move(o); return *this; }
    operator PropertyDesc() const { return d; }
};

Value text(const char* s) { return Value(std::string(s)); }
Value num(double v) { return Value(v); }
Value integer(std::int64_t v) { return Value(v); }

ModuleTypeDesc module(std::string type, std::string label, Stage stage,
                      std::vector<PropertyDesc> properties) {
    ModuleTypeDesc m;
    m.type = std::move(type);
    m.label = std::move(label);
    m.stage = stage;
    m.properties = std::move(properties);
    return m;
}

}  // namespace

const PropertyDesc* ModuleTypeDesc::find(std::string_view key) const {
    const int i = indexOf(key);
    return i < 0 ? nullptr : &properties[static_cast<std::size_t>(i)];
}

int ModuleTypeDesc::indexOf(std::string_view key) const {
    for (std::size_t i = 0; i < properties.size(); ++i) {
        if (properties[i].key == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Registry::Registry() {
    const Color white{1, 1, 1, 1};

    modules_.push_back(module("emission", "Emission", Stage::Spawn, {
        P("rate", "Amount", ValueKind::Scalar, Scalar::constant(20))
            .simple().unit("per second").range(0, 100000).ui(0, 200)
            .help("How many particles appear each second."),
        P("bursts", "Bursts", ValueKind::Bursts, BurstList{})
            .help("Groups of particles released at set moments."),
    }));

    modules_.push_back(module("shape", "Shape", Stage::Spawn, {
        P("shape", "Shape", ValueKind::Enum, text("point"))
            .options({"point", "circle", "rectangle", "sphere", "box", "cone", "path"})
            .help("Where new particles appear. Path: along a line or outline of your own, "
                  "or one traced from a picture (a slot symbol's silhouette)."),
        P("radius", "Radius", ValueKind::Float, num(1.0))
            .range(0, 10000).ui(0, 10)
            .help("Size of a circle, sphere or cone base."),
        P("size", "Size", ValueKind::Vec3, Vec3{1, 1, 1})
            .range(0, 10000).ui(0, 10)
            .help("Width, height and depth of a rectangle or box."),
        P("angle", "Cone Angle", ValueKind::Float, num(25.0))
            .unit("degrees").range(0, 90).ui(0, 90),
        P("emitFrom", "Emit From", ValueKind::Enum, text("volume"))
            .options({"volume", "edge"})
            .help("Fill the whole shape, or only its outline."),
        P("offset", "Position", ValueKind::Vec3, Vec3{0, 0, 0})
            .range(-10000, 10000).ui(-10, 10)
            .help("Where this layer's particles appear, relative to the effect's centre."),
        P("plane", "Plane", ValueKind::Enum, text("upright"))
            .options({"upright", "ground"})
            .help("In a 3D effect, which way this layer's flat parts lie: a circle or rectangle "
                  "it is born on, particles that lie flat, and the turn of Orbit. Upright faces "
                  "the front, like a 2D effect; Ground lies on the floor, for auras, portals and "
                  "shockwaves. A 2D effect is always upright."),
        P("path", "Path", ValueKind::Points,
          PointList{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}})
            .help("The points of the Path shape, in the effect's units, relative to Position. "
                  "Traced from a picture with Fit to target."),
        P("fitTarget", "Fit to Target", ValueKind::Bool, Value(false))
            .help("The Path follows the effect's target picture: traced from its silhouette again "
                  "whenever the target changes, so the effect fits any symbol."),
        P("closed", "Closed", ValueKind::Bool, Value(true))
            .help("A closed path goes all the way round (an outline); an open one is a line."),
        P("padding", "Padding", ValueKind::Float, num(0.0))
            .range(-100, 100).ui(-1, 1)
            .help("Moves the path outward (above 0) or inward (below 0) from where it was drawn or traced."),
        P("thickness", "Thickness", ValueKind::Float, num(0.0))
            .range(0, 100).ui(0, 1)
            .help("How wide a band along the path particles appear in."),
        P("travel", "Travel", ValueKind::Float, num(0.0))
            .unit("rounds per second").range(-50, 50).ui(-3, 3)
            .help("Energy running along the path: particles appear only near a moving head, "
                  "this many times round a second (below 0 the other way). 0: all along at once."),
        P("travelSpan", "Travel Length", ValueKind::Float, num(0.2))
            .range(0.001, 1).ui(0.01, 1)
            .help("How much of the path the running energy covers at once, from a dot to all of it."),
        P("travelHeads", "Travel Heads", ValueKind::Int, integer(1))
            .range(1, 16).ui(1, 8)
            .help("How many heads run along the path, evenly spaced."),
        P("travelMode", "Travel Mode", ValueKind::Enum, text("run"))
            .options({"run", "draw"})
            .help("Run: the energy runs round and round. Draw: the path is drawn on from its start, "
                  "once, at Travel rounds per second (with long-lived particles it stays drawn)."),
        P("alignToPath", "Align to Path", ValueKind::Bool, Value(false))
            .help("Each particle is turned to lie along the path: for crackles and streaks that follow an outline."),
        P("aim", "Send", ValueKind::Enum, text("direction"))
            .options({"direction", "outward", "along"})
            .help("Which way particles from a path are sent: the layer's Direction, straight out "
                  "from the path (in for below 0 speed), or along it."),
    }));

    modules_.push_back(module("initial", "Initial State", Stage::Spawn, {
        P("lifetime", "Lifetime", ValueKind::Scalar, Scalar::constant(1.0))
            .simple().unit("seconds").range(0.001, 3600).ui(0, 10)
            .help("How long each particle lasts."),
        P("speed", "Speed", ValueKind::Scalar, Scalar::constant(2.0))
            .simple().unit("units per second").range(-10000, 10000).ui(0, 20)
            .help("How fast particles leave the emitter."),
        P("direction", "Direction", ValueKind::Vec3, Vec3{0, 1, 0})
            .simple().range(-1000, 1000).ui(-1, 1)
            .help("Which way particles are sent."),
        P("spread", "Spread", ValueKind::Float, num(30.0))
            .simple().unit("degrees").range(0, 180).ui(0, 180)
            .help("How far particles fan out from the direction."),
        P("size", "Size", ValueKind::Scalar, Scalar::constant(0.2))
            .simple().unit("units").range(0, 10000).ui(0, 5),
        P("rotation", "Rotation", ValueKind::Scalar, Scalar::constant(0.0))
            .unit("degrees").range(-36000, 36000).ui(-180, 180),
        P("color", "Color", ValueKind::Color, white).simple(),
        P("opacity", "Opacity", ValueKind::Scalar, Scalar::constant(1.0))
            .range(0, 1).ui(0, 1)
            .help("How see-through each particle starts, multiplying its colour's own. As a curve it reads "
                  "across the layer's time: a layer that brightens and dims without thinning out."),
    }));

    modules_.push_back(module("motion", "Motion", Stage::Update, {
        P("gravity", "Gravity", ValueKind::Vec3, Vec3{0, -9.8, 0})
            .unit("units per second squared").range(-10000, 10000).ui(-20, 20),
        P("drag", "Drag", ValueKind::Float, num(0.0))
            .range(0, 1000).ui(0, 10)
            .help("Slows particles down over time."),
        P("spin", "Spin", ValueKind::Scalar, Scalar::constant(0.0))
            .unit("degrees per second").range(-36000, 36000).ui(-360, 360),
        P("turbulence", "Turbulence", ValueKind::Float, num(0.0))
            .simple().unit("units per second").range(0, 1000).ui(0, 6)
            .help("Carries each particle along a field of swirls, the way air carries smoke: "
                  "0 is none, higher wanders faster. It adds to the particle's own movement."),
        P("turbulenceSize", "Swirl Size", ValueKind::Float, num(1.5))
            .simple().unit("units").range(0.02, 10000).ui(0.2, 6)
            .help("How large the swirls are. Small swirls jitter; large ones drift and curl."),
        P("turbulenceChange", "Swirl Change", ValueKind::Float, num(0.6))
            .unit("per second").range(0, 60).ui(0, 3)
            .help("How quickly the swirls are replaced by new ones. 0 keeps them where they are, "
                  "so particles follow the same winding paths."),
        P("speedOverLife", "Speed over Life", ValueKind::Scalar, Scalar::constant(1.0))
            .unit("multiplier").range(-20, 20).ui(0, 2)
            .help("Speeds each particle up or slows it down along its own path, from birth to death: "
                  "1 is its own speed, 0 stands still, below 0 goes backwards. As a curve this is "
                  "slow in and slow out, a wind-up before the launch, or an overshoot that settles back."),
        P("orbit", "Orbit", ValueKind::Float, num(0.0))
            .unit("degrees per second").range(-3600, 3600).ui(-360, 360)
            .help("Turns every particle about the layer's centre as it travels, so straight paths "
                  "become arcs and spirals. Above 0 turns counter-clockwise, below 0 clockwise."),
    }));

    modules_.push_back(module("overLife", "Over Lifetime", Stage::Update, {
        P("size", "Size", ValueKind::Scalar, Scalar::curve({{0, 1}, {1, 1}}))
            .unit("multiplier").range(0, 1000).ui(0, 2)
            .help("Scales each particle from birth to death."),
        P("color", "Color", ValueKind::Gradient, Gradient{{{0, white}, {1, white}}})
            .help("Tints each particle from birth to death."),
        P("opacity", "Opacity", ValueKind::Scalar, Scalar::curve({{0, 1}, {1, 0}}))
            .range(0, 1).ui(0, 1)
            .help("Fades each particle from birth to death."),
        P("dissolve", "Dissolve", ValueKind::Float, num(0.0))
            .simple().range(0, 1).ui(0, 1)
            .help("Eats each particle away in ragged patches instead of only fading it: 0 never, "
                  "0.5 over the second half of its life, 1 over the whole of it."),
        P("dissolveSoft", "Dissolve Softness", ValueKind::Float, num(0.15))
            .simple().range(0, 1).ui(0, 1)
            .help("How soft the eaten edge is: 0 torn like paper, 1 thinning away like smoke."),
        P("dissolveEdge", "Dissolve Edge Glow", ValueKind::Float, num(0.0))
            .simple().range(0, 1).ui(0, 1)
            .help("How brightly the eaten edge glows, like paper burning: 0 not at all (smoke, "
                  "mist), 1 white-hot."),
        P("stretch", "Squash and Stretch", ValueKind::Scalar, Scalar::constant(0.0))
            .simple().range(-4, 4).ui(-0.6, 0.6)
            .help("Above 0 each particle is drawn longer and thinner, below 0 shorter and wider, "
                  "always keeping its area: 0.5 is half as long again. Along its own upright, or the "
                  "way it is moving when Align is set to movement. As a curve it stretches a shape "
                  "as it launches and squashes it as it lands."),
    }));

    modules_.push_back(module("sprite", "Sprite", Stage::Render, {
        P("texture", "Texture", ValueKind::Asset, AssetRef{})
            .help("Your own picture or sprite sheet, drawn for each particle. None draws the Shape."),
        P("blend", "Blend Mode", ValueKind::Enum, text("alpha"))
            .simple()
            .options({"alpha", "dissolve",
                      "darken", "multiply", "colorburn", "linearburn", "darkercolor",
                      "lighten", "screen", "colordodge", "additive", "lightercolor",
                      "overlay", "softlight", "hardlight", "vividlight", "linearlight", "pinlight", "hardmix",
                      "difference", "exclusion", "subtract", "divide",
                      "hue", "saturation", "color", "luminosity"})
            .help("How this layer's particles are laid over what is behind them, as Photoshop's layer "
                  "blend modes do. Normal covers it; Linear Dodge (Add) adds light, so overlaps glow; "
                  "Multiply darkens; Screen lightens."),
        P("glow", "Glow", ValueKind::Float, num(1.0))
            .simple().range(0, 1000).ui(0, 10)
            .help("Brightness multiplier. Above 1 the particle glows."),
        P("facing", "Facing", ValueKind::Enum, text("camera"))
            .options({"camera", "plane"})
            .help("Turn toward the camera, or stay flat on the effect's plane."),
        P("shape", "Shape", ValueKind::Enum, text("soft"))
            .simple()
            .options({"soft", "disc", "ring", "bubble", "sparkle", "star", "smoke", "square",
                      "diamond", "heart", "streak", "flame", "puff", "burst", "crescent", "orb",
                      "glint", "blaze", "candy", "shard", "drop", "splat", "shockwave",
                      "twinkle", "flare", "rays", "swirl", "bean", "bolt",
                      "starflash", "sliver", "shardring", "vortex", "sigil", "beam",
                      // The shape library (drawn from pictures; see the editor's
                      // ShapeLibrary.h). New shapes go at the end.
                      "triangle", "pentagon", "hexagon", "octagon", "oval", "capsule", "halfdisc",
                      "wedge", "plus", "cross", "frame", "hexframe", "arc", "dashring",
                      "doublering", "softring", "starfour", "starsix", "stareight", "starfat",
                      "starline", "pinwheel", "sunburst", "pow", "bang", "speedlines",
                      "crossflare", "bokeh", "glowdiamond", "cloud", "puffball", "dustcloud",
                      "wisp", "smokering", "steam", "fireball", "ember", "flametongue", "spark",
                      "zigzag", "lightcone", "halo", "glowbar", "droplet", "splash", "ripple",
                      "wave", "leaf", "petal", "flower", "clover", "snowflake", "feather",
                      "pebble", "raindash", "coin", "gem", "emerald", "crystal", "crown", "trophy",
                      "key", "gift", "ribbon", "confetti", "balloon", "donut", "bonbon", "medal",
                      "note", "cherry", "pentacle", "triad", "spiral", "hexshield", "eye", "moon",
                      "wing", "skull", "octagram", "portalring", "crosshair", "yinyang", "runea",
                      "runeb", "runec", "slash", "claw", "crack", "impact", "rock", "splinter",
                      "glass", "gear", "bone", "brick", "arrow", "chevron", "check", "exclaim",
                      "question", "lightning", "shield", "sword", "paw", "zed", "infinity", "lock"})
            .help("What each particle looks like when it has no texture."),
        P("align", "Align", ValueKind::Enum, text("none"))
            .options({"none", "movement"})
            .help("Keep each particle upright, or point it the way it is moving."),
        P("stretch", "Stretch", ValueKind::Float, num(0.0))
            .unit("seconds").range(0, 10).ui(0, 0.3)
            .help("Draws a moving particle as a streak this many seconds of travel long. "
                  "Needs Align set to movement."),
        P("columns", "Columns", ValueKind::Int, integer(1)).range(1, 64)
            .help("For a sprite sheet: how many pictures across the image."),
        P("rows", "Rows", ValueKind::Int, integer(1)).range(1, 64)
            .help("For a sprite sheet: how many pictures down the image."),
        P("frames", "Frames", ValueKind::Int, integer(0)).range(0, 4096)
            .help("How many pictures the sheet holds, read left to right and top to bottom. "
                  "0 uses every cell."),
        P("animate", "Animate", ValueKind::Enum, text("life"))
            .options({"life", "loop", "random"})
            .help("Play the sheet once over each particle's life, loop it at the frame rate, "
                  "or give each particle one random picture."),
        P("fps", "Frame Rate", ValueKind::Float, num(12.0))
            .unit("frames per second").range(0.1, 240).ui(1, 60)
            .help("How fast a looping sheet plays."),
        P("randomStart", "Random Start", ValueKind::Bool, Value(false))
            .help("Start each particle's loop on a different picture."),
        P("trail", "Trail", ValueKind::Float, num(0.0))
            .unit("seconds").range(0, 5).ui(0, 1)
            .help("Draws a ribbon behind each particle along the path it took this many "
                  "seconds back. 0 draws none."),
        P("trailWidth", "Trail Width", ValueKind::Float, num(0.6))
            .range(0, 4).ui(0, 2)
            .help("How wide the trail starts, as a share of the particle's size. It narrows "
                  "to nothing at its end."),
        P("tilt", "Lie Flat", ValueKind::Float, num(0.0))
            .simple().unit("degrees").range(0, 89).ui(0, 85)
            .help("Leans the particle back as if it lay on the ground and you saw it from an "
                  "angle: 0 faces you, higher is flatter. It still spins in its own plane, so a "
                  "turning ring stays a level oval. For magic circles, portals and shockwaves "
                  "on the floor. Not used when Align is set to movement."),
        P("hold", "Hold Frames", ValueKind::Float, num(0.0))
            .simple().unit("drawings per second").range(0, 60).ui(0, 24)
            .help("Shows this layer the way hand-drawn animation is shown: this many drawings a "
                  "second, each held still until the next. 12 is animating \"on twos\", 8 \"on "
                  "threes\". 0 is smooth."),
        // Changes to the shape's own design. They are set from the Shape
        // control, which shows only the ones the chosen shape has.
        P("shapeCount", "Shape Points", ValueKind::Int, integer(0))
            .range(0, 31).ui(0, 16)
            .help("How many points a star has, sides a polygon, rays a sunburst, teeth a gear. "
                  "0 is the shape as it was drawn. Shapes with nothing to count ignore it."),
        P("shapeThickness", "Shape Thickness", ValueKind::Float, num(0.0))
            .range(-1, 1).ui(-1, 1)
            .help("Makes a ring, an outline or the arms of a plus thinner (below 0) or thicker "
                  "(above 0). 0 is the shape as it was drawn."),
        P("shapeSharpness", "Shape Pointiness", ValueKind::Float, num(0.0))
            .range(-1, 1).ui(-1, 1)
            .help("Makes a star's points blunter (below 0) or longer and thinner (above 0). "
                  "0 is the shape as it was drawn."),
        P("shapeRound", "Shape Roundness", ValueKind::Float, num(0.0))
            .range(-1, 1).ui(-1, 1)
            .help("Makes the corners of a shape sharper (below 0) or rounder (above 0). "
                  "0 is the shape as it was drawn."),
    }));

    modules_.push_back(module("sound", "Sound", Stage::Audio, {
        P("sound", "Sound", ValueKind::Asset, AssetRef{})
            .help("The sound this layer plays. Your own sound, or one from the library."),
        P("play", "Play", ValueKind::Enum, text("start"))
            .options({"start", "bursts"})
            .help("Play once when the layer starts, or at every burst of this layer's particles."),
        P("delay", "Delay", ValueKind::Float, num(0.0))
            .unit("seconds").range(-10, 10).ui(-0.5, 1)
            .help("When the sound starts, from the moment it is played for. Below zero it "
                  "starts early: a whoosh before the hit."),
        P("volume", "Volume", ValueKind::Float, num(1.0))
            .range(0, 4).ui(0, 2),
        P("pitch", "Pitch", ValueKind::Float, num(0.0))
            .unit("semitones").range(-24, 24).ui(-12, 12)
            .help("Higher or lower. 12 semitones is one octave."),
        P("pan", "Pan", ValueKind::Float, num(0.0))
            .range(-1, 1).ui(-1, 1)
            .help("Left (-1) to right (1)."),
        P("fadeIn", "Fade In", ValueKind::Float, num(0.0))
            .unit("seconds").range(0, 60).ui(0, 2),
        P("fadeOut", "Fade Out", ValueKind::Float, num(0.0))
            .unit("seconds").range(0, 60).ui(0, 2),
        P("loop", "Loop", ValueKind::Bool, Value(false))
            .help("Repeat the sound until the layer ends."),
        P("trimStart", "Trim Start", ValueKind::Float, num(0.0))
            .unit("seconds").range(0, 3600).ui(0, 2)
            .help("Skip this much of the start of the sound."),
        P("length", "Length", ValueKind::Float, num(0.0))
            .unit("seconds").range(0, 3600).ui(0, 5)
            .help("How much of the sound to play. 0 plays it to the end."),
        P("randomPitch", "Random Pitch", ValueKind::Float, num(0.0))
            .unit("semitones").range(0, 12).ui(0, 4)
            .help("Each time it plays, the pitch moves up or down by up to this much, so "
                  "repeats never sound the same."),
        P("randomVolume", "Random Volume", ValueKind::Float, num(0.0))
            .range(0, 1).ui(0, 1)
            .help("Each time it plays, it is up to this much quieter."),
        P("mute", "Mute", ValueKind::Bool, Value(false)),
    }));

    effect_ = module("effect", "Effect", Stage::Update, {
        P("name", "Name", ValueKind::Text, text("Untitled")),
        P("space", "Space", ValueKind::Enum, text("2d")).options({"2d", "3d"}),
        P("seed", "Seed", ValueKind::Int, integer(1)).range(0, 4294967295.0)
            .help("Same seed, same result. Change it for a new variation."),
        P("duration", "Duration", ValueKind::Float, num(2.0))
            .unit("seconds").range(0.01, 3600).ui(0, 10),
        P("loop", "Loop", ValueKind::Enum, text("loop")).options({"once", "loop"}),
        P("frameRate", "Frame Rate", ValueKind::Float, num(30.0))
            .unit("frames per second").range(1, 240).ui(12, 60),
        P("target", "Target", ValueKind::Asset, AssetRef{})
            .help("The picture the effect is made for: layers set to Fit to target follow its silhouette."),
        P("targetWidth", "Target Width", ValueKind::Float, num(3.0))
            .range(0.01, 10000).ui(0.5, 10).help("How wide the target is drawn, in units."),
        P("targetPosition", "Target Position", ValueKind::Vec3, Vec3{0, 0, 0})
            .range(-10000, 10000).ui(-10, 10).help("Where the middle of the target is."),
    });

    layer_ = module("layer", "Layer", Stage::Update, {
        P("name", "Name", ValueKind::Text, text("Layer")),
        P("enabled", "Enabled", ValueKind::Bool, Value(true)),
        P("start", "Start", ValueKind::Float, num(0.0))
            .unit("seconds").range(0, 3600).ui(0, 10),
        P("duration", "Duration", ValueKind::Float, num(2.0))
            .unit("seconds").range(0.01, 3600).ui(0, 10),
        P("role", "Role", ValueKind::Text, text(""))
            .help("What the layer is for: glow, core, ring, sparks, streaks, body, smoke, trail."),
        P("locked", "Locked", ValueKind::Bool, Value(false))
            .help("A locked layer is left alone by Refine and by rebuilding from a reference."),
        P("group", "Group", ValueKind::Text, text(""))
            .help("The group the layer belongs to: an effect added from the library is one group."),
    });
}

const Registry& Registry::builtin() {
    static const Registry instance;
    return instance;
}

const ModuleTypeDesc* Registry::findModule(std::string_view type) const {
    for (const auto& m : modules_) {
        if (m.type == type) {
            return &m;
        }
    }
    return nullptr;
}

namespace {

Error bad(const PropertyDesc& d, const std::string& why) {
    return makeError(d.label + " " + why, "property '" + d.key + "'");
}

std::string trimNumber(double v) {
    std::string s = std::to_string(v);
    while (!s.empty() && s.back() == '0') {
        s.pop_back();
    }
    if (!s.empty() && s.back() == '.') {
        s.pop_back();
    }
    return s;
}

Status checkNumber(const PropertyDesc& d, double v) {
    if (!std::isfinite(v)) {
        return bad(d, "must be a real number.");
    }
    if (d.min && v < *d.min) {
        return bad(d, "cannot be less than " + trimNumber(*d.min) + ".");
    }
    if (d.max && v > *d.max) {
        return bad(d, "cannot be more than " + trimNumber(*d.max) + ".");
    }
    return {};
}

Status checkColor(const PropertyDesc& d, const Color& c) {
    for (double v : {c.r, c.g, c.b, c.a}) {
        if (!std::isfinite(v) || v < 0) {
            return bad(d, "has an invalid colour value.");
        }
    }
    if (c.r > 1000 || c.g > 1000 || c.b > 1000 || c.a > 1) {
        return bad(d, "has a colour value that is out of range.");
    }
    return {};
}

double clampNumber(const PropertyDesc& d, double v) {
    if (d.min && v < *d.min) {
        v = *d.min;
    }
    if (d.max && v > *d.max) {
        v = *d.max;
    }
    return v;
}

Color clampColor(const Color& c) {
    auto fix = [](double v, double hi) {
        if (!std::isfinite(v)) {
            return 1.0;
        }
        return std::clamp(v, 0.0, hi);
    };
    return Color{fix(c.r, 1000), fix(c.g, 1000), fix(c.b, 1000), fix(c.a, 1)};
}

}  // namespace

Status validateValue(const PropertyDesc& d, const Value& value) {
    if (!holdsKind(value, d.kind)) {
        return bad(d, std::string("needs ") + kindDescription(d.kind) + ".");
    }
    switch (d.kind) {
        case ValueKind::Bool:
        case ValueKind::Asset:
            return {};
        case ValueKind::Int: {
            const auto v = static_cast<double>(std::get<std::int64_t>(value));
            return checkNumber(d, v);
        }
        case ValueKind::Float:
            return checkNumber(d, std::get<double>(value));
        case ValueKind::Text: {
            const auto& s = std::get<std::string>(value);
            if (s.size() > kMaxTextLength) {
                return bad(d, "is too long.");
            }
            if (!detail::isValidUtf8(s)) {
                return bad(d, "contains characters that cannot be saved.");
            }
            return {};
        }
        case ValueKind::Enum: {
            const auto& s = std::get<std::string>(value);
            if (std::find(d.options.begin(), d.options.end(), s) == d.options.end()) {
                return bad(d, "does not have an option called \"" + s + "\".");
            }
            return {};
        }
        case ValueKind::Vec3: {
            const auto& v = std::get<Vec3>(value);
            for (double c : {v.x, v.y, v.z}) {
                if (auto s = checkNumber(d, c); !s) {
                    return s;
                }
            }
            return {};
        }
        case ValueKind::Color:
            return checkColor(d, std::get<Color>(value));
        case ValueKind::Scalar: {
            const auto& s = std::get<Scalar>(value);
            switch (s.kind) {
                case Scalar::Kind::Constant:
                    return checkNumber(d, s.a);
                case Scalar::Kind::Random:
                    if (auto r = checkNumber(d, s.a); !r) {
                        return r;
                    }
                    if (auto r = checkNumber(d, s.b); !r) {
                        return r;
                    }
                    if (s.a > s.b) {
                        return bad(d, "has a minimum above its maximum.");
                    }
                    return {};
                case Scalar::Kind::Curve: {
                    if (s.keys.empty() || s.keys.size() > kMaxCurveKeys) {
                        return bad(d, "needs between 1 and 1024 curve points.");
                    }
                    double previous = -1;
                    for (const auto& k : s.keys) {
                        if (!std::isfinite(k.t) || k.t < 0 || k.t > 1 || k.t <= previous) {
                            return bad(d, "has curve points that are out of order.");
                        }
                        previous = k.t;
                        if (auto r = checkNumber(d, k.v); !r) {
                            return r;
                        }
                    }
                    return {};
                }
            }
            return bad(d, "has an unknown form.");
        }
        case ValueKind::Gradient: {
            const auto& g = std::get<Gradient>(value);
            if (g.keys.empty() || g.keys.size() > kMaxCurveKeys) {
                return bad(d, "needs between 1 and 1024 colour stops.");
            }
            double previous = -1;
            for (const auto& k : g.keys) {
                if (!std::isfinite(k.t) || k.t < 0 || k.t > 1 || k.t <= previous) {
                    return bad(d, "has colour stops that are out of order.");
                }
                previous = k.t;
                if (auto r = checkColor(d, k.color); !r) {
                    return r;
                }
            }
            return {};
        }
        case ValueKind::Points: {
            const auto& p = std::get<PointList>(value);
            if (p.points.size() > kMaxPathPoints) {
                return bad(d, "has too many points.");
            }
            for (const auto& pt : p.points) {
                if (!std::isfinite(pt.x) || !std::isfinite(pt.y) || std::fabs(pt.x) > 10000 || std::fabs(pt.y) > 10000) {
                    return bad(d, "has a point that is not valid.");
                }
            }
            return {};
        }
        case ValueKind::Bursts: {
            const auto& b = std::get<BurstList>(value);
            if (b.items.size() > kMaxBursts) {
                return bad(d, "has too many bursts.");
            }
            for (const auto& item : b.items) {
                if (!std::isfinite(item.time) || item.time < 0 || item.time > 3600) {
                    return bad(d, "has a burst at an invalid time.");
                }
                if (item.count < 0 || item.count > 1000000) {
                    return bad(d, "has a burst with an invalid particle count.");
                }
            }
            return {};
        }
    }
    return bad(d, "has an unknown type.");
}

Value repairValue(const PropertyDesc& d, const Value& value) {
    if (!holdsKind(value, d.kind)) {
        return d.defaultValue;
    }
    Value out = value;
    switch (d.kind) {
        case ValueKind::Bool:
        case ValueKind::Asset:
        case ValueKind::Enum:
            break;
        case ValueKind::Int: {
            const auto v = std::get<std::int64_t>(out);
            const double clamped = clampNumber(d, static_cast<double>(v));
            out = static_cast<std::int64_t>(clamped);
            break;
        }
        case ValueKind::Float:
            out = clampNumber(d, std::get<double>(out));
            break;
        case ValueKind::Text: {
            auto& s = std::get<std::string>(out);
            if (detail::isValidUtf8(s)) {
                s = detail::truncateUtf8(s, kMaxTextLength);
            }
            break;
        }
        case ValueKind::Vec3: {
            auto& v = std::get<Vec3>(out);
            v.x = clampNumber(d, v.x);
            v.y = clampNumber(d, v.y);
            v.z = clampNumber(d, v.z);
            break;
        }
        case ValueKind::Color:
            out = clampColor(std::get<Color>(out));
            break;
        case ValueKind::Scalar: {
            // Only the fields the current form uses are touched; the others
            // stay at zero so equal values always compare equal.
            auto& s = std::get<Scalar>(out);
            if (s.kind == Scalar::Kind::Constant) {
                s.a = clampNumber(d, s.a);
            } else if (s.kind == Scalar::Kind::Random) {
                s.a = clampNumber(d, s.a);
                s.b = clampNumber(d, s.b);
                if (s.a > s.b) {
                    std::swap(s.a, s.b);
                }
            } else if (s.kind == Scalar::Kind::Curve) {
                if (s.keys.size() > kMaxCurveKeys) {
                    s.keys.resize(kMaxCurveKeys);
                }
                for (auto& k : s.keys) {
                    if (std::isfinite(k.t)) {
                        k.t = std::clamp(k.t, 0.0, 1.0);
                    }
                    k.v = clampNumber(d, k.v);
                }
                std::stable_sort(s.keys.begin(), s.keys.end(),
                                 [](const CurveKey& x, const CurveKey& y) { return x.t < y.t; });
                s.keys.erase(std::unique(s.keys.begin(), s.keys.end(),
                                         [](const CurveKey& x, const CurveKey& y) {
                                             return x.t == y.t;
                                         }),
                             s.keys.end());
            }
            break;
        }
        case ValueKind::Gradient: {
            auto& g = std::get<Gradient>(out);
            if (g.keys.size() > kMaxCurveKeys) {
                g.keys.resize(kMaxCurveKeys);
            }
            for (auto& k : g.keys) {
                if (std::isfinite(k.t)) {
                    k.t = std::clamp(k.t, 0.0, 1.0);
                }
                k.color = clampColor(k.color);
            }
            std::stable_sort(g.keys.begin(), g.keys.end(),
                             [](const GradientKey& x, const GradientKey& y) { return x.t < y.t; });
            g.keys.erase(std::unique(g.keys.begin(), g.keys.end(),
                                     [](const GradientKey& x, const GradientKey& y) {
                                         return x.t == y.t;
                                     }),
                         g.keys.end());
            break;
        }
        case ValueKind::Points: {
            auto& p = std::get<PointList>(out);
            if (p.points.size() > kMaxPathPoints) {
                p.points.resize(kMaxPathPoints);
            }
            std::vector<Point2> kept;
            for (const auto& pt : p.points) {
                if (std::isfinite(pt.x) && std::isfinite(pt.y)) {
                    kept.push_back(Point2{std::clamp(pt.x, -10000.0, 10000.0), std::clamp(pt.y, -10000.0, 10000.0)});
                }
            }
            p.points = std::move(kept);
            break;
        }
        case ValueKind::Bursts: {
            auto& b = std::get<BurstList>(out);
            if (b.items.size() > kMaxBursts) {
                b.items.resize(kMaxBursts);
            }
            for (auto& item : b.items) {
                if (std::isfinite(item.time)) {
                    item.time = std::clamp(item.time, 0.0, 3600.0);
                }
                item.count = std::clamp<std::int64_t>(item.count, 0, 1000000);
            }
            break;
        }
    }
    if (!validateValue(d, out)) {
        return d.defaultValue;
    }
    return out;
}

}  // namespace vfx
