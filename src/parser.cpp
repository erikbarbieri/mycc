#include "parser.hpp"

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static Token g_tokens[MAX_TOKENS];
static int g_tokenCount = 0;
static int g_currentTokenIndex = 0;

static SymbolEntry g_symbols[MAX_SYMBOLS];
static int g_symbolCount = 0;
static int g_nextTempNumber = 1;
static int g_nextDataAddress = 0;

static Quad g_quads[MAX_QUADS];
static int g_quadCount = 0;
static int g_parserErrorCount = 0;

static int g_nextLabelNumber = 1;
static int g_nextWhileNumber = 1;

struct Designator {
    bool isArray;
    std::string name;
    std::string indexPlace;
};

const Quad* getQuadList() {
    return g_quads;
}

int getQuadCount() {
    return g_quadCount;
}

int getParserErrorCount() {
    return g_parserErrorCount;
}

static void reportParserError(int line, const std::string& message) {
    std::cerr << "Parser error on line " << line << ": " << message << "\n";
    g_parserErrorCount++;
}

static bool loadTokenFile(const std::filesystem::path& tokenInputPath) {
    std::ifstream inFile(tokenInputPath);
    if (!inFile.is_open()) {
        std::cerr << "Failed to open token file: " << tokenInputPath << "\n";
        return false;
    }

    g_tokenCount = 0;
    std::string line;

    while (std::getline(inFile, line)) {
        if (line.empty()) {
            continue;
        }

        if (g_tokenCount >= MAX_TOKENS) {
            std::cerr << "Too many tokens in token file. Increase MAX_TOKENS.\n";
            return false;
        }

        std::istringstream iss(line);
        int tokenNumber = 0;
        int tokenLine = 0;
        std::string lexeme;
        std::string typeName;

        if (!(iss >> tokenNumber >> tokenLine >> lexeme >> typeName)) {
            std::cerr << "Malformed token file line: " << line << "\n";
            return false;
        }

        TokenType type;
        if (!tokenTypeFromName(typeName, type)) {
            std::cerr << "Unknown token type in token file: " << typeName << "\n";
            return false;
        }

        g_tokens[g_tokenCount].type = type;
        g_tokens[g_tokenCount].lexeme = lexeme;
        g_tokens[g_tokenCount].line = tokenLine;
        g_tokenCount++;
    }

    return true;
}

static bool loadSymbolFile(const std::filesystem::path& symbolInputPath) {
    std::ifstream inFile(symbolInputPath);
    if (!inFile.is_open()) {
        std::cerr << "Failed to open symbol file: " << symbolInputPath << "\n";
        return false;
    }

    g_symbolCount = 0;
    g_nextDataAddress = 0;
    g_nextTempNumber = 1;

    std::string line;
    bool skippedHeader = false;

    while (std::getline(inFile, line)) {
        if (line.empty()) {
            continue;
        }

        if (!skippedHeader) {
            skippedHeader = true;
            continue;
        }

        if (g_symbolCount >= MAX_SYMBOLS) {
            std::cerr << "Too many symbols in symbol file. Increase MAX_SYMBOLS.\n";
            return false;
        }

        std::istringstream iss(line);
        int number = 0;
        std::string symbol;
        std::string classificationName;
        int value = 0;
        int address = 0;
        std::string segmentName;

        if (!(iss >> number >> symbol >> classificationName >> value >> address >>
              segmentName)) {
            std::cerr << "Malformed symbol file line: " << line << "\n";
            return false;
        }

        SymbolClassification classification;
        SegmentType segment;

        if (!symbolClassificationFromName(classificationName, classification)) {
            std::cerr << "Unknown symbol classification: " << classificationName << "\n";
            return false;
        }

        if (!segmentTypeFromName(segmentName, segment)) {
            std::cerr << "Unknown segment type: " << segmentName << "\n";
            return false;
        }

        g_symbols[g_symbolCount].number = number;
        g_symbols[g_symbolCount].symbol = symbol;
        g_symbols[g_symbolCount].classification = classification;
        g_symbols[g_symbolCount].value = value;
        g_symbols[g_symbolCount].address = address;
        g_symbols[g_symbolCount].segment = segment;
        g_symbolCount++;

        int symbolBytes = WORD_SIZE_BYTES;
        if (classification == SYM_ARRAY) {
            symbolBytes = value * WORD_SIZE_BYTES;
        }

        if (segment == SEG_DS && address + symbolBytes > g_nextDataAddress) {
            g_nextDataAddress = address + symbolBytes;
        }

        if (classification == SYM_TEMP && symbol.size() > 1 && symbol[0] == 'T') {
            int n = std::stoi(symbol.substr(1));
            if (n >= g_nextTempNumber) {
                g_nextTempNumber = n + 1;
            }
        }
    }

    return true;
}

static bool writeSymbolFileWithTemps(const std::filesystem::path& symbolOutputPath) {
    std::ofstream outFile(symbolOutputPath);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open symbol output file: " << symbolOutputPath << "\n";
        return false;
    }

    outFile << "#\tSymbol\tClassification\tValue\tAddress\tSegment\n";

    for (int i = 0; i < g_symbolCount; i++) {
        outFile << g_symbols[i].number << '\t' << g_symbols[i].symbol << '\t'
                << symbolClassificationName(g_symbols[i].classification) << '\t'
                << g_symbols[i].value << '\t' << g_symbols[i].address << '\t'
                << segmentTypeName(g_symbols[i].segment) << '\n';
    }

    return true;
}

static bool writeQuadFile(const std::filesystem::path& quadOutputPath) {
    std::ofstream outFile(quadOutputPath);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open quad output file: " << quadOutputPath << "\n";
        return false;
    }

    outFile << "#\tOp\tArg1\tArg2\tResult\n";

    for (int i = 0; i < g_quadCount; i++) {
        outFile << g_quads[i].number << '\t' << g_quads[i].op << '\t' << g_quads[i].arg1 << '\t'
                << g_quads[i].arg2 << '\t' << g_quads[i].result << '\n';
    }

    return true;
}

static int findSymbolByNameLocal(const std::string& name) {
    for (int i = 0; i < g_symbolCount; i++) {
        if (g_symbols[i].symbol == name) {
            return i;
        }
    }
    return -1;
}

static bool appendSymbolLocal(const std::string& symbol, SymbolClassification classification,
                              int value, int address, SegmentType segment) {
    if (g_symbolCount >= MAX_SYMBOLS) {
        std::cerr << "Too many symbols. Increase MAX_SYMBOLS.\n";
        return false;
    }

    g_symbols[g_symbolCount].number = g_symbolCount + 1;
    g_symbols[g_symbolCount].symbol = symbol;
    g_symbols[g_symbolCount].classification = classification;
    g_symbols[g_symbolCount].value = value;
    g_symbols[g_symbolCount].address = address;
    g_symbols[g_symbolCount].segment = segment;
    g_symbolCount++;
    return true;
}

static std::string appendTempSymbolLocal() {
    std::string tempName = "T" + std::to_string(g_nextTempNumber);
    g_nextTempNumber++;

    if (!appendSymbolLocal(tempName, SYM_TEMP, 0, g_nextDataAddress, SEG_DS)) {
        return "";
    }

    g_nextDataAddress += WORD_SIZE_BYTES;
    return tempName;
}

