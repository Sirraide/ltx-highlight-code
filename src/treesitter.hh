#ifndef LTX_HIGHLIGHT_CODE_TREESITTER_HH
#define LTX_HIGHLIGHT_CODE_TREESITTER_HH

#ifdef ENABLE_TREE_SITTER
#    include <tree_sitter/api.h>

/// Parsers go here.
extern "C" {}

/// Wrapper around a tree sitter parser.
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

    static void tree_sitter_highlight(std::string& text, auto lang) {
        auto p = lang.second();

        /// Parse the input.
        p(text);

        /// Get the root note.
        auto root = ts_tree_root_node(p.tree);

        /// Create a query cursor.
        auto cur = ts_query_cursor_new();
        defer { ts_query_cursor_delete(cur); };
        if (options::get<"--debug">()) {
            auto s = ts_node_string(root);
            defer { std::free(s); };
            fmt::print(stderr, "Root node: {}\n", s);
        }

        /// Match results.
        struct tree_sitter_match_result {
            u32 pos;
            bool is_end_node;
            const char* name;
        };
        std::vector<tree_sitter_match_result> matches;

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

                    /// Add it to the list of matches.
                    matches.push_back({.pos = start, .is_end_node = false, .name = name});
                    matches.push_back({.pos = end, .is_end_node = true, .name = nullptr});
                }
            }

            /// Sort the matches in such a way that the matches with the greatest position come first.
            std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) { return a.pos > b.pos; });

            /// Iterate over all matches and apply highlighting.
            for (auto& m : matches) {
                if (m.is_end_node) text.insert(m.pos, END);
                else text.insert(m.pos, colour_string_prefix(lang.first.get().lang_name, m.name));
            }
        }
    }

    static parser parser_c() { die("Sorry, tree-sitter-c is not yet supported."); }
    static parser parser_go() { die("Sorry, tree-sitter-go is not yet supported."); }
    static parser parser_intercept() { die("Sorry, tree-sitter-intercept is not yet supported."); }
    static parser parser_cxx() { die("Sorry, tree-sitter-cpp is not yet supported."); }

private:
    void delete_this() {
        if (handle) {
            if (tree) ts_tree_delete(tree);
            for (auto q : queries) ts_query_delete(q);
            ts_parser_delete(handle);
        }
    }
};
#endif

#endif // LTX_HIGHLIGHT_CODE_TREESITTER_HH
