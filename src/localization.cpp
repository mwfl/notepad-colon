#include <notepad_colon/localization.h>

#include <atomic>
#include <memory>
#include <unordered_map>

namespace notepad_colon {
namespace {
constexpr Translation kChineseSimplified[] = {
#include "strings_zh_cn.inc"
};

constexpr Translation kChineseTraditional[] = {
#include "strings_zh_tw.inc"
};
constexpr Translation kSpanish[] = {
#include "strings_es.inc"
};
constexpr Translation kFrench[] = {
#include "strings_fr.inc"
};
constexpr Translation kHindi[] = {
#include "strings_hi.inc"
};
constexpr UiLanguageInfo kLanguages[]{
    {UiLanguage::english, L"English", "en"},
    {UiLanguage::chinese_simplified, L"简体中文", "zh-Hans"},
    {UiLanguage::chinese_traditional, L"繁體中文", "zh-Hant"},
    {UiLanguage::spanish, L"Español", "es"},
    {UiLanguage::french, L"Français", "fr"},
    {UiLanguage::hindi, L"हिन्दी", "hi"},
};
std::atomic<UiLanguage> active_language{UiLanguage::english};

using Index = std::unordered_map<std::wstring_view, std::wstring_view>;

Index MakeIndex(std::span<const Translation> catalog) {
    Index index;
    index.reserve(catalog.size());
    for (const auto& entry : catalog) index.emplace(entry.english, entry.translated);
    return index;
}
const Index* IndexFor(UiLanguage language) {
    switch (language) {
    case UiLanguage::chinese_simplified: { static const auto index = MakeIndex(kChineseSimplified); return &index; }
    case UiLanguage::chinese_traditional: { static const auto index = MakeIndex(kChineseTraditional); return &index; }
    case UiLanguage::spanish: { static const auto index = MakeIndex(kSpanish); return &index; }
    case UiLanguage::french: { static const auto index = MakeIndex(kFrench); return &index; }
    case UiLanguage::hindi: { static const auto index = MakeIndex(kHindi); return &index; }
    default: return nullptr;
    }
}
}  // namespace

std::span<const UiLanguageInfo> SupportedUiLanguages() noexcept { return kLanguages; }
bool IsValidUiLanguage(UiLanguage language) noexcept {
    return static_cast<std::uint32_t>(language) < std::size(kLanguages);
}
void SetUiLanguage(UiLanguage language) noexcept {
    active_language.store(IsValidUiLanguage(language) ? language : UiLanguage::english);
}

UiLanguage GetUiLanguage() noexcept { return active_language.load(); }

std::wstring Tr(std::wstring_view english) {
    if (const auto* index = IndexFor(active_language.load())) {
        if (const auto found = index->find(english); found != index->end())
            return std::wstring{found->second};
    }
    return std::wstring{english};
}

bool HasTranslation(UiLanguage language, std::wstring_view english) {
    const auto* index = IndexFor(language);
    return index != nullptr && index->contains(english);
}

std::span<const Translation> TranslationCatalog(UiLanguage language) noexcept {
    switch (language) {
    case UiLanguage::chinese_simplified: return kChineseSimplified;
    case UiLanguage::chinese_traditional: return kChineseTraditional;
    case UiLanguage::spanish: return kSpanish;
    case UiLanguage::french: return kFrench;
    case UiLanguage::hindi: return kHindi;
    default: return {};
    }
}

}  // namespace notepad_colon
