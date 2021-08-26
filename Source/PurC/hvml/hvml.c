/*
 * @file hvml.c
 * @author Xu Xiaohong
 * @date 2021/08/23
 * @brief The implementation of public part for hvml parser.
 *
 * Copyright (C) 2021 FMSoft <https://www.fmsoft.cn>
 *
 * This file is a part of PurC (short for Purring Cat), an HVML interpreter.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "private/instance.h"
#include "private/errors.h"
#include "private/debug.h"
#include "private/utils.h"
#include "private/edom.h"
#include "private/hvml.h"
#include "config.h"

#if HAVE(GLIB)
#include <gmodule.h>
#else
#include <stdlib.h>
#endif

#if HAVE(GLIB)
#define    HVML_ALLOC(sz)   g_slice_alloc0(sz)
#define    HVML_FREE(p)     g_slice_free1(sizeof(*p), (gpointer)p)
#else
#define    HVML_ALLOC(sz)   calloc(1, sz)
#define    HVML_FREE(p)     free(p)
#endif

#if 1
#define PRINT_STATE(state_name)
#else
#define PRINT_STATE(state_name)                                             \
    fprintf(stderr, "in %s|wc=%c|hex=%x\n",                                 \
            pchvml_hvml_state_desc(state_name), hvml->wc, hvml->wc);
#endif

#define BEGIN_STATE(state_name)                                             \
    case state_name:                                                        \
    {                                                                       \
        enum hvml_state current_state = state_name;                        \
        UNUSED_PARAM(current_state);                                        \
        PRINT_STATE(current_state);

#define END_STATE()                                                         \
        break;                                                              \
    }

#define RECONSUME_IN(new_state)                                             \
    do {                                                                    \
        hvml->state = new_state;                                           \
        goto next_state;                                                    \
    } while (false)

#define RECONSUME_IN_NEXT(new_state)                                        \
    do {                                                                    \
        hvml->state = new_state;                                           \
        hvml->need_reconsume = true;                                       \
    } while (false)

#define ADVANCE_TO(new_state)                                               \
    do {                                                                    \
        hvml->state = new_state;                                           \
        goto next_input;                                                    \
    } while (false)

#define SWITCH_TO(new_state)                                                \
    do {                                                                    \
        hvml->state = new_state;                                           \
    } while (false)

#define STATE_DESC(state_name)                                              \
    case state_name:                                                        \
        return ""#state_name;                                               \

struct pchvml {
    enum hvml_state state;
    enum hvml_state return_state;
    uint32_t flags;
    size_t queue_size;
    char c[8];
    int c_len;
    wchar_t wc;
    bool need_reconsume;
};

static const char* hvml_err_msgs[] = {
};

static struct err_msg_seg _hvml_err_msgs_seg = {
    { NULL, NULL },
    PURC_ERROR_FIRST_HVML,
    PURC_ERROR_FIRST_HVML + PCA_TABLESIZE(hvml_err_msgs) - 1,
    hvml_err_msgs
};

static inline bool is_whitespace (wchar_t character)
{
    return character == ' ' || character == '\x0A' ||
        character == '\x09' || character == '\x0C';
}

static inline wchar_t to_ascii_lower_unchecked (wchar_t character)
{
        return character | 0x20;
}

static inline UNUSED_FUNCTION bool is_ascii (wchar_t character)
{
    return !(character & ~0x7F);
}

static inline UNUSED_FUNCTION bool is_ascii_lower (wchar_t character)
{
    return character >= 'a' && character <= 'z';
}

static inline UNUSED_FUNCTION bool is_ascii_upper (wchar_t character)
{
     return character >= 'A' && character <= 'Z';
}

static inline UNUSED_FUNCTION bool is_ascii_space (wchar_t character)
{
    return character <= ' ' &&
        (character == ' ' || (character <= 0xD && character >= 0x9));
}

static inline UNUSED_FUNCTION bool is_ascii_digit (wchar_t character)
{
    return character >= '0' && character <= '9';
}

static inline UNUSED_FUNCTION bool is_ascii_binary_digit (wchar_t character)
{
     return character == '0' || character == '1';
}

static inline UNUSED_FUNCTION bool is_ascii_hex_digit (wchar_t character)
{
     return is_ascii_digit(character) ||
         (to_ascii_lower_unchecked(character) >= 'a' &&
          to_ascii_lower_unchecked(character) <= 'f');
}

static inline UNUSED_FUNCTION bool is_ascii_octal_digit (wchar_t character)
{
     return character >= '0' && character <= '7';
}

static inline UNUSED_FUNCTION bool is_ascii_alpha (wchar_t character)
{
    return is_ascii_lower(to_ascii_lower_unchecked(character));
}

static inline UNUSED_FUNCTION bool is_ascii_alpha_numeric (wchar_t character)
{
    return is_ascii_digit(character) || is_ascii_alpha(character);
}


void pchvml_init_once(void)
{
    pcinst_register_error_message_segment(&_hvml_err_msgs_seg);
}

struct pchvml* pchvml_create(uint32_t flags, size_t queue_size)
{
    struct pchvml* parser = (struct pchvml*) HVML_ALLOC(
            sizeof(struct pchvml));
    parser->state = HVML_DATA_STATE;
    parser->flags = flags;
    parser->queue_size = queue_size;
    return parser;
}

void pchvml_reset(struct pchvml* parser, uint32_t flags,
        size_t queue_size)
{
    parser->state = HVML_DATA_STATE;
    parser->flags = flags;
    parser->queue_size = queue_size;
}

void pchvml_destroy(struct pchvml* parser)
{
    if (parser) {
        HVML_FREE(parser);
    }
}

const char* pchvml_hvml_state_desc (enum hvml_state state)
{
    switch (state) {
        STATE_DESC(HVML_DATA_STATE)
        STATE_DESC(HVML_RCDATA_STATE)
        STATE_DESC(HVML_RAWTEXT_STATE)
        STATE_DESC(HVML_PLAINTEXT_STATE)
        STATE_DESC(HVML_TAG_OPEN_STATE)
        STATE_DESC(HVML_END_TAG_OPEN_STATE)
        STATE_DESC(HVML_TAG_NAME_STATE)
        STATE_DESC(HVML_RCDATA_LESS_THAN_SIGN_STATE)
        STATE_DESC(HVML_RCDATA_END_TAG_OPEN_STATE)
        STATE_DESC(HVML_RCDATA_END_TAG_NAME_STATE)
        STATE_DESC(HVML_RAWTEXT_LESS_THAN_SIGN_STATE)
        STATE_DESC(HVML_RAWTEXT_END_TAG_OPEN_STATE)
        STATE_DESC(HVML_RAWTEXT_END_TAG_NAME_STATE)
        STATE_DESC(HVML_BEFORE_ATTRIBUTE_NAME_STATE)
        STATE_DESC(HVML_ATTRIBUTE_NAME_STATE)
        STATE_DESC(HVML_AFTER_ATTRIBUTE_NAME_STATE)
        STATE_DESC(HVML_BEFORE_ATTRIBUTE_VALUE_STATE)
        STATE_DESC(HVML_ATTRIBUTE_VALUE_DOUBLE_QUOTED_STATE)
        STATE_DESC(HVML_ATTRIBUTE_VALUE_SINGLE_QUOTED_STATE)
        STATE_DESC(HVML_ATTRIBUTE_VALUE_UNQUOTED_STATE)
        STATE_DESC(HVML_AFTER_ATTRIBUTE_VALUE_QUOTED_STATE)
        STATE_DESC(HVML_SELF_CLOSING_START_TAG_STATE)
        STATE_DESC(HVML_BOGUS_COMMENT_STATE)
        STATE_DESC(HVML_MARKUP_DECLARATION_OPEN_STATE)
        STATE_DESC(HVML_COMMENT_START_STATE)
        STATE_DESC(HVML_COMMENT_START_DASH_STATE)
        STATE_DESC(HVML_COMMENT_STATE)
        STATE_DESC(HVML_COMMENT_LESS_THAN_SIGN_STATE)
        STATE_DESC(HVML_COMMENT_LESS_THAN_SIGN_BANG_STATE)
        STATE_DESC(HVML_COMMENT_LESS_THAN_SIGN_BANG_DASH_STATE)
        STATE_DESC(HVML_COMMENT_LESS_THAN_SIGN_BANG_DASH_DASH_STATE)
        STATE_DESC(HVML_COMMENT_END_DASH_STATE)
        STATE_DESC(HVML_COMMENT_END_STATE)
        STATE_DESC(HVML_COMMENT_END_BANG_STATE)
        STATE_DESC(HVML_DOCTYPE_STATE)
        STATE_DESC(HVML_BEFORE_DOCTYPE_NAME_STATE)
        STATE_DESC(HVML_DOCTYPE_NAME_STATE)
        STATE_DESC(HVML_AFTER_DOCTYPE_NAME_STATE)
        STATE_DESC(HVML_AFTER_DOCTYPE_PUBLIC_KEYWORD_STATE)
        STATE_DESC(HVML_BEFORE_DOCTYPE_PUBLIC_IDENTIFIER_STATE)
        STATE_DESC(HVML_DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED_STATE)
        STATE_DESC(HVML_DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED_STATE)
        STATE_DESC(HVML_AFTER_DOCTYPE_PUBLIC_IDENTIFIER_STATE)
        STATE_DESC(HVML_BETWEEN_DOCTYPE_PUBLIC_IDENTIFIER_AND_SYSTEM_INFORMATION_STATE)
        STATE_DESC(HVML_AFTER_DOCTYPE_SYSTEM_KEYWORD_STATE)
        STATE_DESC(HVML_BEFORE_DOCTYPE_SYSTEM_INFORMATION_STATE)
        STATE_DESC(HVML_DOCTYPE_SYSTEM_INFORMATION_DOUBLE_QUOTED_STATE)
        STATE_DESC(HVML_DOCTYPE_SYSTEM_INFORMATION_SINGLE_QUOTED_STATE)
        STATE_DESC(HVML_AFTER_DOCTYPE_SYSTEM_INFORMATION_STATE)
        STATE_DESC(HVML_BOGUS_DOCTYPE_STATE)
        STATE_DESC(HVML_CDATA_SECTION_STATE)
        STATE_DESC(HVML_CDATA_SECTION_BRACKET_STATE)
        STATE_DESC(HVML_CDATA_SECTION_END_STATE)
        STATE_DESC(HVML_CHARACTER_REFERENCE_STATE)
        STATE_DESC(HVML_NAMED_CHARACTER_REFERENCE_STATE)
        STATE_DESC(HVML_AMBIGUOUS_AMPERSAND_STATE)
        STATE_DESC(HVML_NUMERIC_CHARACTER_REFERENCE_STATE)
        STATE_DESC(HVML_HEXADECIMAL_CHARACTER_REFERENCE_START_STATE)
        STATE_DESC(HVML_DECIMAL_CHARACTER_REFERENCE_START_STATE)
        STATE_DESC(HVML_HEXADECIMAL_CHARACTER_REFERENCE_STATE)
        STATE_DESC(HVML_DECIMAL_CHARACTER_REFERENCE_STATE)
        STATE_DESC(HVML_NUMERIC_CHARACTER_REFERENCE_END_STATE)
        STATE_DESC(HVML_SPECIAL_ATTRIBUTE_OPERATOR_IN_ATTRIBUTE_NAME_STATE)
        STATE_DESC(HVML_SPECIAL_ATTRIBUTE_OPERATOR_AFTER_ATTRIBUTE_NAME_STATE)
        STATE_DESC(HVML_EJSON_DATA_STATE)
        STATE_DESC(HVML_EJSON_FINISHED_STATE)
        STATE_DESC(HVML_EJSON_CONTROL_STATE)
        STATE_DESC(HVML_EJSON_LEFT_BRACE_STATE)
        STATE_DESC(HVML_EJSON_RIGHT_BRACE_STATE)
        STATE_DESC(HVML_EJSON_LEFT_BRACKET_STATE)
        STATE_DESC(HVML_EJSON_RIGHT_BRACKET_STATE)
        STATE_DESC(HVML_EJSON_LESS_THAN_SIGN_STATE)
        STATE_DESC(HVML_EJSON_GREATER_THAN_SIGN_STATE)
        STATE_DESC(HVML_EJSON_LEFT_PARENTHESIS_STATE)
        STATE_DESC(HVML_EJSON_RIGHT_PARENTHESIS_STATE)
        STATE_DESC(HVML_EJSON_DOLLAR_STATE)
        STATE_DESC(HVML_EJSON_AFTER_VALUE_STATE)
        STATE_DESC(HVML_EJSON_BEFORE_NAME_STATE)
        STATE_DESC(HVML_EJSON_AFTER_NAME_STATE)
        STATE_DESC(HVML_EJSON_NAME_UNQUOTED_STATE)
        STATE_DESC(HVML_EJSON_NAME_SINGLE_QUOTED_STATE)
        STATE_DESC(HVML_EJSON_NAME_DOUBLE_QUOTED_STATE)
        STATE_DESC(HVML_EJSON_VALUE_SINGLE_QUOTED_STATE)
        STATE_DESC(HVML_EJSON_VALUE_DOUBLE_QUOTED_STATE)
        STATE_DESC(HVML_EJSON_AFTER_VALUE_DOUBLE_QUOTED_STATE)
        STATE_DESC(HVML_EJSON_VALUE_TWO_DOUBLE_QUOTED_STATE)
        STATE_DESC(HVML_EJSON_VALUE_THREE_DOUBLE_QUOTED_STATE)
        STATE_DESC(HVML_EJSON_KEYWORD_STATE)
        STATE_DESC(HVML_EJSON_AFTER_KEYWORD_STATE)
        STATE_DESC(HVML_EJSON_BYTE_SEQUENCE_STATE)
        STATE_DESC(HVML_EJSON_AFTER_BYTE_SEQUENCE_STATE)
        STATE_DESC(HVML_EJSON_HEX_BYTE_SEQUENCE_STATE)
        STATE_DESC(HVML_EJSON_BINARY_BYTE_SEQUENCE_STATE)
        STATE_DESC(HVML_EJSON_BASE64_BYTE_SEQUENCE_STATE)
        STATE_DESC(HVML_EJSON_VALUE_NUMBER_STATE)
        STATE_DESC(HVML_EJSON_AFTER_VALUE_NUMBER_STATE)
        STATE_DESC(HVML_EJSON_VALUE_NUMBER_INTEGER_STATE)
        STATE_DESC(HVML_EJSON_VALUE_NUMBER_FRACTION_STATE)
        STATE_DESC(HVML_EJSON_VALUE_NUMBER_EXPONENT_STATE)
        STATE_DESC(HVML_EJSON_VALUE_NUMBER_EXPONENT_INTEGER_STATE)
        STATE_DESC(HVML_EJSON_VALUE_NUMBER_SUFFIX_INTEGER_STATE)
        STATE_DESC(HVML_EJSON_VALUE_NUMBER_INFINITY_STATE)
        STATE_DESC(HVML_EJSON_VALUE_NAN_STATE)
        STATE_DESC(HVML_EJSON_STRING_ESCAPE_STATE)
        STATE_DESC(HVML_EJSON_STRING_ESCAPE_FOUR_HEXADECIMAL_DIGITS_STATE)
        STATE_DESC(HVML_EJSON_JSONEE_VARIABLE_STATE)
        STATE_DESC(HVML_EJSON_JSONEE_FULL_STOP_SIGN_STATE)
        STATE_DESC(HVML_EJSON_JSONEE_KEYWORD_STATE)
        STATE_DESC(HVML_EJSON_JSONEE_STRING_STATE)
        STATE_DESC(HVML_EJSON_AFTER_JSONEE_STRING_STATE)
    }
    return NULL;
}

struct pchvml_token* pchvml_next_token (struct pchvml* hvml,
                                          purc_rwstream_t rws)
{
//next_input:
    if (!hvml->need_reconsume) {
        hvml->c_len = purc_rwstream_read_utf8_char (rws,
                hvml->c, &hvml->wc);
        if (hvml->c_len <= 0) {
            return NULL;
        }
    }
    hvml->need_reconsume = false;

//next_state:
    switch (hvml->state) {
        BEGIN_STATE(HVML_DATA_STATE)
        END_STATE()

        BEGIN_STATE(HVML_RCDATA_STATE)
        END_STATE()

        BEGIN_STATE(HVML_RAWTEXT_STATE)
        END_STATE()

        BEGIN_STATE(HVML_PLAINTEXT_STATE)
        END_STATE()

        BEGIN_STATE(HVML_TAG_OPEN_STATE)
        END_STATE()

        BEGIN_STATE(HVML_END_TAG_OPEN_STATE)
        END_STATE()

        BEGIN_STATE(HVML_TAG_NAME_STATE)
        END_STATE()

        BEGIN_STATE(HVML_RCDATA_LESS_THAN_SIGN_STATE)
        END_STATE()

        BEGIN_STATE(HVML_RCDATA_END_TAG_OPEN_STATE)
        END_STATE()

        BEGIN_STATE(HVML_RCDATA_END_TAG_NAME_STATE)
        END_STATE()

        BEGIN_STATE(HVML_RAWTEXT_LESS_THAN_SIGN_STATE)
        END_STATE()

        BEGIN_STATE(HVML_RAWTEXT_END_TAG_OPEN_STATE)
        END_STATE()

        BEGIN_STATE(HVML_RAWTEXT_END_TAG_NAME_STATE)
        END_STATE()

        BEGIN_STATE(HVML_BEFORE_ATTRIBUTE_NAME_STATE)
        END_STATE()

        BEGIN_STATE(HVML_ATTRIBUTE_NAME_STATE)
        END_STATE()

        BEGIN_STATE(HVML_AFTER_ATTRIBUTE_NAME_STATE)
        END_STATE()

        BEGIN_STATE(HVML_BEFORE_ATTRIBUTE_VALUE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_ATTRIBUTE_VALUE_DOUBLE_QUOTED_STATE)
        END_STATE()

        BEGIN_STATE(HVML_ATTRIBUTE_VALUE_SINGLE_QUOTED_STATE)
        END_STATE()

        BEGIN_STATE(HVML_ATTRIBUTE_VALUE_UNQUOTED_STATE)
        END_STATE()

        BEGIN_STATE(HVML_AFTER_ATTRIBUTE_VALUE_QUOTED_STATE)
        END_STATE()

        BEGIN_STATE(HVML_SELF_CLOSING_START_TAG_STATE)
        END_STATE()

        BEGIN_STATE(HVML_BOGUS_COMMENT_STATE)
        END_STATE()

        BEGIN_STATE(HVML_MARKUP_DECLARATION_OPEN_STATE)
        END_STATE()

        BEGIN_STATE(HVML_COMMENT_START_STATE)
        END_STATE()

        BEGIN_STATE(HVML_COMMENT_START_DASH_STATE)
        END_STATE()

        BEGIN_STATE(HVML_COMMENT_STATE)
        END_STATE()

        BEGIN_STATE(HVML_COMMENT_LESS_THAN_SIGN_STATE)
        END_STATE()

        BEGIN_STATE(HVML_COMMENT_LESS_THAN_SIGN_BANG_STATE)
        END_STATE()

        BEGIN_STATE(HVML_COMMENT_LESS_THAN_SIGN_BANG_DASH_STATE)
        END_STATE()

        BEGIN_STATE(HVML_COMMENT_LESS_THAN_SIGN_BANG_DASH_DASH_STATE)
        END_STATE()

        BEGIN_STATE(HVML_COMMENT_END_DASH_STATE)
        END_STATE()

        BEGIN_STATE(HVML_COMMENT_END_STATE)
        END_STATE()

        BEGIN_STATE(HVML_COMMENT_END_BANG_STATE)
        END_STATE()

        BEGIN_STATE(HVML_DOCTYPE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_BEFORE_DOCTYPE_NAME_STATE)
        END_STATE()

        BEGIN_STATE(HVML_DOCTYPE_NAME_STATE)
        END_STATE()

        BEGIN_STATE(HVML_AFTER_DOCTYPE_NAME_STATE)
        END_STATE()

        BEGIN_STATE(HVML_AFTER_DOCTYPE_PUBLIC_KEYWORD_STATE)
        END_STATE()

        BEGIN_STATE(HVML_BEFORE_DOCTYPE_PUBLIC_IDENTIFIER_STATE)
        END_STATE()

        BEGIN_STATE(HVML_DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED_STATE)
        END_STATE()

        BEGIN_STATE(HVML_DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED_STATE)
        END_STATE()

        BEGIN_STATE(HVML_AFTER_DOCTYPE_PUBLIC_IDENTIFIER_STATE)
        END_STATE()

        BEGIN_STATE(HVML_BETWEEN_DOCTYPE_PUBLIC_IDENTIFIER_AND_SYSTEM_INFORMATION_STATE)
        END_STATE()

        BEGIN_STATE(HVML_AFTER_DOCTYPE_SYSTEM_KEYWORD_STATE)
        END_STATE()

        BEGIN_STATE(HVML_BEFORE_DOCTYPE_SYSTEM_INFORMATION_STATE)
        END_STATE()

        BEGIN_STATE(HVML_DOCTYPE_SYSTEM_INFORMATION_DOUBLE_QUOTED_STATE)
        END_STATE()

        BEGIN_STATE(HVML_DOCTYPE_SYSTEM_INFORMATION_SINGLE_QUOTED_STATE)
        END_STATE()

        BEGIN_STATE(HVML_AFTER_DOCTYPE_SYSTEM_INFORMATION_STATE)
        END_STATE()

        BEGIN_STATE(HVML_BOGUS_DOCTYPE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_CDATA_SECTION_STATE)
        END_STATE()

        BEGIN_STATE(HVML_CDATA_SECTION_BRACKET_STATE)
        END_STATE()

        BEGIN_STATE(HVML_CDATA_SECTION_END_STATE)
        END_STATE()

        BEGIN_STATE(HVML_CHARACTER_REFERENCE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_NAMED_CHARACTER_REFERENCE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_AMBIGUOUS_AMPERSAND_STATE)
        END_STATE()

        BEGIN_STATE(HVML_NUMERIC_CHARACTER_REFERENCE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_HEXADECIMAL_CHARACTER_REFERENCE_START_STATE)
        END_STATE()

        BEGIN_STATE(HVML_DECIMAL_CHARACTER_REFERENCE_START_STATE)
        END_STATE()

        BEGIN_STATE(HVML_HEXADECIMAL_CHARACTER_REFERENCE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_DECIMAL_CHARACTER_REFERENCE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_NUMERIC_CHARACTER_REFERENCE_END_STATE)
        END_STATE()

        BEGIN_STATE(HVML_SPECIAL_ATTRIBUTE_OPERATOR_IN_ATTRIBUTE_NAME_STATE)
        END_STATE()

        BEGIN_STATE(HVML_SPECIAL_ATTRIBUTE_OPERATOR_AFTER_ATTRIBUTE_NAME_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_DATA_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_FINISHED_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_CONTROL_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_LEFT_BRACE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_RIGHT_BRACE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_LEFT_BRACKET_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_RIGHT_BRACKET_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_LESS_THAN_SIGN_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_GREATER_THAN_SIGN_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_LEFT_PARENTHESIS_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_RIGHT_PARENTHESIS_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_DOLLAR_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_AFTER_VALUE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_BEFORE_NAME_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_AFTER_NAME_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_NAME_UNQUOTED_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_NAME_SINGLE_QUOTED_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_NAME_DOUBLE_QUOTED_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_VALUE_SINGLE_QUOTED_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_VALUE_DOUBLE_QUOTED_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_AFTER_VALUE_DOUBLE_QUOTED_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_VALUE_TWO_DOUBLE_QUOTED_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_VALUE_THREE_DOUBLE_QUOTED_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_KEYWORD_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_AFTER_KEYWORD_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_BYTE_SEQUENCE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_AFTER_BYTE_SEQUENCE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_HEX_BYTE_SEQUENCE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_BINARY_BYTE_SEQUENCE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_BASE64_BYTE_SEQUENCE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_VALUE_NUMBER_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_AFTER_VALUE_NUMBER_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_VALUE_NUMBER_INTEGER_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_VALUE_NUMBER_FRACTION_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_VALUE_NUMBER_EXPONENT_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_VALUE_NUMBER_EXPONENT_INTEGER_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_VALUE_NUMBER_SUFFIX_INTEGER_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_VALUE_NUMBER_INFINITY_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_VALUE_NAN_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_STRING_ESCAPE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_STRING_ESCAPE_FOUR_HEXADECIMAL_DIGITS_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_JSONEE_VARIABLE_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_JSONEE_FULL_STOP_SIGN_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_JSONEE_KEYWORD_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_JSONEE_STRING_STATE)
        END_STATE()

        BEGIN_STATE(HVML_EJSON_AFTER_JSONEE_STRING_STATE)
        END_STATE()

        default:
            break;
    }
    return NULL;
}

