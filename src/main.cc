#include <clopts.hh>
#include <queue>
#include <ranges>
#include <tables.hh>
#include <utility>
#include <utils.hh>

#if defined(__has_include)
#    if __has_include(<tree_sitter/api.h>)
#        define HAVE_TREE_SITTER

#        include <tree_sitter/api.h>

extern "C" {
extern const TSLanguage* tree_sitter_cpp(void);
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

struct match_result {
    usz pos{};
    usz len{};
    kind k{};
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

        if (last_match_end != std::string::npos)
            matches.push_back({last_match_end - current->depth + 1, current->depth, last_kind});
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

auto trie_match(std::string& text, const tables::highlight_params& params) -> std::vector<match_result> {
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
    return tr.match(text);
}

/// Highlight code in a string.
void highlight(std::string& text, const tables::highlight_params& params, const std::vector<match_result>& matches) {
    auto print_matches = [&](auto it) {
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

void trim(std::string& s) {
    while (not s.empty() && isspace(s.back())) s.pop_back();
    while (not s.empty() && isspace(s.front())) s.erase(s.begin());
}

struct parser {
    TSParser* handle = nullptr;
    const TSLanguage* lang = nullptr;
    TSTree* tree = nullptr;
    std::vector<TSQuery*> queries;

    /// Create a new parser.
    explicit parser(const TSLanguage* language)
        : lang(language) {
        handle = ts_parser_new();
        ts_parser_set_language(handle, lang);
    }

    /// Copying makes no sense.
    parser(const parser&) = delete;
    parser& operator=(const parser&) = delete;

    /// Move a parser.
    parser(parser&& other)
        : handle(other.handle)
        , lang(std::exchange(other.lang, nullptr))
        , tree(std::exchange(other.tree, nullptr))
        , queries(std::move(other.queries)) {}

    parser& operator=(parser&& other) {
        if (this == std::addressof(other)) return *this;
        delete_this();
        handle = std::exchange(other.handle, nullptr);
        lang = std::exchange(other.lang, nullptr);
        tree = std::exchange(other.tree, nullptr);
        queries = std::move(other.queries);
    }

    /// RAII go brrr.
    ~parser() { delete_this(); }

    /// Create a new query.
    void add_query(std::string_view text) {
        TSQueryError err{};
        u32 err_offset{};
        TSQuery* query = ts_query_new(lang, text.data(), u32(text.size()), &err_offset, &err);

        /// Add the query if creating it succeeded, and print an error otherwise.
        if (query) queries.push_back(query);
        else {
            fmt::print(stderr, "Error creating query (at offset {}): {}\n", err_offset, std::to_underlying(err));
            fmt::print(stderr, "Offending query string:\n{}\n", text);
        }
    }

    /// Parse text.
    void operator()(std::string_view text) {
        tree = ts_parser_parse_string(handle, tree, text.data(), u32(text.size()));
    }

private:
    void delete_this() {
        if (handle) {
            if (tree) ts_tree_delete(tree);
            for (auto q : queries) ts_query_delete(q);
            ts_parser_delete(handle);
        }
    }
};

parser parser_c() { die("Sorry, tree-sitter-c is not yet supported."); }
parser parser_go() { die("Sorry, tree-sitter-go is not yet supported."); }
parser parser_intercept() { die("Sorry, tree-sitter-intercept is not yet supported."); }
parser parser_cxx() {
    parser p{tree_sitter_cpp()};

    /// Set queries.
    static constexpr std::string_view string_literal_query = "(string_literal) @string";
    p.add_query(string_literal_query);

    return p;
}

int main(int argc, char** argv) {
    options::parse(argc, argv);
    std::string lang_str = *options::get<"language">();
    std::string text = options::get<"input">()->contents;

    trim(text);
    std::transform(lang_str.begin(), lang_str.end(), lang_str.begin(), [](char c) { return std::tolower(c); });
    if (lang_str == "text") {
        std::fwrite(text.data(), 1, text.size(), stdout);
        return 0;
    }

    /// Supported languages.
    static const std::unordered_map<std::string_view, std::pair<std::reference_wrapper<const tables::highlight_params>, parser (*)()>> langs{
        {"c", {tables::c_params, parser_c}},
        {"c++", {tables::cxx_params, parser_cxx}},
        {"go", {tables::go_params, parser_go}},
        {"int", {tables::intercept_params, parser_intercept}},
        {"intercept", {tables::intercept_params, parser_intercept}},
    };

    /// Get the language.
    if (not langs.contains(lang_str)) die("Unsupported language '{}'", lang_str);
    auto& lang = langs.at(lang_str);

    /// Match results.
    std::vector<match_result> matches;

    /// Use tree-sitter if requested and present.
    if (options::get<"--tree-sitter">()) {
#ifdef HAVE_TREE_SITTER
        auto p = lang.second();

        /// Parse the input.
        p(text);

        /// Get the root note.
        auto root = ts_tree_root_node(p.tree);

        /// Create a query cursor.
        auto cur = ts_query_cursor_new();
        defer { ts_query_cursor_delete(cur); };

        /// Run it on each query.
        for (auto q : p.queries) {
            TSQueryMatch match;
            ts_query_cursor_exec(cur, q, root);
            while (ts_query_cursor_next_match(cur, &match)) {
                for (auto cap = match.captures; cap < match.captures + match.capture_count; ++cap) {
                    auto n = ts_node_string(cap->node);
                    defer { std::free(n); };

                    /// Get the name of the match.
                    u32 length{};
                    auto name = ts_query_capture_name_for_id(q, cap->index, &length);
                    if (options::get<"--debug">()) fmt::print(stderr, "Match: {} (\"{}\")\n", n, name);

                    /// Get the start and end of the node.
                    auto start = ts_node_start_byte(cap->node);
                    auto end = ts_node_end_byte(cap->node);

                    /// Determine kind from name.
                    if (name == "string"sv) {
                        matches.push_back({.pos = start, .len = 1, .k = kind::string});
                        matches.push_back({.pos = end - 1, .len = 1, .k = kind::string});
                    } else die("Unknown capture name '{}'", name);
                }
            }
        }
#else
        die("tree-sitter support not compiled in");
#endif
    }

    /// Otherwise, use our builtin parsers.
    else { matches = trie_match(text, lang.first); }

    /// Highlight the text and write it back out.
    highlight(text, lang.first, matches);
    std::fwrite(text.data(), 1, text.size(), stdout);
}
