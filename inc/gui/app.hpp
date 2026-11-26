#pragma once

// The window: menu bar, simulation toolbar, toolbox on the left, properties on the right, status
// bar at the bottom and the circuit canvas in the middle.
#include "gui/editor.hpp"

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Clock.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace gui {

class App {
public:
	explicit App(std::filesystem::path programDir);
	~App();
	App(const App&) = delete;
	App& operator=(const App&) = delete;

	struct Options {
		std::filesystem::path file;       // circuit to open
		std::filesystem::path screenshot; // save a picture and quit
		logic::Tick ticks = 0;            // simulation ticks before the screenshot
	};
	int run(const Options& options);

private:
	void loadFonts();
	void menuBar();
	void toolbar(float y);
	void toolbox(float y, float height);
	void properties(float y, float height);
	void statusBar();
	void fileDialog();
	void confirmDialog();
	void messageDialog();
	void chipDialog();
	void breadcrumbs(ImVec2 at);
	bool appKey(const sf::Event::KeyPressed& key);
	void updateTitle();

	// What the file dialog is for
	enum class FileTask { Open, Save, ImportChips, ExportChips };

	// Runs the action now, or after asking when there are unsaved changes
	void guarded(std::function<void()> action);
	void openFile(const std::filesystem::path& path);
	void saveOrAsk();
	void saveTo(const std::filesystem::path& path);
	void importChips(const std::filesystem::path& path);
	void showFileDialog(FileTask task);
	std::filesystem::path examplesDir() const;
	void showMessage(std::string text);

	sf::RenderWindow window_;
	Editor editor_;
	std::filesystem::path programDir_;
	bool imguiReady_ = false;
	float menuHeight_ = 0.f;

	// File dialog
	bool fileDialogOpen_ = false;
	FileTask fileTask_ = FileTask::Open;
	std::filesystem::path dialogDir_;
	std::string dialogName_;
	std::filesystem::path replacePath_; // the existing file the Save dialog asks about
	// New chip from the selection
	bool chipDialogOpen_ = false;
	std::string newChipName_;
	float newChipColor_[3] = {0.20f, 0.27f, 0.41f};
	std::string newChipDescription_;
	std::string newChipError_;
	// Unsaved-changes question
	bool confirmOpen_ = false;
	std::function<void()> pendingAction_;
	// Error / warning message
	std::string message_;
	bool messageOpen_ = false;
	std::string title_;
	// Double clicks on the canvas
	sf::Clock lastClick_;
	sf::Vector2i lastClickAt_{-100, -100};
};

} // namespace gui
