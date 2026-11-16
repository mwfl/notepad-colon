#include <notepad_colon/session.h>

#include <charconv>
#include <array>
#include <algorithm>
#include <stdexcept>
#include <set>
#include <fstream>
#include <sstream>
#include <windows.h>

namespace notepad_colon {
namespace {
std::string Escape(std::wstring_view value) {
    std::ostringstream output;
    for (wchar_t character : value) {
        const auto code = static_cast<std::uint32_t>(character);
        if (character == L'\\' || character == L'\t' || character == L'\n' || character == L'\r') {
            output << '\\' << std::hex << code << ';' << std::dec;
        } else if (code >= 0x20 && code <= 0x7e) {
            output << static_cast<char>(code);
        } else {
            output << '\\' << std::hex << code << ';' << std::dec;
        }
    }
    return output.str();
}

bool Unescape(std::string_view value, std::wstring& output) {
    output.clear();
    for (std::size_t index = 0; index < value.size();) {
        if (value[index] != '\\') {
            output.push_back(static_cast<unsigned char>(value[index++]));
            continue;
        }
        const auto end = value.find(';', index + 1);
        if (end == std::string_view::npos) return false;
        std::uint32_t code = 0;
        const auto parsed = std::from_chars(value.data() + index + 1, value.data() + end, code, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + end || code > 0xffff) return false;
        output.push_back(static_cast<wchar_t>(code));
        index = end + 1;
    }
    return true;
}

template<class T> bool Number(std::string_view value, T& output) {
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), output);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}

std::uint64_t Checksum(std::string_view bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : bytes) { hash ^= static_cast<unsigned char>(byte); hash *= 1099511628211ull; }
    return hash;
}

bool ReadSession(const std::filesystem::path& path, Session& session, bool verify_copies) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    const std::string bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if ((!input.good() && !input.eof()) || !DeserializeSession(bytes, session)) return false;
    const auto parent = std::filesystem::absolute(path).parent_path();
    for (auto& entry : session.documents)
        if (!entry.recovery_file.empty() && entry.recovery_file.is_relative())
            entry.recovery_file = (parent / entry.recovery_file).lexically_normal();
    if (!verify_copies) return true;
    for (const auto& entry : session.documents) {
        if (entry.recovery_file.empty()) continue;
        std::error_code error;
        if (std::filesystem::file_size(entry.recovery_file, error) != entry.recovery_size || error) return false;
        std::ifstream copy(entry.recovery_file, std::ios::binary);
        if (!copy) return false;
        std::array<char, 64 * 1024> chunk{};
        std::uint64_t checksum = 14695981039346656037ull;
        while (copy) {
            copy.read(chunk.data(), chunk.size());
            for (std::streamsize index = 0; index < copy.gcount(); ++index) {
                checksum ^= static_cast<unsigned char>(chunk[static_cast<std::size_t>(index)]);
                checksum *= 1099511628211ull;
            }
        }
        if (!copy.eof() || checksum != entry.recovery_checksum) return false;
    }
    return true;
}

bool WriteDurable(const std::filesystem::path& path, std::string_view bytes) {
    const auto file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool success = true;
    while (!bytes.empty() && success) {
        const auto count = static_cast<DWORD>((std::min<std::size_t>)(bytes.size(), 1024 * 1024));
        DWORD written = 0;
        success = ::WriteFile(file, bytes.data(), count, &written, nullptr) && written == count;
        bytes.remove_prefix(written);
    }
    if (success) success = ::FlushFileBuffers(file) != FALSE;
    ::CloseHandle(file);
    return success;
}
}  // namespace