static bool emitQuad(const std::string& op, const std::string& arg1, const std::string& arg2,
                     const std::string& result) {
    if (g_quadCount >= MAX_QUADS) {
        std::cerr << "Too many quads. Increase MAX_QUADS.\n";
        return false;
    }

    g_quads[g_quadCount].number = g_quadCount + 1;
    g_quads[g_quadCount].op = op;
    g_quads[g_quadCount].arg1 = arg1;
    g_quads[g_quadCount].arg2 = arg2;
    g_quads[g_quadCount].result = result;
    g_quadCount++;
    return true;
}

static std::string makeLabel() {
    std::string label = "L" + std::to_string(g_nextLabelNumber);
    g_nextLabelNumber++;
    return label;
}

static std::string makeWhileTopLabel() {
    std::string label = "W" + std::to_string(g_nextWhileNumber) + "_TOP";
    return label;
}

static std::string makeWhileExitLabel() {
    std::string label = "W" + std::to_string(g_nextWhileNumber) + "_EXIT";
    g_nextWhileNumber++;
    return label;
}

static const Token& currentToken() {
    if (g_currentTokenIndex < g_tokenCount) {
        return g_tokens[g_currentTokenIndex];
    }

    return g_tokens[g_tokenCount - 1];
}

static const Token& peekToken(int offset) {
    int idx = g_currentTokenIndex + offset;
    if (idx < g_tokenCount) {
        return g_tokens[idx];
    }
    return g_tokens[g_tokenCount - 1];
}

static bool isAt(TokenType type) {
    return currentToken().type == type;
}

static void advanceToken() {
    if (g_currentTokenIndex < g_tokenCount) {
        g_currentTokenIndex++;
    }
}

static bool expect(TokenType type, const std::string& message) {
    if (!isAt(type)) {
        reportParserError(currentToken().line,
                          message + ", found '" + currentToken().lexeme + "'");
        return false;
    }

    advanceToken();
    return true;
}

enum ParserSymbol {
    PS_IDENT = 0,
    PS_NUMBER,
    PS_PLUS,
    PS_MINUS,
    PS_TIMES,
    PS_DIVIDE,
    PS_ASSIGN,
    PS_REL_OP,
    PS_IF,
    PS_THEN,
    PS_ELSE,
    PS_WHILE,
    PS_DO,
    PS_ODD,
    PS_CALL,
    PS_PROCEDURE,
    PS_CLASS,
    PS_LPAREN,
    PS_RPAREN,
    PS_LBRACKET,
    PS_RBRACKET,
    PS_SEMI,
    PS_COMMA,
    PS_LBRACE,
    PS_RBRACE,
    PS_END,
    PS_COUNT,
    PS_INVALID = PS_COUNT
};

enum ParserAction { ACT_ERROR = 0, ACT_SHIFT, ACT_REDUCE, ACT_EQUAL, ACT_ACCEPT };

static const char* PARSER_SYMBOL_NAME[PS_COUNT] = {
    "identifier", "number", "+",     "-",  "*",   "/",    "=",         "relop", "IF",
    "THEN",       "ELSE",   "WHILE", "DO", "ODD", "CALL", "PROCEDURE", "CLASS", "(",
    ")",          "[",      "]",     ";",  ",",   "{",    "}",         "end"};

// Rows are the terminal on the PDA operator stack
// Columns are the incoming terminal from the scanner/token stream

//   '<' = ACT_SHIFT   : incoming terminal yields a larger handle, so push it
//   '>' = ACT_REDUCE  : the handle on the stack is complete; pop/reduce it
//   '=' = ACT_EQUAL   : matched pair or grammar delimiter, e.g. '(' with ')'
//   'A' = ACT_ACCEPT  : current expression/phrase is reduced at a legal stop
//   'X' = ACT_ERROR   : no legal precedence relation

