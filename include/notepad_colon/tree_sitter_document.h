#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace notepad_colon {

enum class SyntaxKind {
    none,
    comment,
    string,
    number,
    keyword,
    type,
    function,
    property,
    variable,
    constant,
    preprocessor
};

struct SyntaxSpan {
    std::uint32_t start_byte = 0;
    std::uint32_t end_byte = 0;
    SyntaxKind kind = SyntaxKind::none;
    friend bool operator==(const SyntaxSpan&, const SyntaxSpan&) = default;
};

struct DocumentSymbol {
    std::string name;
    std::string kind;
    std::uint32_t start_byte = 0;
    std::uint32_t end_byte = 0;
};

struct SyntaxEdit {
    std::uint32_t start_byte = 0;
    std::uint32_t old_end_byte = 0;
    std::uint32_t new_end_byte = 0;
    std::uint32_t start_row = 0;
    std::uint32_t start_column = 0;
    std::uint32_t old_end_row = 0;
    std::uint32_t old_end_column = 0;
    std::uint32_t new_end_row = 0;
    std::uint32_t new_end_column = 0;
};

class TreeSitterDocument final {
public:
    TreeSitterDocument();
    ~TreeSitterDocument();
    TreeSitterDocument(TreeSitterDocument&&) noexcept;
    TreeSitterDocument& operator=(TreeSitterDocument&&) noexcept;
    TreeSitterDocument(const TreeSitterDocument&) = delete;
    TreeSitterDocument& operator=(const TreeSitterDocument&) = delete;

    bool ConfigureCpp() noexcept;
    bool ConfigureCpp(std::string_view highlights_query,
                      std::string_view symbols_query = {}) noexcept;
    bool ConfigureJson() noexcept;
    bool ConfigureJson(std::string_view highlights_query,
                       std::string_view symbols_query = {}) noexcept;
    bool ConfigurePython() noexcept;
    bool ConfigureJavaScript() noexcept;
    bool ConfigureTypeScript(bool tsx = false) noexcept;
    bool Parse(std::string_view utf8) noexcept;
    bool Reparse(std::string_view utf8, const SyntaxEdit& edit) noexcept;
    bool IsReady() const noexcept;
    bool HasErrors() const noexcept;
    std::vector<SyntaxSpan> Highlights(std::uint32_t start_byte,
                                       std::uint32_t end_byte) const;
    std::vector<DocumentSymbol> Symbols(std::string_view utf8) const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace notepad_colon
