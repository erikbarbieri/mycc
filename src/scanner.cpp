#include "scanner.hpp"

#include <cctype>
#include <fstream>
#include <iostream>
#include <string>

// Vectors are evil and made by space aliens to slow down my compiler!!
// Avoiding vectors at all costs to keep my impatient peanut eaters happy!

static Token g_tokenList[MAX_TOKENS];
static int g_tokenCount = 0;
static int g_scannerErrorCount = 0;

static int g_currentLine = 1;
static bool g_hasPendingChar = false;
static char g_pendingChar = '\0';

const Token* getTokenList() {
    return g_tokenList;
}

int getTokenCount() {
    return g_tokenCount;
}

int getScannerErrorCount() {
    return g_scannerErrorCount;
}

// Columns for SCANNER_DFA
enum ScannerInputClass {
    SIC_WHITESPACE = 0,
    SIC_OTHER,
    SIC_LETTER,
    SIC_DIGIT,
    SIC_COMMA,
    SIC_SEMICOLON,
    SIC_PLUS,
    SIC_MINUS,
    SIC_ASTERISK,
    SIC_SLASH,
    SIC_LPAREN,
    SIC_RPAREN,
    SIC_LCURLY,
    SIC_RCURLY,
    SIC_LBRACKET,
    SIC_RBRACKET,
    SIC_EQUAL,
    SIC_EXCLAMATION,
    SIC_LESS,
    SIC_GREATER,
    SIC_COUNT
};

// Rows for SCANNER_DFA
enum ScannerState {
    S0_START = 0,
    S1_EMIT_ERROR,
    S2_IDENTIFIER,
    S3_EMIT_IDENTIFIER,
    S4_INTEGER,
    S5_EMIT_INTEGER,
    S6_EMIT_COMMA,
    S7_EMIT_SEMICOLON,
    S8_EMIT_ADD,
    S9_EMIT_SUB,
    S10_EMIT_MUL,
    S11_SLASH_OR_COMMENT,
    S12_COMMENT,
    S13_COMMENT_STAR,
    S14_EMIT_DIV,
    S15_EMIT_LPAREN,
    S16_EMIT_RPAREN,
    S17_EMIT_LCURLY,
    S18_EMIT_RCURLY,
    S19_EQUAL_OR_ASSIGN,
    S20_EMIT_EQUAL_TO,
    S21_EMIT_ASSIGNMENT,
    S22_BANG_OR_NOT_EQUAL,
    S23_EMIT_NOT_EQUAL,
    S24_LT_OR_LE,
    S25_EMIT_LE,
    S26_EMIT_LT,
    S27_GT_OR_GE,
    S28_EMIT_GE,
    S29_EMIT_GT,
    S30_EMIT_LBRACKET,
    S31_EMIT_RBRACKET,
    SCANNER_STATE_COUNT
};