std::string SerializeSession(const Session& session) {
    std::ostringstream output;
    output << "NPCSESSION\t6\t" << session.active_index;
    if (!session.workspace_paths.empty()) {
        for (const auto& path : session.workspace_paths) output << '\t' << Escape(path.wstring());
    } else if (!session.workspace_path.empty()) {
        output << '\t' << Escape(session.workspace_path.wstring());
    }
    output << '\n';
    for (const auto& entry : session.documents) {
        output << static_cast<int>(entry.encoding) << '\t'
               << static_cast<int>(entry.line_ending) << '\t'
               << entry.view.anchor << '\t' << entry.view.caret << '\t'
               << entry.view.first_visible_line << '\t' << (entry.dirty ? 1 : 0) << '\t'
               << Escape(entry.path.wstring()) << '\t' << Escape(entry.recovery_text) << '\t'
               << entry.ansi_code_page << '\t' << entry.window_offset << '\t'
               << Escape(entry.recovery_file.wstring()) << '\t' << entry.recovery_size << '\t'
               << entry.recovery_checksum << '\t' << (entry.disk_state ? 1 : 0) << '\t'
               << (entry.disk_state && entry.disk_state->exists ? 1 : 0) << '\t'
               << (entry.disk_state ? entry.disk_state->size : 0) << '\t'
               << (entry.disk_state ? entry.disk_state->last_write.time_since_epoch().count() : 0) << '\t'
               << (entry.legacy_encoding_override ? 1 : 0) << '\t' << (entry.unicode_bom ? 1 : 0) << '\t'
               << (entry.disk_state ? entry.disk_state->file_index : 0) << '\t'
               << (entry.disk_state ? entry.disk_state->volume_serial : 0) << '\t'
               << (entry.line_ending_override ? static_cast<int>(*entry.line_ending_override) : -1) << '\n';
    }
    const auto contents = output.str();
    output << "NPCEND\t" << session.documents.size() << '\t' << Checksum(contents) << '\n';
    return output.str();
}

