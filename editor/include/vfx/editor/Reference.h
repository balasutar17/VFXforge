// VFX Forge editor: reading a reference picture or clip.
//
// An artist brings a picture, a GIF or a short video of an effect they want
// to recreate. This file looks at it and writes down what can be measured:
// where the effect is, how big, which colours, whether it glows, whether it
// has rings, rays, sparks or streaks, and (for a clip) how it changes over
// time. Reconstruct.h turns those measurements into an editable effect.
//
// Nothing here guesses silently. Every finding says how sure it is and
// whether it was seen in the picture or filled in because the picture could
// not show it (a still image says nothing about speed, for instance).
//
// It is ordinary image arithmetic: no network, no trained model. Lengths in
// the results are fractions of the picture's height after cropping, and
// positions are fractions of its width and height from the top-left.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "vfx/Program.h"
#include "vfx/Result.h"
#include "vfx/editor/Image.h"

namespace vfx::editor {

// The reference as loaded: one picture, or the frames of a clip. All frames
// are the same size. The original file is kept elsewhere, untouched; these
// are only the pixels that are looked at.
struct Reference {
    std::string name;
    std::vector<Image> frames;
    double framesPerSecond = 0;  // 0 for a single picture

    bool moving() const { return frames.size() > 1 && framesPerSecond > 0; }
    double seconds() const {
        return moving() ? static_cast<double>(frames.size()) / framesPerSecond : 0.0;
    }
};

// Checks that every frame is a real picture of one size, and that there are
// not more than can be handled.
inline constexpr int kMaxReferenceFrames = 360;
inline constexpr int kMaxReferenceSide = 4096;
Status validateReference(const Reference& reference);

// What is behind the effect in the reference.
enum class Backdrop {
    Auto,         // work it out
    Transparent,  // the picture's own see-through channel says where the effect is
    Dark,         // light on a dark background
    Light,        // drawn on a light background
    Colour,       // a plain colour the artist picked
};
const char* backdropName(Backdrop backdrop);

struct ReferenceOptions {
    Backdrop backdrop = Backdrop::Auto;
    float keyR = 0, keyG = 0, keyB = 0;  // for Backdrop::Colour, as on screen, 0 to 1

    // The part of the picture to look at, as fractions of its size.
    float cropLeft = 0, cropTop = 0, cropRight = 1, cropBottom = 1;

    // For a clip: the frames to use (lastFrame < 0 means to the end), and
    // how much faster than the clip the effect should play.
    int firstFrame = 0;
    int lastFrame = -1;
    double speed = 1.0;

    // How faint something may be and still count as part of the effect,
    // 0 to 1. Below zero means "decide from how even the background is".
    float cutoff = -1.0f;

    // The longest side of the grid the picture is studied on. More is finer
    // and slower: 128 for a quick look, 192 normally, 256 for fine detail.
    int detail = 192;
};

// The effect separated from its background, on the analysis grid.
struct Matte {
    int width = 0, height = 0;
    std::vector<float> cover;    // how much effect is at each point, 0 to 1
    std::vector<float> r, g, b;  // the effect's own colour there, as on screen

    std::size_t at(int x, int y) const {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(x);
    }
};

struct Swatch {
    float r = 1, g = 1, b = 1;  // as on screen, 0 to 1
    float share = 0;            // how much of the effect is this colour, 0 to 1
};

enum class Basis {
    Observed,  // measured in the reference
    Inferred,  // filled in, because the reference cannot show it
};

// One line of the analysis report.
struct Finding {
    std::string key;    // stable, for code and tests: "glow", "ring", "sparks"...
    std::string label;  // for the artist
    std::string value;  // what was found, in words
    float confidence = 0;  // 0 to 1
    Basis basis = Basis::Observed;
    std::string note;   // why it is uncertain, when it is
};

struct RingFound {
    float radius = 0;     // fraction of the height
    float thickness = 0;  // fraction of the height
    float strength = 0;   // how solid, 0 to 1
    float around = 0;     // how much of the circle it covers, 0 to 1
    Swatch colour;
    std::string shape;    // the closest built-in ring shape
    float growth = 0;     // radius gained per second, when a clip showed it; else 0
};

// A group of small separate pieces: sparks, or streaks.
struct PieceGroup {
    int count = 0;
    float sizeLow = 0, sizeHigh = 0;  // across each piece, fraction of the height
    float lengthLow = 0, lengthHigh = 0;  // along each piece (streaks)
    float nearest = 0, farthest = 0;  // distance from the centre
    Swatch colour;
    std::vector<Swatch> colours;  // when they come in clearly different colours: each, with its share
    std::vector<float> colourSizes;  // and how large the pieces of each colour typically are
    float softness = 0;       // 0 crisp, 1 soft
    bool radial = true;       // pointing away from the centre (streaks)
    float heading = 90;       // where they sit or point, degrees, 0 right, 90 up
    float spread = 180;       // half-angle around the heading that holds them
    std::string shape;        // the built-in shape they most resemble; may be empty
    // From a clip, when it could be measured. Zero when unknown.
    float speed = 0;          // fraction of the height per second, outward
    float lifetime = 0;       // seconds
    float fall = 0;           // downward pull, fraction of the height per second squared
};

// What one picture shows.
struct StillAnalysis {
    int width = 0, height = 0;  // the analysis grid