// Hardcoded 2-D array DFSA table for the scanner
// Rows = states listed in ScannerState
// Columns = character classes listed in ScannerInputClass
static const int SCANNER_DFA[SCANNER_STATE_COUNT][SIC_COUNT] = {
    // ws other let dig   ,  ;  +  -  *  /  (  )  {  }  [  ]  =  !  <  >
    {0, 1, 2, 4, 6, 7, 8, 9, 10, 11, 15, 16, 17, 18, 30, 31, 19, 22, 24, 27}, // 0  start
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},             // 1  emit error
    {3, 3, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3},             // 2  identifier
    {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3}, // 3  emit identifier
    {5, 5, 5, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5}, // 4  integer
    {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5}, // 5  emit integer
    {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, // 6  emit comma
    {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7}, // 7  emit semicolon
    {8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8}, // 8  emit add
    {9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9}, // 9  emit sub
    {10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
     10, 10, 10, 10, 10, 10, 10, 10, 10, 10}, // 10 emit mul
    {14, 14, 14, 14, 14, 14, 14, 14, 12, 14,
     14, 14, 14, 14, 14, 14, 14, 14, 14, 14}, // 11 slash or /*
    {12, 12, 12, 12, 12, 12, 12, 12, 13, 12,
     12, 12, 12, 12, 12, 12, 12, 12, 12, 12}, // 12 inside comment
    {12, 12, 12, 12, 12, 12, 12, 12, 13, 0,
     12, 12, 12, 12, 12, 12, 12, 12, 12, 12}, // 13 saw * in comment
    {14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
     14, 14, 14, 14, 14, 14, 14, 14, 14, 14}, // 14 emit div
    {15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
     15, 15, 15, 15, 15, 15, 15, 15, 15, 15}, // 15 emit (
    {16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
     16, 16, 16, 16, 16, 16, 16, 16, 16, 16}, // 16 emit )
    {17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
     17, 17, 17, 17, 17, 17, 17, 17, 17, 17}, // 17 emit {
    {18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
     18, 18, 18, 18, 18, 18, 18, 18, 18, 18}, // 18 emit }
    {21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
     21, 21, 21, 21, 21, 21, 20, 21, 21, 21}, // 19 = or ==
    {20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
     20, 20, 20, 20, 20, 20, 20, 20, 20, 20}, // 20 emit ==
    {21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
     21, 21, 21, 21, 21, 21, 21, 21, 21, 21},                      // 21 emit =
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 23, 1, 1, 1}, // 22 saw !, expecting =
    {23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
     23, 23, 23, 23, 23, 23, 23, 23, 23, 23}, // 23 emit !=
    {26, 26, 26, 26, 26, 26, 26, 26, 26, 26,
     26, 26, 26, 26, 26, 26, 25, 26, 26, 26}, // 24 < or <=
    {25, 25, 25, 25, 25, 25, 25, 25, 25, 25,
     25, 25, 25, 25, 25, 25, 25, 25, 25, 25}, // 25 emit <=
    {26, 26, 26, 26, 26, 26, 26, 26, 26, 26,
     26, 26, 26, 26, 26, 26, 26, 26, 26, 26}, // 26 emit <
    {29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
     29, 29, 29, 29, 29, 29, 28, 29, 29, 29}, // 27 > or >=
    {28, 28, 28, 28, 28, 28, 28, 28, 28, 28,
     28, 28, 28, 28, 28, 28, 28, 28, 28, 28}, // 28 emit >=
    {29, 29, 29, 29, 29, 29, 29, 29, 29, 29,
     29, 29, 29, 29, 29, 29, 29, 29, 29, 29}, // 29 emit >
    {30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
     30, 30, 30, 30, 30, 30, 30, 30, 30, 30}, // 30 emit [
    {31, 31, 31, 31, 31, 31, 31, 31, 31, 31,
     31, 31, 31, 31, 31, 31, 31, 31, 31, 31} // 31 emit ]
};

static ScannerInputClass classifyChar(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);

    if (std::isspace(uc))
        return SIC_WHITESPACE;
    if (std::isalpha(uc))
        return SIC_LETTER;
    if (std::isdigit(uc))
        return SIC_DIGIT;

    switch (c) {
        case ',':
            return SIC_COMMA;
        case ';':
            return SIC_SEMICOLON;
        case '+':
            return SIC_PLUS;
        case '-':
            return SIC_MINUS;
        case '*':
            return SIC_ASTERISK;
        case '/':
            return SIC_SLASH;
        case '(':
            return SIC_LPAREN;
        case ')':
            return SIC_RPAREN;
        case '{':
            return SIC_LCURLY;
        case '}':
            return SIC_RCURLY;
        case '[':
            return SIC_LBRACKET;
        case ']':
            return SIC_RBRACKET;
        case '=':
            return SIC_EQUAL;
        case '!':
            return SIC_EXCLAMATION;
        case '<':
            return SIC_LESS;
        case '>':
            return SIC_GREATER;
        default:
            return SIC_OTHER;
    }
}

static bool readChar(std::ifstream& inputFile, char& c) {
    if (g_hasPendingChar) {
        c = g_pendingChar;
        g_hasPendingChar = false;

        // A pushed-back newline must count as a newline again when it is
        // consumed later. Otherwise the scanner falls one line behind after
        // an identifier or integer is followed by a newline.
        if (c == '\n') {
            g_currentLine++;
        }

        return true;
    }

    if (!inputFile.get(c)) {
        return false;
    }

    if (c == '\n') {
        g_currentLine++;
    }

    return true;
}

