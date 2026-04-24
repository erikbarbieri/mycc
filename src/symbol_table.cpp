#include "symbol_table.hpp"

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static SymbolEntry g_symbolTable[MAX_SYMBOLS];
static int g_symbolCount = 0;
static int g_symbolTableErrorCount = 0;
static int g_nextDataAddress = 0;
static int g_nextTempNumber = 1;

const SymbolEntry* getSymbolTable() {
    return g_symbolTable;
}

int getSymbolCount() {
    return g_symbolCount;
}

int getSymbolTableErrorCount() {
    return g_symbolTableErrorCount;
}

int findSymbolByName(const std::string& name) {
    for (int i = 0; i < g_symbolCount; i++) {
        if (g_symbolTable[i].symbol == name) {
            return i;
        }
    }
    return -1;
}

int findLiteralByValue(int value) {
    for (int i = 0; i < g_symbolCount; i++) {
        if (g_symbolTable[i].classification == SYM_NUM_LITERAL &&
            g_symbolTable[i].value == value) {
            return i;
        }
    }
    return -1;
}

static void reportSymbolError(int line, const std::string& message) {
    std::cerr << "Symbol table error on line " << line << ": " << message << "\n";
    g_symbolTableErrorCount++;
}

static bool appendSymbol(const std::string& symbol, SymbolClassification classification,
                         int value, int address, SegmentType segment) {
    if (g_symbolCount >= MAX_SYMBOLS) {
        std::cerr << "Too many symbols. Increase MAX_SYMBOLS.\n";
        return false;
    }

    g_symbolTable[g_symbolCount].number = g_symbolCount + 1;
    g_symbolTable[g_symbolCount].symbol = symbol;
    g_symbolTable[g_symbolCount].classification = classification;
    g_symbolTable[g_symbolCount].value = value;
    g_symbolTable[g_symbolCount].address = address;
    g_symbolTable[g_symbolCount].segment = segment;
    g_symbolCount++;
    return true;
}

static bool addProgramName(const std::string& name, int line) {
    if (findSymbolByName(name) >= 0) {
        reportSymbolError(line, "duplicate program name '" + name + "'");
        return false;
    }

    return appendSymbol(name, SYM_PROGRAM_NAME, 0, 0, SEG_CS);
}

static bool addConstSymbol(const std::string& name, int value, int line) {
    if (findSymbolByName(name) >= 0) {
        reportSymbolError(line, "duplicate constant '" + name + "'");
        return false;
    }

    bool ok = appendSymbol(name, SYM_CONST, value, g_nextDataAddress, SEG_DS);
    if (ok) {
        g_nextDataAddress += WORD_SIZE_BYTES;
    }
    return ok;
}

static bool addVarSymbol(const std::string& name, int line) {
    if (findSymbolByName(name) >= 0) {
        reportSymbolError(line, "duplicate variable '" + name + "'");
        return false;
    }

    bool ok = appendSymbol(name, SYM_VAR, 0, g_nextDataAddress, SEG_DS);
    if (ok) {
        g_nextDataAddress += WORD_SIZE_BYTES;
    }
    return ok;
}

static bool addArraySymbol(const std::string& name, int length, int line) {
    if (findSymbolByName(name) >= 0) {
        reportSymbolError(line, "duplicate array '" + name + "'");
        return false;
    }

    if (length <= 0) {
        reportSymbolError(line, "array '" + name + "' must have a positive size");
        return false;
    }

    bool ok = appendSymbol(name, SYM_ARRAY, length, g_nextDataAddress, SEG_DS);
    if (ok) {
        g_nextDataAddress += length * WORD_SIZE_BYTES;
    }
    return ok;
}

static bool addProcedureSymbol(const std::string& name, int line) {
    if (findSymbolByName(name) >= 0) {
        reportSymbolError(line, "duplicate procedure '" + name + "'");
        return false;
    }

    return appendSymbol(name, SYM_PROCEDURE, 0, 0, SEG_CS);
}

