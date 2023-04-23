#include <clopts.hh>
#include <queue>
#include <ranges>
#include <utility>
#include <utils.hh>

using namespace command_line_options;
using options = clopts< // clang-format off
    positional<"language", "The programming language to highlight">,
    positional<"input", "The input file to highlight", file<std::string>>,
    flag<"--debug", "Debug output">,
    help<>
>; // clang-format on

enum struct kind {
    operator_,
    keyword,
    type,
    ignore,
    comment,
    string,
    escape_sequence,
};

/// Trie for Aho-Corasick string matching.
struct trie {
    struct node {
        std::unordered_map<char, node> children;
        bool is_word = false;
        node* fail = nullptr;
        usz depth = 0;
        kind k{};
    };

    struct match_result {
        usz pos{};
        usz len{};
        kind k{};
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
    void insert(std::string_view str, kind k) {
        node* current = &root;
        for (char c : str) current = &current->children[c];
        current->is_word = true;
        current->k = k;
    }

    /// Match a string against the trie. Keep only
    /// the longest matches.
    auto match(std::string_view str) -> std::vector<match_result> {
        std::vector<match_result> matches;
        node* current = &root;
        usz last_match_end = std::string::npos;
        kind last_kind{};

        /// Check if the current node has a child matching `c`, and if so
        /// set the end of the last match to the current position if the
        /// child is the end of a word.
        auto match = [&](usz i, char c) {
            if (not current->children.contains(c)) return false;
            current = &current->children[c];
            if (current->is_word) {
                last_match_end = i;
                last_kind = current->k;
            }
            return true;
        };

        for (usz i = 0; i < str.size(); ++i) {
            char c = str[i];
            if (not match(i, c)) {
                /// If we have a current word append it.
                if (last_match_end != std::string::npos) {
                    matches.push_back({last_match_end - current->depth + 1, current->depth, last_kind});
                    last_match_end = std::string::npos;
                    current = current->fail;
                    --i;
                }

                /// Otherwise, check to see if the current character
                /// is a single-letter word.
                else {
                    current = current->fail;
                    match(i, c);
                }
            }
        }

        if (last_match_end != std::string::npos) matches.push_back({last_match_end - current->depth + 1, current->depth, last_kind});
        return matches;
    }
};

std::string colour_string_prefix(std::string_view langname, std::string_view colour) {
    return fmt::format(R"(\@MD@Color {}\@MD@ {}\@MD@ )", langname, colour);
}

/// Check if this character is valid in an identifier. This is so we highlight
/// e.g. `if`, but not `get_if`.
bool iscontinue(char c) {
    return std::isalnum(c) or c == '_';
}

/// Parameters for highlighting.
struct highlight_params {
    std::string_view lang_name;
    std::span<const char> string_delimiters;
    std::string_view escape_sequences;
    std::string_view line_comment_prefix;
    std::span<const std::string_view> keywords;
};

/// Highlight code in a string.
void highlight(std::string& text, const highlight_params& params) {
    static constexpr std::string_view operators = "+-*/%&|~!=<>?:;.,()[]{}";

    /// Prepend `\\` to escape sequences. Getting LaTeX to print a single escape char into
    /// a file is kind of a pain, so we just resort to printing two of them instead.
    std::vector<std::string> escape_sequences;
    for (auto e : params.escape_sequences) escape_sequences.push_back(fmt::format("\\\\{}", e));

    /// Build trie.
    trie tr;
    for (auto keyword : params.keywords) tr.insert(keyword, kind::keyword);
    for (usz i = 0; i < operators.size(); ++i) tr.insert(operators.substr(i, 1), kind::operator_);
    for (auto c : params.string_delimiters) tr.insert(std::string_view(&c, 1), kind::string);
    for (const auto& e : escape_sequences) tr.insert(e, kind::escape_sequence);
    tr.insert("::", kind::operator_);
    tr.insert("T", kind::type);
    tr.insert(params.line_comment_prefix, kind::comment);
    tr.finalise();

    /// Match keywords.
    auto matches = tr.match(text);
    if (options::get<"--debug">()) {
        for (auto& m : matches) {
            std::string_view k = m.k == kind::keyword ? "keyword" : "operator";
            fmt::print("{}: \"{}\" ({} chars)\n", k, text.substr(m.pos, m.len), m.len);
        }
        std::exit(0);
    }

    /// Macro call to insert for keywords/operators.
    auto kwstr = colour_string_prefix(params.lang_name, "Keyword");
    auto opstr = colour_string_prefix(params.lang_name, "Operator");
    auto tystr = colour_string_prefix(params.lang_name, "Type");
    auto escape_start = fmt::format("\\@MD@Escape@Start {}\\@MD@ ", params.lang_name);
    auto comment_start = fmt::format("\\@MD@LineComment@Start {}\\@MD@ ", params.lang_name);
    auto string_start = fmt::format("\\@MD@String@Start {}\\@MD@ ", params.lang_name);

    /// Whether we’re in a string.
    usz string_end = std::string::npos;
    auto in_string = [&] { return string_end != std::string::npos; };

    /// Highlight matches.
    for (auto& m : rgs::reverse_view(matches)) {
        /// Ignore matches not followed by whitespace, an operator, or the end of the string.
        static constexpr std::string_view allowed = " \t\r\n+-*/%&|^~!=<>?:;.,()[]{}'\"\\";
        if (m.k == kind::ignore) continue;

        /// Mark the start and end of strings.
        if (m.k == kind::string) {
            if (in_string()) {
                text.insert(string_end, "\\@MD@String@End");
                text.insert(m.pos, string_start);
                string_end = std::string::npos;
            } else string_end = m.pos + m.len;
        }

        /// Skip anything other than escape sequences if we’re in as string.
        if (in_string()) {
            if (m.k == kind::escape_sequence) {
                static constexpr std::string_view escape_end = "\\@MD@Escape@End ";
                fmt::print(stderr , "Escape sequence: \"{}\"\n", text.substr(m.pos, m.len));
                text.insert(m.pos + m.len, escape_end);
                text.insert(m.pos, escape_start);
                string_end += escape_end.size() + escape_start.size();
            }
            continue;
        }

        /// Handle operators.
        if (m.k != kind::operator_ and m.pos + m.len < text.size() and not allowed.contains(text[m.pos + m.len])) continue;

        /// If it’s a comment, colour the rest of the line.
        if (m.k == kind::comment) {
            /// Find the end of the line.
            usz end = text.find("\\@MD@Brk", m.pos + m.len);
            if (end == std::string::npos) end = text.find("\\makeatother");
            if (end == std::string::npos) end = text.size();

            /// Colour the line.
            text.insert(end, "\\@MD@LineComment@End ");
            text.insert(m.pos, comment_start);
            continue;
        }

        /// Keywords and types must not be preceded by a character that may be part of an identifier.
        if ((m.k == kind::keyword or m.k == kind::type) and m.pos > 0 and iscontinue(text[m.pos - 1])) continue;

        /// Otherwise, colour the match appropriately.
        text.insert(m.pos + m.len, "\\@MD@ ");
        switch (m.k) {
            case kind::operator_: text.insert(m.pos, opstr); break;
            case kind::keyword: text.insert(m.pos, kwstr); break;
            case kind::type: text.insert(m.pos, tystr); break;
            default: std::unreachable();
        }
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

    highlight(
        text,
        highlight_params{
            .lang_name = "C++",
            .string_delimiters = "'\"",
            .escape_sequences = "'\"\\nrt",
            .line_comment_prefix = "//",
            .keywords = keywords,
        }
    );
}

/// The only thing I can stand less than Go is Go without syntax highlighting.
void highlight_go(std::string& text) {
    static constexpr std::string_view keywords[]{
        "break",
        "default",
        "func",
        "interface",
        "any",
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

    highlight(
        text,
        highlight_params{
            .lang_name = "Go",
            .string_delimiters = "\"'",
            .escape_sequences = "'\"\\nrt",
            .line_comment_prefix = "//",
            .keywords = keywords,
        }
    );
}

int main(int argc, char** argv) {
    options::parse(argc, argv);
    std::string_view lang = *options::get<"language">();
    std::string text = options::get<"input">()->contents;

    if (lang == "C++") highlight_cxx(text);
    else if (lang == "Go") highlight_go(text);
    else if (lang == "Text") {
    } else die("Unknown language '{}'", lang);

    std::fwrite(text.data(), 1, text.size(), stdout);
}
