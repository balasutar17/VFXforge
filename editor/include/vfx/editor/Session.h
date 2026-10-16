// VFX Forge editor: one editing session.
//
// A session is one open effect and everything needed to edit and watch it:
// the document, its undo history, the compiled program, the simulation and
// the playback clock. The window calls into this; nothing here knows what a
// window is.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "vfx/Clock.h"
#include "vfx/CommandStack.h"
#include "vfx/Document.h"
#include "vfx/Effect.h"
#include "vfx/Program.h"
#include "vfx/Result.h"
#include "vfx/Serialize.h"
#include "vfx/Simulation.h"
#include "vfx/Templates.h"
#include "vfx/Value.h"
#include "vfx/editor/Audio.h"
#include "vfx/editor/Image.h"
#include "vfx/editor/Reconstruct.h"
#include "vfx/editor/Selection.h"
#include "vfx/editor/Transform.h"

namespace vfx::editor {

// One Simple-mode control as the panel needs it.
struct ControlView {
    Id layer;
    Id control;
    std::string label;

    // The first property the control drives. Null when the control points at
    // something that no longer exists; such a control is shown as broken.
    const PropertyDesc* desc = nullptr;
    Value value;

    // Where the setting lives, for the Advanced view, which groups settings
    // by this: "Timing" for the layer's own start and length, otherwise the
    // module's name ("Emission", "Motion"...). moduleType is the module's
    // type ("emission", "motion"...), empty for the layer's own fields.
    std::string section;
    std::string moduleType;
};

// A starting effect that looks like something: one warm, glowing fountain.
// Built from ordinary modules, so everything in it can be edited.
Effect makeStarterEffect(const IdSource& newId, bool threeD);

class Session {
public:
    Session();  // opens with the starter effect, 2D
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // ----------------------------------------------------------- files
    void newEffect(bool threeD);

    // Starts a new effect from one of the library's presets.
    Status openPreset(std::string_view presetId);

    // Replaces the open effect only when the file loads. Notes from the
    // loader (repaired values, newer file version) are kept in loadNotes().
    Status open(const std::filesystem::path& path);

    // Fails when there is no file yet, or the file came from a newer version.
    Status save();
    Status saveAs(const std::filesystem::path& path);

    bool hasFile() const { return !path_.empty(); }
    const std::filesystem::path& filePath() const { return path_; }
    bool readOnly() const { return readOnly_; }
    bool dirty() const;
    const std::vector<Diagnostic>& loadNotes() const { return notes_; }

    // -------------------------------------------------------- document
    const Effect& effect() const;
    Document& document();
    CommandStack& commands();
    const CommandStack& commands() const;

    // Every edit goes through the undo history.
    Status set(const Path& path, Value value);
    Status addEmitter(std::string name, Id* created = nullptr);
    Status removeLayer(Id layer);

    // Adds every layer of a library preset to the open effect, as one undo
    // step, as a group named after the preset ("Portal 3D", or "Portal 3D 2"
    // when there is one already). How many layers were added is returned
    // through added. When the preset lasts longer than the effect, the
    // effect is made as long, so none of it is cut off.
    Status addPreset(std::string_view presetId, int* added = nullptr);

    // ---------------------------------------------------------- groups
    // The groups in the effect, in the order their first layers come.
    std::vector<std::string> groups() const;
    // Each is one undo step, and changes every layer of the group, locked
    // or not, through its ordinary properties.
    // Moves the group's emitters (their Position) by this much.
    Status moveGroup(std::string_view group, double dx, double dy, double dz);
    // Makes the group start this much later (earlier when below zero, but
    // not before the effect starts). An effect that plays once grows to fit;
    // a loop keeps its length, and a layer that would run past its end is
    // cut off there.
    Status delayGroup(std::string_view group, double seconds);
    // Makes the group this many times as large, particles and reach, about
    // the middle of its emitters.
    Status sizeGroup(std::string_view group, double factor);
    Status setGroupEnabled(std::string_view group, bool enabled);
    // A locked layer is left alone by the handles (and by Refine).
    Status setGroupLocked(std::string_view group, bool locked);
    Status removeGroup(std::string_view group);
    // One level: the group's layers (and groups) move up into its parent.
    Status ungroup(std::string_view group);
    // Makes the layers a new group, inside the deepest group they all share
    // (out of any group of their own below it), and puts them together in
    // the list. made receives the new group's path.
    Status groupLayers(const std::vector<Id>& layers, std::string_view name, std::string* made = nullptr);
    // Groups are paths ("Fire/Sparks"); every operation above reaches the
    // groups inside too. Move, Delay and Size leave locked layers alone.

    // ------------------------------------------- moving, turning, sizing
    // What a change to a selection reaches (see Selection.h).
    ResolvedSelection resolve(const Selection& selection) const;
    // The middle of those layers' Positions: where the handles stand.
    Vec3 pivotOf(const std::vector<Id>& layers) const;
    // A change made in one go, as one undo step.
    Status transformLayers(const std::vector<Id>& layers, const LayerTransform& t, std::string stepName);
    // A drag of a handle. Each update replaces the one before (it is always
    // measured from where the drag began), and the whole drag undoes as one
    // step. Cancelling puts everything back as it was.
    Status beginTransform(const std::vector<Id>& layers, std::string stepName);
    Status updateTransform(const LayerTransform& t);
    void endTransform();
    void cancelTransform();
    bool transforming() const { return !transformFrom_.empty(); }
    // ------------------------------------------------- the artist's pictures
    // Copies a PNG into the project's images folder and makes the layer draw
    // it, as one undo step. originalName is only used to name the copy.
    Status useImage(Id layer, std::string_view originalName, std::string_view pngBytes);