static bool addLiteralSymbol(int value) {
    if (findLiteralByValue(value) >= 0) {
        return true;
    }

    bool ok =
        appendSymbol(std::to_string(value), SYM_NUM_LITERAL, value, g_nextDataAddress, SEG_DS);
    if (ok) {
        g_nextDataAddress += WORD_SIZE_BYTES;
    }
    return ok;
}

int appendTempSymbol() {
    std::string tempName = "T" + std::to_string(g_nextTempNumber);
    g_nextTempNumber++;

    if (!appendSymbol(tempName, SYM_TEMP, 0, g_nextDataAddress, SEG_DS)) {
        return -1;
    }

    g_nextDataAddress += WORD_SIZE_BYTES;
    return g_symbolCount - 1;
}

static bool loadTokenFile(const std::filesystem::path& tokenInputPath, Token tokens[],
                          int& tokenCount) {
    std::ifstream inFile(tokenInputPath);
    if (!inFile.is_open()) {
        std::cerr << "Failed to open token file: " << tokenInputPath << "\n";
        return false;
    }

    tokenCount = 0;
    std::string line;

    while (std::getline(inFile, line)) {
        if (line.empty()) {
            continue;
        }

        if (tokenCount >= MAX_TOKENS) {
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

        tokens[tokenCount].type = type;
        tokens[tokenCount].lexeme = lexeme;
        tokens[tokenCount].line = tokenLine;
        tokenCount++;
    }

    return true;
}

static std::string toUpperCopy(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); i++) {
        out[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[i])));
    }
    return out;
}

static bool isBuiltinProcedureName(const std::string& name) {
    const std::string upper = toUpperCopy(name);
    return upper == "READINTEGER" || upper == "PRINT";
}

static bool writeSymbolTableFile(const std::filesystem::path& symbolOutputPath) {
    std::ofstream outFile(symbolOutputPath);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open symbol output file: " << symbolOutputPath << "\n";
        return false;
    }

    outFile << "#\tSymbol\tClassification\tValue\tAddress\tSegment\n";

    for (int i = 0; i < g_symbolCount; i++) {
        outFile << g_symbolTable[i].number << '\t' << g_symbolTable[i].symbol << '\t'
                << symbolClassificationName(g_symbolTable[i].classification) << '\t'
                << g_symbolTable[i].value << '\t' << g_symbolTable[i].address << '\t'
                << segmentTypeName(g_symbolTable[i].segment) << '\n';
    }

    return true;
}

// Columns for the symbol-table DFSA
enum SymbolInputClass {
    SIC_CLASS = 0,
    SIC_CONST,
    SIC_VAR,
    SIC_PROCEDURE,
    SIC_CALL,
    SIC_IDENTIFIER,
    SIC_INTEGER,
    SIC_ASSIGN,
    SIC_COMMA,
    SIC_SEMICOLON,
    SIC_LCURLY,
    SIC_RCURLY,
    SIC_LPAREN,
    SIC_RPAREN,
    SIC_LBRACKET,
    SIC_RBRACKET,
    SIC_EOF,
    SIC_OTHER,
    SIC_COUNT
};

// Rows for the two 2-D DFSA tables
enum SymbolState {
    ST0_EXPECT_CLASS = 0,
    ST1_EXPECT_PROGRAM_NAME,
    ST2_EXPECT_LEFT_CURLY,
    ST3_BODY_SCAN,
    ST4_CONST_NAME,
    ST5_CONST_ASSIGN,
    ST6_CONST_VALUE,
    ST7_CONST_AFTER_VALUE,
    ST8_VAR_NAME,
    ST9_VAR_AFTER_NAME,
    ST10_ARRAY_SIZE,
    ST11_ARRAY_RIGHT_BRACKET,
    ST12_ARRAY_AFTER_BRACKET,
    ST13_PROCEDURE_NAME,
    ST14_PROCEDURE_LPAREN,
    ST15_PROCEDURE_RPAREN,
    ST16_PROCEDURE_LEFT_CURLY,
    ST17_CALL_TARGET,
    ST18_END_OF_PROGRAM,
    ST19_ERROR,
    SYMBOL_STATE_COUNT
};