// Programmer garbage is placed on the value stack, operators/delimiters drive this table
// Operator-precedence matrix
// The expression PDA keeps identifiers/literals on valueStack, not opStack
// Therefore PS_END, PS_LPAREN, and PS_LBRACKET must yield to every arithmetic
// operator that can follow the first operand in a phrase, including '*' and '/'!!!
// This bug had me pulling hair out
// Yes, I am allergic to /**/ style comments
static const char PDA_PRECEDENCE_TABLE[PS_COUNT][PS_COUNT] = {
    // Incoming columns:
    // id   num  +    -    *    /    =    rel  IF   THEN ELSE WHILE DO   ODD  CALL PROC CLASS (
    // )    [    ]    ;    ,    {    }    end
    {'X', 'X', '>', '>', '>', '>', '>', '>', 'X', '>', '>', 'X', '>', // PS_IDENT
     'X', 'X', 'X', 'X', 'X', '>', '=', '>', '>', '>', 'X', '>', '>'},
    {'X', 'X', '>', '>', '>', '>', '>', '>', 'X', '>', '>', 'X', '>', // PS_NUMBER
     'X', 'X', 'X', 'X', 'X', '>', 'X', '>', '>', '>', 'X', '>', '>'},
    {'<', '<', '>', '>', '<', '<', 'X', '>', 'X', '>', '>', 'X', '>', // PS_PLUS
     'X', 'X', 'X', 'X', '<', '>', 'X', '>', '>', '>', 'X', '>', '>'},
    {'<', '<', '>', '>', '<', '<', 'X', '>', 'X', '>', '>', 'X', '>', // PS_MINUS
     'X', 'X', 'X', 'X', '<', '>', 'X', '>', '>', '>', 'X', '>', '>'},
    {'<', '<', '>', '>', '>', '>', 'X', '>', 'X', '>', '>', 'X', '>', // PS_TIMES
     'X', 'X', 'X', 'X', '<', '>', 'X', '>', '>', '>', 'X', '>', '>'},
    {'<', '<', '>', '>', '>', '>', 'X', '>', 'X', '>', '>', 'X', '>', // PS_DIVIDE
     'X', 'X', 'X', 'X', '<', '>', 'X', '>', '>', '>', 'X', '>', '>'},
    {'<', '<', '<', '<', 'X', 'X', 'X', 'X', 'X', 'X', '>', 'X', 'X', // PS_ASSIGN
     '<', 'X', 'X', 'X', '<', 'X', 'X', 'X', '>', 'X', 'X', '>', '>'},
    {'<', '<', '<', '<', 'X', 'X', 'X', 'X', 'X', '>', '>', 'X', '>', // PS_REL_OP
     'X', 'X', 'X', 'X', '<', '>', 'X', 'X', '>', 'X', 'X', '>', '>'},
    {'<', '<', '<', '<', 'X', 'X', 'X', 'X', 'X', '=', 'X', 'X', 'X', // PS_IF
     '<', 'X', 'X', 'X', '<', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X'},
    {'<', 'X', 'X', 'X', 'X', 'X', 'X', 'X', '<', 'X', '>', '<', 'X', // PS_THEN
     'X', '<', 'X', 'X', 'X', 'X', 'X', 'X', '>', 'X', '<', '>', '>'},
    {'<', 'X', 'X', 'X', 'X', 'X', 'X', 'X', '<', 'X', 'X', '<', 'X', // PS_ELSE
     'X', '<', 'X', 'X', 'X', 'X', 'X', 'X', '>', 'X', '<', '>', '>'},
    {'<', '<', '<', '<', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', '=', // PS_WHILE
     '<', 'X', 'X', 'X', '<', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X'},
    {'<', 'X', 'X', 'X', 'X', 'X', 'X', 'X', '<', 'X', 'X', '<', 'X', // PS_DO
     'X', '<', 'X', 'X', 'X', 'X', 'X', 'X', '>', 'X', '<', '>', '>'},
    {'<', '<', '<', '<', 'X', 'X', 'X', 'X', 'X', '>', '>', 'X', '>', // PS_ODD
     'X', 'X', 'X', 'X', '<', '>', 'X', 'X', '>', 'X', 'X', '>', '>'},
    {'<', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', // PS_CALL
     'X', 'X', 'X', 'X', '<', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X'},
    {'<', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', // PS_PROCEDURE
     'X', 'X', 'X', 'X', '<', 'X', 'X', 'X', 'X', 'X', '<', 'X', 'X'},
    {'<', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', // PS_CLASS
     'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', '<', 'X', 'X'},
    {'<', '<', '<', '<', '<', '<', 'X', 'X', 'X', 'X', 'X', 'X', 'X', // PS_LPAREN
     'X', 'X', 'X', 'X', '<', '=', 'X', 'X', 'X', 'X', 'X', 'X', 'X'},
    {'X', 'X', '>', '>', '>', '>', 'X', '>', 'X', '>', '>', 'X', '>', // PS_RPAREN
     'X', 'X', 'X', 'X', 'X', '>', 'X', '>', '>', '>', 'X', '>', '>'},
    {'<', '<', '<', '<', '<', '<', 'X', 'X', 'X', 'X', 'X', 'X', 'X', // PS_LBRACKET
     'X', 'X', 'X', 'X', '<', 'X', 'X', '=', 'X', 'X', 'X', 'X', 'X'},
    {'X', 'X', '>', '>', '>', '>', '>', '>', 'X', '>', '>', 'X', '>', // PS_RBRACKET
     'X', 'X', 'X', 'X', 'X', '>', 'X', '>', '>', '>', 'X', '>', '>'},
    {'<', 'X', 'X', 'X', 'X', 'X', 'X', 'X', '<', 'X', 'X', '<', 'X', // PS_SEMI
     'X', '<', '<', 'X', 'X', 'X', 'X', 'X', 'X', 'X', '<', '>', '>'},
    {'<', '<', '<', '<', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', // PS_COMMA
     'X', 'X', 'X', 'X', '<', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X'},
    {'<', 'X', 'X', 'X', 'X', 'X', 'X', 'X', '<', 'X', 'X', '<', 'X', // PS_LBRACE
     'X', '<', '<', '<', 'X', 'X', 'X', 'X', 'X', 'X', '<', '=', 'X'},
    {'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', '>', 'X', 'X', // PS_RBRACE
     'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', '>', 'X', 'X', '>', '>'},
    {'<', '<', '<', '<', '<', '<', 'X', 'A', '<', 'A', 'A', '<', 'A', // PS_END
     'X', '<', '<', '<', '<', 'A', 'X', 'A', 'A', 'A', '<', 'A', 'A'},
};

static ParserSymbol parserSymbolFromToken(TokenType type) {
    switch (type) {
        case TOK_IDENTIFIER:
            return PS_IDENT;
        case TOK_INTEGER:
            return PS_NUMBER;
        case TOK_ADD_OP:
            return PS_PLUS;
        case TOK_SUB_OP:
            return PS_MINUS;
        case TOK_MUL_OP:
            return PS_TIMES;
        case TOK_DIV_OP:
            return PS_DIVIDE;
        case TOK_ASSIGNMENT:
            return PS_ASSIGN;
        case TOK_EQUAL_TO:
        case TOK_NOT_EQUAL_TO:
        case TOK_LESS_THAN:
        case TOK_LESS_THAN_OR_EQUAL_TO:
        case TOK_GREATER_THAN:
        case TOK_GREATER_THAN_OR_EQUAL_TO:
            return PS_REL_OP;
        case TOK_IF:
            return PS_IF;
        case TOK_THEN:
            return PS_THEN;
        case TOK_ELSE:
            return PS_ELSE;
        case TOK_WHILE:
            return PS_WHILE;
        case TOK_DO:
            return PS_DO;
        case TOK_ODD:
            return PS_ODD;
        case TOK_CALL:
            return PS_CALL;
        case TOK_PROCEDURE:
            return PS_PROCEDURE;
        case TOK_CLASS:
            return PS_CLASS;
        case TOK_LPAREN:
            return PS_LPAREN;
        case TOK_RPAREN:
            return PS_RPAREN;
        case TOK_LBRACKET:
            return PS_LBRACKET;
        case TOK_RBRACKET:
            return PS_RBRACKET;
        case TOK_SEMICOLON:
            return PS_SEMI;
        case TOK_COMMA:
            return PS_COMMA;
        case TOK_LCURLY:
            return PS_LBRACE;
        case TOK_RCURLY:
            return PS_RBRACE;
        case TOK_EOF:
            return PS_END;
        default:
            return PS_INVALID;
    }
}

static ParserAction parserActionFromRelation(char relation) {
    switch (relation) {
        case '<':
            return ACT_SHIFT;
        case '>':
            return ACT_REDUCE;
        case '=':
            return ACT_EQUAL;
        case 'A':
            return ACT_ACCEPT;
        default:
            return ACT_ERROR;
    }
}

static char parserRelationChar(ParserSymbol stackTop, ParserSymbol incoming) {
    if (stackTop < 0 || stackTop >= PS_COUNT || incoming < 0 || incoming >= PS_COUNT) {
        return 'X';
    }
    return PDA_PRECEDENCE_TABLE[stackTop][incoming];
}

static ParserAction parserTableAction(ParserSymbol stackTop, ParserSymbol incoming) {
    return parserActionFromRelation(parserRelationChar(stackTop, incoming));
}

static bool parserSymbolIsArithmeticOperator(ParserSymbol symbol) {
    return symbol == PS_PLUS || symbol == PS_MINUS || symbol == PS_TIMES || symbol == PS_DIVIDE;
}

static bool tokenIsInStopSet(TokenType type, const TokenType stopTypes[], int stopCount) {
    for (int i = 0; i < stopCount; i++) {
        if (type == stopTypes[i]) {
            return true;
        }
    }
    return false;
}

static bool reduceTopOperator(std::string valueStack[], int& valueTop, ParserSymbol opStack[],
                              int& opTop, int line) {
    if (opTop <= 0) {
        reportParserError(line, "operator stack underflow during reduction");
        return false;
    }

    ParserSymbol op = opStack[opTop];
    opTop--;

    if (valueTop < 1) {
        reportParserError(line, "missing operands during reduction");
        return false;
    }

    std::string right = valueStack[valueTop--];
    std::string left = valueStack[valueTop--];

    std::string tempName = appendTempSymbolLocal();
    if (tempName.empty()) {
        reportParserError(line, "failed to allocate temporary symbol");
        return false;
    }

    std::string opName;
    if (op == PS_PLUS)
        opName = "+";
    else if (op == PS_MINUS)
        opName = "-";
    else if (op == PS_TIMES)
        opName = "*";
    else if (op == PS_DIVIDE)
        opName = "/";
    else {
        reportParserError(line, "invalid operator during reduction");
        return false;
    }

    if (!emitQuad(opName, left, right, tempName)) {
        reportParserError(line, "failed to emit quad");
        return false;
    }

    valueStack[++valueTop] = tempName;
    return true;
}

static bool parseExpressionUntil(const TokenType stopTypes[], int stopCount,
                                 std::string& outPlace);

static bool parseArrayIndexExpression(std::string& indexPlace) {
    const TokenType stops[] = {TOK_RBRACKET};
    if (!parseExpressionUntil(stops, 1, indexPlace)) {
        return false;
    }

    if (!expect(TOK_RBRACKET, "expected ']' after subscript expression")) {
        return false;
    }

    return true;
}

static bool parseArrayReferenceFromCurrentToken(std::string& outPlace) {
    Token arrayTok = currentToken();
    int symbolIndex = findSymbolByNameLocal(arrayTok.lexeme);
    if (symbolIndex < 0) {
        reportParserError(arrayTok.line,
                          "undeclared identifier '" + arrayTok.lexeme + "' in expression");
        return false;
    }

    if (g_symbols[symbolIndex].classification != SYM_ARRAY) {
        reportParserError(arrayTok.line,
                          "identifier '" + arrayTok.lexeme + "' is not an array");
        return false;
    }

    std::string arrayName = arrayTok.lexeme;
    advanceToken();

    if (!expect(TOK_LBRACKET, "expected '[' after array name")) {
        return false;
    }

    std::string indexPlace;
    if (!parseArrayIndexExpression(indexPlace)) {
        return false;
    }

    std::string tempName = appendTempSymbolLocal();
    if (tempName.empty()) {
        reportParserError(arrayTok.line, "failed to allocate temporary symbol for array read");
        return false;
    }

    if (!emitQuad("LOADA", arrayName, indexPlace, tempName)) {
        reportParserError(arrayTok.line, "failed to emit array load quad");
        return false;
    }

    outPlace = tempName;
    return true;
}

// PDA core for expressions.
// 1) scan tokens left to right,
// 2) place programmer garbage on the value stack,
// 3) place operators/delimiters on the PDA operator stack,
// 4) reduce handles when the table says ACT_REDUCE,
// 5) stop when the table accepts a legal phrase terminator, Arnold!!
static bool parseExpressionUntil(const TokenType stopTypes[], int stopCount,
                                 std::string& outPlace) {
    ParserSymbol opStack[MAX_TOKENS];
    std::string valueStack[MAX_TOKENS];
    int opTop = 0;
    int valueTop = -1;
    bool expectOperand = true;

    opStack[0] = PS_END;

    while (true) {
        const Token& tok = currentToken();

        if (expectOperand) {
            if (tok.type == TOK_IDENTIFIER) {
                int symbolIndex = findSymbolByNameLocal(tok.lexeme);
                if (symbolIndex < 0) {
                    reportParserError(tok.line, "undeclared identifier '" + tok.lexeme +
                                                    "' in expression");
                    return false;
                }

                // Subscripted variable!! parse the bracketed expression, then LOADA
                if (peekToken(1).type == TOK_LBRACKET) {
                    std::string arrayPlace;
                    if (!parseArrayReferenceFromCurrentToken(arrayPlace)) {
                        return false;
                    }
                    valueStack[++valueTop] = arrayPlace;
                    expectOperand = false;
                    continue;
                }

                valueStack[++valueTop] = tok.lexeme;
                advanceToken();
                expectOperand = false;
                continue;
            }

            if (tok.type == TOK_INTEGER) {
                if (findSymbolByNameLocal(tok.lexeme) < 0) {
                    reportParserError(tok.line, "integer literal '" + tok.lexeme +
                                                    "' not found in symbol table");
                    return false;
                }

                valueStack[++valueTop] = tok.lexeme;
                advanceToken();
                expectOperand = false;
                continue;
            }

            if (tok.type == TOK_LPAREN) {
                opStack[++opTop] = PS_LPAREN;
                advanceToken();
                continue;
            }

            if (tok.type == TOK_ADD_OP) {
                advanceToken();
                continue;
            }

            if (tok.type == TOK_SUB_OP) {
                if (findSymbolByNameLocal("0") < 0) {
                    reportParserError(tok.line,
                                      "unary minus requires literal 0 in symbol table");
                    return false;
                }

                valueStack[++valueTop] = "0";
                ParserSymbol incoming = PS_MINUS;
                while (parserTableAction(opStack[opTop], incoming) == ACT_REDUCE) {
                    if (!reduceTopOperator(valueStack, valueTop, opStack, opTop, tok.line)) {
                        return false;
                    }
                }
                opStack[++opTop] = incoming;
                advanceToken();
                continue;
            }

            reportParserError(tok.line,
                              "expected identifier, integer, unary sign, or '(' in expression");
            return false;
        }

        ParserSymbol incoming;
        if (tokenIsInStopSet(tok.type, stopTypes, stopCount)) {
            incoming = parserSymbolFromToken(tok.type);
            if (incoming == PS_INVALID) {
                incoming = PS_END;
            }
        } else {
            incoming = parserSymbolFromToken(tok.type);
            if (incoming == PS_INVALID || (!parserSymbolIsArithmeticOperator(incoming) &&
                                           incoming != PS_LPAREN && incoming != PS_RPAREN)) {
                reportParserError(tok.line,
                                  "unexpected token '" + tok.lexeme + "' in expression");
                return false;
            }
        }

        ParserAction action = parserTableAction(opStack[opTop], incoming);

        if (action == ACT_SHIFT) {
            opStack[++opTop] = incoming;
            advanceToken();
            expectOperand = true;
            continue;
        }

        if (action == ACT_EQUAL) {
            if (opStack[opTop] == PS_LPAREN && incoming == PS_RPAREN) {
                opTop--;
                advanceToken();
                expectOperand = false;
                continue;
            }

            reportParserError(tok.line,
                              "unexpected equal-precedence pair in expression near '" +
                                  tok.lexeme + "'");
            return false;
        }

        if (action == ACT_ACCEPT) {
            break;
        }

        if (action == ACT_REDUCE) {
            if (!reduceTopOperator(valueStack, valueTop, opStack, opTop, tok.line)) {
                return false;
            }
            continue;
        }

        reportParserError(tok.line, std::string("bad PDA table relation: stack '") +
                                        PARSER_SYMBOL_NAME[opStack[opTop]] + "' vs incoming '" +
                                        tok.lexeme + "'");
        return false;
    }

    if (valueTop != 0) {
        reportParserError(currentToken().line, "expression did not reduce to a single value");
        return false;
    }

    outPlace = valueStack[0];
    return true;
}

static bool parseConstPart() {
    if (!isAt(TOK_CONST)) {
        return true;
    }

    advanceToken();

    while (true) {
        if (!expect(TOK_IDENTIFIER, "expected constant name")) {
            return false;
        }
        if (!expect(TOK_ASSIGNMENT, "expected '=' in const declaration")) {
            return false;
        }
        if (!expect(TOK_INTEGER, "expected integer in const declaration")) {
            return false;
        }

        if (isAt(TOK_COMMA)) {
            advanceToken();
            continue;
        }
        break;
    }

    return expect(TOK_SEMICOLON, "expected ';' after const declarations");
}

static bool parseVarPart() {
    if (!isAt(TOK_VAR)) {
        return true;
    }

    advanceToken();

    while (true) {
        if (!expect(TOK_IDENTIFIER, "expected variable name")) {
            return false;
        }

        if (isAt(TOK_LBRACKET)) {
            advanceToken();
            if (!expect(TOK_INTEGER, "expected integer array size inside '[' and ']'")) {
                return false;
            }
            if (!expect(TOK_RBRACKET, "expected ']' after array size")) {
                return false;
            }
        }

        if (isAt(TOK_COMMA)) {
            advanceToken();
            continue;
        }
        break;
    }

    return expect(TOK_SEMICOLON, "expected ';' after variable declarations");
}

static bool parseDeclarationParts() {
    while (isAt(TOK_CONST) || isAt(TOK_VAR)) {
        if (isAt(TOK_CONST)) {
            if (!parseConstPart()) {
                return false;
            }
        }

        if (isAt(TOK_VAR)) {
            if (!parseVarPart()) {
                return false;
            }
        }
    }

    return true;
}

static bool parseDesignator(Designator& out) {
    if (!isAt(TOK_IDENTIFIER)) {
        reportParserError(currentToken().line, "expected identifier at start of designator");
        return false;
    }

    Token idTok = currentToken();
    int idx = findSymbolByNameLocal(idTok.lexeme);
    if (idx < 0) {
        reportParserError(idTok.line, "identifier '" + idTok.lexeme + "' is undeclared");
        return false;
    }

    out.isArray = false;
    out.name = idTok.lexeme;
    out.indexPlace = "-";
    advanceToken();

    if (isAt(TOK_LBRACKET)) {
        if (g_symbols[idx].classification != SYM_ARRAY) {
            reportParserError(idTok.line, "identifier '" + idTok.lexeme + "' is not an array");
            return false;
        }

        out.isArray = true;
        advanceToken();
        if (!parseArrayIndexExpression(out.indexPlace)) {
            return false;
        }
    }

    return true;
}

static bool parseSimpleAssignment() {
    Designator lhs;
    if (!parseDesignator(lhs)) {
        return false;
    }

    int lhsIndex = findSymbolByNameLocal(lhs.name);
    if (lhsIndex < 0) {
        reportParserError(currentToken().line,
                          "assignment target '" + lhs.name + "' is undeclared");
        return false;
    }

    if (!lhs.isArray) {
        if (g_symbols[lhsIndex].classification != SYM_VAR &&
            g_symbols[lhsIndex].classification != SYM_TEMP) {
            reportParserError(currentToken().line,
                              "assignment target '" + lhs.name + "' is not a variable");
            return false;
        }
    }

    if (!expect(TOK_ASSIGNMENT, "expected '=' after assignment target")) {
        return false;
    }

    const TokenType exprStops[] = {TOK_SEMICOLON, TOK_RCURLY, TOK_EOF, TOK_ELSE};
    std::string exprPlace;
    if (!parseExpressionUntil(exprStops, 4, exprPlace)) {
        return false;
    }

    if (lhs.isArray) {
        return emitQuad("STOREA", lhs.name, lhs.indexPlace, exprPlace);
    }

    return emitQuad("=", lhs.name, exprPlace, "-");
}

static std::string toUpperCopyLocal(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); i++) {
        out[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[i])));
    }
    return out;
}

