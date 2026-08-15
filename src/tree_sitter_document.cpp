#include <notepad_colon/tree_sitter_document.h>

#include <tree_sitter/api.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

extern "C" const TSLanguage* tree_sitter_cpp();
extern "C" const TSLanguage* tree_sitter_json();
extern "C" const TSLanguage* tree_sitter_python();
extern "C" const TSLanguage* tree_sitter_javascript();
extern "C" const TSLanguage* tree_sitter_typescript();
extern "C" const TSLanguage* tree_sitter_tsx();

namespace notepad_colon {
namespace {
constexpr std::string_view kHighlightQuery = R"query(
(comment) @comment
(string_literal) @string
(raw_string_literal) @string
(char_literal) @string
(number_literal) @number
(primitive_type) @type
(type_identifier) @type
(namespace_identifier) @type
(function_declarator declarator: (identifier) @function)
(function_declarator declarator: (field_identifier) @function)
(call_expression function: (identifier) @function)
(field_identifier) @property
(preproc_include) @preprocessor
(true) @constant
(false) @constant
(null "nullptr" @constant)
["break" "case" "catch" "class" "co_await" "co_return" "co_yield" "const"
 "constexpr" "constinit" "continue" "default" "do" "else" "enum" "extern"
 "for" "goto" "if" "inline" "return" "static" "struct" "switch" "typedef"
 "union" "volatile" "while"
 "consteval" "delete" "explicit" "final" "friend" "mutable" "namespace"
 "noexcept" "new" "override" "private" "protected" "public" "template"
 "throw" "try" "typename" "using" "concept" "requires" "virtual"] @keyword
)query";

constexpr std::string_view kSymbolQuery = R"query(
(function_declarator declarator: (identifier) @symbol.function)
(function_declarator declarator: (field_identifier) @symbol.function)
(class_specifier name: (type_identifier) @symbol.class body: (_))
(struct_specifier name: (type_identifier) @symbol.struct body: (_))
(enum_specifier name: (type_identifier) @symbol.enum)
)query";

constexpr std::string_view kJsonHighlightQuery = R"query(
(string) @string
(number) @number
[(true) (false) (null)] @constant
(pair key: (string) @property)
)query";

constexpr std::string_view kJsonSymbolQuery = R"query(
(pair key: (string) @symbol.property value: (_))
)query";

constexpr std::string_view kPythonHighlightQuery = R"query(
(comment) @comment
(string) @string
[(integer) (float)] @number
(function_definition name: (identifier) @function)
(call function: (identifier) @function)
(class_definition name: (identifier) @type)
[(true) (false) (none)] @constant
["and" "as" "assert" "async" "await" "break" "case" "class" "continue"
 "def" "del" "elif" "else" "except" "finally" "for" "from" "global" "if"
 "import" "in" "is" "lambda" "match" "nonlocal" "not" "or" "pass" "raise"
 "return" "try" "while" "with" "yield"] @keyword
)query";
constexpr std::string_view kPythonSymbolQuery = R"query(
(function_definition name: (identifier) @symbol.function)
(class_definition name: (identifier) @symbol.class)
)query";
constexpr std::string_view kJavaScriptHighlightQuery = R"query(
(comment) @comment
[(string) (template_string) (regex)] @string
(number) @number
(function_declaration name: (identifier) @function)
(generator_function_declaration name: (identifier) @function)
(method_definition name: [(property_identifier) (private_property_identifier)] @function)
(call_expression function: (identifier) @function)
(member_expression property: (property_identifier) @property)
(class_declaration name: (identifier) @type)
[(true) (false) (null) (undefined)] @constant
["as" "async" "await" "break" "case" "catch" "class" "const" "continue"
 "debugger" "default" "delete" "do" "else" "export" "extends" "finally"
 "for" "from" "function" "get" "if" "import" "in" "instanceof" "let" "new"
 "of" "return" "set" "static" "switch" "throw" "try" "typeof" "var" "void"
 "while" "with" "yield"] @keyword
)query";
constexpr std::string_view kJavaScriptSymbolQuery = R"query(
(function_declaration name: (identifier) @symbol.function)
(generator_function_declaration name: (identifier) @symbol.function)
(method_definition name: [(property_identifier) (private_property_identifier)] @symbol.method)
(class_declaration name: (identifier) @symbol.class)
)query";
constexpr std::string_view kTypeScriptHighlightQuery = R"query(
(comment) @comment
[(string) (template_string) (regex)] @string
(number) @number
(function_declaration name: (identifier) @function)
(generator_function_declaration name: (identifier) @function)
(method_definition name: [(property_identifier) (private_property_identifier)] @function)
(call_expression function: (identifier) @function)
(member_expression property: (property_identifier) @property)
[(class_declaration name: (type_identifier) @type)
 (interface_declaration name: (type_identifier) @type)
 (type_alias_declaration name: (type_identifier) @type)
 (enum_declaration name: (identifier) @type)
 (predefined_type) @type]