enum SymbolAction {
    ACT_NONE = 0,
    ACT_ADD_PROGRAM_NAME,
    ACT_ENTER_BRACE,
    ACT_EXIT_BRACE,
    ACT_STORE_CONST_NAME,
    ACT_ADD_CONST,
    ACT_STORE_VAR_NAME,
    ACT_ADD_VAR,
    ACT_STORE_ARRAY_LENGTH,
    ACT_ADD_ARRAY,
    ACT_ADD_PROCEDURE,
    ACT_CHECK_IDENTIFIER_USE,
    ACT_CHECK_CALL_TARGET,
    ACT_ADD_LITERAL
};

static SymbolInputClass classifyTokenForDfsa(TokenType type) {
    switch (type) {
        case TOK_CLASS:
            return SIC_CLASS;
        case TOK_CONST:
            return SIC_CONST;
        case TOK_VAR:
            return SIC_VAR;
        case TOK_PROCEDURE:
            return SIC_PROCEDURE;
        case TOK_CALL:
            return SIC_CALL;
        case TOK_IDENTIFIER:
            return SIC_IDENTIFIER;
        case TOK_INTEGER:
            return SIC_INTEGER;
        case TOK_ASSIGNMENT:
            return SIC_ASSIGN;
        case TOK_COMMA:
            return SIC_COMMA;
        case TOK_SEMICOLON:
            return SIC_SEMICOLON;
        case TOK_LCURLY:
            return SIC_LCURLY;
        case TOK_RCURLY:
            return SIC_RCURLY;
        case TOK_LPAREN:
            return SIC_LPAREN;
        case TOK_RPAREN:
            return SIC_RPAREN;
        case TOK_LBRACKET:
            return SIC_LBRACKET;
        case TOK_RBRACKET:
            return SIC_RBRACKET;
        case TOK_EOF:
            return SIC_EOF;
        default:
            return SIC_OTHER;
    }
}

// Hardcoded 2-D state table for the symbol-table pass
// Rows = SymbolState values
// Columns = SymbolInputClass values
// This pass reads in token/class pairs from pass 1
// I love tables!! I can’t break my code by accident later, woohoo! (hopefully)
static const int SYMBOL_DFA_NEXT[SYMBOL_STATE_COUNT][SIC_COUNT] = {
    // class const var proc call id  int asg  ,   ;   {   }   (   )   [   ]  eof  oth
    {1, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19}, // 0 expect CLASS
    {19, 19, 19, 19, 19, 2, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
     19}, // 1 expect program name
    {19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 3, 19, 19, 19, 19, 19, 19, 19}, // 2 expect {
    {19, 4, 8, 13, 17, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 18, 3},              // 3 body scan
    {19, 19, 19, 19, 19, 5, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19}, // 4 CONST name
    {19, 19, 19, 19, 19, 19, 19, 6, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19}, // 5 CONST =
    {19, 19, 19, 19, 19, 19, 7, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19}, // 6 CONST value
    {19, 19, 19, 19, 19, 19, 19, 19, 4, 3, 19, 19, 19, 19, 19, 19, 19, 19},  // 7 CONST , or ;
    {19, 19, 19, 19, 19, 9, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19}, // 8 VAR name
    {19, 19, 19, 19, 19, 19, 19, 19, 8, 3, 19, 19, 19, 19, 10, 19, 19, 19},  // 9 VAR after name
    {19, 19, 19, 19, 19, 19, 11, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19}, // 10 array size
    {19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 12, 19, 19}, // 11 array ]
    {19, 19, 19, 19, 19, 19, 19, 19, 8, 3, 19, 19, 19, 19, 19, 19, 19, 19},   // 12 array , or ;
    {19, 19, 19, 19, 19, 14, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
     19}, // 13 PROCEDURE name
    {19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 15, 19, 19, 19, 19, 19}, // 14 PROCEDURE (
    {19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 16, 19, 19, 19, 19}, // 15 PROCEDURE )
    {19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 3, 19, 19, 19, 19, 19, 19, 19},  // 16 PROCEDURE {
    {19, 19, 19, 19, 19, 3, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19},  // 17 CALL target
    {19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 18,
     19}, // 18 end, expect EOF
    {19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19} // 19 error
};