    Backdrop backdrop = Backdrop::Dark;  // as worked out
    Swatch backdropColour;
    float backdropEvenness = 1;  // 1 a perfectly plain background, 0 a busy one
    float cutoff = 0;
    bool additive = true;  // light added onto the background, rather than paint over it

    float centreX = 0.5f, centreY = 0.5f;  // of the effect
    float hotX = 0.5f, hotY = 0.5f;        // its brightest area
    float extent = 0;       // the radius that holds 95% of it
    float halfExtent = 0;   // the radius that holds half of it
    float fill = 0;         // how much of the picture it covers, 0 to 1
    float elongation = 1;   // 1 round; 3 three times as long as wide
    float axis = 0;         // the long direction, degrees, 0 right, 90 up
    float hardness = 0;     // 0 all soft glow, 1 crisp flat shapes
    float mirror = 0;       // how alike left and right are, 0 to 1
    std::vector<Swatch> palette;  // most-used first

    // The round part, read from the centre outward.
    // White at the centre that the glow's colour cannot explain: a white
    // light of its own. (A coloured glow strong enough to burn white in the
    // middle needs none; whiteHot is how far out that reaches.)
    bool hasCore = false;
    float coreRadius = 0;   // the radius its soft sprite fades out at
    float coreLevel = 0;
    Swatch coreColour;
    float whiteHot = 0;
    bool hasGlow = false;
    float glowInner = 0, glowOuter = 0;        // the two radii a glow is fitted with
    float glowInnerLevel = 0, glowOuterLevel = 0;  // their brightness, 0 to 1 and above
    Swatch glowInnerColour, glowOuterColour;
    bool hasBody = false;     // a solid, crisp-edged main shape
    float bodyRadius = 0;
    Swatch bodyColour;
    std::string bodyShape;    // the closest built-in shape
    float bodyMatch = 0;      // how close, 0 to 1
    float bodyTurn = 0;       // degrees it is turned
    int bodyLobes = 0;        // bumps round its outline

    // Rays out of the centre.
    int rays = 0;
    float rayReach = 0;     // how far a typical ray runs
    float rayLongest = 0;   // and the longest
    float rayStrength = 0;  // 0 to 1
    bool raysEven = false;  // evenly spaced
    std::string rayShape;   // the closest built-in flash shape
    float rayMatch = 0;
    float rayTurn = 0;
    Swatch rayColour;

    std::vector<RingFound> rings;
    PieceGroup sparks;
    PieceGroup streaks;
    PieceGroup bits;  // larger loose pieces

    bool hasSmoke = false;
    float smokeRadius = 0;
    Swatch smokeColour;

    // A head with a tail behind it: a comet, a projectile, a flame.
    bool comet = false;
    float headX = 0, headY = 0;
    float headRadius = 0;
    float tailHeading = 0;  // the way the tail points, away from the head
    float tailLength = 0;
    float tailWidthStart = 0, tailWidthEnd = 0;
    std::vector<Swatch> tailColours;  // from the head to the tip

    // More than one separate effect in the picture (a sheet of several).
    int separate = 1;

    std::vector<float> radial;  // brightness from the centre outward, 48 steps to 1.5 x extent
};

struct TimeMarker {
    double time = 0;    // seconds from the first frame used
    std::string key;    // "appear", "burst", "peak", "widest", "second", "fading", "gone"
    std::string label;  // for the artist
};

// How a clip changes. Times are in the effect's own seconds: the clip's
// time divided by the speed option, counted from the first frame used.
struct TimeAnalysis {
    bool available = false;
    int frames = 0;
    double framesPerSecond = 0;  // of the frames as played (after the speed option)

    double start = 0, peak = 0, end = 0;  // seconds
    double main = 0;           // the moment that best stands for the whole effect
    bool continuous = false;   // keeps going, rather than one burst that dies away
    double steadyFrom = 0;     // when something that keeps going has finished starting up
    bool loops = false;
    double loopLength = 0;
    float loopConfidence = 0;
    std::vector<double> bursts;  // moments it suddenly grows

    // One value per frame.
    std::vector<float> energy;      // how much effect there is, 0 to 1 of the most
    std::vector<float> radius;      // its extent, fraction of the height
    std::vector<float> brightness;  // of its brightest parts, 0 to 1
    std::vector<float> centreX, centreY;
    std::vector<Swatch> colour;
    std::vector<float> pieces;      // how many small pieces are in view
    std::vector<float> pieceReach;  // how far out they are, on average

