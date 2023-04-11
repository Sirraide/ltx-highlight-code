#include <clopts.hh>
#include <queue>
#include <ranges>
#include <utils.hh>

using namespace command_line_options;
using options = clopts< // clang-format off
    positional<"language", "The programming language to highlight">,
    positional<"input", "The input file to highlight", file<std::string>>,
    help<>
>; // clang-format on

/// Trie for Aho-Corasick string matching.
struct trie {
    struct node {
        std::unordered_map<char, node> children;
        bool is_word = false;
        node* fail = nullptr;
        usz depth = 0;
    };

    struct match_result {
        usz pos{};
        usz len{};
    };

    node root;

    /// Finalise the trie.
    void finalise() {
        std::queue<node*> queue;

        root.fail = &root;
        for (auto& [_, child] : root.children) {
            child.fail = &root;
            child.depth = 1;
            queue.push(&child);
        }

        while (!queue.empty()) {
            node* current = queue.front();
            queue.pop();

            for (auto& [c, child] : current->children) {
                child.depth = current->depth + 1;
                node* fail = current->fail;
                while (fail and fail != &root) {
                    if (fail->children.contains(c)) {
                        child.fail = &fail->children[c];
                        goto next;
                    }
                    fail = fail->fail;
                }
                child.fail = &root;
            next:
                queue.push(&child);
            }
        }
    }

    /// Insert a string into the trie.
    void insert(std::string_view str) {
        node* current = &root;
        for (char c : str) current = &current->children[c];
        current->is_word = true;
    }

    /// Match a string against the trie. Keep only
    /// the longest matches.
    auto match(std::string_view str) -> std::vector<match_result> {
        std::vector<match_result> matches;
        node* current = &root;
        usz last_match_end = std::string::npos;

        for (usz i = 0; i < str.size(); ++i) {
            char c = str[i];
            if (current->children.contains(c)) {
                current = &current->children[c];
                if (current->is_word) last_match_end = i;
            } else {
                if (last_match_end != std::string::npos) {
                    matches.push_back({last_match_end - current->depth + 1, current->depth});
                    last_match_end = std::string::npos;
                }
                current = current->fail;
            }
        }

        if (last_match_end != std::string::npos) matches.push_back({last_match_end - current->depth + 1, current->depth});
        return matches;
    }
};

/// Highlight keywords in a string.
template <std::size_t N>
void highlight_keywords(std::string& text, std::string_view const (&keywords)[N]) {
    /// Build trie.
    trie tr;
    for (auto keyword : keywords) tr.insert(keyword);
    tr.finalise();

    /// Match keywords.
    auto matches = tr.match(text);

    /// Surround the positions with `\MDKeyword{}`.
    for (auto& m : rgs::reverse_view(matches)) {
        /// Ignore matches not followed by whitespace, an operator, or the end of the string.
        static constexpr std::string_view allowed = " \t\r\n+-*/%&|^~!=<>?:;.,()[]{}'\"\\";
        if (m.pos + m.len < text.size() and not allowed.contains(text[m.pos + m.len])) continue;

        text.insert(m.pos + m.len, "\\@MD@");
        text.insert(m.pos, "\\@MD@Keyword ");
    }
}

void highlight_cxx(std::string& text) {
    static constexpr std::string_view keywords[]{
        "#include",
        "#define",
        "#undef",
        "#if",
        "#ifdef",
        "#ifndef",
        "#else",
        "#elif",
        "#endif",
        "alignas",
        "alignof",
        "and",
        "and_eq",
        "asm",
        "auto",
        "bitand",
        "bitor",
        "bool",
        "break",
        "case",
        "catch",
        "char",
        "char8_t",
        "char16_t",
        "char32_t",
        "class",
        "compl",
        "concept",
        "const",
        "consteval",
        "constexpr",
        "constinit",
        "const_cast",
        "continue",
        "co_await",
        "co_return",
        "co_yield",
        "decltype",
        "default",
        "delete",
        "do",
        "double",
        "dynamic_cast",
        "else",
        "enum",
        "explicit",
        "export",
        "extern",
        "false",
        "float",
        "for",
        "friend",
        "goto",
        "if",
        "inline",
        "int",
        "long",
        "mutable",
        "namespace",
        "new",
        "noexcept",
        "not",
        "not_eq",
        "nullptr",
        "operator",
        "or",
        "or_eq",
        "private",
        "protected",
        "public",
        "register",
        "reinterpret_cast",
        "requires",
        "return",
        "short",
        "signed",
        "sizeof",
        "static",
        "static_assert",
        "static_cast",
        "struct",
        "switch",
        "template",
        "this",
        "thread_local",
        "throw",
        "true",
        "try",
        "typedef",
        "typeid",
        "typename",
        "union",
        "unsigned",
        "using",
        "virtual",
        "void",
        "volatile",
        "wchar_t",
        "while",
        "xor",
        "xor_eq",
    };

    highlight_keywords(text, keywords);
}

/// The only thing I can stand less than Go is Go without syntax highlighting.
void highlight_go(std::string& text) {
    static constexpr std::string_view keywords[]{
        "break",
        "default",
        "func",
        "interface",
        "select",
        "case",
        "defer",
        "go",
        "map",
        "struct",
        "chan",
        "else",
        "goto",
        "package",
        "switch",
        "const",
        "fallthrough",
        "if",
        "range",
        "type",
        "continue",
        "for",
        "import",
        "return",
        "var",
        "bool",
        "byte",
        "complex64",
        "complex128",
        "error",
        "float32",
        "float64",
        "int",
        "int8",
        "int16",
        "int32",
        "int64",
        "rune",
        "string",
        "uint",
        "uint8",
        "uint16",
        "uint32",
        "uint64",
        "uintptr",
        "true",
        "false",
        "iota",
        "nil",
    };

    highlight_keywords(text, keywords);
}

int main(int argc, char** argv) {
    options::parse(argc, argv);
    std::string_view lang = *options::get<"language">();
    std::string text = options::get<"input">()->contents;

    if (lang == "C++") highlight_cxx(text);
    else if (lang == "Go") highlight_go(text);
    else die("Unknown language '{}'", lang);

    std::fwrite(text.data(), 1, text.size(), stdout);
}
