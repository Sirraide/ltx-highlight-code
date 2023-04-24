# LTX Highlight
This is a LaTeX package and program for syntax highlighting of source code. It enables you to use
Markdown-style inline code and code blocks in your LaTeX documents.

Disclaimer: This package changes the default behaviour of LaTeX wrt the parsing of certain tokens
in the input. This may cause problems with other packages or definitions in the preamble. For this
reason, it is recommended to include this package last, after all other packages. See also below
for more information on this.

## Build
To build this run,
```bash
cmake -B out && cmake --build out
```

This will generate a file `ltx-highlight-code` which is the main executable used by the package.

## Usage
To use the package, include it at the top of your document, like so
```latex
\usepackage{md-code-blocks}
```

You can pass the `nohighlighting` option to the package to disable syntax highlighting:
```latex
\usepackage[nohighlighting]{md-code-blocks}
```

### Inline code
To use inline code, surround the code with backticks, like so:
```latex
`std::to_string(42)`
```
Note that special characters such as `&` and `_` are escaped automatically.

To insert a backtick character, use two backticks: ` `` `. Note that this does not work inside of inline code. 

### Code blocks
To use code blocks, surround the code with three backticks, followed by the language name inside 
square brackets like so:
````latex
```[C++]
int main() {
    return 0;
}
```
````

In code blocks, all whitespace—especially line-initial whitespace—as well as special characters are preserved.

The language name is case-sensitive. The following languages are currently supported:
- C++
- Go
- Text (plain text, but monospaced and with `_` and other special characters escaped)

### Syntax Highlighting
The colours used for syntax highlighting must be defined by the user using `\definecolor`. The colour names
are of the form: `MD<language><kind>Color`, where `<language>` is the language name and `<kind>` is one of
the following:
- `Keyword`
- `Type` (used for types that are not keyword; these are currently hard-coded; e.g. `T` in C++)
- `Operator`
- `String`
- `Comment`
- `Escape` (used for escape sequences in strings; e.g. `\n` in C++)

For example, to set the keyword colour for C++ keywords to bright blue, define
```latex
\definecolor{MDC++KeywordColor}{HTML}{0000FF}
```

Note that things will break if you don’t define all of the colours for a language.

### Variables
There are several variables that are exposed by this package and which you can override using
commands such as `\renewcommand`, `\def`, or `\let`. Note that e.g. `\setlength` will not work.

These variables can only be overridden *after* the package has been loaded as it sets default
values for them. 

```latex
%% Skip to insert before and after a code block. Default: \medskipamount
\MDCodeBlockSkipAmount

%% Skip that \parskip is set to inside code blocks. Default: \smallskipamount
\MDCodeBlockLineSkipAmount

%% The file name that is used for the temporary file that is generated 
%% during compilation. Default: temporary.tex.
%%
%% Note: The package will also generate a file with the same name but with
%% an additional .1 appended. You can delete both files at any time.
\MDTempFileName

%% The (path to the) executable that is used to highlight code blocks. 
%% Default: ltx-highlight-code
\MDHighlightEXE
```

### Commands
Furthermore, the package exposes the following commands:
```latex
%% This command executes `<commands>` at the beginning of every code block in a fashion
%% similar to \AtBeginDocument.
\MDAtBeginCodeBlock { <commands> }

%% This command allows the next—and only the next—code block to be broken across pages.
\MDAllowBreak
```

### Compatibility
Currently, including this package means that, at the beginning of the document, i.e. *after* the
preamble, special characters such as `~`, `_`, and others lose their special meaning, which means
that if you want to use e.g. `_` for subscripts in math mode, you’ll have to define aliases for
those characters in the preamble. We hope to fix this in the future.

Furthermore, \` is made active and will be interpreted as the start of inline code or code block. This
*will* break constructs such as ``\char`~=13`` if they occur after the package has been loaded. As such,
any such definitions should be moved to before the package is loaded.