static void pushBackChar(char c) {
    g_hasPendingChar = true;
    g_pendingChar = c;

    if (c == '\n') {
        g_currentLine--;
    }
}

static std::string toUpperCopy(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); i++) {
        out[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[i])));
    }
    return out;
}

static bool isReservedWord(const std::string& s, TokenType& reservedType) {
    const std::string upper = toUpperCopy(s);

    if (upper == "CONST") {
        reservedType = TOK_CONST;
        return true;
    }
    if (upper == "IF") {
        reservedType = TOK_IF;
        return true;
    }
    if (upper == "VAR") {
        reservedType = TOK_VAR;
        return true;
    }
    if (upper == "THEN") {
        reservedType = TOK_THEN;
        return true;
    }
    if (upper == "ELSE") {
        reservedType = TOK_ELSE;
        return true;
    }
    if (upper == "PROCEDURE") {
        reservedType = TOK_PROCEDURE;
        return true;
    }
    if (upper == "WHILE") {
        reservedType = TOK_WHILE;
        return true;
    }
    if (upper == "CALL") {
        reservedType = TOK_CALL;
        return true;
    }
    if (upper == "DO") {
        reservedType = TOK_DO;
        return true;
    }
    if (upper == "ODD") {
        reservedType = TOK_ODD;
        return true;
    }
    if (upper == "CLASS") {
        reservedType = TOK_CLASS;
        return true;
    }

    return false;
}

static void convertIdentifiersToReservedWords() {
    for (int i = 0; i < g_tokenCount; i++) {
        if (g_tokenList[i].type == TOK_IDENTIFIER) {
            TokenType newType;
            if (isReservedWord(g_tokenList[i].lexeme, newType)) {
                g_tokenList[i].type = newType;
            }
        }
    }
}

static void emitToken(Token& outToken, TokenType type, const std::string& lexeme, int line) {
    outToken.type = type;
    outToken.lexeme = lexeme;
    outToken.line = line;
}

// My cat, Tsuki, tried to help me finish writing this emitTokenForState function
// Here’s her insightful code before I picked her up from my keyboard:
// dswebgvffffffffffbn ;,lllllllllllllllllllllllllllllllllllllllllllllllllllllllllmjnhhhhhh
static bool emitTokenForState(int state, Token& outToken, const std::string& lexeme, int line) {
    switch (state) {
        case S1_EMIT_ERROR:
            emitToken(outToken, TOK_ERROR, lexeme, line);
            return true;
        case S3_EMIT_IDENTIFIER:
            emitToken(outToken, TOK_IDENTIFIER, lexeme, line);
            return true;
        case S5_EMIT_INTEGER:
            emitToken(outToken, TOK_INTEGER, lexeme, line);
            return true;
        case S6_EMIT_COMMA:
            emitToken(outToken, TOK_COMMA, ",", line);
            return true;
        case S7_EMIT_SEMICOLON:
            emitToken(outToken, TOK_SEMICOLON, ";", line);
            return true;
        case S8_EMIT_ADD:
            emitToken(outToken, TOK_ADD_OP, "+", line);
            return true;
        case S9_EMIT_SUB:
            emitToken(outToken, TOK_SUB_OP, "-", line);
            return true;
        case S10_EMIT_MUL:
            emitToken(outToken, TOK_MUL_OP, "*", line);
            return true;
        case S14_EMIT_DIV:
            emitToken(outToken, TOK_DIV_OP, "/", line);
            return true;
        case S15_EMIT_LPAREN:
            emitToken(outToken, TOK_LPAREN, "(", line);
            return true;
        case S16_EMIT_RPAREN:
            emitToken(outToken, TOK_RPAREN, ")", line);
            return true;
        case S17_EMIT_LCURLY:
            emitToken(outToken, TOK_LCURLY, "{", line);
            return true;
        case S18_EMIT_RCURLY:
            emitToken(outToken, TOK_RCURLY, "}", line);
            return true;
        case S20_EMIT_EQUAL_TO:
            emitToken(outToken, TOK_EQUAL_TO, "==", line);
            return true;
        case S21_EMIT_ASSIGNMENT:
            emitToken(outToken, TOK_ASSIGNMENT, "=", line);
            return true;
        case S23_EMIT_NOT_EQUAL:
            emitToken(outToken, TOK_NOT_EQUAL_TO, "!=", line);
            return true;
        case S25_EMIT_LE:
            emitToken(outToken, TOK_LESS_THAN_OR_EQUAL_TO, "<=", line);
            return true;
        case S26_EMIT_LT:
            emitToken(outToken, TOK_LESS_THAN, "<", line);
            return true;
        case S28_EMIT_GE:
            emitToken(outToken, TOK_GREATER_THAN_OR_EQUAL_TO, ">=", line);
            return true;
        case S29_EMIT_GT:
            emitToken(outToken, TOK_GREATER_THAN, ">", line);
            return true;
        case S30_EMIT_LBRACKET:
            emitToken(outToken, TOK_LBRACKET, "[", line);
            return true;
        case S31_EMIT_RBRACKET:
            emitToken(outToken, TOK_RBRACKET, "]", line);
            return true;
        default:
            return false;
    }
}

