#include <notepad_colon/encoding_detection.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <memory>
#include <optional>
#include <vector>

namespace notepad_colon {
namespace {
constexpr std::size_t kSampleBytes = 64 * 1024;
constexpr unsigned int kGb18030 = 54936;
constexpr unsigned int kBig5 = 950;
constexpr unsigned int kShiftJis = 932;
constexpr unsigned int kEucJp = 51932;
constexpr unsigned int kUhc = 949;
constexpr unsigned int kWestern = 1252;

constexpr std::array kLegacyCodePages{
    LegacyCodePage{kGb18030, L"Chinese Simplified (GB18030)"},
    LegacyCodePage{kBig5, L"Chinese Traditional (Big5)"},
    LegacyCodePage{kShiftJis, L"Japanese (Shift-JIS)"},
    LegacyCodePage{kEucJp, L"Japanese (EUC-JP)"},
    LegacyCodePage{kUhc, L"Korean (EUC-KR)"},
    LegacyCodePage{kWestern, L"Western European (Windows-1252)"}};

// The most frequent characters of each language. A correct decode lands a
// large share of its non-ASCII characters in the matching table; a wrong
// decode produces near-random ideographs and lands almost none.
constexpr std::wstring_view kSimplifiedChinese =
    L"的一是不了人我在有他这为之大来以个中上们到说国和地也子时道出而要于就下得可你年生自会那后能对着事其里所去行过家十用发天如然作方成者多日都三小军二无同么经法当起与好看学进种将还分此心前面又定见只主没公从"
    L"知全两样现理使已正新些意关点或力长机实本外电明高问并手等西性开情由重入想第间它四口化物水被向度体表战员头最老才因工次什合真很果相场比路内气望特教管利命提感做代位级光门即先原产通声区处总东界打世南回几任少话别建更身车认资活直立信北记受系指民结条她质设五女件则变科目量至反金题听住白马书字红传权单干文统步达张万办局边城革报服程名难便基持数务市展社议政府党农业企济技术"
    L"请谢吗呢吧啊哪谁怎给让把跟还应该须须词语言译码试错误查找替换编辑视图帮助设置选项文件夹保存删除复制粘贴输入输出显示列表数据库网络软件硬件版本更新配置参数函数变量类型返回";
constexpr std::wstring_view kTraditionalChinese =
    L"的一是不了人我在有他這為之大來以個中上們到說國和地也子時道出而要於就下得可你年生自會那後能對著事其裡所去行過家十用發天如然作方成者多日都三小軍二無同麼經法當起與好看學進種將還分此心前面又定見只主沒公從"
    L"知全兩樣現理使已正新些意關點或力長機實本外電明高問並手等西性開情由重入想第間它四口化物水被向度體表戰員頭最老才因工次什合真很果相場比路內氣望特教管利命提感做代位級光門即先原產通聲區處總東界打世南回幾任少話別建更身車認資活直立信北記受係指民結條她質設五女件則變科目量至反金題聽住白馬書字紅傳權單幹文統步達張萬辦局邊城報服程名難便基持數務市展社議政府黨農業企濟技術臺灣"
    L"請謝嗎呢吧啊哪誰怎給讓把跟還應該須詞語言譯碼試錯誤查找替換編輯視圖幫助設置選項檔案夾儲存刪除複製貼上輸入輸出顯示列表資料庫網路軟體硬體版本更新設定參數函數變數類型傳回";
constexpr std::wstring_view kJapanese =
    L"日本人年大国中会長時間出見行生後前方上下合分手自学者事同場新東京円気社月子目的内高化部度業問題代表明地力理体定物通発開関連性最全対動用法政民主議員市家入"
    L"語文字漢読書話聞食飲買売思知言使作持待立始終続変教習計画数値設結果処情報確認登録削除検索追加更新保存表示選択入力出力機能必要場合以外今私彼女何誰";
constexpr std::wstring_view kKorean =
    L"이다는에의을가하고지한를로서은도기리사아니어대해으있나자시수그인것과들정게부전스제만장일라여주구보소와면었히공원상동성국화조내적우러세할야무오모려신등경며마위개진비관계중방치미문연선실분요거발터합재된학유명간음생저식물데금안입습까될였않없같때더또및"
    L"년월현차통말번역단점람속강남북민회업용체형태프트드크르레코파타카피노호두루추출력설행운영양청예외";

using Table = std::bitset<65536>;

struct Tables {
    Table simplified, traditional, japanese, korean;
    Tables() {
        for (const auto value : kSimplifiedChinese) simplified.set(value);
        for (const auto value : kTraditionalChinese) traditional.set(value);
        for (const auto value : kJapanese) japanese.set(value);
        for (const auto value : kKorean) korean.set(value);
        // Hiragana, katakana and the prolonged sound mark.
        for (unsigned value = 0x3041; value <= 0x3096; ++value) japanese.set(value);
        for (unsigned value = 0x30a1; value <= 0x30fc; ++value) japanese.set(value);
    }
};

const Tables& GetTables() {
    static const auto tables = std::make_unique<Tables>();
    return *tables;
}

bool IsSharedCjkPunctuation(wchar_t value) noexcept {
    return (value >= 0x3000 && value <= 0x3011) || value == 0x201c || value == 0x201d ||
           value == 0x2018 || value == 0x2019 || value == 0x2014 || value == 0x2026 ||
           value == 0xff01 || value == 0xff08 || value == 0xff09 || value == 0xff0c ||
           value == 0xff1a || value == 0xff1b || value == 0xff1f || value == 0x30fb;
}

std::optional<std::wstring> StrictDecode(std::span<const std::uint8_t> bytes,
                                         unsigned int code_page) {
    if (bytes.empty()) return std::wstring{};
    // A bounded sample can end inside a multi-byte character; tolerate that.
    for (std::size_t trim = 0; trim <= 3 && trim < bytes.size(); ++trim) {
        const auto count = static_cast<int>(bytes.size() - trim);
        const auto* input = reinterpret_cast<const char*>(bytes.data());
        const int required = ::MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, input, count,
                                                   nullptr, 0);
        if (required <= 0) continue;
        std::wstring text(static_cast<std::size_t>(required), L'\0');
        if (::MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, input, count, text.data(),
                                  required) == required)
            return text;
    }
    return std::nullopt;
}

