#include <clopts.hh>
#include <queue>
#include <ranges>
#include <utility>
#include <utils.hh>

#if defined(__has_include)
#    if __has_include(<tree_sitter/api.h>)
#        define HAVE_TREE_SITTER
#        include <tree_sitter/api.h>

extern "C" {
extern const TSLanguage *tree_sitter_cpp(void);
}
#    endif
#endif

using namespace command_line_options;
using options = clopts< // clang-format off
    positional<"language", "The programming language to highlight">,
    positional<"input", "The input file to highlight", file<std::string>>,
    flag<"--debug", "Debug output">,
    flag<"--tree-sitter", "Use tree-sitter to perform syntax highlighting">,
    help<>
>; // clang-format on

/// Characters used by the package.
#define ESC "\x10"
#define BEG "\x02"
#define END "\x03"

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
    return fmt::format(ESC "@@MDStyle" BEG "{}" END BEG "{}" END BEG, langname, colour);
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
    std::span<const std::string_view> types;
};

/// Highlight code in a string.
void highlight(std::string& text, const highlight_params& params) {
    static constexpr std::string_view operators = "+-*/%&|~!=<>?:;.,()[]{}";

    /// Prepend `\` to escape sequences.
    std::vector<std::string> escape_sequences;
    for (auto e : params.escape_sequences) escape_sequences.push_back(fmt::format("\\{}", e));

    /// Build trie.
    trie tr;
    for (auto keyword : params.keywords) tr.insert(keyword, kind::keyword);
    for (usz i = 0; i < operators.size(); ++i) tr.insert(operators.substr(i, 1), kind::operator_);
    for (auto c : params.string_delimiters) tr.insert(std::string_view(&c, 1), kind::string);
    for (const auto& e : escape_sequences) tr.insert(e, kind::escape_sequence);
    for (const auto& t : params.types) tr.insert(t, kind::type);
    tr.insert("::", kind::operator_);
    tr.insert(params.line_comment_prefix, kind::comment);
    tr.finalise();

    /// Match keywords.
    auto matches = tr.match(text);
    auto print_matches = [&] (auto it) {
        if (options::get<"--debug">()) {
            for (; it != matches.rend(); ++it) {
                auto& m = *it;
                fmt::print(stderr, "{}: \"{}\" ({} chars)\n", std::to_underlying(m.k), text.substr(m.pos, m.len), m.len);
            }

            fmt::print(stderr, "\n\n\n");
        }
    };

    /// Macro call to insert for keywords/operators.
    auto typeset_kw = colour_string_prefix(params.lang_name, "Keyword");
    auto typeset_op = colour_string_prefix(params.lang_name, "Operator");
    auto typeset_ty = colour_string_prefix(params.lang_name, "Type");
    auto typeset_esc = colour_string_prefix(params.lang_name, "Escape");
    auto typeset_com = colour_string_prefix(params.lang_name, "Comment");
    auto typeset_str = colour_string_prefix(params.lang_name, "String");

    /// Whether we’re in a string.
    usz string_end = std::string::npos;
    auto in_string = [&] { return string_end != std::string::npos; };

    /// Highlight matches.
    for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
        /// Ignore matches not followed by whitespace, an operator, or the end of the string.
        static constexpr std::string_view allowed = " \t\r\n+-*/%&|^~!=<>?:;.,()[]{}'\"\\";
        auto& m = *it;
        if (m.k == kind::ignore) continue;
        print_matches(it);

        /// Mark the start and end of strings.
        if (m.k == kind::string) {
            if (in_string()) {
                text.insert(string_end, END);
                text.insert(m.pos, typeset_str);
                string_end = std::string::npos;
                continue;
            } else string_end = m.pos + m.len;
        }

        /// Skip anything other than escape sequences if we’re in as string.
        if (in_string()) {
            if (m.k == kind::escape_sequence) {
                text.insert(m.pos + m.len, END);
                text.insert(m.pos, typeset_esc);
                string_end += typeset_esc.size() + sizeof(END) - 1;
            }
            continue;
        }

        /// If it’s a comment, colour the rest of the line.
        if (m.k == kind::comment) {
            /// Find the end of the line.
            usz end = text.find('\n', m.pos + m.len);
            if (end == std::string::npos) end = text.size();

            /// Colour the line.
            text.insert(end, END);
            text.insert(m.pos, typeset_com);
            continue;
        }

        /// Keywords and types must not be preceded by a character that may be part of an identifier.
        if ((m.k == kind::keyword or m.k == kind::type) and m.pos > 0 and iscontinue(text[m.pos - 1])) continue;

        /// Otherwise, colour the match appropriately.
        text.insert(m.pos + m.len, END);
        switch (m.k) {
            case kind::operator_: text.insert(m.pos, typeset_op); break;
            case kind::keyword: text.insert(m.pos, typeset_kw); break;
            case kind::type: text.insert(m.pos, typeset_ty); break;
            default: die("Unreachable: Invalid match kind: {}", std::to_underlying(m.k));
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
        "break",
        "case",
        "catch",
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
        "dynamic_cast",
        "else",
        "enum",
        "explicit",
        "export",
        "extern",
        "false",
        "for",
        "friend",
        "goto",
        "if",
        "inline",
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
        "using",
        "virtual",
        "volatile",
        "while",
        "xor",
        "xor_eq",
    };

    static constexpr std::string_view types[]{
        "bool",
        "char",
        "char8_t",
        "char16_t",
        "char32_t",
        "double",
        "float",
        "int",
        "long",
        "short",
        "signed",
        "T",
        "unsigned",
        "void",
        "wchar_t",
    };

    highlight(
        text,
        highlight_params{
            .lang_name = "C++",
            .string_delimiters = "'\"",
            .escape_sequences = "'\"\\nrt",
            .line_comment_prefix = "//",
            .keywords = keywords,
            .types = types,
        }
    );
}

