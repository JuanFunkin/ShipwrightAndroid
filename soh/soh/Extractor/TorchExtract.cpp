#include "TorchExtract.h"

#include <exception>
#include <fstream>
#include <filesystem>
#include <memory>
#include <utility>

#include "spdlog/spdlog.h"

#include "Companion.h"
#include "factories/BaseFactory.h"

namespace fs = std::filesystem;

namespace SohTorch {

static std::string sLastError;

const std::string& LastError() {
    return sLastError;
}

void SetLastError(const std::string& message) {
    sLastError = message;
    SPDLOG_ERROR("Extraction error: {}", message);
    std::ofstream file("SoH-extraction-error.txt", std::ios::trunc);
    if (file.is_open()) {
        file << message << "\n";
    }
}

size_t CountAssetFiles(const std::string& ymlDir) {
    std::error_code ec;
    if (!fs::is_directory(ymlDir, ec)) {
        return 0;
    }

    size_t count = 0;
    for (fs::recursive_directory_iterator it(ymlDir, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }
        if (it->is_regular_file(ec) && it->path().extension() == ".yml") {
            count++;
        }
    }
    return count;
}

std::string Extract(std::vector<uint8_t> rom, const std::string& srcDir, const std::string& destDir,
                    const std::string& portVersion, std::atomic<size_t>* progress) {
    std::string archiveName;
    sLastError.clear();

    try {
        // Companion::Instance is a raw global with no getter; factories dereference it.
        auto companion = std::make_unique<Companion>(std::move(rom), ArchiveType::O2R, false, srcDir, destDir);
        Companion::Instance = companion.get();
        companion->SetVersion(portVersion);
        companion->SetPhaseCallback([progress](int) {
            if (progress != nullptr) {
                (*progress)++;
            }
        });

        // Init is the whole run; it calls Process() internally.
        companion->Init(ExportType::Binary);

        // config.yml names the archive per rom; ask rather than guess, and ask before the
        // companion goes away.
        archiveName = fs::path(companion->GetOutputPath()).filename().string();

        // Companion holds every parsed asset, so don't leak it into the game's lifetime.
        companion.reset();
        Companion::Instance = nullptr;
    } catch (const std::exception& e) {
        SetLastError(std::string("Torch extraction failed: ") + e.what() + "\nAssets folder: " + srcDir);
        Companion::Instance = nullptr;
        return "";
    } catch (...) {
        SetLastError("Torch extraction failed with an unknown exception.\nAssets folder: " + srcDir);
        Companion::Instance = nullptr;
        return "";
    }

    // Process() returns void and several fatal paths only log and return, so confirm the
    // archive is really there rather than trusting the run.
    std::error_code ec;
    if (archiveName.empty() || !fs::exists(fs::path(destDir) / archiveName, ec)) {
        SetLastError("Torch finished but produced no archive.\nAssets folder: " + srcDir +
                     "\nIf that folder is missing or empty, copy the \"assets\" folder from the download next to the "
                     "game.");
        return "";
    }

    return archiveName;
}

} // namespace SohTorch