    double widest = 0;        // when the extent is greatest
    float growth = 0;         // how fast it spreads at first, fraction of height per second
    float slowing = 0;        // how quickly that spreading dies off (a drag figure)
    float driftX = 0, driftY = 0;  // how the whole effect travels, per second, y up
    float pullX = 0, pullY = 0;    // how that travel bends, per second squared
    float turning = 0;        // degrees per second, counter-clockwise
    float turningConfidence = 0;

    std::vector<TimeMarker> markers;
};

struct ReferenceAnalysis {
    StillAnalysis still;  // of the picture, or of the frame that best stands for the clip
    TimeAnalysis time;
    int stillFrame = 0;   // which frame `still` describes

    // A burst often opens with a flash that is over in an instant and looks
    // nothing like what follows. When the clip shows one, it is described
    // separately: `early` is the clip at its brightest, and it has given way
    // to the main part by earlyEnd.
    bool hasEarly = false;
    StillAnalysis early;
    int earlyFrame = 0;
    double earlyTime = 0, earlyEnd = 0;  // seconds, as in TimeAnalysis

    std::vector<Finding> findings;       // the report
    std::vector<std::string> uncertain;  // things the reference cannot settle

    // How each part was worked out, in a line each ("background: plain colour
    // keyed out", "tracking: whole-effect measurements, not single
    // particles"), so the report can say what a smarter method might add.
    std::vector<std::string> methods;

    double cost = 0;  // seconds the analysis took
};

// Called now and then during long work with how far along it is (0 to 1)
// and what is being done. Return false to stop; the work then ends early
// with an error saying it was cancelled.
using Progress = std::function<bool(float done, const char* stage)>;

// Separates the effect from its background in one frame.
Result<Matte> makeMatte(const Image& frame, const ReferenceOptions& options,
                        StillAnalysis* describe = nullptr);

// The whole analysis.
Result<ReferenceAnalysis> analyzeReference(const Reference& reference, const ReferenceOptions& options,
                                           const Progress& progress = {});

// The frames a clip's options select: first and last index, inclusive.
void frameRange(const Reference& reference, const ReferenceOptions& options, int& first, int& last);

// The cropped frame as a picture of at most maxSide pixels a side, for
// showing next to the result.
Image referencePicture(const Reference& reference, const ReferenceOptions& options, int frame,
                       int maxSide);

// A part of the reference lifted out as a picture, for a layer to draw when
// no built-in shape is close enough: the rays of a painted flash, or a
// hand-drawn main shape. These are the reference's own pixels, so they are
// only made when the artist asks for them.
struct Cutout {
    std::string part;  // "rays" or "body"
    Image image;       // square, see-through outside the part
    float centreX = 0.5f, centreY = 0.5f;  // where its middle sits in the reference
    float halfSize = 0.5f;                 // half its side, as a fraction of the reference's height
};
std::vector<Cutout> makeCutouts(const Reference& reference, const ReferenceOptions& options,
                                const ReferenceAnalysis& analysis, int maxSide = 384);

// A colour as "#rrggbb", and in a word or two an artist would use.
std::string hexColour(const Swatch& colour);
std::string colourName(const Swatch& colour);

// ------------------------------------------------- measuring built-in shapes
// Used both to pick the shape closest to something in a reference and to
// size a layer so the shape comes out as large as what was seen.

struct ShapeMeasure {
    float extent = 1;      // radius holding 95% of the shape, in half-particles (0 to 1.4)
    float halfLevel = 1;   // radius at which a round average falls to half its centre
    float outer = 1;       // radius at which it falls to 5%
    float ringRadius = 0;  // where the brightest circle is, for ring-like shapes
    float centre = 1;      // coverage at the centre
    float reach = 1;       // how far its farthest point is from the middle
    float shine = 0;       // how much of it the shape itself turns white, 0 to 1
    float middleX = 0, middleY = 0;  // where its weight is centred, in half-particles, y up
};
const ShapeMeasure& measureShape(SpriteShape shape);

// How alike a patch of a matte and a built-in shape are, from -1 to 1,
// trying the shape at every turn in steps of 7.5 degrees. The patch is the
// square of the given half-size around (cx, cy), in grid pixels.
// With raysOnly, the round part of both (the median round each circle) is
// taken away first, so only rays, points and lobes are compared.
float matchShape(const Matte& matte, float cx, float cy, float halfSize, SpriteShape shape,
                 float* bestTurn = nullptr, bool raysOnly = false);

// How alike the outline of a solid patch and a built-in shape are: the
// overlap of the two silhouettes (0 to 1) at the best turn, plus a little
// for lighter and darker areas falling in the same places (a highlight, a
// shaded side). For crisp, flat-coloured shapes.
// `overlap` receives the outline overlap alone.
float matchOutline(const Matte& matte, float cx, float cy, float halfSize, SpriteShape shape,
                   float* bestTurn = nullptr, float* overlap = nullptr);

}  // namespace vfx::editor