void highlight_c(std::string& text) {
    static constexpr std::string_view keywords[]{
        "_Alignas",
        "_Alignof",
        "_Atomic",
        "_BitInt",
        "_Bool",
        "_Complex",
        "_Decimal32",
        "_Decimal64",
        "_Decimal128",
        "_Generic",
        "_Imaginary",
        "_Noreturn",
        "_Pragma",
        "_Static_assert",
        "_Thread_local",
        "#embed",
        "#error",
        "#include",
        "#line",
        "#pragma",
        "#warning",
        "#define",
        "#undef",
        "#if",
        "#ifdef",
        "#ifndef",
        "#else",
        "#elif",
        "#elifdef",
        "#elifndef",
        "#endif",
        "alignas",
        "alignof",
        "asm",
        "auto",
        "break",
        "case",
        "const",
        "constexpr",
        "continue",
        "default",
        "do",
        "else",
        "enum",
        "extern",
        "false",
        "float",
        "for",
        "fortran",
        "goto",
        "if",
        "inline",
        "NULL",
        "nullptr",
        "register",
        "restrict",
        "return",
        "sizeof",
        "static",
        "static_assert",
        "struct",
        "switch",
        "thread_local",
        "true",
        "typedef",
        "typeof",
        "typeof_unqual",
        "union",
        "volatile",
        "while",
    };

    static constexpr std::string_view types[]{
        "bool",
        "char",
        "double",
        "float",
        "int",
        "long",
        "short",
        "signed",
        "unsigned",
        "void",
    };

    highlight(
        text,
        highlight_params{
            .lang_name = "C",
            .string_delimiters = "'\"",
            .escape_sequences = "'\"\\nrt",
            .line_comment_prefix = "//",
            .keywords = keywords,
            .types = types,
        }
    );
}