static bool isBuiltinProcedureNameLocal(const std::string& name) {
    const std::string upper = toUpperCopyLocal(name);
    return upper == "READINTEGER" || upper == "PRINT";
}

static bool parseCallStatement() {
    if (!expect(TOK_CALL, "expected CALL to start call statement")) {
        return false;
    }

    if (!isAt(TOK_IDENTIFIER)) {
        reportParserError(currentToken().line, "expected procedure name after CALL");
        return false;
    }

    const Token procTok = currentToken();
    const std::string procName = procTok.lexeme;
    const std::string procUpper = toUpperCopyLocal(procName);
    const bool isBuiltin = isBuiltinProcedureNameLocal(procName);

    if (!isBuiltin) {
        const int procIndex = findSymbolByNameLocal(procName);
        if (procIndex < 0 || g_symbols[procIndex].classification != SYM_PROCEDURE) {
            reportParserError(procTok.line,
                              "CALL target '" + procName + "' is not a known procedure");
            return false;
        }
    }

    advanceToken();

    if (!expect(TOK_LPAREN, "expected '(' after procedure name")) {
        return false;
    }

    std::string argName = "-";

    if (isBuiltin) {
        if (!isAt(TOK_RPAREN)) {
            Designator arg;
            if (!parseDesignator(arg)) {
                return false;
            }

            if (procUpper == "READINTEGER") {
                if (arg.isArray) {
                    std::string tempName = appendTempSymbolLocal();
                    if (tempName.empty()) {
                        reportParserError(procTok.line,
                                          "failed to allocate temporary for array input");
                        return false;
                    }
                    if (!emitQuad("CALL", procName, tempName, "-")) {
                        return false;
                    }
                    if (!emitQuad("STOREA", arg.name, arg.indexPlace, tempName)) {
                        return false;
                    }
                } else {
                    int argIndex = findSymbolByNameLocal(arg.name);
                    SymbolClassification c = g_symbols[argIndex].classification;
                    if (c != SYM_VAR && c != SYM_TEMP) {
                        reportParserError(procTok.line,
                                          "CALL ReadInteger target is not writable");
                        return false;
                    }
                    argName = arg.name;
                }
            } else if (procUpper == "PRINT") {
                if (arg.isArray) {
                    std::string tempName = appendTempSymbolLocal();
                    if (tempName.empty()) {
                        reportParserError(procTok.line,
                                          "failed to allocate temporary for array print");
                        return false;
                    }
                    if (!emitQuad("LOADA", arg.name, arg.indexPlace, tempName)) {
                        return false;
                    }
                    argName = tempName;
                } else {
                    argName = arg.name;
                }
            }

            if (isAt(TOK_COMMA)) {
                reportParserError(currentToken().line,
                                  "multiple CALL parameters are not supported");
                return false;
            }
        }
    } else {
        if (!isAt(TOK_RPAREN)) {
            reportParserError(
                currentToken().line,
                "user procedures currently support only empty parameter lists: CALL Name()");
            return false;
        }
    }

    if (!expect(TOK_RPAREN, "expected ')' to end CALL argument list")) {
        return false;
    }

    if (argName != "-" || !isBuiltin) {
        return emitQuad("CALL", procName, argName, "-");
    }

    return true;
}

