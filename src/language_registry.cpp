#include <notepad_colon/language_registry.h>

#include <nlohmann/json.hpp>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace notepad_colon {
namespace {
constexpr std::uintmax_t kMaximumDefinitionBytes = 1024 * 1024;

std::optional<std::wstring> FromUtf8(std::string_view value) {
    if (value.empty()) return std::wstring{};
    const auto length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                               static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return std::nullopt;
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    return ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), length) == length
        ? std::optional<std::wstring>{std::move(result)} : std::nullopt;
}

std::wstring Lower(std::wstring value) {
    std::ranges::transform(value, value.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return value;
}

bool ValidId(std::string_view id) noexcept {
    return !id.empty() && id.size() <= 64 &&
        std::ranges::all_of(id, [](unsigned char value) {
            return std::islower(value) || std::isdigit(value) || value == '-' || value == '_';
        });
}

bool Within(const std::filesystem::path& child, const std::filesystem::path& parent) noexcept {
    std::error_code error;
    const auto resolved_child = std::filesystem::weakly_canonical(child, error);
    if (error) return false;
    const auto resolved_parent = std::filesystem::weakly_canonical(parent, error);
    if (error) return false;
    auto child_iterator = resolved_child.begin();
    for (auto parent_iterator = resolved_parent.begin(); parent_iterator != resolved_parent.end();
         ++parent_iterator, ++child_iterator) {
        if (child_iterator == resolved_child.end() ||
            Lower(child_iterator->wstring()) != Lower(parent_iterator->wstring())) return false;
    }
    return true;
}

std::optional<std::string> ReadUtf8(const std::filesystem::path& path,
                                    std::uintmax_t maximum = kMaximumDefinitionBytes) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum) return std::nullopt;
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    std::string bytes{std::istreambuf_iterator<char>{input}, {}};
    return input.good() || input.eof() ? std::optional<std::string>{std::move(bytes)} : std::nullopt;
}

std::optional<std::filesystem::path> SafeRelativePath(
    const std::filesystem::path& root, const nlohmann::json& value) {
    if (!value.is_string()) return std::nullopt;
    const auto decoded = FromUtf8(value.get<std::string>());
    if (!decoded) return std::nullopt;
    const auto path = root / *decoded;
    return Within(path, root) ? std::optional<std::filesystem::path>{path} : std::nullopt;
}

std::string_view BuiltinId(Language language) noexcept {
    constexpr std::array ids{"plain-text", "cpp", "csharp", "java", "javascript",
        "typescript", "python", "json", "xml", "html", "css", "markdown", "cmake",
        "powershell", "batch", "ini", "yaml", "sql", "rust"};
    const auto index = static_cast<std::size_t>(language);
    return index < ids.size() ? ids[index] : ids.front();
}

std::vector<std::wstring> BuiltinExtensions(Language language) {
    switch (language) {
    case Language::cpp: return {L".c", L".cc", L".cpp", L".cxx", L".h", L".hh", L".hpp", L".hxx"};
    case Language::csharp: return {L".cs"};
    case Language::java: return {L".java"};
    case Language::javascript: return {L".js", L".jsx", L".mjs"};
    case Language::typescript: return {L".ts", L".tsx"};
    case Language::python: return {L".py", L".pyw"};
    case Language::json: return {L".json", L".jsonc"};
    case Language::xml: return {L".xml", L".xaml", L".svg"};
    case Language::html: return {L".html", L".htm"};
    case Language::css: return {L".css", L".scss", L".less"};
    case Language::markdown: return {L".md", L".markdown"};
    case Language::cmake: return {L".cmake"};
    case Language::powershell: return {L".ps1", L".psm1", L".psd1"};
    case Language::batch: return {L".bat", L".cmd"};
    case Language::ini: return {L".ini", L".cfg", L".conf"};
    case Language::yaml: return {L".yaml", L".yml"};
    case Language::sql: return {L".sql"};
    case Language::rust: return {L".rs"};
    case Language::plain_text: return {L".txt"};
    }
    return {};
}
}

LanguageRegistry::LanguageRegistry() { ResetBuiltins(); }

void LanguageRegistry::ResetBuiltins() {
    languages_.clear();
    errors_.clear();
    for (const auto language : AllLanguages()) {
        const auto& profile = GetLanguageProfile(language);
        RegisteredLanguage registered;
        registered.id = std::string(BuiltinId(language));
        registered.name = std::wstring(profile.name);
        registered.fallback_lexer = std::string(profile.lexer);
        registered.builtin = language;
        registered.extensions = BuiltinExtensions(language);
        if (language == Language::cmake) registered.filenames.push_back(L"cmakelists.txt");
        languages_.push_back(std::move(registered));
    }
}

