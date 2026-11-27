// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "types.h"
#include "ff.h"
#include "gbr/format.h"
#include "storage/gbr_library.h"

namespace {

std::map<std::string, std::vector<uint8>> files;
std::string open_path;
usize directory_index = 0;
usize directory_opens = 0;

std::vector<uint8> make_file(const std::vector<uint8>& header) {
    std::vector<uint8> file = header;
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
    if (item == files.end()) {
        return FR_NO_FILE;
    }
    open_path = path;
    file->obj.objsize = item->second.size();
    file->fptr = 0;
    return FR_OK;
}

extern "C" FRESULT f_read(FIL* file, void* output, const UINT length, UINT* read) {
    const auto& contents = files.at(open_path);
    *read = static_cast<UINT>(std::min<usize>(length, contents.size() - file->fptr));
    std::memcpy(output, contents.data() + file->fptr, *read);
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

extern "C" FRESULT f_close(FIL*) {
    open_path.clear();
    return FR_OK;
}

int main() {
    files["0:/DCIM/00001.gbr"] = make_file({'K', 'G', 'B', 'R', 1, 160, 8, 0});
    const Gbr::Format::PrintControl control{3, 0xA5, 0x2D, 0x7F};
    const auto v2_header = Gbr::Format::make_gbr_header(160, 8, control);
    files["0:/DCIM/photo.GBR"] = make_file({v2_header.begin(), v2_header.end()});
    files["0:/DCIM/bad.gbr"] = {'K', 'G', 'B', 'R', 2};
    files["0:/DCIM/notes.txt"] = {};

    Storage::GbrCatalog catalog;
    assert(catalog.refresh() && catalog.count() == 3);
    assert(directory_opens == 1);
    char name[Storage::GBR_NAME_CAPACITY]{};
    assert(catalog.name_at(2, name, sizeof(name)) && std::strcmp(name, "photo.GBR") == 0);
    assert(!catalog.name_at(3, name, sizeof(name)));
    assert(directory_opens == 1);

    Storage::GbrImage image;
    assert(image.load("00001.gbr"));
    assert(image.size() == 320 && image.data()[0] == 0xA5);
    assert(image.source_rows() == 8 && image.inner_margin_rows() == 0);
    assert(image.print_rows() == 40);
    assert(image.print_control() == Gbr::Format::DEFAULT_PRINT_CONTROL);
    assert(image.load("photo.GBR"));
    assert(image.print_control() == control);
    assert(image.print_rows() == 128);
    const Gbr::Format::PrintControl second_control{0, 0x21, 0x1B, 0x00};
    const auto file_header = Gbr::Format::make_gbr_file_header(2);
    const auto first_block = Gbr::Format::make_gbr_block_header(160, 8, control);
    const auto second_block = Gbr::Format::make_gbr_block_header(160, 8, second_control);
    std::vector<uint8> multi{file_header.begin(), file_header.end()};
    multi.insert(multi.end(), first_block.begin(), first_block.end());
    multi.insert(multi.end(), 320, 0xA5);
    multi.insert(multi.end(), second_block.begin(), second_block.end());
    multi.insert(multi.end(), 320, 0x5A);
    files["0:/DCIM/multi.gbr"] = multi;
    assert(image.load("multi.gbr"));
    assert(image.block_count() == 2 && image.block_index() == 0 && image.total_size() == 640);
    assert(image.source_rows() == 16 && image.inner_margin_rows() == 56);
    assert(image.print_rows() == 160);
    assert(image.size() == 320 && image.data()[0] == 0xA5 && image.print_control() == control);
    assert(image.next_block());
    assert(image.block_index() == 1 && image.data()[0] == 0x5A);
    assert(image.print_control() == second_control);
    assert(!image.next_block());
    assert(image.select_block(0) && image.data()[0] == 0xA5);
    assert(image.select_block(1) && image.data()[0] == 0x5A);
    assert(!image.select_block(2));

    files["0:/DCIM/wrong-version.gbr"] = multi;
    files["0:/DCIM/wrong-version.gbr"][6] = 3;
    assert(!image.load("wrong-version.gbr"));
    files["0:/DCIM/wrong-count.gbr"] = multi;
    files["0:/DCIM/wrong-count.gbr"][5] = 1;
    assert(!image.load("wrong-count.gbr"));
    files["0:/DCIM/truncated.gbr"] = multi;
    files["0:/DCIM/truncated.gbr"].pop_back();
    assert(!image.load("truncated.gbr"));
    assert(!image.load("bad.gbr") && image.data() == nullptr && image.print_rows() == 0);
    assert(!image.load("../photo.GBR"));
    catalog.clear();
    assert(catalog.count() == 0);
}
