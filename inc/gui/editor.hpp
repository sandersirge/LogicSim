#pragma once

// The circuit editor: the document with undo/redo, the selection, mouse and keyboard editing on
// the canvas, and the live simulation (recompiled whenever the circuit changes, keeping the state
// of the parts that are still there).
#include "core/compiler.hpp"
#include "core/simulator.hpp"
#include "gui/geometry.hpp"

#include <SFML/Window/Event.hpp>

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace gui {

class Editor {
public:
	Editor();

	// --- Document
	const logic::Document& document() const { return doc_; }
	void newDocument();
	bool open(const std::filesystem::path& path, std::string& error);
	bool save(const std::filesystem::path& path, std::string& error);
	const std::filesystem::path& path() const { return path_; }
	bool modified() const { return modified_; }
	std::vector<std::string> loadWarnings;

	// --- Editing
	void startPlacing(logic::Kind kind);
	bool placing() const { return mode_ == Mode::Placing; }
	void cancel(); // stop placing / wiring, clear the selection
	void undo();
	void redo();
	bool canUndo() const { return !undo_.empty(); }
	bool canRedo() const { return !redo_.empty(); }
	void deleteSelection();
	void rotateSelection();
	void selectAll();
	void copySelection();
	void cut();
	void paste();
	void duplicate();
	bool hasSelection() const { return !selected_.empty() || selectedWire_ >= 0; }
	bool canPaste() const { return !clipboard_.components.empty(); }

	// --- Chips
	// The circuit being edited: the main circuit, or the inside of a chip (editPath() leads to it)
	const Circuit& circuit() const { return current(); }
	// Chip names from the main circuit down to the chip being edited (empty: the main circuit)
	const std::vector<std::string>& editPath() const { return editPath_; }
	// Opens a chip to edit its inside (its input pins can be clicked to try it out)
	void enterChip(const std::string& name);
	// Goes back up to the given depth of editPath() (0: the main circuit)
	void leaveTo(size_t depth);
	// Whether an instance of the chip can be placed here (a chip cannot contain itself)
	bool canPlace(std::string_view chip) const;
	void startPlacingChip(const std::string& name);
	// Makes a chip of the selection and adds it to the library; returns an error message or ""
	std::string createChip(const std::string& name, const std::string& color, const std::string& description);
	// Removes an unused chip from the library; false when it is used or open
	bool deleteChip(const std::string& name);
	// Adds the chips of another file; returns messages about chips that had to be renamed
	std::vector<std::string> importChips(const logic::Library& library);
	// A double click: opens the chip under the mouse
	void doubleClicked(ImVec2 screen);

	// --- Canvas input (screen coordinates)
	void mousePressed(sf::Mouse::Button button, ImVec2 screen, bool shift);
	void mouseReleased(sf::Mouse::Button button, ImVec2 screen);
	void mouseMoved(ImVec2 screen);
	void wheel(float delta, ImVec2 screen);
	// Keys for the canvas; true when handled
	bool keyPressed(const sf::Event::KeyPressed& key);
	void setCanvas(ImVec2 min, ImVec2 max) {
		canvasMin_ = min;
		canvasMax_ = max;
	}
	void fitView();
	const Camera& camera() const { return camera_; }

	// --- Simulation
	void update(float seconds);
	bool running() const { return running_; }
	void setRunning(bool running) { running_ = running; }
	void stepTick();
	void stepEvent();
	void resetSimulation();
	float ticksPerSecond = 20.f;
	logic::Tick time() const { return sim_.now(); }
	bool oscillating() const { return oscillating_; }
	const logic::Compiled& compiled() const { return compiled_; }
	std::size_t eventsPerSecond() const { return eventsPerSecond_; }
	logic::Value pinValue(int component, int pin) const;
	const std::set<int>& selection() const { return selected_; }
	// Where wires of different signals run on top of each other (they are not connected)
	const std::vector<Segment>& overlaps() const { return overlaps_; }

	// --- Drawing and panels
	void draw(ImDrawList* list);
	void propertiesPanel();
	// Text for the status bar about the thing under the mouse
	std::string hoverText() const;

	// Screenshots: run the simulation for this many ticks first
	void runTicks(logic::Tick ticks);

private:
	enum class Mode { Idle, Placing, Moving, Selecting, Wiring, Panning, Pressing, WirePressed };

	// How the simulation goes on after the circuit is recompiled
	enum class Restart {
		Continue, // after an edit: parts that are still there keep their state
		PowerOn,  // a new document or Reset: every wire X, stored values 0
	};

	// Call before changing the document: ends a drag in progress and saves an undo step
	void beginChange();
	void pushUndo(logic::Document before);
	// Ends a mouse gesture in progress: a move so far becomes an undo step (or is reverted), a wire
	// being drawn is dropped, a button held down is let go. Placing goes on.
	void endGesture(bool revert);
	// Call after changing the document: recompiles and continues the simulation
	void changed();
	// Call after only moving or rotating parts (the netlist stays the same)
	void moved();
	void rebuild(Restart restart, bool settle = true);
	// Keeps wires on the same pins (by name) after a component's pins changed
	void remapPins(int id, const ComponentSpec& before);

	ComponentSpec specOf(const Component& c) const { return logic::specOf(c, doc_.library); }
	Circuit& current();
	const Circuit& current() const;
	// After a change inside a chip: wires to its instances follow its pins
	void syncPorts();
	// After undo / redo: leaves chips that no longer exist
	void checkEditPath();
	// The settings of the chip being edited (nothing selected)
	void chipPanel();
	ImVec2 mouseWorld() const { return camera_.toWorld(mouse_); }
	Point snapped(ImVec2 world) const;
	std::optional<PinRef> pinAt(ImVec2 world) const;
	int componentAt(ImVec2 world) const; // id or -1
	int wireAt(ImVec2 world) const;      // index or -1
	bool insideBox(const Component& c, ImVec2 world, float margin = 0.f) const;
	bool isInteractive(const Component& c) const;
	// A click (not a drag) on a switch, input pin bit or button
	bool interact(Component& c, ImVec2 world, bool press);
	void finishWire(PinRef end);
	// Starts a new wire from the same source as wire `index`, following it up to the grid point
	// nearest to `world` (a branch)
	void startBranch(int index, ImVec2 world);
	void pasteCircuit(const Circuit& source, Point offset);
	void drawJunctions(ImDrawList* list) const;
	// The wire layout caches: overlaps (after the netlist or positions change) and junction dots
	// (also while dragging)
	void findOverlaps();
	void findJunctions();
	int overlapAt(ImVec2 world) const; // index into overlaps_ or -1

	logic::Document doc_;
	std::filesystem::path path_;
	bool modified_ = false;
	std::vector<logic::Document> undo_, redo_;
	// Editing inside chips: the chip names, and for each level above the simulation and the view
	// to go back to
	std::vector<std::string> editPath_;
	std::vector<logic::Simulator> parkedSims_;
	std::vector<Camera> parkedCameras_;
	std::vector<int> ports_; // portIds of the chip being edited, before the latest change
	std::string chipName_;   // the name field of the chip panel

	Camera camera_;
	ImVec2 canvasMin_{0, 0}, canvasMax_{800, 600};
	ImVec2 mouse_{0, 0};
	Mode mode_ = Mode::Idle;
	Mode beforePan_ = Mode::Idle;
	bool editSnapshot_ = false; // an undo step was saved for the property edit in progress
	int romTextFor_ = -1;
	std::string romText_;
	std::set<int> selected_;
	int selectedWire_ = -1;

	// Placing
	logic::Kind placeKind_ = logic::Kind::And;
	logic::Props placeProps_;
	int placeRotation_ = 0;
	// Moving / selecting / panning
	ImVec2 pressWorld_{0, 0};
	ImVec2 pressScreen_{0, 0};
	Point moved_{0, 0};
	int pressedComponent_ = -1;
	int pressedWire_ = -1;
	bool dragged_ = false;
	logic::Document beforeMove_;
	// Wiring
	PinRef wireStart_;
	std::vector<Point> wireBends_;

	Circuit clipboard_;
	int pasteCount_ = 0;

	logic::Compiled compiled_;
	logic::Simulator sim_;
	std::set<int> errorComponents_;
	std::vector<Segment> overlaps_;
	std::vector<std::pair<Point, PinRef>> junctions_; // where a wire branches, and its source pin
	bool running_ = true;
	double tickDebt_ = 0.0;
	bool oscillating_ = false;
	std::size_t eventsPerSecond_ = 0;
	std::size_t eventCounter_ = 0;
	float eventTimer_ = 0.f;
};

} // namespace gui
