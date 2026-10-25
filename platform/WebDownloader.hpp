// CppSekai web port — in-game chart/audio downloader + IDBFS persistence.
//
// The Windows build shells out to chartdl.exe from main()'s SelectDownload
// branch; a browser cannot spawn processes, so this module replaces that hand-off
// with an ImGui card inside the game. It is deliberately small and self-contained
// so main.cpp only needs three calls:
//
//     platform::webDownloader::mountStorage();     // once, before the chart scan
//     platform::webDownloader::open();             // on game::SelectDownload
//     if (platform::webDownloader::draw()) { ... } // per frame in the select state
//
// Downloads are plain fetch()es into the emscripten filesystem, and /charts is
// an IDBFS mount so the files survive a page reload.
#pragma once

namespace platform
{
namespace webDownloader
{

// Mounts IDBFS on /charts (creating it if needed). Safe to call twice.
void mountStorage();

// Opens the card (called from the SelectDownload action).
void open();

[[nodiscard]] bool visible();

// Draws the card and drives the download state machine.
// Returns true once a batch just finished, so the caller can re-scan the chart
// folders (the same thing main.cpp does after chartdl.exe exits on Windows).
bool draw();

} // namespace webDownloader
} // namespace platform
