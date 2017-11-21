/*
*   Copyright (c) 2017, Sergey Mashkov
*
*   This source code is released for free distribution under the terms of the
*   GNU General Public License version 2 or (at your option) any later version.
*/
#include "general.h"        /* must always come first */

#include "debug.h"
#include "entry.h"
#include "keyword.h"
#include "read.h"
#include "main.h"
#include "objpool.h"
#include "routines.h"
#include "vstring.h"
#include "options.h"
#include "xtag.h"

static int Lang_kotlin;

static kindDefinition KotlinKinds[] = {
	{ true, 'c', "class",  "classes" },
	{ true, 'f', "function", "functions" },
    { true, 't', "typealias", "typealiases" },
    { true, 'C', "const", "constants" }
};

enum DeclarationKind {
    kind_class,
    kind_function,
    kind_typealias,
    kind_const
};

enum KeywordType {
    keyword_package,
    keyword_import,
    keyword_class,
    keyword_interface,
    keyword_typealias,
    keyword_fun,
    keyword_val,
    keyword_var,
    keyword_object,
    keyword_other,

    modifier_private,
    modifier_protected,
    modifier_public,
    modifier_internal,
    modifier_sealed,
    modifier_enum,
    modifier_abstract,
    modifier_open,
    modifier_override,
    modifier_final,
    modifier_suspend,
    modifier_const
};

enum TokenType {
    token_keyword,
    token_identifier,
    token_string,
    token_number,
    token_par_open,
    token_par_close,
    token_curly_open,
    token_curly_close,
    token_square_open,
    token_square_close,
    token_arrow_open,
    token_arrow_close,
    token_sem,
    token_comma,
    token_dot,
    token_star,
    token_eof,
    token_comment_start,
    token_comment_end,
    token_comment_line,
    token_colon,
    token_bax,
    token_other,
        /* here we don't need to enumarate all keywords as we only need to parse declarations (top-level parsing) */
};

static const keywordTable KotlinKeywordTable[] = {
    { "package", keyword_package },
    { "import", keyword_import },
    { "class", keyword_class },
    { "interface", keyword_interface },
    { "typealias", keyword_typealias },
    { "fun", keyword_fun },
    { "val", keyword_val },
    { "var", keyword_var },
    { "object", keyword_object },
    { "private", modifier_private },
    { "protected", modifier_protected },
    { "public", modifier_public },
    { "internal", modifier_internal },
    { "sealed", modifier_sealed },
    { "enum", modifier_enum },
    { "abstract", modifier_abstract },
    { "open", modifier_open },
    { "override", modifier_override },
    { "final", modifier_final },
    { "suspend", modifier_suspend },
    { "const", modifier_const }
};

static int delimiters[128];

static void init_delimiters() {
    int i;
    for (i = 0; i < 128; ++i) delimiters[i] = 0;

    delimiters[' '] = 1;
    delimiters['\r'] = 1;
    delimiters['\n'] = 1;
    delimiters['\t'] = 1;
    delimiters[';'] = 1;
    delimiters[','] = 1;
    delimiters['('] = 1;
    delimiters[')'] = 1;
    delimiters['{'] = 1;
    delimiters['}'] = 1;
    delimiters['<'] = 1;
    delimiters['>'] = 1;
    delimiters['.'] = 1;
}

struct Token {
    int keyword;
    int token;

    unsigned long line_number;
    MIOPos file_position;

    vString* buffer;
};


static void parse_number(struct Token *const token) {
    int end = 0;
    vString * buffer = token->buffer;

    while (true) {
        int ch = getcFromInputFile();
        if (ch >= '0' && ch <= '9') {
            vStringPut(buffer, ch);
        } else if (ch == '_') {
            vStringPut(buffer, ch);
        } else if (ch == '.') {
            vStringPut(buffer, ch);
            break;
        } else {
            ungetcToInputFile(ch);
            end = 1;
            break;
        }
    }

    if (end == 0) {
        while (true) {
            int ch = getcFromInputFile();
            if (ch >= '0' && ch <= '9') {
                vStringPut(buffer, ch);
            } else if (ch == '_') {
                vStringPut(buffer, ch);
            } else if (ch == 'f') {
                vStringPut(buffer, ch);
                break;
            } else {
                ungetcToInputFile(ch);
                break;
            }
        }
    }

    token->token = token_number;
}


