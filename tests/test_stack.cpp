#include <catch2/catch_amalgamated.hpp>

#include <memory>
#include <vector>

#include "helpers.h"

using namespace vfx;
using testing::propertyPath;
using testing::sampleEffect;
using testing::set;
using testing::text;

TEST_CASE("Undo and redo walk back and forth through edits") {
    Document document(sampleEffect());
    CommandStack stack(document);
    const Path spread = propertyPath(document.effect(), 0, "initial", "spread");

    std::vector<std::string> states{text(document)};
    for (double v : {10.0, 20.0, 40.0}) {
        REQUIRE(stack.push(set(spread, Value(v))).ok());
        states.push_back(text(document));
    }
    CHECK(stack.undoCount() == 3);
    CHECK(stack.undoName() == "Change Spread");
    CHECK(stack.redoName().empty());

    for (int i = 2; i >= 0; --i) {
        REQUIRE(stack.undo().ok());
        CHECK(text(document) == states[static_cast<std::size_t>(i)]);
    }
    CHECK_FALSE(stack.canUndo());
    CHECK_FALSE(stack.undo().ok());
    CHECK(stack.redoName() == "Change Spread");

    for (std::size_t i = 1; i <= 3; ++i) {
        REQUIRE(stack.redo().ok());
        CHECK(text(document) == states[i]);
    }
    CHECK_FALSE(stack.canRedo());
    CHECK_FALSE(stack.redo().ok());
}

TEST_CASE("A refused edit adds nothing to the history") {
    Document document(sampleEffect());
    CommandStack stack(document);
    const Path spread = propertyPath(document.effect(), 0, "initial", "spread");
    CHECK_FALSE(stack.push(set(spread, Value(9999.0))).ok());
    CHECK_FALSE(stack.push(nullptr).ok());
    CHECK_FALSE(stack.canUndo());
    CHECK_FALSE(stack.isDirty());
}

TEST_CASE("A new edit after undo discards what could have been redone") {
    Document document(sampleEffect());
    CommandStack stack(document);
    const Path spread = propertyPath(document.effect(), 0, "initial", "spread");
    REQUIRE(stack.push(set(spread, Value(10.0))).ok());
    REQUIRE(stack.push(set(spread, Value(20.0))).ok());
    REQUIRE(stack.undo().ok());
    CHECK(stack.canRedo());
    REQUIRE(stack.push(set(spread, Value(55.0))).ok());
    CHECK_FALSE(stack.canRedo());
    CHECK(stack.undoCount() == 2);
    REQUIRE(stack.undo().ok());
    CHECK(document.get(spread).value() == Value(10.0));
}

TEST_CASE("Setting a value it already has is not an undo step and keeps redo") {
    Document document(sampleEffect());
    CommandStack stack(document);
    const Path spread = propertyPath(document.effect(), 0, "initial", "spread");
    REQUIRE(stack.push(set(spread, Value(10.0))).ok());
    REQUIRE(stack.undo().ok());
    REQUIRE(stack.push(set(spread, Value(30.0))).ok());  // 30 is what it already is
    CHECK(stack.undoCount() == 0);
    CHECK(stack.canRedo());
    CHECK_FALSE(stack.isDirty());
}

TEST_CASE("The unsaved mark follows the save point through undo and redo") {
    Document document(sampleEffect());
    CommandStack stack(document);
    const Path spread = propertyPath(document.effect(), 0, "initial", "spread");
    CHECK_FALSE(stack.isDirty());

    REQUIRE(stack.push(set(spread, Value(10.0))).ok());
    CHECK(stack.isDirty());
    stack.markSaved();
    CHECK_FALSE(stack.isDirty());

    REQUIRE(stack.push(set(spread, Value(20.0))).ok());
    CHECK(stack.isDirty());
    REQUIRE(stack.undo().ok());
    CHECK_FALSE(stack.isDirty());  // back at the saved state
    REQUIRE(stack.undo().ok());
    CHECK(stack.isDirty());  // before the saved state is also a difference
    REQUIRE(stack.redo().ok());
    CHECK_FALSE(stack.isDirty());
}