    // Goes back to the layer's Shape. The picture is dropped from the effect
    // when no other layer uses it (the file stays on disk).
    Status clearImage(Id layer);

    // The folder pictures are stored relative to: the effect file's folder,
    // or, until the effect is first saved, a private scratch folder.
    std::filesystem::path projectFolder() const;

    // The decoded pictures, kept in step with the effect on every tick.
    const ImageSet& images() const { return images_; }

    // Pictures the effect refers to that could not be loaded.
    const std::vector<std::string>& imageProblems() const { return imageProblems_; }

    // ------------------------------------------------------------ sound
    // Gives the layer a sound (adding its Sound module if it has none), as
    // one undo step. The WAV is copied into the project's sounds folder.
    Status useSound(Id layer, std::string_view originalName, std::string_view wavBytes);
    // The same with a sound from the built-in library.
    Status useLibrarySound(Id layer, std::string_view soundId);
    // Takes the layer's Sound module away; the sound is dropped from the
    // effect when nothing else uses it.
    Status clearSound(Id layer);

    // The decoded sounds, kept in step with the effect on every tick.
    const SoundSet& sounds() const { return sounds_; }
    const std::vector<std::string>& soundProblems() const { return soundProblems_; }

    // ------------------------------------------- changing the whole effect
    // Replaces the open effect's layers, length and loop setting with
    // another effect's, as one undo step. Pictures the new layers draw are
    // written into the project folder first. Used when an effect is rebuilt
    // from a reference, refined, or an earlier version is brought back.
    Status applyEffect(const Effect& next, std::string stepName, const std::vector<PictureUse>* pictures = nullptr);

    // Moves a layer to a place in the list, or copies it (the copy goes
    // just above the original).
    Status moveLayer(Id layer, int newIndex);
    Status duplicateLayer(Id layer, Id* created = nullptr);

    // Keeps the original reference file with the project, untouched: it is
    // copied into the project's "reference" folder and listed with the
    // effect, together with notes (the settings it was read with, as JSON
    // text). An earlier reference is replaced in the list; its file stays.
    Status keepReference(std::string_view originalName, std::string_view bytes, std::string_view notesJson);
    // Updates just the notes of the kept reference.
    Status setReferenceNotes(std::string_view notesJson);
    struct KeptReference {
        bool found = false;
        std::filesystem::path file;  // where the copy is
        std::string name;            // its file name
        std::string notes;           // the JSON text kept with it
    };
    KeptReference keptReference() const;

    Status undo();
    Status redo();

    // A slider drag: everything between begin and end undoes as one step.
    void beginEdit(std::string name);
    void endEdit();

    // ------------------------------------------------- Simple controls
    std::vector<ControlView> controls(Id layer) const;

    // The Advanced view: every setting of the layer, grouped. The layer's
    // own start and length first, then each module's settings in the order
    // the module declares them. Each can be changed with setControl.
    std::vector<ControlView> properties(Id layer) const;

    // Sets every property the control drives, as one undo step.
    Status setControl(Id layer, Id control, const Value& value);

    // -------------------------------------------------------- playback
    PlaybackClock& clock();
    const PlaybackClock& clock() const;

    // Moves time on by this much real time and brings the simulation to the
    // moment the clock shows. Call once per displayed frame, playing or not:
    // it also picks up edits and scrubbing.
    void tick(double realSeconds);

    // The particles at the current moment. Valid until the next tick.
    const RenderFrame& frame() const;

    std::uint32_t particleCount() const;

    // Layers that asked for more particles than the safety limit allows.
    std::vector<Id> cappedLayers() const;

    // Counts up each time the open effect is replaced (new or open), so a
    // view knows to rebuild itself from scratch.
    std::uint64_t generation() const { return generation_; }

private:
    struct State;

    void adopt(Effect effect);

    std::unique_ptr<State> state_;
    std::filesystem::path path_;
    bool readOnly_ = false;
    std::vector<Diagnostic> notes_;
    std::uint64_t generation_ = 0;
    ImageSet images_;
    std::vector<std::string> imageProblems_;
    std::vector<Layer> transformFrom_;  // the layers as a handle drag found them
    std::string transformName_;
    SoundSet sounds_;
    std::vector<std::string> soundProblems_;

    Status attachSound(Id layer, const std::string& relativePath);
    void ensureScratch();
    std::filesystem::path scratch_;  // created on first use, removed with the session

    void syncImages();
};

// -------------------------------------------------------------- helpers

// Colours are stored linear. Screens and colour pickers use sRGB.
double linearToSrgb(double linear);
double srgbToLinear(double srgb);

// A direction as two angles an artist can turn: heading runs counter-
// clockwise in the flat plane from "right" (0) through "up" (90), and tilt
// lifts out of that plane toward the viewer (3D only). Degrees.
struct Heading {
    double heading = 90.0;
    double tilt = 0.0;
};
Heading headingFromDirection(const Vec3& direction);
Vec3 directionFromHeading(const Heading& heading);

}  // namespace vfx::editor