static void parse_token(struct Token * token) {
    token->keyword = keyword_other;
    token->token = token_other;
    token->line_number = getInputLineNumber();
    token->file_position = getInputFilePosition();
    vStringClear(token->buffer);

    int first;
    while (true) {
        int ch = getcFromInputFile();
        if (ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t') continue;
        if (ch == EOF) {
            token->token = token_eof;
            return;
        }

        first = ch;
        token->line_number = getInputLineNumber();
        token->file_position = getInputFilePosition();
        break;
    }

    switch (first) {
        case '(':
            token->token = token_par_open;
            return;
        case ')':
            token->token = token_par_close;
            return;
        case '{':
            token->token = token_curly_open;
            return;
        case '}':
            token->token = token_curly_close;
            return;
        case '[':
            token->token = token_square_open;
            return;
        case ']':
            token->token = token_square_close;
            return;
        case '.':
            token->token = token_dot;
            return;
        case ',':
            token->token = token_comma;
            return;
        case '<':
            token->token = token_arrow_open;
            return;
        case '>':
            token->token = token_arrow_close;
            return;
        case ';':
            token->token = token_sem;
            return;
        case ':':
            token->token = token_colon;
            return;
        case '$':
            token->token = token_bax;
            return;
    }

    if (first == '/') {
        int second = getcFromInputFile();
        if (second == '/') {
            token->token = token_comment_line;
            return;
        } else if (second == '*') {
            token->token = token_comment_start;
            return;
        } else {
            ungetcToInputFile(second);
            token->token = token_other;
            return;
        }
    }

    if (first >= '0' && first <= '9') {
        vStringPut(token->buffer, first);
        parse_number(token);
        return;
    }

    vStringPut(token->buffer, first);

    vString *const buffer = token->buffer;
    int* dd = delimiters;

    while (true) {
        int ch = getcFromInputFile();
        if (ch == EOF) break;
        if (ch >= 0 && ch < 128 && dd[ch] == 1) {
            ungetcToInputFile(ch);
            break;
        }
        vStringPut(buffer, ch);
    }

    token->keyword = lookupKeyword(vStringValue(buffer), Lang_kotlin);
    if (token->keyword == KEYWORD_NONE) {
        token->token = token_identifier;
    } else {
        token->token = token_keyword;
    }
}

static void skip_comment_block() {
    int c = 0;
    while (true) {
        int first = getcFromInputFile();

        if (first == '*' && c == 0) {
            int second = getcFromInputFile();
            if (second == '/') break;
            if (second == '*') c = 1;
            else c = 0;
        } else if (first == '/' && c == 1) break;
        else c = 0;
    }
}

static void skip_until_eol() {
    while (true) {
        int ch = getcFromInputFile();
        if (ch == EOF || ch == '\n') return;
    }
}

static void skip_open_close(int expected_close) {
    int curly = 0;
    int square = 0;
    int pars = 0;
    int arrows = 0;

    if (expected_close == ')') pars ++;
    else if (expected_close == '}') curly ++;
    else if (expected_close == ']') square ++;
    else if (expected_close == '>') arrows ++;
    // else TODO fail

    while (true) {
        int ch = getcFromInputFile();

        if (ch == EOF) return;
        if (ch == '(') pars ++;
        else if (ch == ')') pars --;
        else if (ch == '{') curly ++;
        else if (ch == '}') curly --;
        else if (ch == '[') square ++;
        else if (ch == ']') square --;
        else if (ch == '<') arrows ++;
        else if (ch == '>') arrows --;
        else if (ch == '/') {
            int next = getcFromInputFile();
            if (next == '/') skip_until_eol();
            else if (next == '*') skip_comment_block();
            // else discard character
        }
        // else discard character
        // TODO skip string literal

        if (ch == expected_close && pars == 0 &&
                square == 0 && curly == 0 && arrows == 0) {
            break;
        }
    }
}

static void make_tag_entry(struct Token * t, int kind) {
    tagEntryInfo e;
    initTagEntry(&e, vStringValue(t->buffer), kind);

    e.lineNumber = t->line_number;
    e.filePosition = t->file_position;

    makeTagEntry(&e);
}

static void find_kotlin_tags() {
    struct Token t;
    struct Token sub;

    t.buffer = vStringNew();
    sub.buffer = vStringNew();

    while (true) {
        parse_token(&t);

        if (t.token == token_eof) break;
        if (t.token == token_comment_start) {
            skip_comment_block();
        } else if (t.token == token_comment_line) {
            skip_until_eol();
        } else if (t.token == token_keyword) {
            // printf("keyword %s\n", vStringValue(t.buffer));
            if (t.keyword == modifier_public || t.keyword == modifier_internal ||
                    t.keyword == modifier_private || t.keyword == modifier_abstract ||
                    t.keyword == modifier_protected ||
                    t.keyword == modifier_open ||
                    t.keyword == modifier_final ||
                    t.keyword == modifier_override ||
                    t.keyword == modifier_sealed || 
                    t.keyword == modifier_suspend) {
            } else if (t.keyword == keyword_class || t.keyword == keyword_interface || t.keyword == keyword_object) {
                // class name
                parse_token(&t);

                if (t.token == token_identifier) {
                    make_tag_entry(&t, kind_class);
                }

                skip_until_eol();
            } else if (t.keyword == keyword_typealias) {
                // typealias A=real type ....
                
                parse_token(&t);
                
                if (t.token == token_identifier) {
                    make_tag_entry(&t, kind_typealias);
                }
            } else if (t.keyword == modifier_const) {
                // const val xx [:type] = initializer
                
                parse_token(&t);
                if (t.keyword == keyword_val) {
                    parse_token(&t);

                    if (t.token == token_identifier) {
                        make_tag_entry(&t, kind_const);
                    }
                }

                skip_until_eol();
            } else if (t.keyword == keyword_fun) {
                // angle, receiver or function name
                parse_token(&t);

                if (t.token == token_arrow_open) {
                    // type parameters list
                    skip_open_close('>');
                    parse_token(&t);
                }

                if (t.token == token_identifier) {
                    // receiver or function name

                    parse_token(&sub);

                    if (sub.token == token_dot) {
                        parse_token(&sub);
                        if (sub.token == token_identifier) {
                            make_tag_entry(&sub, kind_function);
                        }
                    } else if (sub.token == token_par_open) {
                        make_tag_entry(&t, kind_function);
                    } else {
                        // recover unexpected
                        skip_until_eol();
                    }
                } else {
                    // recover unexpected
                    skip_until_eol();
                }
            } else {
                skip_until_eol();
            }
        } else if (t.token == token_identifier) {
            // most likely yet another modifier, simply ignore it
        }
    }

    vStringDelete(t.buffer);
    vStringDelete(sub.buffer);
}

static void initialize (const langType language)
{
	Lang_kotlin = language;
}


static const char *const extensions [] = { "kt", "kts", NULL };

extern parserDefinition* KotlinParser(void) {
    init_delimiters();

	parserDefinition* def = parserNew("Kotlin");

	def->kindTable      = KotlinKinds;
	def->kindCount  = ARRAY_SIZE (KotlinKinds);
	def->extensions = extensions;
	def->parser     = find_kotlin_tags;
	def->useCork    = true; // TODO do I need it?

    def->keywordTable = KotlinKeywordTable;
    def->keywordCount = ARRAY_SIZE(KotlinKeywordTable);

    def->initialize = &initialize;

	return def;
}

