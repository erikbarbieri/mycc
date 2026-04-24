#pragma once

#include <string>

constexpr int MAX_TOKENS = 10000;
constexpr int MAX_SYMBOLS = 2000;
constexpr int MAX_QUADS = 4000;

inline constexpr int WORD_SIZE_BYTES = 2;

enum TokenType {
    TOK_ERROR = 0,
    TOK_IDENTIFIER,
    TOK_INTEGER,

    TOK_COMMA,
    TOK_SEMICOLON,

    TOK_ADD_OP,
    TOK_SUB_OP,
    TOK_MUL_OP,
    TOK_DIV_OP,

    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LCURLY,
    TOK_RCURLY,
    TOK_LBRACKET,
    TOK_RBRACKET,

    TOK_ASSIGNMENT,
    TOK_EQUAL_TO,
    TOK_NOT_EQUAL_TO,
    TOK_LESS_THAN,
    TOK_LESS_THAN_OR_EQUAL_TO,
    TOK_GREATER_THAN,
    TOK_GREATER_THAN_OR_EQUAL_TO,

    TOK_CONST,
    TOK_IF,
    TOK_VAR,
    TOK_THEN,
    TOK_ELSE,
    TOK_PROCEDURE,
    TOK_WHILE,
    TOK_CALL,
    TOK_DO,
    TOK_ODD,
    TOK_CLASS,

    TOK_EOF
};

enum SegmentType { SEG_NONE = 0, SEG_CS, SEG_DS };

enum SymbolClassification {
    SYM_NONE = 0,
    SYM_PROGRAM_NAME,
    SYM_CONST,
    SYM_VAR,
    SYM_ARRAY,
    SYM_NUM_LITERAL,
    SYM_TEMP,
    SYM_PROCEDURE
};

struct Token {
    TokenType type;
    std::string lexeme;
    int line;
};

struct SymbolEntry {
    int number;
    std::string symbol;
    SymbolClassification classification;
    int value;
    int address;
    SegmentType segment;
};

struct Quad {
    int number;
    std::string op;
    std::string arg1;
    std::string arg2;
    std::string result;
};

// If only I knew Ada can automatically do this for you before choosing C++. Oh well!
inline const char* tokenTypeName(TokenType t) {
    switch (t) {
        case TOK_ERROR:
            return "Error";
        case TOK_IDENTIFIER:
            return "Identifier";
        case TOK_INTEGER:
            return "Integer";

        case TOK_COMMA:
            return "Comma";
        case TOK_SEMICOLON:
            return "Semicolon";

        case TOK_ADD_OP:
            return "AddOp";
        case TOK_SUB_OP:
            return "SubtractOp";
        case TOK_MUL_OP:
            return "MultiplyOp";
        case TOK_DIV_OP:
            return "DivideOp";

        case TOK_LPAREN:
            return "LeftParen";
        case TOK_RPAREN:
            return "RightParen";
        case TOK_LCURLY:
            return "LeftCurly";
        case TOK_RCURLY:
            return "RightCurly";
        case TOK_LBRACKET:
            return "LeftBracket";
        case TOK_RBRACKET:
            return "RightBracket";

        case TOK_ASSIGNMENT:
            return "Assignment";
        case TOK_EQUAL_TO:
            return "EqualToOp";
        case TOK_NOT_EQUAL_TO:
            return "NotEqualToOp";
        case TOK_LESS_THAN:
            return "LessThanOp";
        case TOK_LESS_THAN_OR_EQUAL_TO:
            return "LessThanOrEqualToOp";
        case TOK_GREATER_THAN:
            return "GreaterThanOp";
        case TOK_GREATER_THAN_OR_EQUAL_TO:
            return "GreaterThanOrEqualToOp";

        case TOK_CONST:
            return "CONST";
        case TOK_IF:
            return "IF";
        case TOK_VAR:
            return "VAR";
        case TOK_THEN:
            return "THEN";
        case TOK_ELSE:
            return "ELSE";
        case TOK_PROCEDURE:
            return "PROCEDURE";
        case TOK_WHILE:
            return "WHILE";
        case TOK_CALL:
            return "CALL";
        case TOK_DO:
            return "DO";
        case TOK_ODD:
            return "ODD";
        case TOK_CLASS:
            return "CLASS";

        case TOK_EOF:
            return "EOF";
        default:
            return "Unknown";
    }
}