TEST_CASE("If the saved state can no longer be reached the document stays unsaved") {
    Document document(sampleEffect());
    CommandStack stack(document);
    const Path spread = propertyPath(document.effect(), 0, "initial", "spread");
    REQUIRE(stack.push(set(spread, Value(10.0))).ok());
    REQUIRE(stack.push(set(spread, Value(20.0))).ok());
    stack.markSaved();
    REQUIRE(stack.undo().ok());
    REQUIRE(stack.push(set(spread, Value(99.0))).ok());  // the saved state is now gone
    CHECK(stack.isDirty());
    REQUIRE(stack.undo().ok());
    CHECK(stack.isDirty());
    REQUIRE(stack.redo().ok());
    CHECK(stack.isDirty());
}

TEST_CASE("A slider drag becomes one undo step") {
    Document document(sampleEffect());
    CommandStack stack(document);
    const Path spread = propertyPath(document.effect(), 0, "initial", "spread");
    const std::string before = text(document);
    int notifications = 0;
    document.subscribe([&](const Change&) { ++notifications; });

    stack.beginTransaction("Change Spread");
    CHECK(stack.inTransaction());
    for (int i = 1; i <= 150; ++i) {
        REQUIRE(stack.push(set(spread, Value(static_cast<double>(i)))).ok());
        // The preview follows the drag live.
        REQUIRE(document.get(spread).value() == Value(static_cast<double>(i)));
    }
    CHECK(notifications == 150);
    CHECK(stack.isDirty());
    CHECK_FALSE(stack.canUndo());       // not while the drag is in progress
    CHECK_FALSE(stack.undo().ok());
    stack.endTransaction();

    CHECK(stack.undoCount() == 1);
    CHECK(document.get(spread).value() == Value(150.0));
    REQUIRE(stack.undo().ok());
    CHECK(text(document) == before);
    REQUIRE(stack.redo().ok());
    CHECK(document.get(spread).value() == Value(150.0));
}

TEST_CASE("A drag that ends where it began leaves no undo step") {
    Document document(sampleEffect());
    CommandStack stack(document);
    const Path spread = propertyPath(document.effect(), 0, "initial", "spread");
    stack.beginTransaction("Change Spread");
    REQUIRE(stack.push(set(spread, Value(80.0))).ok());
    REQUIRE(stack.push(set(spread, Value(30.0))).ok());
    stack.endTransaction();
    CHECK(stack.undoCount() == 0);
    CHECK_FALSE(stack.isDirty());
}

TEST_CASE("A refused value in the middle of a drag does not break the drag") {
    Document document(sampleEffect());
    CommandStack stack(document);
    const Path spread = propertyPath(document.effect(), 0, "initial", "spread");
    const std::string before = text(document);
    stack.beginTransaction("Change Spread");
    REQUIRE(stack.push(set(spread, Value(100.0))).ok());
    CHECK_FALSE(stack.push(set(spread, Value(5000.0))).ok());
    CHECK(document.get(spread).value() == Value(100.0));
    REQUIRE(stack.push(set(spread, Value(120.0))).ok());
    stack.endTransaction();
    CHECK(stack.undoCount() == 1);
    REQUIRE(stack.undo().ok());
    CHECK(text(document) == before);
}

TEST_CASE("Cancelling a transaction puts everything back") {
    Document document(sampleEffect());
    CommandStack stack(document);
    const auto& effect = document.effect();
    const std::string before = text(document);

    stack.beginTransaction("Experiment");
    REQUIRE(stack.push(set(propertyPath(effect, 0, "initial", "spread"), Value(80.0))).ok());
    REQUIRE(stack.push(std::make_unique<RemoveLayerCommand>(effect.layers[1].id)).ok());
    REQUIRE(stack.push(set(Path::effect("duration"), Value(9.0))).ok());
    stack.cancelTransaction();

    CHECK_FALSE(stack.inTransaction());
    CHECK(text(document) == before);
    CHECK(stack.undoCount() == 0);
    CHECK_FALSE(stack.isDirty());
}

TEST_CASE("Different edits in one transaction undo together, in the right order") {
    Document document(sampleEffect());
    CommandStack stack(document);
    const auto& effect = document.effect();
    const std::string before = text(document);
    const IdSource newId = [&document]() { return document.newId(); };

    stack.beginTransaction("Add Shockwave");
    REQUIRE(stack.push(std::make_unique<AddLayerCommand>(makeBasicEmitter(newId, "Shockwave")))
                .ok());
    const Id added = effect.layers.back().id;
    REQUIRE(stack.push(set(Path::layerField(added, "start"), Value(0.25))).ok());
    REQUIRE(stack.push(std::make_unique<MoveLayerCommand>(added, 0)).ok());
    stack.endTransaction();
    const std::string after = text(document);

    CHECK(stack.undoCount() == 1);
    CHECK(stack.undoName() == "Add Shockwave");
    REQUIRE(stack.undo().ok());
    CHECK(text(document) == before);
    REQUIRE(stack.redo().ok());
    CHECK(text(document) == after);
}

