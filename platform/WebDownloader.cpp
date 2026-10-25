// CppSekai web port — in-game downloader (see WebDownloader.hpp).
//
// URL rules are copied verbatim from downloader/chartdl.cpp:
//   chart  https://assets.unipjsk.com/startapp/music/music_score/<id4>_01/<diff>
//   audio  https://assets.unipjsk.com/ondemand/music/long/<asset>/<asset>.mp3
// CN-only songs (id >= 10000) live on storage.sekai.best with a different layout.
//
// Both hosts were measured to send Access-Control-Allow-Origin, so the browser
// can fetch them directly - no proxy.

#include "WebDownloader.hpp"

#include <emscripten.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "imgui.h"
#include "nlohmann/json.hpp"

// The game's own font: song select pushes it explicitly (game/SongSelect.cpp),
// which is why 42px CJK/JA renders there and plain ImGui::Text (default font,
// usually ImGui's built-in ASCII bitmap face) turns Japanese into tofu here.
#include "game/Intro.hpp"
#include "game/Ui.hpp" // ui::beginCard / capsuleButton / cardTitle

// Provided by web/idbfs-lib.js (--js-library). Returning 0 means the backend was
// dropped from the link, which tells mountStorage() the mount would throw.
extern "C" int cppsekai_idbfs_supported();