static bool isRelOp(TokenType type) {
    return type == TOK_EQUAL_TO || type == TOK_NOT_EQUAL_TO || type == TOK_LESS_THAN ||
           type == TOK_LESS_THAN_OR_EQUAL_TO || type == TOK_GREATER_THAN ||
           type == TOK_GREATER_THAN_OR_EQUAL_TO;
}

static std::string falseBranchMnemonicForRelOp(TokenType type) {
    switch (type) {
        case TOK_EQUAL_TO:
            return "NE";
        case TOK_NOT_EQUAL_TO:
            return "EQ";
        case TOK_LESS_THAN:
            return "GE";
        case TOK_LESS_THAN_OR_EQUAL_TO:
            return "GT";
        case TOK_GREATER_THAN:
            return "LE";
        case TOK_GREATER_THAN_OR_EQUAL_TO:
            return "LT";
        default:
            return "??";
    }
}

static std::string relOpLexeme(TokenType type) {
    switch (type) {
        case TOK_EQUAL_TO:
            return "==";
        case TOK_NOT_EQUAL_TO:
            return "!=";
        case TOK_LESS_THAN:
            return "<";
        case TOK_LESS_THAN_OR_EQUAL_TO:
            return "<=";
        case TOK_GREATER_THAN:
            return ">";
        case TOK_GREATER_THAN_OR_EQUAL_TO:
            return ">=";
        default:
            return "??";
    }
}

