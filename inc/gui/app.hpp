#pragma once

// The window: menu bar, simulation toolbar, toolbox on the left, properties on the right, status
// bar at the bottom and the circuit canvas in the middle.
#include "gui/editor.hpp"

#include <SFML/Graphics/RenderWindow.hpp>

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
	bool appKey(const sf::Event::KeyPressed& key);
	void updateTitle();

	// Runs the action now, or after asking when there are unsaved changes
	void guarded(std::function<void()> action);
	void openFile(const std::filesystem::path& path);
	void saveOrAsk();
	void showFileDialog(bool saving);
	std::filesystem::path examplesDir() const;

	sf::RenderWindow window_;
	Editor editor_;
	std::filesystem::path programDir_;
	bool imguiReady_ = false;
	float menuHeight_ = 0.f;

	// File dialog
	bool fileDialogOpen_ = false;
	bool fileDialogSaving_ = false;
	std::filesystem::path dialogDir_;
	std::string dialogName_;
	// Unsaved-changes question
	bool confirmOpen_ = false;
	std::function<void()> pendingAction_;
	// Error / warning message
	std::string message_;
	bool messageOpen_ = false;
	std::string title_;
};

} // namespace gui
