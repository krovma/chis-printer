// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "ff.h"
#include "gbp/format/gbr.h"
#include "storage/gbr_library.h"

namespace {

std::map<std::string, std::vector<std::uint8_t>> files;
std::string open_path;
std::size_t directory_index = 0;
std::size_t directory_opens = 0;

std::vector<std::uint8_t> make_file(const std::vector<std::uint8_t>& header) {
    std::vector<std::uint8_t> file = header;
    file.insert(file.end(), 320, 0xA5);
    return file;
}

}  // namespace

extern "C" FRESULT f_opendir(DIR*, const TCHAR* path) {
    assert(std::strcmp(path, "0:/DCIM") == 0);
    ++directory_opens;
    directory_index = 0;
    return FR_OK;
}

extern "C" FRESULT f_readdir(DIR*, FILINFO* entry) {
    if (directory_index == files.size()) {
        entry->fname[0] = '\0';
        return FR_OK;
    }
    auto item = files.begin();
    std::advance(item, directory_index++);
    const auto name = item->first.substr(item->first.find_last_of('/') + 1);
    std::snprintf(entry->fname, sizeof(entry->fname), "%s", name.c_str());
    entry->fattrib = 0;
    return FR_OK;
}

extern "C" FRESULT f_closedir(DIR*) { return FR_OK; }

extern "C" FRESULT f_open(FIL* file, const TCHAR* path, const BYTE mode) {
    assert(mode == FA_READ);
    const auto item = files.find(path);
    if (item == files.end()) return FR_NO_FILE;
    open_path = path;
    file->obj.objsize = item->second.size();
    file->fptr = 0;
    return FR_OK;
}

extern "C" FRESULT f_read(FIL* file, void* output, const UINT length, UINT* read) {
    const auto& contents = files.at(open_path);
    *read = static_cast<UINT>(std::min<std::size_t>(length, contents.size() - file->fptr));
    std::memcpy(output, contents.data() + file->fptr, *read);
    file->fptr += *read;
    return FR_OK;
}

extern "C" FRESULT f_lseek(FIL* file, const FSIZE_t offset) {
    if (offset > file->obj.objsize) return FR_INVALID_PARAMETER;
    file->fptr = offset;
    return FR_OK;
}

extern "C" FRESULT f_close(FIL*) {
    open_path.clear();
    return FR_OK;
}

int main() {
    files["0:/DCIM/00001.gbr"] = make_file({'K', 'G', 'B', 'R', 1, 160, 8, 0});
    const gbp::format::PrintControl control{3, 0xA5, 0x2D, 0x7F};
    const auto v2_header = gbp::format::make_gbr_header(160, 8, control);
    files["0:/DCIM/photo.GBR"] = make_file({v2_header.begin(), v2_header.end()});
    files["0:/DCIM/bad.gbr"] = {'K', 'G', 'B', 'R', 2};
    files["0:/DCIM/notes.txt"] = {};

    storage::GbrCatalog catalog;
    assert(catalog.refresh() && catalog.count() == 3);
    assert(directory_opens == 1);
    char name[storage::GBR_NAME_CAPACITY]{};
    assert(catalog.name_at(2, name, sizeof(name)) && std::strcmp(name, "photo.GBR") == 0);
    assert(!catalog.name_at(3, name, sizeof(name)));
    assert(directory_opens == 1);

    storage::GbrImage image;
    assert(image.load("00001.gbr"));
    assert(image.size() == 320 && image.data()[0] == 0xA5);
    assert(image.print_control() == gbp::format::DEFAULT_PRINT_CONTROL);
    assert(image.load("photo.GBR"));
    assert(image.print_control() == control);
    assert(!image.load("bad.gbr") && image.data() == nullptr);
    assert(!image.load("../photo.GBR"));
    catalog.clear();
    assert(catalog.count() == 0);
}