static bool emitAtEndOfFile(int state, Token& outToken, const std::string& lexeme, int line) {
    if (state == S12_COMMENT || state == S13_COMMENT_STAR) {
        emitToken(outToken, TOK_ERROR, "UnterminatedComment", line);
        return true;
    }

    if (state == S2_IDENTIFIER)
        return emitTokenForState(S3_EMIT_IDENTIFIER, outToken, lexeme, line);
    if (state == S4_INTEGER)
        return emitTokenForState(S5_EMIT_INTEGER, outToken, lexeme, line);
    if (state == S11_SLASH_OR_COMMENT)
        return emitTokenForState(S14_EMIT_DIV, outToken, lexeme, line);
    if (state == S19_EQUAL_OR_ASSIGN)
        return emitTokenForState(S21_EMIT_ASSIGNMENT, outToken, lexeme, line);
    if (state == S22_BANG_OR_NOT_EQUAL) {
        emitToken(outToken, TOK_ERROR, "!", line);
        return true;
    }
    if (state == S24_LT_OR_LE)
        return emitTokenForState(S26_EMIT_LT, outToken, lexeme, line);
    if (state == S27_GT_OR_GE)
        return emitTokenForState(S29_EMIT_GT, outToken, lexeme, line);

    emitToken(outToken, TOK_EOF, "EOF", g_currentLine);
    return true;
}

