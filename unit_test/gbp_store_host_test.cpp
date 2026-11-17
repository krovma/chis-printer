// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "ff.h"
#include "gbp/format/gbr.h"
#include "storage/gbr_filename.h"
#include "storage/gbp_store.h"

namespace {

std::map<std::string, std::vector<std::uint8_t>> files;
std::string open_path;
std::size_t directory_index = 0;
bool directory_exists = false;
bool fail_next_write = false;

void finish_write(storage::GbpStore& store) {
    for (int steps = 0; store.busy() && steps < 20; ++steps) {
        assert(store.step() != storage::GbpStore::StepResult::Error);
    }
    assert(!store.busy());
}

}  // namespace

extern "C" FRESULT f_opendir(DIR*, const TCHAR* path) {
    assert(std::string(path) == "0:/DCIM");
    directory_index = 0;
    return directory_exists ? FR_OK : FR_NO_PATH;
}

extern "C" FRESULT f_mkdir(const TCHAR* path) {
    assert(std::string(path) == "0:/DCIM");
    directory_exists = true;
    return FR_OK;
}

extern "C" FRESULT f_readdir(DIR*, FILINFO* entry) {
    if (directory_index == files.size()) {
        entry->fname[0] = '\0';
        return FR_OK;
    }
    auto file = files.begin();
    std::advance(file, directory_index++);
    const std::string name = file->first.substr(file->first.find_last_of('/') + 1);
    std::snprintf(entry->fname, sizeof(entry->fname), "%s", name.c_str());
    entry->fattrib = 0;
    return FR_OK;
}

extern "C" FRESULT f_closedir(DIR*) {
    return FR_OK;
}

extern "C" FRESULT f_open(FIL*, const TCHAR* path, const BYTE mode) {
    assert(mode == (FA_CREATE_NEW | FA_WRITE));
    if (files.count(path) != 0) {
        return FR_EXIST;
    }
    open_path = path;
    files[open_path] = {};
    return FR_OK;
}

extern "C" FRESULT f_write(FIL*, const void* data, const UINT size, UINT* written) {
    if (fail_next_write) {
        fail_next_write = false;
        *written = 0;
        return FR_DISK_ERR;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    files[open_path].insert(files[open_path].end(), bytes, bytes + size);
    *written = size;
    return FR_OK;
}

extern "C" FRESULT f_close(FIL*) {
    open_path.clear();
    return FR_OK;
}

extern "C" FRESULT f_unlink(const TCHAR* path) {
    return files.erase(path) == 1 ? FR_OK : FR_NO_FILE;
}

int main() {
    std::array<std::uint8_t, 320> first_image{};
    storage::GbpStore initial_store;
    assert(initial_store.initialize());
    assert(directory_exists);
    assert(initial_store.begin(first_image.data(), first_image.size()));
    finish_write(initial_store);
    assert(files.count("0:/DCIM/00001.gbr") == 1);

    files["0:/DCIM/00099.gbr"] = {};
    files["0:/DCIM/100000.GBR"] = {};
    files["0:/DCIM/999999.gbp"] = {};
    files["0:/DCIM/notes.gbr"] = {};
    storage::GbpStore restarted_store;
    assert(restarted_store.initialize());
    files["0:/DCIM/100001.gbr"] = {};

    std::vector<std::uint8_t> image(1600);
    for (std::size_t i = 0; i < image.size(); ++i) {
        image[i] = static_cast<std::uint8_t>(i);
    }
    assert(!restarted_store.begin(image.data(), 1599));
    assert(restarted_store.begin(image.data(), image.size()));
    finish_write(restarted_store);
    const auto& stored = files.at("0:/DCIM/100002.gbr");
    const auto header = gbp::format::make_gbr_header(160, 40);
    assert(stored.size() == header.size() + image.size());
    assert(std::equal(header.begin(), header.end(), stored.begin()));
    assert(std::equal(image.begin(), image.end(), stored.begin() + header.size()));
    assert(files.count("0:/DCIM/100001.gbr") == 1);

    char seven_digit_path[40]{};
    assert(storage::format_gbr_path(1000000, seven_digit_path, sizeof(seven_digit_path)));
    assert(std::string(seven_digit_path) == "0:/DCIM/1000000.gbr");

    fail_next_write = true;
    assert(restarted_store.begin(image.data(), image.size()));
    assert(restarted_store.step() == storage::GbpStore::StepResult::Error);
    assert(files.count("0:/DCIM/100003.gbr") == 0);
    assert(restarted_store.begin(first_image.data(), first_image.size()));
    finish_write(restarted_store);
    assert(files.count("0:/DCIM/100004.gbr") == 1);
}