[(true) (false) (null) (undefined)] @constant
["abstract" "as" "asserts" "async" "await" "break" "case" "catch" "class"
 "const" "continue" "debugger" "declare" "default" "delete" "do" "else" "enum"
 "export" "extends" "finally" "for" "from" "function" "get" "if" "implements"
 "import" "in" "infer" "instanceof" "interface" "is" "keyof" "let" "namespace"
 "new" "of" "override" "private" "protected" "public" "readonly" "return" "set"
 "static" "switch" "throw" "try" "type" "typeof" "var" "void" "while" "with"
 "yield"] @keyword
)query";
constexpr std::string_view kTypeScriptSymbolQuery = R"query(
(function_declaration name: (identifier) @symbol.function)
(generator_function_declaration name: (identifier) @symbol.function)
(method_definition name: [(property_identifier) (private_property_identifier)] @symbol.method)
(class_declaration name: (type_identifier) @symbol.class)
(interface_declaration name: (type_identifier) @symbol.interface)
(type_alias_declaration name: (type_identifier) @symbol.type)
(enum_declaration name: (identifier) @symbol.enum)
)query";

SyntaxKind CaptureKind(std::string_view capture) noexcept {
    if (capture == "comment") return SyntaxKind::comment;
    if (capture == "string") return SyntaxKind::string;
    if (capture == "number") return SyntaxKind::number;
    if (capture == "keyword") return SyntaxKind::keyword;
    if (capture == "type") return SyntaxKind::type;
    if (capture == "function") return SyntaxKind::function;
    if (capture == "property") return SyntaxKind::property;
    if (capture == "variable") return SyntaxKind::variable;
    if (capture == "constant") return SyntaxKind::constant;
    if (capture == "preprocessor") return SyntaxKind::preprocessor;
    return SyntaxKind::none;
}

TSPoint Point(std::uint32_t row, std::uint32_t column) noexcept { return {row, column}; }

bool ConfigureParser(TSParser*& parser, TSQuery*& highlight_query, TSQuery*& symbol_query,
                     const TSLanguage* language, std::string_view highlights,
                     std::string_view symbols) noexcept {
    parser = ts_parser_new();
    if (!parser || !language || !ts_parser_set_language(parser, language)) return false;
    std::uint32_t offset = 0;
    TSQueryError error = TSQueryErrorNone;
    highlight_query = ts_query_new(language, highlights.data(),
        static_cast<std::uint32_t>(highlights.size()), &offset, &error);
    if (!highlight_query) return false;
    if (!symbols.empty()) {
        symbol_query = ts_query_new(language, symbols.data(),
            static_cast<std::uint32_t>(symbols.size()), &offset, &error);
        if (!symbol_query) return false;
    }
    return true;
}
}

struct TreeSitterDocument::State {
    TSParser* parser = nullptr;
    TSTree* tree = nullptr;
    TSQuery* highlight_query = nullptr;
    TSQuery* symbol_query = nullptr;