bool DeserializeSession(std::string_view input, Session& session) noexcept {
    try {
        std::optional<std::size_t> document_count;
        if (input.starts_with("NPCSESSION\t5\t") || input.starts_with("NPCSESSION\t6\t")) {
            const auto footer_start = input.rfind("\nNPCEND\t");
            if (footer_start == std::string_view::npos || input.back() != '\n') return false;
            const auto footer = input.substr(footer_start + 8, input.size() - footer_start - 9);
            const auto separator = footer.find('\t');
            std::size_t count = 0;
            std::uint64_t checksum = 0;
            if (separator == std::string_view::npos || !Number(footer.substr(0, separator), count) ||
                !Number(footer.substr(separator + 1), checksum)) return false;
            input = input.substr(0, footer_start + 1);
            if (Checksum(input) != checksum) return false;
            document_count = count;
        }
        std::istringstream stream{std::string(input)};
        std::string line;
        if (!std::getline(stream, line)) return false;
        std::vector<std::string> header_fields;
        std::istringstream header_stream{line};
        std::string header_field;
        while (std::getline(header_stream, header_field, '\t')) header_fields.push_back(header_field);
        if (!line.empty() && line.back() == '\t') header_fields.emplace_back();
        if (header_fields.size() < 3 ||
            header_fields[0] != "NPCSESSION" ||
            (header_fields[1] != "1" && header_fields[1] != "2" &&
             header_fields[1] != "3" && header_fields[1] != "4" && header_fields[1] != "5" && header_fields[1] != "6")) return false;
        const bool version6 = header_fields[1] == "6";
        const bool version5 = header_fields[1] == "5" || version6;
        const bool version4 = header_fields[1] == "4" || version5;
        if ((header_fields[1] == "1" && header_fields.size() != 3) ||
            (header_fields[1] == "2" && header_fields.size() != 4)) return false;
        std::size_t active = 0;
        const auto active_text = std::string_view{header_fields[2]};
        const auto active_result = std::from_chars(active_text.data(), active_text.data() + active_text.size(), active);
        if (active_result.ec != std::errc{} || active_result.ptr != active_text.data() + active_text.size()) return false;

        Session parsed;
        parsed.active_index = active;
        if (header_fields[1] == "2") {
            std::wstring workspace;
            if (!Unescape(header_fields[3], workspace)) return false;
            parsed.workspace_path = workspace;
            if (!workspace.empty()) parsed.workspace_paths.emplace_back(workspace);
        } else if (header_fields[1] == "3" || version4) {
            for (std::size_t index = 3; index < header_fields.size(); ++index) {
                std::wstring workspace;
                if (!Unescape(header_fields[index], workspace) || workspace.empty()) return false;
                parsed.workspace_paths.emplace_back(workspace);
            }
            if (!parsed.workspace_paths.empty()) parsed.workspace_path = parsed.workspace_paths.front();
        }
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            std::vector<std::string> fields;
            std::istringstream row{line};
            std::string field;
            while (std::getline(row, field, '\t')) fields.push_back(field);
            if (!line.empty() && line.back() == '\t') fields.emplace_back();
            if (fields.size() != (version6 ? 22u : version5 ? 17u : version4 ? 9u : 8u)) return false;
            SessionEntry entry;
            int encoding = 0, ending = 0;
            if (!Number(fields[0], encoding) || !Number(fields[1], ending)) return false;
            if (encoding < 0 || encoding > static_cast<int>(Encoding::ansi) ||
                ending < 0 || ending > static_cast<int>(LineEnding::cr)) return false;
            entry.encoding = static_cast<Encoding>(encoding);
            entry.line_ending = static_cast<LineEnding>(ending);
            if (!Number(fields[2], entry.view.anchor) || !Number(fields[3], entry.view.caret) ||
                !Number(fields[4], entry.view.first_visible_line)) return false;
            entry.dirty = fields[5] == "1";
            if (fields[5] != "0" && fields[5] != "1") return false;
            if (entry.view.anchor < 0 || entry.view.caret < 0 || entry.view.first_visible_line < 0)
                return false;
            if (version4) {
                const auto& page = fields[8];
                const auto parsed_page = std::from_chars(page.data(), page.data() + page.size(), entry.ansi_code_page);
                if (parsed_page.ec != std::errc{} || parsed_page.ptr != page.data() + page.size() ||
                    (entry.encoding == Encoding::ansi && entry.ansi_code_page != 0 &&
                     !::IsValidCodePage(entry.ansi_code_page))) return false;
            }
            std::wstring path;
            if (!Unescape(fields[6], path) || !Unescape(fields[7], entry.recovery_text)) return false;
            entry.path = path;
            entry.legacy_encoding_override = entry.encoding == Encoding::ansi && entry.ansi_code_page != 0;
            if (version5) {
                std::wstring copy;
                if (!Number(fields[9], entry.window_offset) || !Unescape(fields[10], copy) ||
                    !Number(fields[11], entry.recovery_size) || !Number(fields[12], entry.recovery_checksum)) return false;
                entry.recovery_file = copy;
                if (!copy.empty() && (!entry.dirty || !entry.recovery_text.empty())) return false;
                int has_state = 0, exists = 0;
                FileState state;
                std::filesystem::file_time_type::duration::rep ticks = 0;
                if (!Number(fields[13], has_state) || (has_state != 0 && has_state != 1) ||
                    !Number(fields[14], exists) || (exists != 0 && exists != 1) ||
                    !Number(fields[15], state.size) || !Number(fields[16], ticks)) return false;
                state.exists = exists != 0;
                state.last_write = std::filesystem::file_time_type{std::filesystem::file_time_type::duration{ticks}};
                if (has_state) entry.disk_state = state;
                if (version6) {
                    int pinned = 0, bom = 0;
                    if (!Number(fields[17], pinned) || (pinned != 0 && pinned != 1) ||
                        !Number(fields[18], bom) || (bom != 0 && bom != 1) ||
                        !Number(fields[19], state.file_index) || !Number(fields[20], state.volume_serial)) return false;
                    entry.legacy_encoding_override = pinned != 0;
                    entry.unicode_bom = bom != 0;
                    if (has_state) entry.disk_state = state;
                    int override_ending = -1;
                    if (!Number(fields[21], override_ending) || override_ending < -1 ||
                        override_ending > static_cast<int>(LineEnding::cr)) return false;
                    if (override_ending >= 0) entry.line_ending_override = static_cast<LineEnding>(override_ending);
                }
            }
            parsed.documents.push_back(std::move(entry));
        }
        if (!parsed.documents.empty() && parsed.active_index >= parsed.documents.size()) return false;
        if (document_count && parsed.documents.size() != *document_count) return false;
        session = std::move(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool SaveSessionAtomic(const std::filesystem::path& path, const Session& session) noexcept {
    std::filesystem::path temporary;
    try {
        std::error_code target_error;
        if (std::filesystem::is_directory(path, target_error)) return false;
        if (target_error && target_error != std::errc::no_such_file_or_directory) return false;
        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error) return false;
        }
        Session durable = session;
        std::set<std::filesystem::path> keep;
        for (auto& entry : durable.documents) {
            if (!entry.large_snapshot && entry.recovery_file.empty()) continue;
            auto snapshot = entry.large_snapshot;
            if (!snapshot) {
                auto buffer = std::make_shared<LargeFileBuffer>();
                if (!buffer->Open(entry.recovery_file, false)) throw std::runtime_error("recovery source unavailable");
                snapshot = std::move(buffer);
            }
            const auto directory = std::filesystem::absolute(path.wstring() + L".data");
            const auto copy = snapshot->Checkpoint(directory);
            if (!copy) throw std::runtime_error("recovery copy failed");
            if (!entry.large_snapshot && (copy->size != entry.recovery_size || copy->checksum != entry.recovery_checksum))
                throw std::runtime_error("recovery source changed");
            // The frozen source may itself be an earlier recovery copy still
            // backing an open editor. Never prune it while it is in use.
            keep.insert(snapshot->SourcePath());
            entry.recovery_file = copy->path;
            entry.recovery_checksum = copy->checksum;
            entry.recovery_size = snapshot->Size();
            entry.recovery_text.clear();
            entry.large_snapshot.reset();
        }
        temporary = path.wstring() + L".tmp";
        auto manifest = durable;
        const auto manifest_parent = std::filesystem::absolute(path).parent_path();
        for (auto& entry : manifest.documents)
            if (!entry.recovery_file.empty()) entry.recovery_file = entry.recovery_file.lexically_relative(manifest_parent);
        if (!WriteDurable(temporary, SerializeSession(manifest))) throw std::runtime_error("session write failed");
        // Preserve a parseable generation before replacing the manifest. Each
        // large copy is immutable, so both generations remain independently usable.
        Session old;
        if (ReadSession(path, old, true)) {
            const auto previous = path.wstring() + L".previous";
            const auto staging = previous + L".tmp";
            if (!::CopyFileW(path.c_str(), staging.c_str(), FALSE) ||
                !::MoveFileExW(staging.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("session backup failed");
        }
        if (::MoveFileExW(temporary.c_str(), path.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
            throw std::runtime_error("session commit failed");
        }
        Session previous;
        static_cast<void>(ReadSession(path.wstring() + L".previous", previous, false));
        for (const auto* generation : {&durable, &previous})
            for (const auto& entry : generation->documents)
                if (!entry.recovery_file.empty()) keep.insert(entry.recovery_file);
        // Only this manifest's dedicated directory and generated extension are
        // eligible. Leave original files, named sessions and active sources alone.
        const auto directory = std::filesystem::absolute(path.wstring() + L".data");
        std::error_code cleanup_error;
        if (std::filesystem::is_directory(directory, cleanup_error)) {
            const std::filesystem::directory_iterator end;
            for (std::filesystem::directory_iterator item{directory, cleanup_error};
                 !cleanup_error && item != end; item.increment(cleanup_error)) {
                if (item->path().extension() == L".npcrecovery" && !keep.contains(item->path()))
                    std::filesystem::remove(item->path(), cleanup_error);
            }
        }
        return true;
    } catch (...) {
        std::error_code ignored;
        if (!temporary.empty()) std::filesystem::remove(temporary, ignored);
        return false;
    }
}

bool LoadSession(const std::filesystem::path& path, Session& session, bool* used_previous) noexcept {
    try {
        if (used_previous) *used_previous = false;
        Session candidate;
        if (ReadSession(path, candidate, true)) { session = std::move(candidate); return true; }
        if (!ReadSession(path.wstring() + L".previous", candidate, true)) return false;
        if (used_previous) *used_previous = true;
        session = std::move(candidate);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace notepad_colon
