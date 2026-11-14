#include <notepad_colon/localization.h>

#include <atomic>
#include <memory>
#include <unordered_map>

namespace notepad_colon {
namespace {
constexpr Translation kChineseSimplified[] = {
#include "strings_zh_cn.inc"
};

std::atomic<UiLanguage> active_language{UiLanguage::english};

using Index = std::unordered_map<std::wstring_view, std::wstring_view>;

const Index& ChineseIndex() {
    static const auto index = [] {
        auto built = std::make_unique<Index>();
        built->reserve(std::size(kChineseSimplified));
        for (const auto& entry : kChineseSimplified) built->emplace(entry.english, entry.translated);
        return built;
    }();
    return *index;
}

const Index* IndexFor(UiLanguage language) {
    return language == UiLanguage::chinese_simplified ? &ChineseIndex() : nullptr;
}
}  // namespace

void SetUiLanguage(UiLanguage language) noexcept { active_language.store(language); }

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
    if (language == UiLanguage::chinese_simplified) return kChineseSimplified;
    return {};
}

}  // namespace notepad_colon
