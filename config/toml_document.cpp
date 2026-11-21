// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "config/toml_document.h"

#include <cstring>

namespace Config {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

std::string_view header_name(std::string_view line) {
    line = trim(line);
    if (line.size() < 3 || line[0] != '[' || line[1] == '[') return {};
    const usize close = line.find(']');
    if (close == std::string_view::npos) return {};
    const std::string_view suffix = trim(line.substr(close + 1));
    if (!suffix.empty() && suffix.front() != '#') return {};
    return trim(line.substr(1, close - 1));
}

bool is_header(const std::string_view line) {
    const std::string_view text = trim(line);
    return !header_name(text).empty() ||
        (text.size() >= 4 && text[0] == '[' && text[1] == '[' &&
         text.find("]]", 2) != std::string_view::npos);
}

bool child_of(const std::string_view child, const std::string_view parent) {
    return child.size() > parent.size() && child.substr(0, parent.size()) == parent &&
           child[parent.size()] == '.';
}

std::string_view trailing_comment(const std::string_view line) {
    char quote = '\0';
    bool escaped = false;
    for (usize index = 0; index < line.size(); ++index) {
        const char ch = line[index];
        if (quote != '\0') {
            if (escaped) escaped = false;
            else if (ch == '\\' && quote == '"') escaped = true;
            else if (ch == quote) quote = '\0';
        } else if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '#') {
            return line.substr(index);
        }
    }
    return {};
}

bool append(char* output, const usize capacity, usize& size, const std::string_view text) {
    if (size > capacity || text.size() > capacity - size) return false;
    std::memcpy(output + size, text.data(), text.size());
    size += text.size();
    return true;
}

bool append_line(char* output, const usize capacity, usize& size,
                 const std::string_view key, const std::string_view value) {
    if (size != 0 && output[size - 1] != '\n' && !append(output, capacity, size, "\n")) return false;
    return append(output, capacity, size, key) && append(output, capacity, size, " = ") &&
           append(output, capacity, size, value) && append(output, capacity, size, "\n");
}

bool append_section(char* output, const usize capacity, usize& size, const std::string_view section,
                    const std::string_view key, const std::string_view value) {
    if (size != 0 && output[size - 1] != '\n' && !append(output, capacity, size, "\n")) return false;
    if (size != 0 && (size < 2 || output[size - 2] != '\n') &&
        !append(output, capacity, size, "\n")) return false;
    return append(output, capacity, size, "[") && append(output, capacity, size, section) &&
           append(output, capacity, size, "]\n") && append_line(output, capacity, size, key, value);
}

struct Layout {
    bool explicit_section = false;
    bool root_sibling = false;
};

Layout scan_layout(const std::string_view input, const std::string_view section) {
    Layout layout{};
    bool root = true;
    usize offset = 0;
    while (offset < input.size()) {
        const usize newline = input.find('\n', offset);
        const usize end = newline == std::string_view::npos ? input.size() : newline;
        const std::string_view line = trim(input.substr(offset, end - offset));
        if (is_header(line)) {
            if (header_name(line) == section) layout.explicit_section = true;
            root = false;
        } else if (root) {
            const usize equals = line.find('=');
            const std::string_view name = trim(line.substr(0, equals));
            if (child_of(name, section)) layout.root_sibling = true;
        }
        offset = newline == std::string_view::npos ? input.size() : newline + 1;
    }
    return layout;
}

}  // namespace

bool update_toml_value(const std::string_view input, const std::string_view path,
                       const std::string_view value, char* output, const usize capacity,
                       usize& output_size) {
    output_size = 0;
    const usize dot = path.rfind('.');
    if (output == nullptr || capacity == 0 || dot == std::string_view::npos ||
        dot == 0 || dot + 1 >= path.size() || value.empty()) return false;
    const std::string_view section = path.substr(0, dot);
    const std::string_view key = path.substr(dot + 1);
    const Layout layout = scan_layout(input, section);
    if (input.empty() && !append(output, capacity, output_size, "format_version = 1\n")) return false;

    bool root = true;
    bool inserted = false;
    std::string_view active_section;
    usize offset = 0;
    while (offset < input.size()) {
        const usize newline = input.find('\n', offset);
        const bool has_newline = newline != std::string_view::npos;
        const usize end = has_newline ? newline : input.size();
        const std::string_view raw_line = input.substr(offset, end - offset);
        const std::string_view clean_line = trim(raw_line);
        if (is_header(clean_line)) {
            if (!inserted && active_section == section &&
                !append_line(output, capacity, output_size, key, value)) return false;
            if (!inserted && active_section == section) inserted = true;
            if (!inserted && root && layout.root_sibling &&
                !append_line(output, capacity, output_size, path, value)) return false;
            if (!inserted && root && layout.root_sibling) inserted = true;
            const std::string_view next_section = header_name(clean_line);
            if (!inserted && !layout.explicit_section && !layout.root_sibling &&
                child_of(next_section, section)) {
                if (!append_section(output, capacity, output_size, section, key, value)) return false;
                inserted = true;
            }
            if (!append(output, capacity, output_size,
                        input.substr(offset, end - offset + static_cast<usize>(has_newline)))) return false;
            active_section = next_section;
            root = false;
        } else {
            const usize equals = raw_line.find('=');
            const std::string_view name = trim(raw_line.substr(0, equals));
            const bool matches = equals != std::string_view::npos &&
                ((root && name == path) ||
                 (!root && active_section == section && name == key) ||
                 (!root && child_of(path, active_section) &&
                  path.substr(active_section.size() + 1) == name));
            if (matches) {
                if (!append(output, capacity, output_size, name) ||
                    !append(output, capacity, output_size, " = ") ||
                    !append(output, capacity, output_size, value)) return false;
                const std::string_view comment = trailing_comment(raw_line);
                if (!comment.empty() &&
                    (!append(output, capacity, output_size, " ") ||
                     !append(output, capacity, output_size, comment))) return false;
                if (has_newline && !append(output, capacity, output_size, "\n")) return false;
                inserted = true;
            } else if (!append(output, capacity, output_size,
                               input.substr(offset, end - offset + static_cast<usize>(has_newline)))) {
                return false;
            }
        }
        offset = has_newline ? newline + 1 : input.size();
    }
    if (!inserted) {
        if (active_section == section) {
            if (!append_line(output, capacity, output_size, key, value)) return false;
        } else if (root && layout.root_sibling) {
            if (!append_line(output, capacity, output_size, path, value)) return false;
        } else if (!append_section(output, capacity, output_size, section, key, value)) {
            return false;
        }
    }
    return true;
}

}  // namespace Config