static bool parseBooleanExpressionAndBranch(const std::string& controlOp,
                                            const std::string& falseLabel,
                                            TokenType stopKeyword) {
    if (isAt(TOK_ODD)) {
        advanceToken();

        const TokenType oddStops[] = {stopKeyword};
        std::string oddPlace;
        if (!parseExpressionUntil(oddStops, 1, oddPlace)) {
            return false;
        }

        if (!emitQuad("ODD", oddPlace, "-", "-")) {
            reportParserError(currentToken().line, "failed to emit ODD test quad");
            return false;
        }

        if (!emitQuad(controlOp, falseLabel, "NE", "-")) {
            reportParserError(currentToken().line, "failed to emit ODD control-flow quad");
            return false;
        }

        return true;
    }

    const TokenType leftStops[] = {TOK_EQUAL_TO,     TOK_NOT_EQUAL_TO,
                                   TOK_LESS_THAN,    TOK_LESS_THAN_OR_EQUAL_TO,
                                   TOK_GREATER_THAN, TOK_GREATER_THAN_OR_EQUAL_TO};

    std::string leftPlace;
    if (!parseExpressionUntil(leftStops, 6, leftPlace)) {
        return false;
    }

    TokenType relType = currentToken().type;
    if (!isRelOp(relType)) {
        reportParserError(currentToken().line,
                          "expected relational operator in boolean expression");
        return false;
    }

    std::string relLex = relOpLexeme(relType);
    std::string inverseMnemonic = falseBranchMnemonicForRelOp(relType);
    advanceToken();

    const TokenType rightStops[] = {stopKeyword};
    std::string rightPlace;
    if (!parseExpressionUntil(rightStops, 1, rightPlace)) {
        return false;
    }

    if (!emitQuad(relLex, leftPlace, rightPlace, "-")) {
        reportParserError(currentToken().line, "failed to emit relational quad");
        return false;
    }

    if (!emitQuad(controlOp, falseLabel, inverseMnemonic, "-")) {
        reportParserError(currentToken().line, "failed to emit control-flow fix-up quad");
        return false;
    }

    return true;
}

enum StatementShape {
    SHAPE_INVALID = 0,
    SHAPE_ASSIGN,
    SHAPE_COMPOUND,
    SHAPE_IF,
    SHAPE_WHILE,
    SHAPE_CALL
};

static const StatementShape STATEMENT_SHAPE_TABLE[TOK_EOF + 1] = {
    SHAPE_INVALID,  // error
    SHAPE_ASSIGN,   // identifier
    SHAPE_INVALID,  // integer
    SHAPE_INVALID,  // comma
    SHAPE_INVALID,  // semicolon
    SHAPE_INVALID,  // +
    SHAPE_INVALID,  // -
    SHAPE_INVALID,  // *
    SHAPE_INVALID,  // /
    SHAPE_INVALID,  // (
    SHAPE_INVALID,  // )
    SHAPE_COMPOUND, // {
    SHAPE_INVALID,  // }
    SHAPE_INVALID,  // [
    SHAPE_INVALID,  // ]
    SHAPE_INVALID,  // =
    SHAPE_INVALID,  // ==
    SHAPE_INVALID,  // !=
    SHAPE_INVALID,  // <
    SHAPE_INVALID,  // <=
    SHAPE_INVALID,  // >
    SHAPE_INVALID,  // >=
    SHAPE_INVALID,  // const
    SHAPE_IF,       // if
    SHAPE_INVALID,  // var
    SHAPE_INVALID,  // then
    SHAPE_INVALID,  // else
    SHAPE_INVALID,  // procedure
    SHAPE_WHILE,    // while
    SHAPE_CALL,     // call
    SHAPE_INVALID,  // do
    SHAPE_INVALID,  // odd
    SHAPE_INVALID,  // class
    SHAPE_INVALID   // eof
};

struct FixupEntry {
    std::string label;
    int quadNumber;
};

static FixupEntry g_fixupStack[MAX_TOKENS];
static int g_fixupTop = -1;

static FixupEntry g_whileTopStack[MAX_TOKENS];
static int g_whileTopTop = -1;

static bool pushFixupEntry(const std::string& label, int quadNumber) {
    if (g_fixupTop + 1 >= MAX_TOKENS) {
        reportParserError(currentToken().line, "fix-up stack overflow");
        return false;
    }

    g_fixupTop++;
    g_fixupStack[g_fixupTop].label = label;
    g_fixupStack[g_fixupTop].quadNumber = quadNumber;
    return true;
}

static bool popFixupEntry(FixupEntry& out) {
    if (g_fixupTop < 0) {
        reportParserError(currentToken().line, "fix-up stack underflow");
        return false;
    }

    out = g_fixupStack[g_fixupTop--];
    return true;
}

static bool pushWhileTopEntry(const std::string& label, int quadNumber) {
    if (g_whileTopTop + 1 >= MAX_TOKENS) {
        reportParserError(currentToken().line, "while-top stack overflow");
        return false;
    }

    g_whileTopTop++;
    g_whileTopStack[g_whileTopTop].label = label;
    g_whileTopStack[g_whileTopTop].quadNumber = quadNumber;
    return true;
}

static bool popWhileTopEntry(FixupEntry& out) {
    if (g_whileTopTop < 0) {
        reportParserError(currentToken().line, "while-top stack underflow");
        return false;
    }

    out = g_whileTopStack[g_whileTopTop--];
    return true;
}

enum ParserLookaheadClass {
    LOOK_INVALID = 0,
    LOOK_ASSIGN,
    LOOK_CALL,
    LOOK_COMPOUND,
    LOOK_IF,
    LOOK_WHILE,
    LOOK_SEMICOLON,
    LOOK_STOP,
    LOOK_ELSE,
    LOOK_COUNT
};

enum ParserNonterminal {
    NT_STATEMENT_LIST = 0,
    NT_STATEMENT_LIST_TAIL,
    NT_STATEMENT,
    NT_ELSE_PART,
    NT_COUNT
};

enum ParserSemanticAction {
    ACT_PARSE_ASSIGN = 0,
    ACT_PARSE_CALL,
    ACT_PARSE_IF_HEADER,
    ACT_CLOSE_IF_NO_ELSE,
    ACT_ELSE_PREFIX,
    ACT_CLOSE_IF_ELSE,
    ACT_PARSE_WHILE_HEADER,
    ACT_CLOSE_WHILE
};

enum ParserStackEntryKind { ENTRY_TERMINAL = 0, ENTRY_NONTERMINAL, ENTRY_ACTION };

struct ParserStackEntry {
    ParserStackEntryKind kind;
    int value;
};

enum ParserProduction {
    PROD_INVALID = 0,
    PROD_STATEMENT_LIST_TO_STATEMENT_TAIL,
    PROD_STATEMENT_LIST_TO_EPSILON,
    PROD_STATEMENT_LIST_TAIL_TO_SEMI_STATEMENT_TAIL,
    PROD_STATEMENT_LIST_TAIL_TO_EPSILON,
    PROD_STATEMENT_TO_ASSIGN,
    PROD_STATEMENT_TO_CALL,
    PROD_STATEMENT_TO_COMPOUND,
    PROD_STATEMENT_TO_IF,
    PROD_STATEMENT_TO_WHILE,
    PROD_ELSE_PART_TO_ELSE_STATEMENT,
    PROD_ELSE_PART_TO_EPSILON
};