    ~State() {
        if (symbol_query) ts_query_delete(symbol_query);
        if (highlight_query) ts_query_delete(highlight_query);
        if (tree) ts_tree_delete(tree);
        if (parser) ts_parser_delete(parser);
    }
};

TreeSitterDocument::TreeSitterDocument() : state_(std::make_unique<State>()) {}
TreeSitterDocument::~TreeSitterDocument() = default;
TreeSitterDocument::TreeSitterDocument(TreeSitterDocument&&) noexcept = default;
TreeSitterDocument& TreeSitterDocument::operator=(TreeSitterDocument&&) noexcept = default;

bool TreeSitterDocument::ConfigureCpp() noexcept {
    return ConfigureCpp(kHighlightQuery, kSymbolQuery);
}

bool TreeSitterDocument::ConfigureCpp(std::string_view highlights_query,
                                      std::string_view symbols_query) noexcept {
    state_ = std::make_unique<State>();
    state_->parser = ts_parser_new();
    const auto* language = tree_sitter_cpp();
    if (!state_->parser || !language || !ts_parser_set_language(state_->parser, language)) return false;
    std::uint32_t offset = 0;
    TSQueryError error = TSQueryErrorNone;
    state_->highlight_query = ts_query_new(language, highlights_query.data(),
        static_cast<std::uint32_t>(highlights_query.size()), &offset, &error);
    if (!state_->highlight_query) return false;
    if (!symbols_query.empty()) {
        state_->symbol_query = ts_query_new(language, symbols_query.data(),
            static_cast<std::uint32_t>(symbols_query.size()), &offset, &error);
        if (!state_->symbol_query) return false;
    }
    return true;
}

bool TreeSitterDocument::ConfigureJson() noexcept {
    return ConfigureJson(kJsonHighlightQuery, kJsonSymbolQuery);
}

bool TreeSitterDocument::ConfigureJson(std::string_view highlights_query,
                                       std::string_view symbols_query) noexcept {
    state_ = std::make_unique<State>();
    state_->parser = ts_parser_new();
    const auto* language = tree_sitter_json();
    if (!state_->parser || !language || !ts_parser_set_language(state_->parser, language)) return false;
    std::uint32_t offset = 0;
    TSQueryError error = TSQueryErrorNone;
    state_->highlight_query = ts_query_new(language, highlights_query.data(),
        static_cast<std::uint32_t>(highlights_query.size()), &offset, &error);
    if (!state_->highlight_query) return false;
    if (!symbols_query.empty()) {
        state_->symbol_query = ts_query_new(language, symbols_query.data(),
            static_cast<std::uint32_t>(symbols_query.size()), &offset, &error);
        if (!state_->symbol_query) return false;
    }
    return true;
}

bool TreeSitterDocument::ConfigurePython() noexcept {
    state_ = std::make_unique<State>();
    return ConfigureParser(state_->parser, state_->highlight_query, state_->symbol_query,
                           tree_sitter_python(), kPythonHighlightQuery, kPythonSymbolQuery);
}
bool TreeSitterDocument::ConfigureJavaScript() noexcept {
    state_ = std::make_unique<State>();
    return ConfigureParser(state_->parser, state_->highlight_query, state_->symbol_query,
                           tree_sitter_javascript(), kJavaScriptHighlightQuery,
                           kJavaScriptSymbolQuery);
}
bool TreeSitterDocument::ConfigureTypeScript(bool tsx) noexcept {
    state_ = std::make_unique<State>();
    return ConfigureParser(state_->parser, state_->highlight_query, state_->symbol_query,
                           tsx ? tree_sitter_tsx() : tree_sitter_typescript(),
                           kTypeScriptHighlightQuery, kTypeScriptSymbolQuery);
}

bool TreeSitterDocument::Parse(std::string_view utf8) noexcept {
    if (!state_->parser || utf8.size() > UINT32_MAX) return false;
    auto* tree = ts_parser_parse_string(state_->parser, nullptr, utf8.data(),
                                        static_cast<std::uint32_t>(utf8.size()));
    if (!tree) return false;
    if (state_->tree) ts_tree_delete(state_->tree);
    state_->tree = tree;
    return true;
}

