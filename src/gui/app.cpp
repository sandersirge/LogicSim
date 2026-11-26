#include "gui/app.hpp"

#include "gui/draw.hpp"

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/View.hpp>
#include <SFML/System/Clock.hpp>
#include <imgui-SFML.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <format>
#include <initializer_list>

namespace gui {

namespace {

constexpr float TOOLBAR_HEIGHT = 46.f;
constexpr float TOOLBOX_WIDTH = 220.f;
constexpr float PROPERTIES_WIDTH = 290.f;
constexpr float STATUS_HEIGHT = 30.f;

constexpr ImGuiWindowFlags PANEL_FLAGS =
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;

const char* firstExisting(std::initializer_list<const char*> paths) {
	for (const char* path : paths) {
		std::error_code error;
		if (std::filesystem::exists(path, error)) {
			return path;
		}
	}
	return nullptr;
}

void place(float x, float y, float w, float h) {
	ImGui::SetNextWindowPos({x, y});
	ImGui::SetNextWindowSize({w, h});
}

void applyTheme() {
	ImGuiStyle& style = ImGui::GetStyle();
	ImGui::StyleColorsDark(&style);
	style.WindowPadding = {12.f, 10.f};
	style.FramePadding = {8.f, 5.f};
	style.ItemSpacing = {8.f, 7.f};
	style.WindowRounding = 0.f;
	style.FrameRounding = 5.f;
	style.PopupRounding = 6.f;
	style.GrabRounding = 5.f;
	style.WindowBorderSize = 0.f;
	ImVec4* c = style.Colors;
	const ImVec4 background{0.105f, 0.115f, 0.145f, 1.f};
	const ImVec4 frame{0.165f, 0.180f, 0.225f, 1.f};
	const ImVec4 hover{0.30f, 0.42f, 0.70f, 1.f};
	const ImVec4 accent{0.47f, 0.64f, 1.f, 1.f};
	c[ImGuiCol_WindowBg] = background;
	c[ImGuiCol_PopupBg] = ImVec4{0.13f, 0.14f, 0.18f, 0.98f};
	c[ImGuiCol_MenuBarBg] = ImVec4{0.085f, 0.09f, 0.115f, 1.f};
	c[ImGuiCol_FrameBg] = frame;
	c[ImGuiCol_FrameBgHovered] = ImVec4{0.205f, 0.225f, 0.285f, 1.f};
	c[ImGuiCol_Button] = ImVec4{0.20f, 0.23f, 0.30f, 1.f};
	c[ImGuiCol_ButtonHovered] = hover;
	c[ImGuiCol_ButtonActive] = accent;
	c[ImGuiCol_Header] = ImVec4{0.20f, 0.23f, 0.30f, 1.f};
	c[ImGuiCol_HeaderHovered] = hover;
	c[ImGuiCol_HeaderActive] = accent;
	c[ImGuiCol_CheckMark] = accent;
	c[ImGuiCol_SliderGrab] = accent;
	c[ImGuiCol_SliderGrabActive] = ImVec4{0.62f, 0.75f, 1.f, 1.f};
}

// A button drawn as pressed while `active`
bool toggleButton(const char* label, bool active, ImVec2 size = {0.f, 0.f}) {
	if (active) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.27f, 0.36f, 0.58f, 1.f});
	}
	bool clicked = ImGui::Button(label, size);
	if (active) {
		ImGui::PopStyleColor();
	}
	return clicked;
}

} // namespace

App::App(std::filesystem::path programDir)
    : window_(sf::VideoMode({1500u, 900u}), "LogicSim"), programDir_(std::move(programDir)) {
	window_.setVerticalSyncEnabled(true);
	window_.setMinimumSize(sf::Vector2u{1000u, 650u});
	imguiReady_ = ImGui::SFML::Init(window_, false);
	ImGui::GetIO().IniFilename = nullptr;
	applyTheme();
	loadFonts();
	dialogDir_ = examplesDir();
}