struct Score {
    double ratio = 0.0;
    std::size_t typical = 0;
    std::size_t non_ascii = 0;
};

Score ScoreText(std::wstring_view text, const Table& table, bool penalize_halfwidth_kana) {
    Score score;
    std::size_t suspicious = 0;
    for (const auto value : text) {
        if (value < 0x80) continue;
        ++score.non_ascii;
        if (table.test(value) || IsSharedCjkPunctuation(value)) ++score.typical;
        // Private use, replacement and C1 controls never appear in real text.
        else if ((value >= 0xe000 && value <= 0xf8ff) || value == 0xfffd || value < 0xa0)
            ++suspicious;
        // Shift-JIS maps single bytes 0xA1-0xDF to half-width katakana, which
        // makes many unrelated byte streams decode "successfully".
        else if (penalize_halfwidth_kana && value >= 0xff61 && value <= 0xff9f)
            ++suspicious;
    }
    if (score.non_ascii == 0) return score;
    const auto typical = static_cast<double>(score.typical) - 2.0 * static_cast<double>(suspicious);
    score.ratio = (std::max)(0.0, typical / static_cast<double>(score.non_ascii));
    return score;
}

const Table* TableFor(unsigned int code_page) {
    const auto& tables = GetTables();
    switch (code_page) {
    case 936: case kGb18030: return &tables.simplified;
    case kBig5: return &tables.traditional;
    case kShiftJis: case kEucJp: case 20932: return &tables.japanese;
    case kUhc: case 51949: return &tables.korean;
    default: return nullptr;
    }
}
}  // namespace

std::span<const LegacyCodePage> SupportedLegacyCodePages() noexcept { return kLegacyCodePages; }

std::wstring CodePageName(unsigned int code_page) {
    if (code_page == 936) return L"Chinese Simplified (GBK)";
    for (const auto& entry : kLegacyCodePages)
        if (entry.code_page == code_page) return std::wstring{entry.name};
    CPINFOEXW information{};
    if (::GetCPInfoExW(code_page, 0, &information) && information.CodePageName[0] != L'\0')
        return information.CodePageName;
    return L"Code page " + std::to_wstring(code_page);
}

CodePageGuess DetectLegacyCodePage(std::span<const std::uint8_t> bytes,
                                   unsigned int system_code_page) {
    const unsigned int system = system_code_page ? system_code_page : ::GetACP();
    const auto sample = bytes.first((std::min)(bytes.size(), kSampleBytes));
    CodePageGuess best;
    bool system_decodes = false;
    const std::array candidates{kGb18030, kBig5, kShiftJis, kEucJp, kUhc};
    for (const auto code_page : candidates) {
        if (!::IsValidCodePage(code_page)) continue;
        const auto text = StrictDecode(sample, code_page);
        if (!text) continue;
        const bool is_system = code_page == system || (code_page == kGb18030 && system == 936);
        system_decodes = system_decodes || is_system;
        const auto score = ScoreText(*text, *TableFor(code_page), code_page == kShiftJis);
        // The system code page wins near-ties: it is the likeliest author.
        const double adjusted = score.ratio + (is_system ? 0.05 : 0.0);
        if (score.non_ascii != 0 && adjusted > best.confidence) {
            best.code_page = code_page;
            best.confidence = adjusted;
        }
    }
    // Typical text places well over a third of its characters in the tables;
    // random ideographs from a wrong decode stay in the low single digits.
    if (best.code_page != 0 && best.confidence >= 0.2) {
        best.detected = true;
        return best;
    }
    CodePageGuess fallback;
    if (TableFor(system) == nullptr || system_decodes) {
        fallback.code_page = system;
    } else if (StrictDecode(sample, system)) {
        fallback.code_page = system;
    } else {
        // A CJK system code page that rejects the bytes: single-byte Western
        // text is the only remaining reading that cannot fail.
        fallback.code_page = kWestern;
    }
    return fallback;
}

}  // namespace notepad_colon