std::size_t LanguageRegistry::LoadDirectory(const std::filesystem::path& directory) {
    errors_.clear();
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) return 0;
    std::size_t loaded = 0;
    for (std::filesystem::directory_iterator iterator(directory,
             std::filesystem::directory_options::skip_permission_denied, error), end;
         iterator != end && !error; iterator.increment(error)) {
        const auto path = iterator->path();
        if (!iterator->is_regular_file(error) || Lower(path.extension().wstring()) != L".json") continue;
        try {
            const auto bytes = ReadUtf8(path);
            if (!bytes) throw std::runtime_error("definition is unreadable or exceeds 1 MiB");
            const auto json = nlohmann::json::parse(*bytes);
            RegisteredLanguage language;
            language.id = json.at("id").get<std::string>();
            const auto decoded_name = FromUtf8(json.at("name").get<std::string>());
            if (!decoded_name) throw std::runtime_error("name is not valid UTF-8");
            language.name = *decoded_name;
            if (!ValidId(language.id) || language.name.empty() || language.name.size() > 128)
                throw std::runtime_error("invalid id or name");
            if (Find(language.id)) throw std::runtime_error("duplicate language id");
            for (const auto& value : json.value("extensions", nlohmann::json::array())) {
                const auto decoded = FromUtf8(value.get<std::string>());
                if (!decoded) throw std::runtime_error("extension is not valid UTF-8");
                auto extension = Lower(*decoded);
                if (extension.empty() || extension.front() != L'.' || extension.size() > 32)
                    throw std::runtime_error("invalid extension");
                language.extensions.push_back(std::move(extension));
            }
            for (const auto& value : json.value("filenames", nlohmann::json::array())) {
                const auto decoded = FromUtf8(value.get<std::string>());
                if (!decoded) throw std::runtime_error("filename is not valid UTF-8");
                language.filenames.push_back(Lower(*decoded));
            }
            language.fallback_lexer = json.value("fallbackLexer", std::string{});
            if (const auto found = json.find("treeSitter"); found != json.end()) {
                const auto highlights = SafeRelativePath(directory, found->at("highlights"));
                if (!highlights) throw std::runtime_error("Tree-sitter query escapes the language directory");
                TreeSitterDefinition syntax;
                syntax.grammar = found->value("grammar", std::string{});
                if (syntax.grammar != "cpp" && syntax.grammar != "json")
                    throw std::runtime_error("grammar must be a sandboxed built-in: cpp or json");
                const auto query = ReadUtf8(*highlights);
                if (!query) throw std::runtime_error("highlight query is unreadable");
                syntax.highlights_query = *query;
                if (const auto symbols = found->find("symbols"); symbols != found->end()) {
                    const auto symbols_path = SafeRelativePath(directory, *symbols);
                    const auto symbols_query = symbols_path ? ReadUtf8(*symbols_path) : std::nullopt;
                    if (!symbols_query) throw std::runtime_error("symbol query is unreadable");
                    syntax.symbols_query = *symbols_query;
                }
                language.tree_sitter = std::move(syntax);
            }
            if (language.extensions.empty() && language.filenames.empty())
                throw std::runtime_error("at least one extension or filename is required");
            languages_.push_back(std::move(language));
            ++loaded;
        } catch (const std::exception& exception) {
            errors_.push_back({path, exception.what()});
        }
    }
    if (error) errors_.push_back({directory, error.message()});
    return loaded;
}

const RegisteredLanguage* LanguageRegistry::Find(std::string_view id) const noexcept {
    const auto found = std::ranges::find(languages_, id, &RegisteredLanguage::id);
    return found == languages_.end() ? nullptr : &*found;
}

const RegisteredLanguage* LanguageRegistry::Detect(const std::filesystem::path& path) const noexcept {
    const auto filename = Lower(path.filename().wstring());
    const auto extension = Lower(path.extension().wstring());
    const auto found = std::ranges::find_if(languages_, [&](const RegisteredLanguage& language) {
        return std::ranges::find(language.filenames, filename) != language.filenames.end() ||
               std::ranges::find(language.extensions, extension) != language.extensions.end();
    });
    return found == languages_.end() ? Find("plain-text") : &*found;
}

}  // namespace notepad_colon
