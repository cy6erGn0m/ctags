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
    { true, 't', "typealias", "typealiases" }
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
    keyword_other,

    modifier_private,
    modifier_protected,
    modifier_public,
    modifier_internal,
    modifier_sealed,
    modifier_enum,
    modifier_abstract
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
    token_other
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
    { "private", modifier_private },
    { "protected", modifier_protected },
    { "public", modifier_public },
    { "internal", modifier_internal },
    { "sealed", modifier_sealed },
    { "enum", modifier_enum },
    { "abstract", modifier_abstract }
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
        } else if (ch == '.') {
            vStringPut(buffer, ch);
            break;
        } else {
            ungetcToInputFile(ch);
            end = 1;
        }
    }

    if (end == 0) {
        while (true) {
            int ch = getcFromInputFile();
            if (ch >= '0' && ch <= '9') {
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


static void token(struct Token * token) {
    token->keyword = keyword_other;
    token->token = token_other;
    token->line_number = getInputLineNumber();
    token->file_position = getInputFilePosition();
    vStringClear(token->buffer);

    int first;
    while (true) {
        int ch = getcFromInputFile();
        if (ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t') continue;
        if (ch == EOF) return;
        first = ch;
        break;

        vStringPut(token->buffer, ch);
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
    }

    if (first >= '0' && first <= '9') {
        vStringPut(token->buffer, first);
        parse_number(token);
        return;
    }

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

static void skip_until_eol() {
    while (true) {
        int ch = getcFromInputFile();
        if (ch == EOF || ch == '\n') return;
    }
}

static void find_kotlin_tags() {
    struct Token t;
    t.buffer = vStringNew();

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