TEST_CASE("Nested transactions commit once, at the outermost end") {
    Document document(sampleEffect());
    CommandStack stack(document);
    const auto& effect = document.effect();
    stack.beginTransaction("Outer");
    REQUIRE(stack.push(set(propertyPath(effect, 0, "initial", "spread"), Value(80.0))).ok());
    stack.beginTransaction("Inner");
    REQUIRE(stack.push(set(propertyPath(effect, 1, "initial", "spread"), Value(70.0))).ok());
    stack.endTransaction();
    CHECK(stack.inTransaction());
    CHECK(stack.undoCount() == 0);
    stack.endTransaction();
    CHECK_FALSE(stack.inTransaction());
    CHECK(stack.undoCount() == 1);
    CHECK(stack.undoName() == "Outer");

    stack.endTransaction();  // an unmatched end is harmless
    stack.cancelTransaction();
    CHECK(stack.undoCount() == 1);
}

TEST_CASE("Dragging a control bound to two properties still makes one undo step") {
    Document document(sampleEffect());
    CommandStack stack(document);
    const auto& effect = document.effect();
    const Path a = propertyPath(effect, 0, "initial", "spread");
    const Path b = propertyPath(effect, 1, "initial", "spread");
    const std::string before = text(document);

    stack.beginTransaction("Change Spread");
    for (int i = 1; i <= 50; ++i) {
        std::vector<CommandPtr> both;
        both.push_back(set(a, Value(static_cast<double>(i))));
        both.push_back(set(b, Value(static_cast<double>(i * 2))));
        REQUIRE(stack.push(std::make_unique<CompositeCommand>("Change Spread", std::move(both)))
                    .ok());
    }
    stack.endTransaction();

    CHECK(stack.undoCount() == 1);
    CHECK(document.get(a).value() == Value(50.0));
    CHECK(document.get(b).value() == Value(100.0));
    REQUIRE(stack.undo().ok());
    CHECK(text(document) == before);
    REQUIRE(stack.redo().ok());
    CHECK(document.get(b).value() == Value(100.0));
}

TEST_CASE("History is capped and the oldest steps fall away") {
    Document document(sampleEffect());
    CommandStack stack(document, 5);
    const Path spread = propertyPath(document.effect(), 0, "initial", "spread");
    for (int i = 1; i <= 12; ++i) {
        REQUIRE(stack.push(set(spread, Value(static_cast<double>(i)))).ok());
    }
    CHECK(stack.undoCount() == 5);
    while (stack.canUndo()) {
        REQUIRE(stack.undo().ok());
    }
    CHECK(document.get(spread).value() == Value(7.0));  // 12 edits, the last 5 undone
    CHECK(stack.isDirty());  // the original saved state fell off the end
}

TEST_CASE("The save point survives the cap when it is still within reach") {
    Document document(sampleEffect());
    CommandStack stack(document, 5);
    const Path spread = propertyPath(document.effect(), 0, "initial", "spread");
    for (int i = 1; i <= 4; ++i) {
        REQUIRE(stack.push(set(spread, Value(static_cast<double>(i)))).ok());
    }
    stack.markSaved();
    for (int i = 5; i <= 7; ++i) {
        REQUIRE(stack.push(set(spread, Value(static_cast<double>(i)))).ok());
    }
    REQUIRE(stack.undo().ok());
    REQUIRE(stack.undo().ok());
    CHECK(stack.isDirty());
    REQUIRE(stack.undo().ok());
    CHECK(document.get(spread).value() == Value(4.0));
    CHECK_FALSE(stack.isDirty());
}

TEST_CASE("Clearing the history keeps the truth about unsaved changes") {
    Document document(sampleEffect());
    CommandStack stack(document);
    const Path spread = propertyPath(document.effect(), 0, "initial", "spread");
    REQUIRE(stack.push(set(spread, Value(10.0))).ok());
    stack.clear();
    CHECK_FALSE(stack.canUndo());
    CHECK(stack.isDirty());
    stack.markSaved();
    stack.clear();
    CHECK_FALSE(stack.isDirty());
}
