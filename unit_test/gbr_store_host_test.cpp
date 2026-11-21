// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "types.h"
#include "ff.h"
#include "gbr/format.h"
#include "storage/gbr_filename.h"
#include "storage/gbr_store.h"

namespace {

std::map<std::string, std::vector<uint8>> files;
std::string open_path;
usize directory_index = 0;
bool directory_exists = false;
bool fail_next_write = false;
bool fail_next_close = false;

void finish_write(Storage::GbrStore& store) {
    for (int steps = 0; store.busy() && steps < 20; ++steps) {
        assert(store.step() != Storage::GbrStore::StepResult::Error);
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

extern "C" FRESULT f_open(FIL* file, const TCHAR* path, const BYTE mode) {
    if (mode == (FA_READ | FA_WRITE)) {
        const auto item = files.find(path);
        if (item == files.end()) {
            return FR_NO_FILE;
        }
        open_path = path;
        file->fptr = 0;
        file->obj.objsize = item->second.size();
        return FR_OK;
    }
    assert(mode == (FA_CREATE_NEW | FA_WRITE));
    if (files.count(path) != 0) {
        return FR_EXIST;
    }
    open_path = path;
    files[open_path] = {};
    file->fptr = 0;
    file->obj.objsize = 0;
    return FR_OK;
}

extern "C" FRESULT f_read(FIL* file, void* data, const UINT size, UINT* read) {
    const auto& contents = files.at(open_path);
    *read = static_cast<UINT>(std::min<usize>(size, contents.size() - file->fptr));
    std::copy_n(contents.data() + file->fptr, *read, static_cast<uint8*>(data));
    file->fptr += *read;
    return FR_OK;
}

extern "C" FRESULT f_lseek(FIL* file, const FSIZE_t offset) {
    if (offset > file->obj.objsize) {
        return FR_INVALID_PARAMETER;
    }
    file->fptr = offset;
    return FR_OK;
}

extern "C" FRESULT f_truncate(FIL* file) {
    auto& contents = files.at(open_path);
    contents.resize(file->fptr);
    file->obj.objsize = contents.size();
    return FR_OK;
}

extern "C" FRESULT f_write(FIL* file, const void* data, const UINT size, UINT* written) {
    if (fail_next_write) {
        fail_next_write = false;
        *written = 0;
        return FR_DISK_ERR;
    }
    const auto* bytes = static_cast<const uint8*>(data);
    auto& contents = files.at(open_path);
    if (file->fptr + size > contents.size()) {
        contents.resize(file->fptr + size);
    }
    std::copy_n(bytes, size, contents.begin() + file->fptr);
    file->fptr += size;
    file->obj.objsize = contents.size();
    *written = size;
    return FR_OK;
}

extern "C" FRESULT f_close(FIL*) {
    open_path.clear();
    if (!fail_next_close) {
        return FR_OK;
    }
    fail_next_close = false;
    return FR_DISK_ERR;
}

extern "C" FRESULT f_unlink(const TCHAR* path) {
    return files.erase(path) == 1 ? FR_OK : FR_NO_FILE;
}

int main() {
    std::array<uint8, 320> first_image{};
    const Gbr::Format::PrintControl default_print{1, 0x13, 0xE4, 0x40};
    const Gbr::Format::PrintControl custom_print{0, 0xA5, 0x2D, 0xFF};
    Storage::GbrStore initial_store;
    char last_name[32]{};
    assert(!initial_store.last_filename(last_name, sizeof(last_name)));
    assert(initial_store.initialize());
    assert(directory_exists);
    assert(initial_store.begin(first_image.data(), first_image.size(), default_print));
    finish_write(initial_store);
    assert(initial_store.last_filename(last_name, sizeof(last_name)));
    assert(std::string(last_name) == "00001.gbr");
    assert(!initial_store.last_filename(last_name, 4));
    assert(files.count("0:/DCIM/00001.gbr") == 1);
    const auto first_file_header = Gbr::Format::make_gbr_file_header(1);
    const auto first_block_header = Gbr::Format::make_gbr_block_header(160, 8, default_print);
    const auto& first_stored = files.at("0:/DCIM/00001.gbr");
    assert(first_stored.size() == 6 + 8 + first_image.size());
    assert(std::equal(first_file_header.begin(), first_file_header.end(), first_stored.begin()));
    assert(std::equal(first_block_header.begin(), first_block_header.end(), first_stored.begin() + 6));

    files["0:/DCIM/00099.gbr"] = {};
    files["0:/DCIM/100000.GBR"] = {};
    files["0:/DCIM/999999.gbp"] = {};
    files["0:/DCIM/notes.gbr"] = {};
    Storage::GbrStore restarted_store;
    assert(restarted_store.initialize());
    files["0:/DCIM/100001.gbr"] = {};

    std::vector<uint8> image(1600);
    for (usize i = 0; i < image.size(); ++i) {
        image[i] = static_cast<uint8>(i);
    }
    assert(!restarted_store.begin(image.data(), 1599, custom_print));
    assert(restarted_store.begin(image.data(), image.size(), custom_print));
    finish_write(restarted_store);
    assert(restarted_store.last_filename(last_name, sizeof(last_name)));
    assert(std::string(last_name) == "100002.gbr");
    const auto& stored = files.at("0:/DCIM/100002.gbr");
    const auto header = Gbr::Format::make_gbr_file_header(1);
    const auto block = Gbr::Format::make_gbr_block_header(160, 40, custom_print);
    assert(stored.size() == header.size() + block.size() + image.size());
    assert(std::equal(header.begin(), header.end(), stored.begin()));
    assert(std::equal(block.begin(), block.end(), stored.begin() + header.size()));
    assert(std::equal(image.begin(), image.end(), stored.begin() + header.size() + block.size()));
    assert(files.count("0:/DCIM/100001.gbr") == 1);

    const auto original = stored;
    fail_next_write = true;
    assert(restarted_store.begin(first_image.data(), first_image.size(), default_print, true));
    assert(restarted_store.step() == Storage::GbrStore::StepResult::Error);
    assert(files.at("0:/DCIM/100002.gbr") == original);

    assert(restarted_store.begin(first_image.data(), first_image.size(), default_print, true));
    assert(restarted_store.step() == Storage::GbrStore::StepResult::Progress);
    fail_next_write = true;
    assert(restarted_store.step() == Storage::GbrStore::StepResult::Error);
    assert(files.at("0:/DCIM/100002.gbr") == original);

    assert(restarted_store.begin(first_image.data(), first_image.size(), default_print, true));
    assert(restarted_store.step() == Storage::GbrStore::StepResult::Progress);
    assert(restarted_store.step() == Storage::GbrStore::StepResult::Progress);
    fail_next_write = true;
    assert(restarted_store.step() == Storage::GbrStore::StepResult::Error);
    assert(files.at("0:/DCIM/100002.gbr") == original);

    assert(restarted_store.begin(first_image.data(), first_image.size(), default_print, true));
    finish_write(restarted_store);
    const auto& appended = files.at("0:/DCIM/100002.gbr");
    assert(appended.size() == original.size() + 8 + first_image.size());
    assert(appended[5] == 2);
    assert(restarted_store.last_filename(last_name, sizeof(last_name)));
    assert(std::string(last_name) == "100002.gbr");
    assert(std::equal(first_block_header.begin(), first_block_header.end(), appended.begin() + original.size()));

    char seven_digit_path[40]{};
    assert(Storage::format_gbr_path(1000000, seven_digit_path, sizeof(seven_digit_path)));
    assert(std::string(seven_digit_path) == "0:/DCIM/1000000.gbr");

    fail_next_write = true;
    assert(restarted_store.begin(image.data(), image.size(), custom_print));
    assert(restarted_store.step() == Storage::GbrStore::StepResult::Error);
    assert(files.count("0:/DCIM/100003.gbr") == 0);
    assert(restarted_store.begin(first_image.data(), first_image.size(), default_print, true));
    finish_write(restarted_store);
    assert(files.count("0:/DCIM/100004.gbr") == 1);
    assert(files.at("0:/DCIM/100002.gbr")[5] == 2);

    fail_next_close = true;
    assert(restarted_store.begin(first_image.data(), first_image.size(), default_print, true));
    for (int steps = 0; steps < 10 && restarted_store.busy(); ++steps) {
        if (restarted_store.step() == Storage::GbrStore::StepResult::Error) {
            break;
        }
    }
    assert(!restarted_store.busy());
    assert(restarted_store.begin(first_image.data(), first_image.size(), default_print, true));
    finish_write(restarted_store);
    assert(files.count("0:/DCIM/100005.gbr") == 1);
}