static bool getNextToken(std::ifstream& inputFile, Token& outToken) {
    int state = S0_START;
    std::string lexeme = "";
    int tokenLine = g_currentLine;
    char c = '\0';

    while (true) {
        if (!readChar(inputFile, c)) {
            return emitAtEndOfFile(state, outToken, lexeme, tokenLine);
        }

        const ScannerInputClass inputClass = classifyChar(c);
        const int nextState = SCANNER_DFA[state][inputClass];

        if (state == S0_START) {
            tokenLine = g_currentLine;

            if (nextState == S0_START) {
                continue;
            }

            if (nextState == S2_IDENTIFIER || nextState == S4_INTEGER ||
                nextState == S11_SLASH_OR_COMMENT || nextState == S19_EQUAL_OR_ASSIGN ||
                nextState == S22_BANG_OR_NOT_EQUAL || nextState == S24_LT_OR_LE ||
                nextState == S27_GT_OR_GE) {
                lexeme = std::string(1, c);
                state = nextState;
                continue;
            }

            if (emitTokenForState(nextState, outToken, std::string(1, c), tokenLine)) {
                return true;
            }

            emitToken(outToken, TOK_ERROR, std::string(1, c), tokenLine);
            return true;
        }

        if (state == S2_IDENTIFIER) {
            if (nextState == S2_IDENTIFIER) {
                lexeme += c;
                continue;
            }

            pushBackChar(c);
            return emitTokenForState(S3_EMIT_IDENTIFIER, outToken, lexeme, tokenLine);
        }

        if (state == S4_INTEGER) {
            if (nextState == S4_INTEGER) {
                lexeme += c;
                continue;
            }

            pushBackChar(c);
            return emitTokenForState(S5_EMIT_INTEGER, outToken, lexeme, tokenLine);
        }

        if (state == S11_SLASH_OR_COMMENT) {
            if (nextState == S12_COMMENT) {
                state = S12_COMMENT;
                lexeme.clear();
                continue;
            }

            pushBackChar(c);
            return emitTokenForState(S14_EMIT_DIV, outToken, "/", tokenLine);
        }

        if (state == S12_COMMENT) {
            state = nextState;
            continue;
        }

        if (state == S13_COMMENT_STAR) {
            state = nextState;
            continue;
        }

        if (state == S19_EQUAL_OR_ASSIGN) {
            if (nextState == S20_EMIT_EQUAL_TO) {
                return emitTokenForState(S20_EMIT_EQUAL_TO, outToken, "==", tokenLine);
            }

            pushBackChar(c);
            return emitTokenForState(S21_EMIT_ASSIGNMENT, outToken, "=", tokenLine);
        }

        if (state == S22_BANG_OR_NOT_EQUAL) {
            if (nextState == S23_EMIT_NOT_EQUAL) {
                return emitTokenForState(S23_EMIT_NOT_EQUAL, outToken, "!=", tokenLine);
            }

            pushBackChar(c);
            emitToken(outToken, TOK_ERROR, "!", tokenLine);
            return true;
        }

        if (state == S24_LT_OR_LE) {
            if (nextState == S25_EMIT_LE) {
                return emitTokenForState(S25_EMIT_LE, outToken, "<=", tokenLine);
            }

            pushBackChar(c);
            return emitTokenForState(S26_EMIT_LT, outToken, "<", tokenLine);
        }

        if (state == S27_GT_OR_GE) {
            if (nextState == S28_EMIT_GE) {
                return emitTokenForState(S28_EMIT_GE, outToken, ">=", tokenLine);
            }

            pushBackChar(c);
            return emitTokenForState(S29_EMIT_GT, outToken, ">", tokenLine);
        }
    }
}

// Scanner?? I hardly know her!!
bool scanFile(const std::filesystem::path& inputPath,
              const std::filesystem::path& tokenOutputPath) {
    std::ifstream inputFile(inputPath);
    std::ofstream tokenOutputFile(tokenOutputPath);

    if (!inputFile.is_open()) {
        std::cerr << "Failed to open input file: " << inputPath << "\n";
        return false;
    }

    if (!tokenOutputFile.is_open()) {
        std::cerr << "Failed to open token output file: " << tokenOutputPath << "\n";
        return false;
    }

    g_tokenCount = 0;
    g_scannerErrorCount = 0;
    g_currentLine = 1;
    g_hasPendingChar = false;
    g_pendingChar = '\0';

    while (true) {
        if (g_tokenCount >= MAX_TOKENS) {
            std::cerr << "Too many tokens. Increase MAX_TOKENS.\n";
            return false;
        }

        Token token;
        if (!getNextToken(inputFile, token)) {
            return false;
        }

        g_tokenList[g_tokenCount] = token;
        g_tokenCount++;

        if (token.type == TOK_ERROR) {
            g_scannerErrorCount++;
        }

        if (token.type == TOK_EOF) {
            break;
        }
    }

    // Keep reserved-word recognition outside the DFSA so the scanner table stays smaller!!
    convertIdentifiersToReservedWords();

    for (int i = 0; i < g_tokenCount; i++) {
        tokenOutputFile << (i + 1) << '\t' << g_tokenList[i].line << '\t'
                        << g_tokenList[i].lexeme << '\t' << tokenTypeName(g_tokenList[i].type)
                        << '\n';
    }

    return (g_scannerErrorCount == 0);
}