bool TreeSitterDocument::Reparse(std::string_view utf8, const SyntaxEdit& edit) noexcept {
    if (!state_->tree || utf8.size() > UINT32_MAX) return Parse(utf8);
    const TSInputEdit input_edit{edit.start_byte, edit.old_end_byte, edit.new_end_byte,
        Point(edit.start_row, edit.start_column), Point(edit.old_end_row, edit.old_end_column),
        Point(edit.new_end_row, edit.new_end_column)};
    ts_tree_edit(state_->tree, &input_edit);
    auto* tree = ts_parser_parse_string(state_->parser, state_->tree, utf8.data(),
                                        static_cast<std::uint32_t>(utf8.size()));
    if (!tree) return false;
    ts_tree_delete(state_->tree);
    state_->tree = tree;
    return true;
}

bool TreeSitterDocument::IsReady() const noexcept { return state_->tree != nullptr; }
bool TreeSitterDocument::HasErrors() const noexcept {
    return state_->tree && ts_node_has_error(ts_tree_root_node(state_->tree));
}

std::vector<SyntaxSpan> TreeSitterDocument::Highlights(
    std::uint32_t start_byte, std::uint32_t end_byte) const {
    std::vector<SyntaxSpan> spans;
    if (!state_->tree || !state_->highlight_query || start_byte >= end_byte) return spans;
    TSQueryCursor* cursor = ts_query_cursor_new();
    if (!cursor) return spans;
    ts_query_cursor_set_byte_range(cursor, start_byte, end_byte);
    ts_query_cursor_exec(cursor, state_->highlight_query, ts_tree_root_node(state_->tree));
    TSQueryMatch match{};
    std::uint32_t capture_index = 0;
    while (ts_query_cursor_next_capture(cursor, &match, &capture_index)) {
        const auto& capture = match.captures[capture_index];
        std::uint32_t name_length = 0;
        const char* name = ts_query_capture_name_for_id(
            state_->highlight_query, capture.index, &name_length);
        const auto kind = CaptureKind({name, name_length});
        if (kind != SyntaxKind::none) {
            const auto begin = ts_node_start_byte(capture.node);
            const auto end = ts_node_end_byte(capture.node);
            if (begin < end) spans.push_back({begin, end, kind});
        }
    }
    ts_query_cursor_delete(cursor);
    std::ranges::sort(spans, {}, &SyntaxSpan::start_byte);
    return spans;
}

std::vector<DocumentSymbol> TreeSitterDocument::Symbols(std::string_view utf8) const {
    std::vector<DocumentSymbol> symbols;
    if (!state_->tree || !state_->symbol_query) return symbols;
    TSQueryCursor* cursor = ts_query_cursor_new();
    if (!cursor) return symbols;
    ts_query_cursor_exec(cursor, state_->symbol_query, ts_tree_root_node(state_->tree));
    TSQueryMatch match{};
    while (ts_query_cursor_next_match(cursor, &match)) {
        for (std::uint16_t index = 0; index < match.capture_count; ++index) {
            const auto& capture = match.captures[index];
            const auto begin = ts_node_start_byte(capture.node);
            const auto end = ts_node_end_byte(capture.node);
            if (begin >= end || end > utf8.size()) continue;
            std::uint32_t name_length = 0;
            const char* name = ts_query_capture_name_for_id(
                state_->symbol_query, capture.index, &name_length);
            std::string_view capture_name{name, name_length};
            const auto separator = capture_name.find('.');
            symbols.push_back({std::string(utf8.substr(begin, end - begin)),
                separator == std::string_view::npos ? std::string(capture_name)
                                                    : std::string(capture_name.substr(separator + 1)),
                begin, end});
        }
    }
    ts_query_cursor_delete(cursor);
    return symbols;
}

}  // namespace notepad_colon
