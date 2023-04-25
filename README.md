# LTX Highlight
This is a LaTeX package and program for syntax highlighting of source code. It enables you to use
Markdown-style inline code and code blocks in your LaTeX documents.

This package requires a reasonably up-to-date version of XeLaTeX to function properly.

Make sure you pass `--enable-write18` to `xelatex` when compiling your document to 
enable syntax highlighting. This allows `xelatex` to run shell commands, which is
necessary since the highlighting is done by an external program.

## Build
To build this run,
```bash
cmake -B out && cmake --build out
```

This will generate a file `ltx-highlight-code` which is the main executable used by the package.

## Usage
To use the package, include it at the top of your document, like so
```latex
\usepackage{ltx-highlight-code}
```

### Inline code
To use inline code, surround the code with backticks, like so:
```latex
`std::to_string(42)`
```
Note that special characters such as `&` and `_` are escaped automatically.

To insert a backtick character, use two backticks: ` `` `. Note that this does not work inside of inline code. 

### Inline code in other commands
Code blocks and inline code are rather volatile in that passing them to other
commands (e.g. `\textit`, `\footnote`, `\section`) will probably break them. Using them
outside of regular text is best avoided.

Instead, you can use the `\MDPrepareCode` and `\MDInsertCode` commands which will prepare
inline code for insertion and then insert it later, respectively. For example, to insert
the code `std::to_string(42)` into a footnote, you would do something like this:
```latex
\MDPrepareCode{`std::to_string(42)`}
\footnote{To convert 42 to a string in C++, do \MDInsertCode.}
```

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
- C
- int (Intercept)
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

### Using LaTeX commands in code blocks.
Within code blocks, special LaTeX characters (e.g. `_`, ` \ `, ...) are treated as normal characters. This
means that if you want to use LaTeX commands inside your code blocks, you’ll have to
define replacements for ` \ `, `{`, and `}`.

To simplify this process, the package provides the commands below, which make it so
LaTeX treats the supplied character as either ` \ `, `{`, or `}` in code blocks—and only in
code blocks.
```latex
\MDSetEscapeCharacter{<character>}     %% Sets a replacement for \
\MDSetGroupBeginCharacter{<character>} %% Sets a replacement for {
\MDSetGroupEndCharacter{<character>}   %% Sets a replacement for }
```

So, for example, to include the equivalent of `\textit{foo}` in a code block, you would
have to do something like this:
````latex
\MDSetEscapeCharacter{⁂}
\MDSetGroupBeginCharacter{⟨}
\MDSetGroupEndCharacter{⟩}

```[C++]
⁂textit⟨foo⟩
```
````

### Commands
The package exposes the following commands for customisation:
```latex
%% Enable/disable syntax highlighting. This is permanent. Default: enabled.
\MDEnableHighlighting
\MDDisableHighlighting

%% Skip to insert before and after a code block. Default: \medskipamount.
\MDSetCodeBlockSkip{<skip>}

%% Skip to insert for an empty line in a code block. Default: \baselineskip.
\MDSetCodeBlockEmptyLineSkip{<skip>}

%% Skip that \parskip is set to inside code blocks. Default: \smallskipamount.
\MDSetCodeBlockLineSkip{<skip>}

%% The file name that is used for the temporary file that is generated 
%% during compilation. Default: temporary.tex.
%%
%% Note: The package will also generate a file with the same name but with
%% an additional .1 appended. You can delete both files at any time.
\MDSetTempFileName{<filename>}

%% The (path to the) executable that is used to highlight code blocks. 
%% Default: ltx-highlight-code
\MDSetHighlightExe{<executable>}

%% This command executes `<commands>` at the beginning of every code block in a fashion
%% similar to \AtBeginDocument.
\MDAtBeginCodeBlock { <commands> }

%% This command allows the next—and only the next—code block to be broken across pages.
\MDAllowBreak
```

### Compatibility
`` ` `` is made active and will be interpreted as the start of inline code or code block. This
*will* break constructs such as ``\char`~=13`` in the main document (though not in the preamble). 