App::~App() {
	if (imguiReady_) {
		ImGui::SFML::Shutdown();
	}
}

void App::loadFonts() {
	ImGuiIO& io = ImGui::GetIO();
	const char* regular = firstExisting({"C:/Windows/Fonts/segoeui.ttf", "/System/Library/Fonts/SFNS.ttf",
	                                     "/System/Library/Fonts/Helvetica.ttc",
	                                     "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	                                     "/usr/share/fonts/TTF/DejaVuSans.ttf"});
	ImFont* canvas = nullptr;
	if (regular != nullptr && io.Fonts->AddFontFromFileTTF(regular, 17.f) != nullptr) {
		// A large copy for the canvas, so labels stay sharp when zoomed in
		canvas = io.Fonts->AddFontFromFileTTF(regular, 34.f);
	} else {
		io.Fonts->AddFontDefault();
	}
	if (!ImGui::SFML::UpdateFontTexture()) {
		io.Fonts->Clear();
		io.Fonts->AddFontDefault();
		canvas = nullptr;
		(void)ImGui::SFML::UpdateFontTexture();
	}
	setCanvasFont(canvas);
}

std::filesystem::path App::examplesDir() const {
	std::error_code error;
	for (const std::filesystem::path& dir :
	     {programDir_ / "examples", std::filesystem::current_path() / "examples"}) {
		if (std::filesystem::is_directory(dir, error)) {
			return dir;
		}
	}
	return std::filesystem::current_path(error);
}

void App::updateTitle() {
	std::string name =
	    editor_.path().empty() ? editor_.document().main.name : editor_.path().filename().string();
	std::string title = std::format("{}{} - LogicSim", name, editor_.modified() ? " *" : "");
	if (title != title_) {
		title_ = title;
		window_.setTitle(title);
	}
}

// ------------------------------------------------------------------------------------- Files

void App::guarded(std::function<void()> action) {
	if (editor_.modified()) {
		pendingAction_ = std::move(action);
		confirmOpen_ = true;
	} else {
		action();
	}
}

void App::openFile(const std::filesystem::path& path) {
	std::string error;
	if (!editor_.open(path, error)) {
		message_ = error;
		messageOpen_ = true;
		return;
	}
	dialogDir_ = path.parent_path();
	if (!editor_.loadWarnings.empty()) {
		message_ = "Some parts of the file were skipped:\n";
		for (const std::string& w : editor_.loadWarnings) {
			message_ += "  " + w + "\n";
		}
		messageOpen_ = true;
	}
}

void App::saveOrAsk() {
	if (editor_.path().empty()) {
		showFileDialog(true);
		return;
	}
	std::string error;
	if (!editor_.save(editor_.path(), error)) {
		message_ = error;
		messageOpen_ = true;
	}
}

void App::showFileDialog(bool saving) {
	fileDialogOpen_ = true;
	fileDialogSaving_ = saving;
	if (!editor_.path().empty()) {
		dialogDir_ = editor_.path().parent_path();
		dialogName_ = editor_.path().filename().string();
	} else {
		dialogName_ = saving ? editor_.document().main.name + ".json" : "";
	}
}

void App::fileDialog() {
	if (fileDialogOpen_) {
		ImGui::OpenPopup("File");
		fileDialogOpen_ = false;
	}
	ImVec2 size{620.f, 460.f};
	ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
	if (!ImGui::BeginPopupModal("File", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}
	ImGui::Text("%s", fileDialogSaving_ ? "Save circuit" : "Open circuit");
	ImGui::TextDisabled("%s", dialogDir_.string().c_str());
	if (ImGui::Button("Up") && dialogDir_.has_parent_path()) {
		dialogDir_ = dialogDir_.parent_path();
	}
	ImGui::SameLine();
	if (ImGui::Button("Examples")) {
		dialogDir_ = examplesDir();
	}

	std::filesystem::path chosen;
	ImGui::BeginChild("files", {0.f, -ImGui::GetFrameHeightWithSpacing() * 2.2f}, ImGuiChildFlags_Borders);
	std::error_code error;
	std::vector<std::filesystem::directory_entry> entries;
	for (const auto& entry : std::filesystem::directory_iterator(dialogDir_, error)) {
		std::error_code e;
		if (entry.is_directory(e) || entry.path().extension() == ".json") {
			entries.push_back(entry);
		}
	}
	std::ranges::sort(entries, [](const auto& a, const auto& b) {
		std::error_code e;
		bool da = a.is_directory(e), db = b.is_directory(e);
		return da != db ? da : a.path().filename() < b.path().filename();
	});
	for (const auto& entry : entries) {
		std::error_code e;
		bool dir = entry.is_directory(e);
		std::string name = entry.path().filename().string();
		std::string label = dir ? "[" + name + "]" : name;
		if (ImGui::Selectable(label.c_str(), !dir && name == dialogName_,
		                      ImGuiSelectableFlags_AllowDoubleClick)) {
			if (dir) {
				dialogDir_ = entry.path();
			} else {
				dialogName_ = name;
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					chosen = entry.path();
				}
			}
		}
	}
	ImGui::EndChild();

	ImGui::SetNextItemWidth(-200.f);
	bool enter = ImGui::InputText("##name", &dialogName_, ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine();
	if (ImGui::Button(fileDialogSaving_ ? "Save" : "Open", {90.f, 0.f}) || enter) {
		if (!dialogName_.empty()) {
			chosen = dialogDir_ / dialogName_;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", {90.f, 0.f}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		ImGui::CloseCurrentPopup();
	}
	if (!chosen.empty()) {
		ImGui::CloseCurrentPopup();
		if (fileDialogSaving_) {
			if (chosen.extension() != ".json") {
				chosen += ".json";
			}
			std::string saveError;
			if (!editor_.save(chosen, saveError)) {
				message_ = saveError;
				messageOpen_ = true;
			}
			dialogDir_ = chosen.parent_path();
		} else {
			openFile(chosen);
		}
	}
	ImGui::EndPopup();
}

void App::confirmDialog() {
	if (confirmOpen_) {
		ImGui::OpenPopup("Unsaved changes");
		confirmOpen_ = false;
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
	if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("The circuit has unsaved changes. Discard them?");
		ImGui::Spacing();
		if (ImGui::Button("Discard", {110.f, 0.f})) {
			ImGui::CloseCurrentPopup();
			if (pendingAction_) {
				pendingAction_();
			}
			pendingAction_ = nullptr;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", {110.f, 0.f}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			ImGui::CloseCurrentPopup();
			pendingAction_ = nullptr;
		}
		ImGui::EndPopup();
	}
}

void App::messageDialog() {
	if (messageOpen_) {
		ImGui::OpenPopup("Message");
		messageOpen_ = false;
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
	if (ImGui::BeginPopupModal("Message", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextUnformatted(message_.c_str());
		if (ImGui::Button("OK", {110.f, 0.f}) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

// ------------------------------------------------------------------------------------ Panels

void App::menuBar() {
	if (!ImGui::BeginMainMenuBar()) {
		return;
	}
	menuHeight_ = ImGui::GetWindowSize().y;
	if (ImGui::BeginMenu("File")) {
		if (ImGui::MenuItem("New", "Ctrl+N")) {
			guarded([this] { editor_.newDocument(); });
		}
		if (ImGui::MenuItem("Open...", "Ctrl+O")) {
			guarded([this] { showFileDialog(false); });
		}
		if (ImGui::BeginMenu("Examples")) {
			std::error_code error;
			std::vector<std::filesystem::path> files;
			for (const auto& entry : std::filesystem::directory_iterator(examplesDir(), error)) {
				if (entry.path().extension() == ".json") {
					files.push_back(entry.path());
				}
			}
			std::ranges::sort(files);
			if (files.empty()) {
				ImGui::TextDisabled("No examples found");
			}
			for (const std::filesystem::path& file : files) {
				if (ImGui::MenuItem(file.stem().string().c_str())) {
					guarded([this, file] { openFile(file); });
				}
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Save", "Ctrl+S")) {
			saveOrAsk();
		}
		if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S")) {
			showFileDialog(true);
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
			guarded([this] { window_.close(); });
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Edit")) {
		if (ImGui::MenuItem("Undo", "Ctrl+Z", false, editor_.canUndo())) {
			editor_.undo();
		}
		if (ImGui::MenuItem("Redo", "Ctrl+Y", false, editor_.canRedo())) {
			editor_.redo();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Cut", "Ctrl+X", false, editor_.hasSelection())) {
			editor_.cut();
		}
		if (ImGui::MenuItem("Copy", "Ctrl+C", false, editor_.hasSelection())) {
			editor_.copySelection();
		}
		if (ImGui::MenuItem("Paste", "Ctrl+V", false, editor_.canPaste())) {
			editor_.paste();
		}
		if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, editor_.hasSelection())) {
			editor_.duplicate();
		}
		if (ImGui::MenuItem("Delete", "Del", false, editor_.hasSelection())) {
			editor_.deleteSelection();
		}
		if (ImGui::MenuItem("Rotate", "R", false, editor_.hasSelection())) {
			editor_.rotateSelection();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Select all", "Ctrl+A")) {
			editor_.selectAll();
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Simulation")) {
		if (ImGui::MenuItem(editor_.running() ? "Pause" : "Run", "Space")) {
			editor_.setRunning(!editor_.running());
		}
		if (ImGui::MenuItem("Step one tick", ".")) {
			editor_.stepTick();
		}
		if (ImGui::MenuItem("Step one event")) {
			editor_.stepEvent();
		}
		if (ImGui::MenuItem("Reset (power on)")) {
			editor_.resetSimulation();
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("View")) {
		if (ImGui::MenuItem("Fit circuit", "F")) {
			editor_.fitView();
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Help")) {
		ImGui::TextDisabled("Mouse");
		ImGui::BulletText("Toolbox click, then grid click: place (Shift: place more)");
		ImGui::BulletText("Drag from a pin to a pin: wire (click on the grid adds bends)");
		ImGui::BulletText("Drag a part: move; drag on the grid: select an area");
		ImGui::BulletText("Click a switch / input pin bit: flip it; hold a button");
		ImGui::BulletText("Right or middle drag: pan; wheel: zoom; right click: cancel");
		ImGui::TextDisabled("Wire colours");
		ImGui::BulletText("bright green 1, dark green 0, blue unknown (X), yellow partly known");
		ImGui::EndMenu();
	}
	ImGui::EndMainMenuBar();
}

void App::toolbar(float y) {
	float width = static_cast<float>(window_.getSize().x);
	place(0.f, y, width, TOOLBAR_HEIGHT);
	ImGui::Begin("Toolbar", nullptr, PANEL_FLAGS | ImGuiWindowFlags_NoScrollbar);
	if (toggleButton(editor_.running() ? "Pause" : "Run", editor_.running(), {80.f, 0.f})) {
		editor_.setRunning(!editor_.running());
	}
	ImGui::SetItemTooltip("Run or pause the simulation  (Space)");
	ImGui::SameLine();
	if (ImGui::Button("Step tick")) {
		editor_.stepTick();
	}
	ImGui::SetItemTooltip("Advance time by one tick  (.)");
	ImGui::SameLine();
	if (ImGui::Button("Step event")) {
		editor_.stepEvent();
	}
	ImGui::SetItemTooltip("Process a single signal change");
	ImGui::SameLine();
	if (ImGui::Button("Reset")) {
		editor_.resetSimulation();
	}
	ImGui::SetItemTooltip("Power on again: every wire unknown, stored values 0");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(220.f);
	ImGui::SliderFloat("##speed", &editor_.ticksPerSecond, 1.f, 100000.f, "%.0f ticks/s",
	                   ImGuiSliderFlags_Logarithmic);
	ImGui::SetItemTooltip(
	    "Simulation speed. Each gate delays its output by its delay (1 tick by default), so at low "
	    "speed you can watch signals travel.");
	ImGui::SameLine();
	ImGui::TextDisabled("t = %llu", static_cast<unsigned long long>(editor_.time()));
	if (editor_.oscillating()) {
		ImGui::SameLine();
		ImGui::TextColored(ImVec4{0.96f, 0.72f, 0.38f, 1.f}, "Does not settle (oscillating)");
	}
	ImGui::End();
}

void App::toolbox(float y, float height) {
	place(0.f, y, TOOLBOX_WIDTH, height);
	ImGui::Begin("Toolbox", nullptr, PANEL_FLAGS);
	float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
	for (logic::Category category :
	     {logic::Category::Gates, logic::Category::InputOutput, logic::Category::Wiring,
	      logic::Category::Plexers, logic::Category::FlipFlops, logic::Category::Memory}) {
		std::string header(logic::categoryName(category));
		if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
			continue;
		}
		int column = 0;
		for (const logic::KindInfo& kind : logic::allKinds()) {
			if (kind.category != category) {
				continue;
			}
			if (column % 2 == 1) {
				ImGui::SameLine();
			}
			std::string label(kind.name);
			if (ImGui::Button(label.c_str(), {buttonWidth, 0.f})) {
				editor_.startPlacing(kind.kind);
			}
			ImGui::SetItemTooltip("%s", std::string(kind.tooltip).c_str());
			column++;
		}
	}
	ImGui::End();
}

void App::properties(float y, float height) {
	float x = static_cast<float>(window_.getSize().x) - PROPERTIES_WIDTH;
	place(x, y, PROPERTIES_WIDTH, height);
	ImGui::Begin("Properties", nullptr, PANEL_FLAGS);
	ImGui::TextDisabled("PROPERTIES");
	ImGui::Separator();
	editor_.propertiesPanel();

	const logic::Compiled& compiled = editor_.compiled();
	size_t errors = std::ranges::count_if(compiled.diagnostics, &logic::Diagnostic::error);
	if (errors > 0) {
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextColored(ImVec4{0.95f, 0.4f, 0.4f, 1.f}, "%zu problem%s in the circuit", errors,
		                   errors == 1 ? "" : "s");
		for (const logic::Diagnostic& d : compiled.diagnostics) {
			ImGui::BulletText("%s", d.message.c_str());
		}
	}
	ImGui::End();
}

void App::statusBar() {
	auto size = sf::Vector2f(window_.getSize());
	place(0.f, size.y - STATUS_HEIGHT, size.x, STATUS_HEIGHT);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.f, 5.f});
	ImGui::Begin("Status", nullptr, PANEL_FLAGS | ImGuiWindowFlags_NoScrollbar);
	ImGui::PopStyleVar();
	const logic::Netlist& netlist = editor_.compiled().netlist;
	ImGui::TextDisabled("%s  |  %zu parts, %zu nets  |  %zu events/s",
	                    editor_.running() ? "Running" : "Paused", netlist.prims.size(), netlist.nets.size(),
	                    editor_.eventsPerSecond());
	std::string hover = editor_.hoverText();
	if (!hover.empty()) {
		ImGui::SameLine();
		ImGui::Text("  |  %s", hover.c_str());
	}
	ImGui::End();
}

// ------------------------------------------------------------------------------------- Keys

bool App::appKey(const sf::Event::KeyPressed& key) {
	using Key = sf::Keyboard::Key;
	if (!key.control) {
		return false;
	}
	switch (key.code) {
	case Key::N:
		guarded([this] { editor_.newDocument(); });
		return true;
	case Key::O:
		guarded([this] { showFileDialog(false); });
		return true;
	case Key::S:
		key.shift ? showFileDialog(true) : saveOrAsk();
		return true;
	case Key::Q:
		guarded([this] { window_.close(); });
		return true;
	default:
		return false;
	}
}

// -------------------------------------------------------------------------------------- Loop

int App::run(const Options& options) {
	if (!imguiReady_) {
		return 1;
	}
	if (!options.file.empty()) {
		std::string error;
		if (!editor_.open(options.file, error)) {
			if (!options.screenshot.empty()) {
				return 2;
			}
			message_ = error;
			messageOpen_ = true;
		}
	}
	bool fitPending = !options.file.empty();
	sf::Clock clock;
	int frames = 0;
	while (window_.isOpen()) {
		while (const std::optional event = window_.pollEvent()) {
			ImGui::SFML::ProcessEvent(window_, *event);
			const ImGuiIO& io = ImGui::GetIO();
			if (event->is<sf::Event::Closed>()) {
				guarded([this] { window_.close(); });
			} else if (const auto* resized = event->getIf<sf::Event::Resized>()) {
				window_.setView(sf::View(sf::FloatRect({0.f, 0.f}, sf::Vector2f(resized->size))));
			} else if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
				if (!io.WantCaptureKeyboard && !appKey(*key)) {
					editor_.keyPressed(*key);
				}
			} else if (const auto* press = event->getIf<sf::Event::MouseButtonPressed>()) {
				if (!io.WantCaptureMouse) {
					editor_.mousePressed(
					    press->button,
					    {static_cast<float>(press->position.x), static_cast<float>(press->position.y)},
					    sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) ||
					        sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift));
				}
			} else if (const auto* release = event->getIf<sf::Event::MouseButtonReleased>()) {
				editor_.mouseReleased(release->button, {static_cast<float>(release->position.x),
				                                        static_cast<float>(release->position.y)});
			} else if (const auto* move = event->getIf<sf::Event::MouseMoved>()) {
				editor_.mouseMoved(
				    {static_cast<float>(move->position.x), static_cast<float>(move->position.y)});
			} else if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>()) {
				if (!io.WantCaptureMouse) {
					editor_.wheel(wheel->delta, {static_cast<float>(wheel->position.x),
					                             static_cast<float>(wheel->position.y)});
				}
			}
		}

		sf::Time elapsed = clock.restart();
		ImGui::SFML::Update(window_, elapsed);
		auto size = sf::Vector2f(window_.getSize());
		menuBar();
		float top = menuHeight_;
		toolbar(top);
		float panelsTop = top + TOOLBAR_HEIGHT;
		float panelsHeight = size.y - panelsTop - STATUS_HEIGHT;
		toolbox(panelsTop, panelsHeight);
		properties(panelsTop, panelsHeight);
		statusBar();
		fileDialog();
		confirmDialog();
		messageDialog();

		editor_.setCanvas({TOOLBOX_WIDTH, panelsTop}, {size.x - PROPERTIES_WIDTH, size.y - STATUS_HEIGHT});
		if (fitPending && frames >= 1) {
			editor_.fitView();
			fitPending = false;
			if (!options.screenshot.empty() && options.ticks > 0) {
				editor_.setRunning(false);
				editor_.runTicks(options.ticks);
			}
		}
		if (options.screenshot.empty()) {
			editor_.update(elapsed.asSeconds());
		}
		editor_.draw(ImGui::GetBackgroundDrawList());
		updateTitle();

		window_.clear(sf::Color(22, 24, 30));
		ImGui::SFML::Render(window_);
		if (!options.screenshot.empty() && ++frames == 4) {
			sf::Texture capture(window_.getSize());
			capture.update(window_);
			return capture.copyToImage().saveToFile(options.screenshot) ? 0 : 1;
		}
		if (options.screenshot.empty()) {
			frames++;
		}
		window_.display();
	}
	return 0;
}

} // namespace gui