inline bool tokenTypeFromName(const std::string& name, TokenType& outType) {
    if (name == "Error") {
        outType = TOK_ERROR;
        return true;
    }
    if (name == "Identifier") {
        outType = TOK_IDENTIFIER;
        return true;
    }
    if (name == "Integer") {
        outType = TOK_INTEGER;
        return true;
    }
    if (name == "Comma") {
        outType = TOK_COMMA;
        return true;
    }
    if (name == "Semicolon") {
        outType = TOK_SEMICOLON;
        return true;
    }
    if (name == "AddOp") {
        outType = TOK_ADD_OP;
        return true;
    }
    if (name == "SubtractOp") {
        outType = TOK_SUB_OP;
        return true;
    }
    if (name == "MultiplyOp") {
        outType = TOK_MUL_OP;
        return true;
    }
    if (name == "DivideOp") {
        outType = TOK_DIV_OP;
        return true;
    }
    if (name == "LeftParen") {
        outType = TOK_LPAREN;
        return true;
    }
    if (name == "RightParen") {
        outType = TOK_RPAREN;
        return true;
    }
    if (name == "LeftCurly") {
        outType = TOK_LCURLY;
        return true;
    }
    if (name == "RightCurly") {
        outType = TOK_RCURLY;
        return true;
    }
    if (name == "LeftBracket") {
        outType = TOK_LBRACKET;
        return true;
    }
    if (name == "RightBracket") {
        outType = TOK_RBRACKET;
        return true;
    }
    if (name == "Assignment") {
        outType = TOK_ASSIGNMENT;
        return true;
    }
    if (name == "EqualToOp") {
        outType = TOK_EQUAL_TO;
        return true;
    }
    if (name == "NotEqualToOp") {
        outType = TOK_NOT_EQUAL_TO;
        return true;
    }
    if (name == "LessThanOp") {
        outType = TOK_LESS_THAN;
        return true;
    }
    if (name == "LessThanOrEqualToOp") {
        outType = TOK_LESS_THAN_OR_EQUAL_TO;
        return true;
    }
    if (name == "GreaterThanOp") {
        outType = TOK_GREATER_THAN;
        return true;
    }
    if (name == "GreaterThanOrEqualToOp") {
        outType = TOK_GREATER_THAN_OR_EQUAL_TO;
        return true;
    }
    if (name == "CONST") {
        outType = TOK_CONST;
        return true;
    }
    if (name == "IF") {
        outType = TOK_IF;
        return true;
    }
    if (name == "VAR") {
        outType = TOK_VAR;
        return true;
    }
    if (name == "THEN") {
        outType = TOK_THEN;
        return true;
    }
    if (name == "ELSE") {
        outType = TOK_ELSE;
        return true;
    }
    if (name == "PROCEDURE") {
        outType = TOK_PROCEDURE;
        return true;
    }
    if (name == "WHILE") {
        outType = TOK_WHILE;
        return true;
    }
    if (name == "CALL") {
        outType = TOK_CALL;
        return true;
    }
    if (name == "DO") {
        outType = TOK_DO;
        return true;
    }
    if (name == "ODD") {
        outType = TOK_ODD;
        return true;
    }
    if (name == "CLASS") {
        outType = TOK_CLASS;
        return true;
    }
    if (name == "EOF") {
        outType = TOK_EOF;
        return true;
    }
    return false;
}

inline const char* segmentTypeName(SegmentType s) {
    switch (s) {
        case SEG_CS:
            return "CS";
        case SEG_DS:
            return "DS";
        default:
            return "NONE";
    }
}

inline bool segmentTypeFromName(const std::string& name, SegmentType& out) {
    if (name == "CS") {
        out = SEG_CS;
        return true;
    }
    if (name == "DS") {
        out = SEG_DS;
        return true;
    }
    if (name == "NONE") {
        out = SEG_NONE;
        return true;
    }
    return false;
}

// Again, wish I knew about Ada. "Your time is free :)" - Dr. Burris
inline const char* symbolClassificationName(SymbolClassification c) {
    switch (c) {
        case SYM_PROGRAM_NAME:
            return "ProgramName";
        case SYM_CONST:
            return "CONST";
        case SYM_VAR:
            return "VAR";
        case SYM_ARRAY:
            return "ARRAY";
        case SYM_NUM_LITERAL:
            return "NumLit";
        case SYM_TEMP:
            return "TEMP";
        case SYM_PROCEDURE:
            return "PROCEDURE";
        default:
            return "NONE";
    }
}

inline bool symbolClassificationFromName(const std::string& name, SymbolClassification& out) {
    if (name == "ProgramName") {
        out = SYM_PROGRAM_NAME;
        return true;
    }
    if (name == "CONST") {
        out = SYM_CONST;
        return true;
    }
    if (name == "VAR") {
        out = SYM_VAR;
        return true;
    }
    if (name == "ARRAY") {
        out = SYM_ARRAY;
        return true;
    }
    if (name == "NumLit") {
        out = SYM_NUM_LITERAL;
        return true;
    }
    if (name == "TEMP") {
        out = SYM_TEMP;
        return true;
    }
    if (name == "PROCEDURE") {
        out = SYM_PROCEDURE;
        return true;
    }
    if (name == "NONE") {
        out = SYM_NONE;
        return true;
    }
    return false;
}