static const ParserProduction PARSER_TABLE[NT_COUNT][LOOK_COUNT] = {
    // NT_STATEMENT_LIST
    {PROD_INVALID, PROD_STATEMENT_LIST_TO_STATEMENT_TAIL, PROD_STATEMENT_LIST_TO_STATEMENT_TAIL,
     PROD_STATEMENT_LIST_TO_STATEMENT_TAIL, PROD_STATEMENT_LIST_TO_STATEMENT_TAIL,
     PROD_STATEMENT_LIST_TO_STATEMENT_TAIL, PROD_INVALID, PROD_STATEMENT_LIST_TO_EPSILON,
     PROD_INVALID},

    // NT_STATEMENT_LIST_TAIL
    {PROD_INVALID, PROD_INVALID, PROD_INVALID, PROD_INVALID, PROD_INVALID, PROD_INVALID,
     PROD_STATEMENT_LIST_TAIL_TO_SEMI_STATEMENT_TAIL, PROD_STATEMENT_LIST_TAIL_TO_EPSILON,
     PROD_INVALID},

    // NT_STATEMENT
    {PROD_INVALID, PROD_STATEMENT_TO_ASSIGN, PROD_STATEMENT_TO_CALL, PROD_STATEMENT_TO_COMPOUND,
     PROD_STATEMENT_TO_IF, PROD_STATEMENT_TO_WHILE, PROD_INVALID, PROD_INVALID, PROD_INVALID},

    // NT_ELSE_PART
    {PROD_INVALID, PROD_ELSE_PART_TO_EPSILON, PROD_ELSE_PART_TO_EPSILON,
     PROD_ELSE_PART_TO_EPSILON, PROD_ELSE_PART_TO_EPSILON, PROD_ELSE_PART_TO_EPSILON,
     PROD_ELSE_PART_TO_EPSILON, PROD_ELSE_PART_TO_EPSILON, PROD_ELSE_PART_TO_ELSE_STATEMENT},
};

static ParserLookaheadClass parserLookaheadClassForToken(TokenType type, TokenType stopToken) {
    if (type == TOK_EOF || type == stopToken) {
        return LOOK_STOP;
    }

    if (type == TOK_SEMICOLON) {
        return LOOK_SEMICOLON;
    }

    if (type == TOK_ELSE) {
        return LOOK_ELSE;
    }

    if (type >= 0 && type <= TOK_EOF) {
        StatementShape shape = STATEMENT_SHAPE_TABLE[type];
        switch (shape) {
            case SHAPE_ASSIGN:
                return LOOK_ASSIGN;
            case SHAPE_CALL:
                return LOOK_CALL;
            case SHAPE_COMPOUND:
                return LOOK_COMPOUND;
            case SHAPE_IF:
                return LOOK_IF;
            case SHAPE_WHILE:
                return LOOK_WHILE;
            default:
                return LOOK_INVALID;
        }
    }

    return LOOK_INVALID;
}

static bool pushParserStackEntry(ParserStackEntry stack[], int& top, ParserStackEntryKind kind,
                                 int value) {
    if (top + 1 >= MAX_TOKENS) {
        reportParserError(currentToken().line, "parser stack overflow");
        return false;
    }

    top++;
    stack[top].kind = kind;
    stack[top].value = value;
    return true;
}

static bool expandProduction(ParserProduction production, ParserStackEntry stack[], int& top) {
    switch (production) {
        case PROD_STATEMENT_LIST_TO_STATEMENT_TAIL:
            return pushParserStackEntry(stack, top, ENTRY_NONTERMINAL,
                                        NT_STATEMENT_LIST_TAIL) &&
                   pushParserStackEntry(stack, top, ENTRY_NONTERMINAL, NT_STATEMENT);

        case PROD_STATEMENT_LIST_TO_EPSILON:
            return true;

        case PROD_STATEMENT_LIST_TAIL_TO_SEMI_STATEMENT_TAIL:
            return pushParserStackEntry(stack, top, ENTRY_NONTERMINAL,
                                        NT_STATEMENT_LIST_TAIL) &&
                   pushParserStackEntry(stack, top, ENTRY_NONTERMINAL, NT_STATEMENT) &&
                   pushParserStackEntry(stack, top, ENTRY_TERMINAL, TOK_SEMICOLON);

        case PROD_STATEMENT_LIST_TAIL_TO_EPSILON:
            return true;

        case PROD_STATEMENT_TO_ASSIGN:
            return pushParserStackEntry(stack, top, ENTRY_ACTION, ACT_PARSE_ASSIGN);

        case PROD_STATEMENT_TO_CALL:
            return pushParserStackEntry(stack, top, ENTRY_ACTION, ACT_PARSE_CALL);

        case PROD_STATEMENT_TO_COMPOUND:
            return pushParserStackEntry(stack, top, ENTRY_TERMINAL, TOK_RCURLY) &&
                   pushParserStackEntry(stack, top, ENTRY_NONTERMINAL, NT_STATEMENT_LIST) &&
                   pushParserStackEntry(stack, top, ENTRY_TERMINAL, TOK_LCURLY);

        case PROD_STATEMENT_TO_IF:
            return pushParserStackEntry(stack, top, ENTRY_NONTERMINAL, NT_ELSE_PART) &&
                   pushParserStackEntry(stack, top, ENTRY_NONTERMINAL, NT_STATEMENT) &&
                   pushParserStackEntry(stack, top, ENTRY_ACTION, ACT_PARSE_IF_HEADER);

        case PROD_STATEMENT_TO_WHILE:
            return pushParserStackEntry(stack, top, ENTRY_ACTION, ACT_CLOSE_WHILE) &&
                   pushParserStackEntry(stack, top, ENTRY_NONTERMINAL, NT_STATEMENT) &&
                   pushParserStackEntry(stack, top, ENTRY_ACTION, ACT_PARSE_WHILE_HEADER);

        case PROD_ELSE_PART_TO_ELSE_STATEMENT:
            return pushParserStackEntry(stack, top, ENTRY_ACTION, ACT_CLOSE_IF_ELSE) &&
                   pushParserStackEntry(stack, top, ENTRY_NONTERMINAL, NT_STATEMENT) &&
                   pushParserStackEntry(stack, top, ENTRY_ACTION, ACT_ELSE_PREFIX) &&
                   pushParserStackEntry(stack, top, ENTRY_TERMINAL, TOK_ELSE);

        case PROD_ELSE_PART_TO_EPSILON:
            return pushParserStackEntry(stack, top, ENTRY_ACTION, ACT_CLOSE_IF_NO_ELSE);

        default:
            reportParserError(currentToken().line, "invalid parser production");
            return false;
    }
}

