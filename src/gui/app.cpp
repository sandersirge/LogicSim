#include "gui/app.hpp"

#include "core/json_io.hpp"
#include "core/version.hpp"
#include "gui/draw.hpp"
#include "gui/logo.hpp"

#include <SFML/Config.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/View.hpp>
#include <SFML/System/Clock.hpp>
#include <imgui-SFML.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <initializer_list>

namespace gui {

namespace {

constexpr float TOOLBAR_HEIGHT = 46.f;
constexpr float TOOLBOX_WIDTH = 232.f;
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
	// The logo as window icon (title bar, taskbar, window switcher); on Windows the .exe has it too
	std::vector<std::uint8_t> icon = drawLogo(64);
	window_.setIcon(sf::Vector2u{64u, 64u}, icon.data());
	std::vector<std::uint8_t> logo = drawLogo(128);
	logoReady_ = logo_.resize(sf::Vector2u{128u, 128u});
	if (logoReady_) {
		logo_.update(logo.data());
		logo_.setSmooth(true);
	}
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
	// Next to the program, in an install's share folder (and an AppImage), in the macOS app bundle's
	// Resources, or the working folder (a source checkout)
	std::error_code error;
	for (const std::filesystem::path& dir :
	     {programDir_ / "examples", programDir_ / ".." / "share" / "logicsim" / "examples",
	      programDir_ / ".." / "Resources" / "examples", std::filesystem::current_path() / "examples"}) {
		if (std::filesystem::is_directory(dir, error)) {
			return std::filesystem::weakly_canonical(dir, error);
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

void App::showMessage(std::string text) {
	message_ = std::move(text);
	messageOpen_ = true;
}

void App::openFile(const std::filesystem::path& path) {
	std::string error;
	if (!editor_.open(path, error)) {
		showMessage(error);
		return;
	}
	dialogDir_ = path.parent_path();
	if (!editor_.loadWarnings.empty()) {
		std::string text = "Some parts of the file were skipped:\n";
		for (const std::string& w : editor_.loadWarnings) {
			text += "  " + w + "\n";
		}
		showMessage(text);
	}
}

void App::importChips(const std::filesystem::path& path) {
	logic::LoadResult loaded = logic::loadFile(path);
	if (!loaded.ok) {
		showMessage(loaded.error);
		return;
	}
	dialogDir_ = path.parent_path();
	if (loaded.document.library.chips.empty()) {
		showMessage(std::format("{} has no chips.", path.filename().string()));
		return;
	}
	std::string text;
	for (const std::string& line : editor_.importChips(loaded.document.library)) {
		text += line + "\n";
	}
	for (const std::string& w : loaded.warnings) {
		text += "  " + w + "\n";
	}
	showMessage(text);
}

void App::saveOrAsk() {
	if (editor_.path().empty()) {
		showFileDialog(FileTask::Save);
		return;
	}
	std::string error;
	if (!editor_.save(editor_.path(), error)) {
		showMessage(error);
	}
}

void App::showFileDialog(FileTask task) {
	fileDialogOpen_ = true;
	fileTask_ = task;
	if (!editor_.path().empty()) {
		dialogDir_ = editor_.path().parent_path();
	}
	switch (task) {
	case FileTask::Open:
	case FileTask::ImportChips:
		dialogName_.clear();
		break;
	case FileTask::Save:
		dialogName_ = editor_.path().empty() ? editor_.document().main.name + ".json"
		                                     : editor_.path().filename().string();
		break;
	case FileTask::ExportChips:
		dialogName_ = "chips.json";
		break;
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
	bool saving = fileTask_ == FileTask::Save || fileTask_ == FileTask::ExportChips;
	const char* title = "Open circuit";
	const char* action = "Open";
	switch (fileTask_) {
	case FileTask::Open:
		break;
	case FileTask::Save:
		title = "Save circuit";
		action = "Save";
		break;
	case FileTask::ImportChips:
		title = "Import the chips of a circuit or chip library file";
		action = "Import";
		break;
	case FileTask::ExportChips:
		title = "Export all chips as a chip library";
		action = "Export";
		break;
	}
	ImGui::Text("%s", title);
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
	if (ImGui::Button(action, {90.f, 0.f}) || enter) {
		if (!dialogName_.empty()) {
			chosen = dialogDir_ / dialogName_;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", {90.f, 0.f}) ||
	    (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen("Replace file"))) {
		ImGui::CloseCurrentPopup();
	}
	bool done = false;
	if (!chosen.empty()) {
		if (saving) {
			if (chosen.extension() != ".json") {
				chosen += ".json";
			}
			std::error_code e;
			bool current = fileTask_ == FileTask::Save && chosen == editor_.path();
			if (!current && std::filesystem::exists(chosen, e)) {
				replacePath_ = chosen;
				ImGui::OpenPopup("Replace file");
			} else {
				saveTo(chosen);
				done = true;
			}
		} else if (fileTask_ == FileTask::ImportChips) {
			importChips(chosen);
			done = true;
		} else {
			openFile(chosen);
			done = true;
		}
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
	if (ImGui::BeginPopupModal("Replace file", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("%s already exists. Replace it?", replacePath_.filename().string().c_str());
		ImGui::Spacing();
		if (ImGui::Button("Replace", {110.f, 0.f})) {
			ImGui::CloseCurrentPopup();
			saveTo(replacePath_);
			done = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", {110.f, 0.f}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	if (done) {
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void App::saveTo(const std::filesystem::path& path) {
	std::string error;
	bool ok = fileTask_ == FileTask::ExportChips
	              ? logic::saveFile(path, logic::saveLibrary(editor_.document().library), error)
	              : editor_.save(path, error);
	if (!ok) {
		showMessage(error);
	}
	dialogDir_ = path.parent_path();
}
void App::chipDialog() {
	if (chipDialogOpen_) {
		ImGui::OpenPopup("New chip");
		chipDialogOpen_ = false;
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
	if (!ImGui::BeginPopupModal("New chip", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}
	ImGui::PushTextWrapPos(420.f);
	ImGui::TextWrapped("The selected parts become a chip. Its input and output pins are the pins of the "
	                   "chip (top to bottom, named by their labels). The circuit itself stays as it is; "
	                   "place the chip from the toolbox.");
	ImGui::PopTextWrapPos();
	ImGui::Spacing();
	if (ImGui::IsWindowAppearing()) {
		ImGui::SetKeyboardFocusHere();
	}
	ImGui::SetNextItemWidth(260.f);
	bool enter = ImGui::InputText("Name", &newChipName_, ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::ColorEdit3("Colour", newChipColor_, ImGuiColorEditFlags_NoInputs);
	ImGui::InputTextMultiline("Description", &newChipDescription_, ImVec2{260.f, 60.f});
	if (!newChipError_.empty()) {
		ImGui::TextColored(ImVec4{0.95f, 0.4f, 0.4f, 1.f}, "%s", newChipError_.c_str());
	}
	ImGui::Spacing();
	if (ImGui::Button("Create", {110.f, 0.f}) || enter) {
		auto channel = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
		std::string color = std::format("#{:02x}{:02x}{:02x}", channel(newChipColor_[0]),
		                                channel(newChipColor_[1]), channel(newChipColor_[2]));
		newChipError_ = editor_.createChip(newChipName_, color, newChipDescription_);
		if (newChipError_.empty()) {
			ImGui::CloseCurrentPopup();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", {110.f, 0.f}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void App::breadcrumbs(ImVec2 at) {
	const std::vector<std::string>& path = editor_.editPath();
	if (path.empty()) {
		return;
	}
	ImGui::SetNextWindowPos(at);
	ImGui::SetNextWindowBgAlpha(0.92f);
	ImGui::Begin("Breadcrumbs", nullptr,
	             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
	                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
	                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
	// Each level above the chip being edited is a button that goes back to it
	size_t target = path.size();
	std::string main = editor_.document().main.name.empty() ? "Circuit" : editor_.document().main.name;
	if (ImGui::SmallButton(main.c_str())) {
		target = 0;
	}
	for (size_t i = 0; i < path.size(); i++) {
		ImGui::SameLine();
		ImGui::TextDisabled(">");
		ImGui::SameLine();
		if (i + 1 == path.size()) {
			ImGui::Text("%s", path[i].c_str());
		} else if (ImGui::SmallButton(std::format("{}##level{}", path[i], i).c_str())) {
			target = i + 1;
		}
	}
	ImGui::SameLine();
	ImGui::TextDisabled("   editing a chip: changes apply to every instance");
	ImGui::End();
	if (target < path.size()) {
		editor_.leaveTo(target);
	}
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
			guarded([this] { showFileDialog(FileTask::Open); });
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
			showFileDialog(FileTask::Save);
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Import chips...")) {
			showFileDialog(FileTask::ImportChips);
		}
		ImGui::SetItemTooltip("Add the chips of another circuit or chip library file");
		if (ImGui::MenuItem("Export chips...", nullptr, false, !editor_.document().library.chips.empty())) {
			showFileDialog(FileTask::ExportChips);
		}
		ImGui::SetItemTooltip("Save all chips of this circuit as a chip library");
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
		ImGui::BulletText("Double-click a chip: open it");
		ImGui::BulletText("Right or middle drag: pan; wheel: zoom; right click: cancel");
		ImGui::TextDisabled("Wire colours");
		ImGui::BulletText("bright green 1, dark green 0, blue unknown (X), yellow partly known");
		ImGui::Separator();
		if (ImGui::MenuItem("About LogicSim")) {
			aboutOpen_ = true;
		}
		ImGui::EndMenu();
	}
	ImGui::EndMainMenuBar();
}

void App::aboutDialog() {
	if (aboutOpen_) {
		ImGui::OpenPopup("About LogicSim");
		aboutOpen_ = false;
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
	if (!ImGui::BeginPopupModal("About LogicSim", nullptr,
	                            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}
	if (logoReady_) {
		ImGui::Image(logo_, sf::Vector2f{96.f, 96.f});
		ImGui::SameLine();
	}
	ImGui::BeginGroup();
	ImGui::Dummy({0.f, 14.f});
	ImGui::Text("LogicSim %s", std::string(version::NUMBER).c_str());
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 330.f);
	ImGui::TextWrapped("%s", std::string(version::DESCRIPTION).c_str());
	ImGui::PopTextWrapPos();
	ImGui::TextDisabled("%s", std::string(version::HOMEPAGE).c_str());
	ImGui::EndGroup();
	ImGui::Spacing();
	ImGui::TextDisabled("Built with SFML %d.%d, Dear ImGui %s and nlohmann/json", SFML_VERSION_MAJOR,
	                    SFML_VERSION_MINOR, IMGUI_VERSION);
	ImGui::Spacing();
	if (ImGui::Button("Copy homepage link", {170.f, 0.f})) {
		ImGui::SetClipboardText(std::string(version::HOMEPAGE).c_str());
	}
	ImGui::SameLine();
	if (ImGui::Button("Close", {110.f, 0.f}) || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
	    ImGui::IsKeyPressed(ImGuiKey_Enter)) {
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
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
	// Exit at the right end; asks first when there are unsaved changes
	const float exitWidth = 80.f;
	ImGui::SameLine();
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
	                     std::max(0.f, ImGui::GetContentRegionAvail().x - exitWidth));
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.62f, 0.17f, 0.17f, 1.f});
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.80f, 0.23f, 0.22f, 1.f});
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.92f, 0.31f, 0.28f, 1.f});
	if (ImGui::Button("Exit", {exitWidth, 0.f})) {
		guarded([this] { window_.close(); });
	}
	ImGui::PopStyleColor(3);
	ImGui::SetItemTooltip("Close LogicSim  (Ctrl+Q)");
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
		// Two buttons a row, or one when a name of this category would not fit in half a row (with
		// the font that was loaded: names are wider in DejaVu Sans than in Segoe UI)
		const float padding = 5.f;
		bool twoColumns = true;
		for (const logic::KindInfo& kind : logic::allKinds()) {
			if (kind.category == category) {
				float needed =
				    ImGui::CalcTextSize(kind.name.data(), kind.name.data() + kind.name.size()).x + 2 * padding;
				twoColumns = twoColumns && needed <= buttonWidth;
			}
		}
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {padding, ImGui::GetStyle().FramePadding.y});
		int column = 0;
		for (const logic::KindInfo& kind : logic::allKinds()) {
			if (kind.category != category) {
				continue;
			}
			if (twoColumns && column % 2 == 1) {
				ImGui::SameLine();
			}
			std::string label(kind.name);
			if (ImGui::Button(label.c_str(), {twoColumns ? buttonWidth : -1.f, 0.f})) {
				editor_.startPlacing(kind.kind);
			}
			ImGui::SetItemTooltip("%s", std::string(kind.tooltip).c_str());
			column++;
		}
		ImGui::PopStyleVar();
	}

	// Custom chips: made from a selection, placed like any part, opened to edit their inside
	if (ImGui::CollapsingHeader("Chips", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::BeginDisabled(editor_.selection().empty());
		if (ImGui::Button("New chip from selection...", {-1.f, 0.f})) {
			newChipName_.clear();
			newChipDescription_.clear();
			newChipError_.clear();
			chipDialogOpen_ = true;
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("Select a circuit with its input and output pins: they become the pins of "
			                  "the chip, top to bottom");
		}
		const auto& chips = editor_.document().library.chips;
		if (chips.empty()) {
			ImGui::TextDisabled("No chips yet");
		}
		std::string open, remove;
		for (const auto& [name, chip] : chips) {
			bool placeable = editor_.canPlace(name);
			ImU32 tint = parseColor(chip.color, IM_COL32(52, 70, 104, 255));
			ImVec4 color = ImGui::ColorConvertU32ToFloat4(tint);
			ImGui::PushStyleColor(ImGuiCol_Button,
			                      ImVec4{color.x * 0.6f, color.y * 0.6f, color.z * 0.6f, 1.f});
			ImGui::BeginDisabled(!placeable);
			if (ImGui::Button(name.c_str(), {-1.f, 0.f})) {
				editor_.startPlacingChip(name);
			}
			ImGui::EndDisabled();
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
				std::string tip = chip.description.empty() ? name : chip.description;
				tip += placeable ? "\nRight click: open, delete"
				                 : "\nCannot go here: a chip cannot contain itself";
				ImGui::SetTooltip("%s", tip.c_str());
			}
			if (ImGui::BeginPopupContextItem(name.c_str())) {
				if (ImGui::MenuItem("Open chip")) {
					open = name;
				}
				int uses = logic::countInstances(editor_.document(), name);
				bool inUse =
				    uses > 0 || std::ranges::find(editor_.editPath(), name) != editor_.editPath().end();
				if (ImGui::MenuItem("Delete chip", nullptr, false, !inUse)) {
					remove = name;
				}
				if (inUse) {
					ImGui::SetItemTooltip("Only an unused chip can be deleted (used %d time%s)", uses,
					                      uses == 1 ? "" : "s");
				}
				ImGui::EndPopup();
			}
		}
		if (!open.empty()) {
			editor_.leaveTo(0); // the toolbox opens a chip from the main circuit
			editor_.enterChip(open);
		}
		if (!remove.empty()) {
			editor_.deleteChip(remove);
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
	size_t overlaps = editor_.overlaps().size();
	if (errors > 0 || overlaps > 0) {
		ImGui::Spacing();
		ImGui::Separator();
	}
	if (errors > 0) {
		ImGui::TextColored(ImVec4{0.95f, 0.4f, 0.4f, 1.f}, "%zu problem%s in the circuit", errors,
		                   errors == 1 ? "" : "s");
		for (const logic::Diagnostic& d : compiled.diagnostics) {
			ImGui::BulletText("%s", d.message.c_str());
		}
	}
	if (overlaps > 0) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.96f, 0.59f, 0.16f, 1.f});
		ImGui::TextWrapped("Wires of different signals lie on top of each other in %zu place%s (shown in "
		                   "orange). They look connected but are not; move one of them.",
		                   overlaps, overlaps == 1 ? "" : "s");
		ImGui::PopStyleColor();
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
		guarded([this] { showFileDialog(FileTask::Open); });
		return true;
	case Key::S:
		key.shift ? showFileDialog(FileTask::Save) : saveOrAsk();
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
	aboutOpen_ = options.about;
	if (!options.screenshot.empty()) {
		// A screenshot needs no mouse cursors; leaving them alone also keeps it working on displays
		// without cursor support (a bare Xvfb in a container)
		ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	}
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
					ImVec2 at{static_cast<float>(press->position.x), static_cast<float>(press->position.y)};
					editor_.mousePressed(press->button, at,
					                     sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) ||
					                         sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift));
					if (press->button == sf::Mouse::Button::Left) {
						sf::Vector2i distance = press->position - lastClickAt_;
						bool twice = lastClick_.restart().asSeconds() < 0.4f && std::abs(distance.x) <= 4 &&
						             std::abs(distance.y) <= 4;
						lastClickAt_ = twice ? sf::Vector2i{-100, -100} : press->position;
						if (twice) {
							editor_.doubleClicked(at);
						}
					}
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
		chipDialog();
		confirmDialog();
		messageDialog();
		aboutDialog();

		editor_.setCanvas({TOOLBOX_WIDTH, panelsTop}, {size.x - PROPERTIES_WIDTH, size.y - STATUS_HEIGHT});
		breadcrumbs({TOOLBOX_WIDTH + 10.f, panelsTop + 8.f});
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