namespace platform
{
namespace webDownloader
{
namespace
{

constexpr const char* kChartDir = "/charts";
constexpr const char* kDiffNames[5] = {"easy", "normal", "hard", "expert", "master"};
constexpr int kMaxJobs = 96;

enum class State
{
    Closed,
    Open,
};

State gState = State::Closed;
bool gStorageMounted = false;
bool gCatalogLoaded = false;
bool gDbLoaded = false;

struct SongRow
{
    int id = 0;
    std::string title;
    std::string pronunciation; // musics.json kana, used by the search box
    std::string assetbundleName; // e.g. jacket_s_018 - NOT id4 ("jacket_s_0018" is 404)
    std::vector<std::string> vocalAssets;
    std::vector<std::string> aliases; // music-aliases.json community names
    double fillerSec = 0.0;           // leading silence of the official mp3
    int levels[5] = {0, 0, 0, 0, 0};
    bool have[5] = {false, false, false, false, false};
};
std::vector<SongRow> gSongs;

struct Job
{
    int slot = -1;
    int songIndex = -1;
    std::string label;
    bool finished = false;
    bool ok = false;
};
std::vector<Job> gJobs;
int gNextSlot = 0;
bool gBatchActive = false;
bool gRescanPending = false;
char gFilter[96] = "";
int gSelected = -1;
std::string gLog;
int gLastFetched = -1;

// ---- JS bridge --------------------------------------------------------
EM_JS(void, js_init, (), {
    if (!Module.__cppsekaiDl) {
        Module.__cppsekaiDl = { jobs: new Array(96).fill(null), done: 0, failed: 0 };
    }
});

// 1 once the /charts IDBFS restore (async!) has finished. main() runs the chart
// scan synchronously right after boot, so without this the first scan always
// sees an empty /charts and the player has to press refresh.
EM_JS(int, js_storage_ready, (), {
    return Module.__cppsekaiDlReady ? 1 : 0;
});

EM_JS(void, js_fetch_to_fs, (const char* url, const char* path, int slot), {
    const dl = Module.__cppsekaiDl;
    const u = UTF8ToString(url);
    const p = UTF8ToString(path);
    dl.jobs[slot] = { state: 1 };
    fetch(u)
        .then((r) => {
            if (!r.ok) {
                throw new Error('HTTP ' + r.status);
            }
            return r.arrayBuffer();
        })
        .then((buf) => {
            const bytes = new Uint8Array(buf);
            const slash = p.lastIndexOf('/');
            if (slash > 0) {
                try { Module.FS.mkdirTree(p.substring(0, slash)); } catch (e) {}
            }
            try {
                Module.FS.writeFile(p, bytes);
            } catch (e) {
                dl.jobs[slot] = { state: -1, err: String(e) };
                dl.failed++;
                _cppsekai_dl_done(slot, 0);
                return;
            }
            dl.jobs[slot] = { state: 2, bytes: bytes.length };
            dl.done++;
            _cppsekai_dl_done(slot, 1);
        })
        .catch((e) => {
            dl.jobs[slot] = { state: -1, err: String((e && e.message) || e) };
            dl.failed++;
            _cppsekai_dl_done(slot, 0);
        });
});

EM_JS(int, js_job_state, (int slot), {
    const dl = Module.__cppsekaiDl;
    const j = dl && dl.jobs[slot];
    return j ? j.state : 0;
});

// ---- helpers ----------------------------------------------------------
std::string id4(int id)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d", id);
    return buf;
}

bool fileExists(const std::string& path)
{
    if (FILE* f = std::fopen(path.c_str(), "rb"))
    {
        std::fclose(f);
        return true;
    }
    return false;
}

std::string chartUrl(int id, const char* diff)
{
    if (id >= 10000)
    {
        return "https://storage.sekai.best/sekai-cn-assets/music/music_score/" + id4(id) + "_01/" + diff + ".txt";
    }
    return "https://assets.unipjsk.com/startapp/music/music_score/" + id4(id) + "_01/" + diff;
}

std::string audioUrl(const std::string& asset, int id)
{
    if (id >= 10000)
    {
        return "https://storage.sekai.best/sekai-cn-assets/music/long/" + asset + "/" + asset + ".mp3";
    }
    return "https://assets.unipjsk.com/ondemand/music/long/" + asset + "/" + asset + ".mp3";
}

// Cover art. The CDN stem is musics.json's assetbundleName ("jacket_s_018"),
// saved locally as "<id>.png" because that is what findSidecar() probes.
std::string jacketUrl(const std::string& assetbundleName)
{
    if (assetbundleName.empty())
    {
        return {};
    }
    return "https://assets.unipjsk.com/startapp/music/jacket/" + assetbundleName + "/" + assetbundleName + ".png";
}

bool readText(const char* path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return !out.empty();
}

void loadCatalog()
{
    if (gCatalogLoaded)
    {
        return;
    }
    gCatalogLoaded = true;

    std::string text;
    if (!readText("/musics.json", text))
    {
        gLog += "找不到 /musics.json\n";
        return;
    }
    nlohmann::json musics = nlohmann::json::parse(text, nullptr, false);
    if (musics.is_discarded() || !musics.is_array())
    {
        gLog += "/musics.json 解析失败\n";
        return;
    }

    nlohmann::json vocals = nlohmann::json::object();
    if (readText("/music-vocals.json", text))
    {
        nlohmann::json v = nlohmann::json::parse(text, nullptr, false);
        if (!v.is_discarded())
        {
            vocals = v;
        }
    }
    nlohmann::json levels = nlohmann::json::object();
    if (readText("/music-levels.json", text))
    {
        nlohmann::json l = nlohmann::json::parse(text, nullptr, false);
        if (!l.is_discarded())
        {
            levels = l;
        }
    }
    // Community aliases: { "<musicId>": ["别名", ...] }. Search matches these too.
    nlohmann::json aliases = nlohmann::json::object();
    if (readText("/music-aliases.json", text))
    {
        nlohmann::json a = nlohmann::json::parse(text, nullptr, false);
        if (!a.is_discarded())
        {
            aliases = a;
        }
    }

    for (const auto& m : musics)
    {
        if (!m.contains("id") || !m.contains("title"))
        {
            continue;
        }
        SongRow row;
        row.id = m["id"].get<int>();
        row.title = m["title"].get<std::string>();
        if (m.contains("pronunciation") && m["pronunciation"].is_string())
        {
            row.pronunciation = m["pronunciation"].get<std::string>();
        }
        if (m.contains("fillerSec") && m["fillerSec"].is_number())
        {
            row.fillerSec = m["fillerSec"].get<double>();
        }
        if (m.contains("assetbundleName") && m["assetbundleName"].is_string())
        {
            row.assetbundleName = m["assetbundleName"].get<std::string>();
        }
        const std::string key = std::to_string(row.id);
        if (vocals.contains(key) && vocals[key].is_array())
        {
            for (const auto& v : vocals[key])
            {
                if (v.contains("asset"))
                {
                    row.vocalAssets.push_back(v["asset"].get<std::string>());
                }
            }
        }
        if (levels.contains(key) && levels[key].is_array())
        {
            int i = 0;
            for (const auto& lv : levels[key])
            {
                if (i < 5 && lv.is_number())
                {
                    row.levels[i] = lv.get<int>();
                }
                ++i;
            }
        }
        if (aliases.contains(key) && aliases[key].is_array())
        {
            for (const auto& a : aliases[key])
            {
                if (a.is_string())
                {
                    row.aliases.push_back(a.get<std::string>());
                }
            }
        }
        gSongs.push_back(std::move(row));
    }
    gDbLoaded = true;
    std::printf("[webdl] catalog: %d songs\n", static_cast<int>(gSongs.size()));
    std::fflush(stdout);
}

void refreshHaveFlags()
{
    for (auto& row : gSongs)
    {
        for (int d = 0; d < 5; ++d)
        {
            row.have[d] = fileExists(std::string(kChartDir) + "/" + id4(row.id) + "_" + kDiffNames[d] + ".sus");
        }
    }
}

bool jobActive()
{
    for (const auto& j : gJobs)
    {
        if (!j.finished)
        {
            return true;
        }
    }
    return false;
}

void queueOne(int songIndex, const std::string& url, const std::string& path)
{
    Job job;
    job.slot = gNextSlot++ % kMaxJobs;
    job.songIndex = songIndex;
    job.label = path;
    gJobs.push_back(job);
    js_fetch_to_fs(url.c_str(), path.c_str(), job.slot);
}

void startDownload(int songIndex)
{
    const SongRow& song = gSongs[static_cast<size_t>(songIndex)];
    if (song.vocalAssets.empty())
    {
        gLog += id4(song.id) + " 没有演唱版本信息\n";
        return;
    }
    for (int d = 0; d < 5; ++d)
    {
        if (song.have[d] || song.levels[d] <= 0)
        {
            continue;
        }
        queueOne(songIndex, chartUrl(song.id, kDiffNames[d]),
            std::string(kChartDir) + "/" + id4(song.id) + "_" + kDiffNames[d] + ".sus");
    }
    // Every vocal version, not just the first: the song select's 切换歌手 panel
    // lists exactly the assets whose .mp3 exists next to the chart, so one file
    // per version is what makes the other singers selectable.
    for (const auto& asset : song.vocalAssets)
    {
        const std::string audioPath = std::string(kChartDir) + "/" + asset + ".mp3";
        if (!fileExists(audioPath))
        {
            queueOne(songIndex, audioUrl(asset, song.id), audioPath);
        }
    }
    // Jacket (song select's cover). findSidecar() looks for "<chart-music-id>.png"
    // next to the chart, so the file is named by the id, not by the CDN's
    // "jacket_s_<id>" stem.
    {
        const std::string jacketPath = std::string(kChartDir) + "/" + id4(song.id) + ".png";
        const std::string url = jacketUrl(song.assetbundleName);
        if (!fileExists(jacketPath) && !url.empty())
        {
            queueOne(songIndex, url, jacketPath);
        }
    }
    const std::string sidecarPath = std::string(kChartDir) + "/" + id4(song.id) + ".json";
    if (!fileExists(sidecarPath))
    {
        // fillerSec is what lines the chart's time 0 up with the end of the
        // official mp3's leading silence; without it the music runs ahead of
        // the notes (the "曲比谱快" report).
        std::ofstream out(sidecarPath);
        out << "{\"fillerSec\":" << song.fillerSec << "}";
    }
    gBatchActive = true;
    gLog += "开始下载 " + id4(song.id) + " " + song.title + "\n";
}

} // namespace

void mountStorage()
{
    if (gStorageMounted)
    {
        return;
    }
    gStorageMounted = true;
    js_init();
    // Calling this is what keeps web/idbfs-lib.js (and its $IDBFS dependency)
    // in the link. Without a compiled reference the backend is dead-code
    // eliminated and FS.mount throws "IDBFS is not defined".
    if (cppsekai_idbfs_supported() == 0)
    {
        std::printf("[webdl] IDBFS unavailable - /charts will not persist\n");
        std::fflush(stdout);
    }
    // Persistence lives in the page layer (web/persist-lib.js + the preRun hook
    // injected by inject-persistence.py), NOT in IDBFS: IDBFS restores
    // asynchronously, but main() reads userdata.json and scans /charts
    // synchronously at boot, so an async restore always reads nothing. The page
    // restores everything into MEMFS *before* the module starts instead.
    EM_ASM({
        try { FS.mkdir('/charts'); } catch (e) {}
        // Seed charts are preloaded to /seed (a preload at /charts would be
        // hidden); copy them in now, synchronously, so the boot scan sees them.
        try {
            FS.readdir('/seed').forEach(function (name) {
                var src = '/seed/' + name;
                var dst = '/charts/' + name;
                var st = FS.analyzePath(src);
                if (!st.exists || st.isFolder) {
                    return;
                }
                if (!FS.analyzePath(dst).exists) {
                    FS.writeFile(dst, FS.readFile(src));
                }
            });
        } catch (e) {
            console.warn('[webdl] seed copy skipped', e);
        }
        Module.__cppsekaiDlReady = true;
    });
}

void open()
{
    loadCatalog();
    refreshHaveFlags();
    gState = State::Open;
    gLog.clear();
    gLog += "选一首歌，点「下载」。谱面和音频会存进浏览器的 IndexedDB，刷新不丢。\n";
}

bool visible()
{
    return gState == State::Open;
}

bool draw()
{
    // The /charts IDBFS restore is asynchronous but main() scans synchronously,
    // so the very first scan runs on an empty directory. The moment the restore
    // lands, ask the caller to scan again (this is what used to require a manual
    // refresh after every reload).
    static bool sStorageReadySeen = false;
    if (!sStorageReadySeen && js_storage_ready() == 1)
    {
        sStorageReadySeen = true;
        gRescanPending = true;
    }
    if (gState != State::Open)
    {
        return false;
    }

    // Poll finished jobs.
    bool anyActive = false;
    for (auto& job : gJobs)
    {
        if (job.finished)
        {
            continue;
        }
        const int st = js_job_state(job.slot);
        if (st == 1)
        {
            anyActive = true;
        }
        else if (st == 2)
        {
            job.finished = true;
            job.ok = true;
        }
        else if (st < 0)
        {
            job.finished = true;
            job.ok = false;
        }
    }
    if (gBatchActive && !anyActive)
    {
        // Everything settled: persist to IndexedDB and let the caller re-scan.
        gBatchActive = false;
        refreshHaveFlags();
        EM_ASM({
            try {
                FS.syncfs(false, function (err) {
                    if (err) { console.warn('[webdl] persist failed', err); }
                });
            } catch (e) {}
        });
        gRescanPending = true;
    }

    bool rescan = false;
    if (gRescanPending)
    {
        gRescanPending = false;
        rescan = true;
    }

    // Same face the song select draws its song titles with, but sized through
    // the UI scale: the raw 42px face is meant for the 1080p virtual canvas and
    // is huge inside a dialog-sized card. This is exactly what the song select
    // does (PushFont(body, 17.0f * k)).
    const float k = ui::scale();
    ImFont* body = game::bodyFont();
    if (body != nullptr)
    {
        ImGui::PushFont(body, 17.0f * k);
    }

    // Same sizing rule as the settings card (main.cpp):
    //   ImVec2 cardSize = ImVec2(380.0f * s, 800.0f * s);
    //   cardCenter = (18*s + w/2, 18*s + h/2)
    // i.e. design pixels scaled by ui::scale(), anchored near the top-left, not
    // a percentage of the screen.
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 size(560.0f * k, 700.0f * k);
    ImVec2 center(18.0f * k + size.x * 0.5f, 18.0f * k + size.y * 0.5f);
    bool closeClicked = false;
    if (ui::beginCard("webdlcard", &center, &size, true, true, &closeClicked, true))
    {
        // The card's own text colour (#444466). Without this every label comes
        // out in ImGui's default light grey and is invisible on the white card.
        ImGui::PushStyleColor(ImGuiCol_Text, ui::kText);
        ui::cardTitle("下载谱面 / 音频", size.x - 56.0f * k);

    ImGui::InputTextWithHint("##webdl_filter", "搜索曲名 / ID / 读音 / 别名", gFilter, sizeof(gFilter));
    ImGui::SameLine();
    // Plain button, not ui::capsuleButton: that one centers itself with
    // SetCursorScreenPos, which inside a SameLine row pushes it outside the card.
    if (ImGui::Button("刷新"))
    {
        refreshHaveFlags();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d 首曲目", static_cast<int>(gSongs.size()));

    ImGui::BeginChild("##webdl_list", ImVec2(0.0f, -140.0f), true);
    const std::string filter = gFilter;
    // Only rows that pass the filter go through the clipper; drawing all 736
    // rows (each with 5 status glyphs) every frame was the single biggest cost
    // in this card.
    static std::vector<int> visible;
    visible.clear();
    for (int i = 0; i < static_cast<int>(gSongs.size()); ++i)
    {
        const SongRow& row = gSongs[static_cast<size_t>(i)];
        if (!filter.empty())
        {
            // Match id / title / kana reading / community aliases, same set the
            // song select's search box accepts.
            std::string hay = id4(row.id) + " " + row.title + " " + row.pronunciation;
            for (const auto& a : row.aliases)
            {
                hay += " " + a;
            }
            if (hay.find(filter) == std::string::npos)
            {
                continue;
            }
        }
        visible.push_back(i);
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible.size()));
    while (clipper.Step())
    {
        for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n)
        {
            const int i = visible[static_cast<size_t>(n)];
            const SongRow& row = gSongs[static_cast<size_t>(i)];
            char label[320];
            std::snprintf(label, sizeof(label), "%s  %s##%d", id4(row.id).c_str(), row.title.c_str(), i);
            ImGui::PushID(i);
            if (ImGui::Selectable(label, gSelected == i))
            {
                gSelected = i;
            }
            ImGui::SameLine(500.0f * ui::scale());
            for (int d = 0; d < 5; ++d)
            {
                ImGui::TextColored(
                    row.have[d] ? ImVec4(0.45f, 0.90f, 0.60f, 1.0f) : ImVec4(0.45f, 0.48f, 0.56f, 1.0f),
                    "%s", row.have[d] ? "●" : "○");
                ImGui::SameLine();
            }
            ImGui::NewLine();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (gSelected >= 0 && gSelected < static_cast<int>(gSongs.size()))
    {
        const SongRow& sel = gSongs[static_cast<size_t>(gSelected)];
        char head[320];
        std::snprintf(head, sizeof(head), "已选：%s  %s", id4(sel.id).c_str(), sel.title.c_str());
        ImGui::TextUnformatted(head);
        ImGui::SameLine();
        ImGui::BeginDisabled(jobActive());
        if (ImGui::Button("下载全部难度 + 音频", ImVec2(260.0f, 0.0f)))
        {
            startDownload(gSelected);
        }
        ImGui::EndDisabled();
    }
    else
    {
        ImGui::TextUnformatted("从列表里选一首歌");
    }

    int active = 0;
    int done = 0;
    int failed = 0;
    for (const auto& job : gJobs)
    {
        if (!job.finished)
        {
            active++;
        }
        else if (job.ok)
        {
            done++;
        }
        else
        {
            failed++;
        }
    }
    ImGui::Text("进行中 %d   完成 %d   失败 %d", active, done, failed);
    ImGui::BeginChild("##webdl_log", ImVec2(0.0f, 0.0f), false);
    ImGui::TextUnformatted(gLog.c_str());
    ImGui::EndChild();

        ImGui::PopStyleColor();
        ui::endCard();
    }
    if (closeClicked)
    {
        gState = State::Closed;
    }
    if (body != nullptr)
    {
        ImGui::PopFont();
    }
    return rescan;
}

} // namespace webDownloader
} // namespace platform

// Called from the JS fetch bridge.
extern "C" EMSCRIPTEN_KEEPALIVE void cppsekai_dl_done(int slot, int ok)
{
    // The draw() poll already observes the JS state; this hook exists so a
    // finished transfer can be logged even while the card is closed.
    std::printf("[webdl] job %d %s\n", slot, ok == 1 ? "ok" : "FAILED");
    std::fflush(stdout);
}