static bool executeParserAction(ParserSemanticAction action) {
    if (action == ACT_PARSE_ASSIGN) {
        return parseSimpleAssignment();
    }

    if (action == ACT_PARSE_CALL) {
        return parseCallStatement();
    }

    if (action == ACT_PARSE_IF_HEADER) {
        if (!expect(TOK_IF, "expected IF to start if statement")) {
            return false;
        }

        const std::string falseLabel = makeLabel();
        if (!parseBooleanExpressionAndBranch("THEN", falseLabel, TOK_THEN)) {
            return false;
        }
        if (!expect(TOK_THEN, "expected THEN after boolean expression")) {
            return false;
        }

        return pushFixupEntry(falseLabel, g_quadCount);
    }

    if (action == ACT_CLOSE_IF_NO_ELSE) {
        FixupEntry falseFixup;
        if (!popFixupEntry(falseFixup)) {
            return false;
        }

        if (!emitQuad(falseFixup.label, "-", "-", "-")) {
            reportParserError(currentToken().line, "failed to emit IF false label");
            return false;
        }
        return true;
    }

    if (action == ACT_ELSE_PREFIX) {
        FixupEntry falseFixup;
        if (!popFixupEntry(falseFixup)) {
            return false;
        }

        const std::string endLabel = makeLabel();
        if (!emitQuad("JMP", endLabel, "-", "-")) {
            reportParserError(currentToken().line, "failed to emit jump over ELSE part");
            return false;
        }

        if (!emitQuad(falseFixup.label, "-", "-", "-")) {
            reportParserError(currentToken().line, "failed to emit ELSE entry label");
            return false;
        }

        return pushFixupEntry(endLabel, g_quadCount);
    }

    if (action == ACT_CLOSE_IF_ELSE) {
        FixupEntry endFixup;
        if (!popFixupEntry(endFixup)) {
            return false;
        }

        if (!emitQuad(endFixup.label, "-", "-", "-")) {
            reportParserError(currentToken().line, "failed to emit IF end label");
            return false;
        }
        return true;
    }

    if (action == ACT_PARSE_WHILE_HEADER) {
        if (!expect(TOK_WHILE, "expected WHILE to start while statement")) {
            return false;
        }

        const std::string whileTopLabel = makeWhileTopLabel();
        if (!emitQuad(whileTopLabel, "-", "-", "-")) {
            reportParserError(currentToken().line, "failed to emit WHILE top label");
            return false;
        }
        if (!pushWhileTopEntry(whileTopLabel, g_quadCount)) {
            return false;
        }

        const std::string whileExitLabel = makeWhileExitLabel();
        if (!parseBooleanExpressionAndBranch("DO", whileExitLabel, TOK_DO)) {
            return false;
        }
        if (!expect(TOK_DO, "expected DO after boolean expression")) {
            return false;
        }

        return pushFixupEntry(whileExitLabel, g_quadCount);
    }

    if (action == ACT_CLOSE_WHILE) {
        FixupEntry whileTop;
        FixupEntry whileExit;

        if (!popWhileTopEntry(whileTop)) {
            return false;
        }
        if (!popFixupEntry(whileExit)) {
            return false;
        }

        if (!emitQuad("JMP", whileTop.label, "-", "-")) {
            reportParserError(currentToken().line, "failed to emit WHILE back jump");
            return false;
        }
        if (!emitQuad(whileExit.label, "-", "-", "-")) {
            reportParserError(currentToken().line, "failed to emit WHILE exit label");
            return false;
        }
        return true;
    }

    reportParserError(currentToken().line, "unknown parser action");
    return false;
}

static bool parseStatementSequencePDA(TokenType stopToken) {
    ParserStackEntry parserStack[MAX_TOKENS];
    int parserTop = -1;

    if (!pushParserStackEntry(parserStack, parserTop, ENTRY_NONTERMINAL, NT_STATEMENT_LIST)) {
        return false;
    }

    while (parserTop >= 0) {
        ParserStackEntry entry = parserStack[parserTop--];

        if (entry.kind == ENTRY_TERMINAL) {
            TokenType expected = static_cast<TokenType>(entry.value);
            if (!expect(expected, std::string("expected '") + tokenTypeName(expected) + "'")) {
                return false;
            }
            continue;
        }

        if (entry.kind == ENTRY_ACTION) {
            if (!executeParserAction(static_cast<ParserSemanticAction>(entry.value))) {
                return false;
            }
            continue;
        }

        ParserLookaheadClass look =
            parserLookaheadClassForToken(currentToken().type, stopToken);
        ParserProduction production =
            PARSER_TABLE[static_cast<ParserNonterminal>(entry.value)][look];

        if (production == PROD_INVALID) {
            reportParserError(currentToken().line, "no parser-table entry for token '" +
                                                       currentToken().lexeme + "'");
            return false;
        }

        if (!expandProduction(production, parserStack, parserTop)) {
            return false;
        }
    }

    return true;
}

static bool parseProcedureDefinition();

static bool parseProcedureParts() {
    while (isAt(TOK_PROCEDURE)) {
        if (!parseProcedureDefinition()) {
            return false;
        }
    }
    return true;
}

static bool parseProcedureDefinition() {
    if (!expect(TOK_PROCEDURE, "expected PROCEDURE")) {
        return false;
    }

    if (!isAt(TOK_IDENTIFIER)) {
        reportParserError(currentToken().line, "expected procedure name after PROCEDURE");
        return false;
    }

    const std::string procName = currentToken().lexeme;
    advanceToken();

    if (!expect(TOK_LPAREN, "expected '(' after procedure name")) {
        return false;
    }
    if (!expect(TOK_RPAREN, "expected ')' after procedure name")) {
        return false;
    }
    if (!expect(TOK_LCURLY, "expected '{' to start procedure body")) {
        return false;
    }

    if (!emitQuad("PROC", procName, "-", "-")) {
        reportParserError(currentToken().line, "failed to emit procedure start quad");
        return false;
    }

    if (!parseDeclarationParts()) {
        return false;
    }

    if (!parseStatementSequencePDA(TOK_RCURLY)) {
        return false;
    }

    if (!expect(TOK_RCURLY, "expected '}' to end procedure body")) {
        return false;
    }

    if (!emitQuad("RET", "-", "-", "-")) {
        reportParserError(currentToken().line, "failed to emit procedure return quad");
        return false;
    }

    return true;
}

static bool parseProgram() {
    if (!expect(TOK_CLASS, "expected CLASS to start program")) {
        return false;
    }
    if (!expect(TOK_IDENTIFIER, "expected program name after CLASS")) {
        return false;
    }
    if (!expect(TOK_LCURLY, "expected '{' after program name")) {
        return false;
    }

    if (!parseDeclarationParts()) {
        return false;
    }
    if (!parseProcedureParts()) {
        return false;
    }

    if (!emitQuad("START", "-", "-", "-")) {
        reportParserError(currentToken().line, "failed to emit START quad");
        return false;
    }

    if (!parseStatementSequencePDA(TOK_RCURLY)) {
        return false;
    }

    if (!expect(TOK_RCURLY, "expected '}' at end of program")) {
        return false;
    }
    if (!expect(TOK_EOF, "expected EOF after program")) {
        return false;
    }
    return true;
}

// Parser??? I hardly know her!!!
bool buildQuadsFromTokenAndSymbolFiles(const std::filesystem::path& tokenInputPath,
                                       const std::filesystem::path& symbolInputPath,
                                       const std::filesystem::path& quadOutputPath,
                                       const std::filesystem::path& symbolWithTempsOutputPath) {
    g_parserErrorCount = 0;
    g_quadCount = 0;
    g_currentTokenIndex = 0;
    g_nextLabelNumber = 1;
    g_nextWhileNumber = 1;
    g_fixupTop = -1;
    g_whileTopTop = -1;

    if (!loadTokenFile(tokenInputPath)) {
        return false;
    }

    if (!loadSymbolFile(symbolInputPath)) {
        return false;
    }

    if (!parseProgram()) {
        return false;
    }

    if (!writeQuadFile(quadOutputPath)) {
        return false;
    }

    if (!writeSymbolFileWithTemps(symbolWithTempsOutputPath)) {
        return false;
    }

    return (g_parserErrorCount == 0);
}
