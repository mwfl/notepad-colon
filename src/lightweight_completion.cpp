#include <notepad_colon/lightweight_completion.h>

#include <algorithm>
#include <cctype>
#include <set>

namespace notepad_colon {
namespace {
constexpr std::size_t kMaximumDocumentBytes = 8u * 1024u * 1024u;
constexpr std::size_t kMaximumIdentifierBytes = 128;

bool Identifier(unsigned char value) noexcept {
  return std::isalnum(value) != 0 || value == '_';
}

bool StartsWithInsensitive(std::string_view value,
                           std::string_view prefix) noexcept {
  if (value.size() < prefix.size())
    return false;
  for (std::size_t index = 0; index < prefix.size(); ++index)
    if (std::tolower(static_cast<unsigned char>(value[index])) !=
        std::tolower(static_cast<unsigned char>(prefix[index])))
      return false;
  return true;
}

void AddWords(std::set<std::string> &values, std::string_view text,
              std::string_view prefix) {
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    while (cursor < text.size() &&
           !Identifier(static_cast<unsigned char>(text[cursor])))
      ++cursor;
    const auto begin = cursor;
    while (cursor < text.size() &&
           Identifier(static_cast<unsigned char>(text[cursor])))
      ++cursor;
    const auto word = text.substr(begin, cursor - begin);
    if (word.size() > prefix.size() && word.size() <= kMaximumIdentifierBytes &&
        StartsWithInsensitive(word, prefix))
      values.emplace(word);
  }
}
} // namespace

CompletionResult CompleteLocally(std::string_view utf8, std::size_t caret_byte,
                                 Language language,
                                 std::span<const DocumentSymbol> symbols,
                                 std::size_t maximum_candidates) {
  CompletionResult result;
  if (utf8.size() > kMaximumDocumentBytes || caret_byte > utf8.size() ||
      maximum_candidates == 0)
    return result;
  auto begin = caret_byte;
  while (begin > 0 && Identifier(static_cast<unsigned char>(utf8[begin - 1])))
    --begin;
  const auto prefix = utf8.substr(begin, caret_byte - begin);
  result.prefix_bytes = prefix.size();
  if (prefix.empty() || prefix.size() > kMaximumIdentifierBytes)
    return result;

  std::set<std::string> values;
  const auto &profile = GetLanguageProfile(language);
  AddWords(values, profile.primary_keywords, prefix);
  AddWords(values, profile.secondary_keywords, prefix);
  AddWords(values, utf8, prefix);
  for (const auto &symbol : symbols)
    if (symbol.name.size() > prefix.size() &&
        symbol.name.size() <= kMaximumIdentifierBytes &&
        StartsWithInsensitive(symbol.name, prefix))
      values.emplace(symbol.name);
  values.erase(std::string(prefix));
  result.candidates.reserve((std::min)(maximum_candidates, values.size()));
  for (auto &value : values) {
    if (result.candidates.size() == maximum_candidates)
      break;
    result.candidates.push_back(value);
  }
  return result;
}

} // namespace notepad_colon