// Hardcoded 2-D action table for the symbol-table DFSA
// Each cell says what symbol-table action is triggered by that row/column entry
static const int SYMBOL_DFA_ACTION[SYMBOL_STATE_COUNT][SIC_COUNT] = {
    // class  const  var  proc  call  id  int  asg  ,  ;  {  }  (  )  [  ]  eof
    // oth
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE}, // 0
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_ADD_PROGRAM_NAME, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE}, // 1
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_ENTER_BRACE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE}, // 2
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_CHECK_IDENTIFIER_USE,
     ACT_ADD_LITERAL, ACT_NONE, ACT_NONE, ACT_NONE, ACT_ENTER_BRACE, ACT_EXIT_BRACE, ACT_NONE,
     ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE}, // 3
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_STORE_CONST_NAME, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE}, // 4
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE}, // 5
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_ADD_CONST, ACT_NONE,
     ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE}, // 6
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE}, // 7
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_STORE_VAR_NAME, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE}, // 8
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_ADD_VAR, ACT_ADD_VAR, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_NONE}, // 9
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_STORE_ARRAY_LENGTH,
     ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_NONE}, // 10
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE}, // 11
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_ADD_ARRAY, ACT_ADD_ARRAY, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_NONE}, // 12
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_ADD_PROCEDURE, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE}, // 13
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE}, // 14
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE}, // 15
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_ENTER_BRACE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE}, // 16
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_CHECK_CALL_TARGET, ACT_NONE,
     ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_NONE}, // 17
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE}, // 18
    {ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE, ACT_NONE,
     ACT_NONE} // 19
};

static const char* expectedStateMessage(SymbolState state) {
    switch (state) {
        case ST0_EXPECT_CLASS:
            return "expected CLASS at start of program";
        case ST1_EXPECT_PROGRAM_NAME:
            return "expected program name after CLASS";
        case ST2_EXPECT_LEFT_CURLY:
            return "expected '{' after program name";
        case ST4_CONST_NAME:
            return "expected constant name after CONST";
        case ST5_CONST_ASSIGN:
            return "expected '=' in constant declaration";
        case ST6_CONST_VALUE:
            return "expected integer constant value";
        case ST7_CONST_AFTER_VALUE:
            return "expected ',' or ';' after constant value";
        case ST8_VAR_NAME:
            return "expected variable or array name after VAR";
        case ST9_VAR_AFTER_NAME:
            return "expected ',', ';', or '[' after variable name";
        case ST10_ARRAY_SIZE:
            return "expected integer array size inside '[' and ']'";
        case ST11_ARRAY_RIGHT_BRACKET:
            return "expected ']' after array size";
        case ST12_ARRAY_AFTER_BRACKET:
            return "expected ',' or ';' after array declaration";
        case ST13_PROCEDURE_NAME:
            return "expected procedure name after PROCEDURE";
        case ST14_PROCEDURE_LPAREN:
            return "expected '(' after procedure name";
        case ST15_PROCEDURE_RPAREN:
            return "expected ')' after procedure name";
        case ST16_PROCEDURE_LEFT_CURLY:
            return "expected '{' to start procedure body";
        case ST17_CALL_TARGET:
            return "expected procedure name after CALL";
        case ST18_END_OF_PROGRAM:
            return "expected EOF after final '}'";
        default:
            return "unexpected token in symbol-table DFSA";
    }
}