void highlight_intercept(std::string& text) {
    static constexpr std::string_view keywords[]{
        "as",
        "else",
        "for",
        "if",
        "type",
        "while",
    };

    static constexpr std::string_view types[]{
        "byte",
        "integer",
        "s8",
        "s16",
        "s32",
        "s64",
        "u8",
        "u16",
        "u32",
        "u64",
        "void",
    };

    highlight(
        text,
        highlight_params{
            .lang_name = "int",
            .string_delimiters = "'\"",
            .escape_sequences = "'\"\\nrtfvaeb",
            .line_comment_prefix = ";;",
            .keywords = keywords,
            .types = types,
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
        "true",
        "false",
        "iota",
        "nil",
    };

    static constexpr std::string_view types[]{
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
        "map",
        "rune",
        "string",
        "T",
        "uint",
        "uint8",
        "uint16",
        "uint32",
        "uint64",
        "uintptr",
    };

    highlight(
        text,
        highlight_params{
            .lang_name = "Go",
            .string_delimiters = "\"'",
            .escape_sequences = "'\"\\nrt",
            .line_comment_prefix = "//",
            .keywords = keywords,
            .types = types,
        }
    );
}

void trim(std::string& s) {
    while (not s.empty() && isspace(s.back())) s.pop_back();
    while (not s.empty() && isspace(s.front())) s.erase(s.begin());
}

int main(int argc, char** argv) {
    options::parse(argc, argv);
    std::string_view lang = *options::get<"language">();
    std::string text = options::get<"input">()->contents;

    trim(text);
    if (lang == "Text") goto echo;

    /// Use tree-sitter if requested and present. Note: We leak all the memory allocated
    /// by tree-sitter. This is an application, not a library. Fix this if we ever end up
    /// making this a library.
#ifdef HAVE_TREE_SITTER
    if (options::get<"--tree-sitter">()) {
        auto parser = ts_parser_new();
        defer { ts_parser_delete(parser); };

        /// Set the language.
        if (lang == "C++") ts_parser_set_language(parser, tree_sitter_cpp());
        else die("Unknown language '{}'", lang);

        /// Parse the input.
        auto tree = ts_parser_parse_string(parser, nullptr, text.data(), u32(text.size()));
        defer { ts_tree_delete(tree); };

        /// Print tree.
        auto root = ts_tree_root_node(tree);
        auto s = ts_node_string(root);
        defer { free(s); };
        fmt::print("{}\n\n", s);

        /// Inorder tree walk.
        auto walk = [&](auto&& f) {
            auto node = ts_tree_root_node(tree);
            auto cursor = ts_tree_cursor_new(node);
            defer { ts_tree_cursor_delete(&cursor); };

            while (ts_tree_cursor_goto_first_child(&cursor)) {
                f(cursor);
                while (ts_tree_cursor_goto_next_sibling(&cursor)) {
                    f(cursor);
                }
            }
        };

        /// Positions.
        std::vector<std::pair<usz, usz>> positions;

        /// Highlight keywords.
        walk([&](auto& cursor) {
            auto node = ts_tree_cursor_current_node(&cursor);
            auto type = ts_node_type(node);
            if (type != "primitive_type"sv) return;

            auto start = ts_node_start_byte(node);
            auto end = ts_node_end_byte(node);
            auto len = end - start;

            positions.emplace_back(start, len);
        });

        /// Print positions.
        for (auto [start, len] : positions) {
           fmt::print("Keyword at {} with length {}\n", start, len);
        }

        std::exit(0);
    }

    /// Otherwise, use the built-in highlighters.
    else
#endif
    {
        if (lang == "C++") highlight_cxx(text);
        else if (lang == "Go") highlight_go(text);
        else if (lang == "C") highlight_c(text);
        else if (lang == "int") highlight_intercept(text);
        else die("Unknown language '{}'", lang);
    }

echo:
    std::fwrite(text.data(), 1, text.size(), stdout);
}