bool buildSymbolTableFromTokenFile(const std::filesystem::path& tokenInputPath,
                                   const std::filesystem::path& symbolOutputPath) {
    Token tokens[MAX_TOKENS];
    int tokenCount = 0;

    if (!loadTokenFile(tokenInputPath, tokens, tokenCount)) {
        return false;
    }

    g_symbolCount = 0;
    g_symbolTableErrorCount = 0;
    g_nextDataAddress = 0;
    g_nextTempNumber = 1;

    SymbolState state = ST0_EXPECT_CLASS;
    int braceDepth = 0;

    std::string pendingName = "";
    int pendingLine = 0;
    int pendingArrayLength = 0;

    for (int i = 0; i < tokenCount; i++) {
        const Token& tok = tokens[i];
        const SymbolInputClass inputClass = classifyTokenForDfsa(tok.type);
        SymbolState nextState = static_cast<SymbolState>(SYMBOL_DFA_NEXT[state][inputClass]);
        const SymbolAction action =
            static_cast<SymbolAction>(SYMBOL_DFA_ACTION[state][inputClass]);

        if (nextState == ST19_ERROR) {
            reportSymbolError(tok.line, std::string(expectedStateMessage(state)) + ", found '" +
                                            tok.lexeme + "'");
            return false;
        }

        switch (action) {
            case ACT_NONE:
                break;

            case ACT_ADD_PROGRAM_NAME:
                if (!addProgramName(tok.lexeme, tok.line))
                    return false;
                break;

            case ACT_ENTER_BRACE:
                braceDepth++;
                break;

            case ACT_EXIT_BRACE:
                braceDepth--;
                if (braceDepth < 0) {
                    reportSymbolError(tok.line, "too many right braces in source program");
                    return false;
                }
                if (braceDepth == 0) {
                    nextState = ST18_END_OF_PROGRAM;
                }
                break;

            case ACT_STORE_CONST_NAME:
                pendingName = tok.lexeme;
                pendingLine = tok.line;
                break;

            case ACT_ADD_CONST:
                if (!addConstSymbol(pendingName, std::stoi(tok.lexeme), pendingLine))
                    return false;
                break;

            case ACT_STORE_VAR_NAME:
                pendingName = tok.lexeme;
                pendingLine = tok.line;
                pendingArrayLength = 0;
                break;

            case ACT_ADD_VAR:
                if (!addVarSymbol(pendingName, pendingLine))
                    return false;
                break;

            case ACT_STORE_ARRAY_LENGTH:
                pendingArrayLength = std::stoi(tok.lexeme);
                break;

            case ACT_ADD_ARRAY:
                if (!addArraySymbol(pendingName, pendingArrayLength, pendingLine))
                    return false;
                break;

            case ACT_ADD_PROCEDURE:
                if (!addProcedureSymbol(tok.lexeme, tok.line))
                    return false;
                break;

            case ACT_CHECK_IDENTIFIER_USE: {
                int idx = findSymbolByName(tok.lexeme);
                if (idx < 0) {
                    reportSymbolError(tok.line, "identifier '" + tok.lexeme +
                                                    "' used before declaration");
                    return false;
                }
                break;
            }

            case ACT_CHECK_CALL_TARGET:
                if (!isBuiltinProcedureName(tok.lexeme)) {
                    int idx = findSymbolByName(tok.lexeme);
                    if (idx < 0 || g_symbolTable[idx].classification != SYM_PROCEDURE) {
                        reportSymbolError(tok.line, "procedure '" + tok.lexeme +
                                                        "' called before declaration");
                        return false;
                    }
                }
                break;

            case ACT_ADD_LITERAL:
                if (!addLiteralSymbol(std::stoi(tok.lexeme)))
                    return false;
                break;
        }

        state = nextState;
    }

    if (braceDepth != 0) {
        const int line = (tokenCount > 0) ? tokens[tokenCount - 1].line : 0;
        reportSymbolError(line, "program did not end with balanced braces");
        return false;
    }

    if (state != ST18_END_OF_PROGRAM) {
        const int line = (tokenCount > 0) ? tokens[tokenCount - 1].line : 0;
        reportSymbolError(line, "symbol-table DFSA did not reach an accepting end state");
        return false;
    }

    return writeSymbolTableFile(symbolOutputPath) && (g_symbolTableErrorCount == 0);
}
