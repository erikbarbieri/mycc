#include "code_generator.hpp"

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "compiler_types.hpp"

struct CodeLine {
    bool isLabel;
    int address;
    std::string text;
};

static SymbolEntry g_symbols[MAX_SYMBOLS];
static int g_symbolCount = 0;

static Quad g_quads[MAX_QUADS];
static int g_quadCount = 0;

static CodeLine g_codeLines[20000];
static int g_codeLineCount = 0;
static int g_nextCodeAddress = 0;

static int g_codeGeneratorErrorCount = 0;
static bool g_needsReadIntegerRuntime = false;
static bool g_needsPrintRuntime = false;
static bool g_needsArrayBoundsRuntime = false;

// Small optimization!! keep a computed temp in AX when the next quad immediately consumes it
// This removes the redundant store / load pair!
static bool g_axCacheValid = false;
static std::string g_axCachePlace = "";

int getCodeGeneratorErrorCount() {
    return g_codeGeneratorErrorCount;
}

static void resetCodeGeneratorState() {
    g_symbolCount = 0;
    g_quadCount = 0;
    g_codeLineCount = 0;
    g_nextCodeAddress = 0;
    g_codeGeneratorErrorCount = 0;
    g_needsReadIntegerRuntime = false;
    g_needsPrintRuntime = false;
    g_needsArrayBoundsRuntime = false;
    g_axCacheValid = false;
    g_axCachePlace = "";
}

static void reportCodeGeneratorError(const std::string& message) {
    std::cerr << "Code generator error: " << message << "\n";
    g_codeGeneratorErrorCount++;
}

static std::string sanitizeLabel(const std::string& name) {
    std::string out = "";

    for (size_t i = 0; i < name.size(); i++) {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (std::isalnum(c) || c == '_') {
            out += static_cast<char>(c);
        } else {
            out += '_';
        }
    }

    if (out.empty()) {
        out = "generated_symbol";
    }

    if (std::isdigit(static_cast<unsigned char>(out[0]))) {
        out = "S_" + out;
    }

    return out;
}

static std::string toUpperCopy(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); i++) {
        out[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[i])));
    }
    return out;
}

static int splitTabLine(const std::string& line, std::string parts[], int maxParts) {
    int count = 0;
    std::string current = "";

    for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == '\t') {
            if (count < maxParts) {
                parts[count] = current;
            }
            count++;
            current = "";
        } else {
            current += line[i];
        }
    }

    if (count < maxParts) {
        parts[count] = current;
    }
    count++;

    return count;
}

static bool loadSymbols(const std::filesystem::path& symbolInputPath) {
    std::ifstream input(symbolInputPath);
    if (!input.is_open()) {
        reportCodeGeneratorError("failed to open symbol file: " + symbolInputPath.string());
        return false;
    }

    std::string line;
    bool isFirstLine = true;

    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }

        if (isFirstLine) {
            isFirstLine = false;
            continue;
        }

        if (g_symbolCount >= MAX_SYMBOLS) {
            reportCodeGeneratorError("too many symbols for fixed array");
            return false;
        }

        std::string parts[6];
        int count = splitTabLine(line, parts, 6);
        if (count < 6) {
            reportCodeGeneratorError("malformed symbol line: " + line);
            return false;
        }

        SymbolClassification classification;
        SegmentType segment;

        if (!symbolClassificationFromName(parts[2], classification)) {
            reportCodeGeneratorError("unknown symbol classification: " + parts[2]);
            return false;
        }

        if (!segmentTypeFromName(parts[5], segment)) {
            reportCodeGeneratorError("unknown symbol segment: " + parts[5]);
            return false;
        }

        SymbolEntry entry;
        entry.number = std::stoi(parts[0]);
        entry.symbol = parts[1];
        entry.classification = classification;
        entry.value = std::stoi(parts[3]);
        entry.address = std::stoi(parts[4]);
        entry.segment = segment;

        g_symbols[g_symbolCount] = entry;
        g_symbolCount++;
    }

    return true;
}

static bool loadQuads(const std::filesystem::path& quadInputPath) {
    std::ifstream input(quadInputPath);
    if (!input.is_open()) {
        reportCodeGeneratorError("failed to open quad file: " + quadInputPath.string());
        return false;
    }

    std::string line;
    bool isFirstLine = true;

    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }

        if (isFirstLine) {
            isFirstLine = false;
            continue;
        }

        if (g_quadCount >= MAX_QUADS) {
            reportCodeGeneratorError("too many quads for fixed array");
            return false;
        }

        std::string parts[5];
        int count = splitTabLine(line, parts, 5);
        if (count < 5) {
            reportCodeGeneratorError("malformed quad line: " + line);
            return false;
        }

        Quad q;
        q.number = std::stoi(parts[0]);
        q.op = parts[1];
        q.arg1 = parts[2];
        q.arg2 = parts[3];
        q.result = parts[4];

        g_quads[g_quadCount] = q;
        g_quadCount++;
    }

    return true;
}

static int findSymbolIndexByName(const std::string& name) {
    for (int i = 0; i < g_symbolCount; i++) {
        if (g_symbols[i].symbol == name) {
            return i;
        }
    }

    return -1;
}

static int arrayLengthFor(const std::string& name) {
    int index = findSymbolIndexByName(name);
    if (index < 0)
        return -1;
    if (g_symbols[index].classification != SYM_ARRAY)
        return -1;
    return g_symbols[index].value;
}

static std::string symbolToAsmLabel(const std::string& symbolName) {
    int index = findSymbolIndexByName(symbolName);
    if (index < 0) {
        return "";
    }

    const SymbolEntry& entry = g_symbols[index];
    if (entry.classification == SYM_NUM_LITERAL) {
        return "Lit_" + entry.symbol;
    }

    return sanitizeLabel(entry.symbol);
}

static std::string memoryOperandForPlace(const std::string& place) {
    std::string label = symbolToAsmLabel(place);
    if (label.empty()) {
        reportCodeGeneratorError("unknown symbol/place referenced in quad stream: " + place);
        return "[generated_symbol]";
    }

    return "[" + label + "]";
}

static bool emitInstruction(const std::string& text) {
    if (g_codeLineCount >= 20000) {
        reportCodeGeneratorError("too many generated code lines");
        return false;
    }

    g_codeLines[g_codeLineCount].isLabel = false;
    g_codeLines[g_codeLineCount].address = g_nextCodeAddress;
    g_codeLines[g_codeLineCount].text = text;
    g_codeLineCount++;
    g_nextCodeAddress += WORD_SIZE_BYTES;
    return true;
}

static bool emitLabel(const std::string& labelName) {
    if (g_codeLineCount >= 20000) {
        reportCodeGeneratorError("too many generated code lines");
        return false;
    }

    g_codeLines[g_codeLineCount].isLabel = true;
    g_codeLines[g_codeLineCount].address = g_nextCodeAddress;
    g_codeLines[g_codeLineCount].text = sanitizeLabel(labelName) + ":";
    g_codeLineCount++;
    return true;
}

static std::string jumpMnemonicFromQuadCode(const std::string& name) {
    if (name == "EQ")
        return "je";
    if (name == "NE")
        return "jne";
    if (name == "LT")
        return "jl";
    if (name == "LE")
        return "jle";
    if (name == "GT")
        return "jg";
    if (name == "GE")
        return "jge";

    reportCodeGeneratorError("unknown jump mnemonic code: " + name);
    return "jmp";
}

static bool isRelationalOp(const std::string& op) {
    return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

static bool isLabelQuad(const Quad& q) {
    if (q.arg1 != "-" || q.arg2 != "-" || q.result != "-") {
        return false;
    }

    if (q.op.empty()) {
        return false;
    }

    return q.op[0] == 'L' || q.op[0] == 'W';
}

static void invalidateAxCache() {
    g_axCacheValid = false;
    g_axCachePlace = "";
}

static void rememberAxPlace(const std::string& place) {
    g_axCacheValid = true;
    g_axCachePlace = place;
}

static bool isTempPlace(const std::string& place) {
    int index = findSymbolIndexByName(place);
    if (index < 0) {
        return false;
    }

    return g_symbols[index].classification == SYM_TEMP;
}

static bool emitLoadAxFromPlace(const std::string& place) {
    if (g_axCacheValid && g_axCachePlace == place) {
        return true;
    }

    if (!emitInstruction("mov ax, word " + memoryOperandForPlace(place))) {
        return false;
    }

    rememberAxPlace(place);
    return true;
}

static bool quadUsesPlaceAsSource(const Quad& q, const std::string& place) {
    if (q.op == "+" || q.op == "-" || q.op == "*" || q.op == "/" || isRelationalOp(q.op)) {
        return q.arg1 == place || q.arg2 == place;
    }

    if (q.op == "=") {
        return q.arg2 == place;
    }

    if (q.op == "ODD") {
        return q.arg1 == place;
    }

    if (q.op == "LOADA") {
        return q.arg2 == place;
    }

    if (q.op == "STOREA") {
        return q.arg2 == place || q.result == place;
    }

    if (q.op == "CALL") {
        const std::string procNameUpper = toUpperCopy(q.arg1);
        return procNameUpper == "PRINT" && q.arg2 == place;
    }

    return false;
}

static int sourceUseCountAfterQuad(int quadIndex, const std::string& place) {
    int count = 0;
    for (int i = quadIndex + 1; i < g_quadCount; i++) {
        if (quadUsesPlaceAsSource(g_quads[i], place)) {
            count++;
        }
    }

    return count;
}

static bool nextQuadCanUseAx(int quadIndex, const std::string& place) {
    if (quadIndex + 1 >= g_quadCount) {
        return false;
    }

    const Quad& next = g_quads[quadIndex + 1];

    if (next.op == "+" || next.op == "-" || next.op == "*" || next.op == "/" ||
        isRelationalOp(next.op)) {
        return next.arg1 == place;
    }

    if (next.op == "=") {
        return next.arg2 == place;
    }

    if (next.op == "ODD") {
        return next.arg1 == place;
    }

    if (next.op == "CALL") {
        const std::string procNameUpper = toUpperCopy(next.arg1);
        return procNameUpper == "PRINT" && next.arg2 == place;
    }

    return false;
}

static bool shouldKeepTempOnlyInAx(int quadIndex, const std::string& place) {
    return isTempPlace(place) && sourceUseCountAfterQuad(quadIndex, place) == 1 &&
           nextQuadCanUseAx(quadIndex, place);
}

static bool emitStoreAxToPlace(const std::string& place) {
    if (!emitInstruction("mov word " + memoryOperandForPlace(place) + ", ax")) {
        return false;
    }

    rememberAxPlace(place);
    return true;
}

static bool emitReadIntegerCall(const std::string& target) {
    g_needsReadIntegerRuntime = true;

    if (!emitInstruction("call GetAnInteger"))
        return false;
    invalidateAxCache();
    if (!emitInstruction("mov ax, word [ReadInt]"))
        return false;
    if (!emitStoreAxToPlace(target))
        return false;
    return true;
}

static bool emitPrintCall(const std::string& source) {
    g_needsPrintRuntime = true;

    if (!emitLoadAxFromPlace(source))
        return false;
    if (!emitInstruction("call ConvertIntegerToString"))
        return false;
    if (!emitInstruction("call PrintResult"))
        return false;
    invalidateAxCache();
    return true;
}

static bool emitArrayAddressSetup(const std::string& arrayName, const std::string& indexPlace) {
    int length = arrayLengthFor(arrayName);
    if (length <= 0) {
        reportCodeGeneratorError("array setup requested for non-array symbol: " + arrayName);
        return false;
    }

    g_needsArrayBoundsRuntime = true;

    if (!emitLoadAxFromPlace(indexPlace))
        return false;
    if (!emitInstruction("cmp ax, " + std::to_string(length)))
        return false;
    if (!emitInstruction("jae ArrayBoundsError"))
        return false;
    if (!emitInstruction("movzx rax, ax"))
        return false;
    if (!emitInstruction("shl rax, 1"))
        return false;
    if (!emitInstruction("lea rbx, [rel " + sanitizeLabel(arrayName) + "]"))
        return false;
    invalidateAxCache();
    return true;
}

static bool generateUserCodeFromQuads() {
    for (int i = 0; i < g_quadCount; i++) {
        const Quad& q = g_quads[i];

        if (q.op == "PROC") {
            invalidateAxCache();
            if (!emitLabel(q.arg1))
                return false;
        } else if (q.op == "RET") {
            invalidateAxCache();
            if (!emitInstruction("ret"))
                return false;
        } else if (q.op == "START") {
            invalidateAxCache();
            if (!emitLabel("START"))
                return false;
        } else if (q.op == "ODD") {
            if (!emitLoadAxFromPlace(q.arg1))
                return false;
            if (!emitInstruction("and ax, 1"))
                return false;
            if (!emitInstruction("cmp ax, 1"))
                return false;
            invalidateAxCache();
        } else if (q.op == "+") {
            if (!emitLoadAxFromPlace(q.arg1))
                return false;
            if (!emitInstruction("add ax, word " + memoryOperandForPlace(q.arg2)))
                return false;
            if (shouldKeepTempOnlyInAx(i, q.result)) {
                rememberAxPlace(q.result);
            } else if (!emitStoreAxToPlace(q.result)) {
                return false;
            }
        } else if (q.op == "-") {
            if (!emitLoadAxFromPlace(q.arg1))
                return false;
            if (!emitInstruction("sub ax, word " + memoryOperandForPlace(q.arg2)))
                return false;
            if (shouldKeepTempOnlyInAx(i, q.result)) {
                rememberAxPlace(q.result);
            } else if (!emitStoreAxToPlace(q.result)) {
                return false;
            }
        } else if (q.op == "*") {
            if (!emitLoadAxFromPlace(q.arg1))
                return false;
            if (!emitInstruction("mul word " + memoryOperandForPlace(q.arg2)))
                return false;
            if (shouldKeepTempOnlyInAx(i, q.result)) {
                rememberAxPlace(q.result);
            } else if (!emitStoreAxToPlace(q.result)) {
                return false;
            }
        } else if (q.op == "/") {
            if (!emitLoadAxFromPlace(q.arg1))
                return false;
            if (!emitInstruction("xor dx, dx"))
                return false;
            if (!emitInstruction("div word " + memoryOperandForPlace(q.arg2)))
                return false;
            if (shouldKeepTempOnlyInAx(i, q.result)) {
                rememberAxPlace(q.result);
            } else if (!emitStoreAxToPlace(q.result)) {
                return false;
            }
        } else if (q.op == "=") {
            if (!emitLoadAxFromPlace(q.arg2))
                return false;
            if (!emitStoreAxToPlace(q.arg1))
                return false;
        } else if (q.op == "LOADA") {
            if (!emitArrayAddressSetup(q.arg1, q.arg2))
                return false;
            if (!emitInstruction("mov dx, word [rbx + rax]"))
                return false;
            if (shouldKeepTempOnlyInAx(i, q.result)) {
                if (!emitInstruction("mov ax, dx"))
                    return false;
                rememberAxPlace(q.result);
            } else {
                if (!emitInstruction("mov word " + memoryOperandForPlace(q.result) + ", dx"))
                    return false;
                invalidateAxCache();
            }
        } else if (q.op == "STOREA") {
            if (!emitArrayAddressSetup(q.arg1, q.arg2))
                return false;
            if (!emitInstruction("mov dx, word " + memoryOperandForPlace(q.result)))
                return false;
            if (!emitInstruction("mov word [rbx + rax], dx"))
                return false;
            invalidateAxCache();
        } else if (isRelationalOp(q.op)) {
            if (!emitLoadAxFromPlace(q.arg1))
                return false;
            if (!emitInstruction("cmp ax, word " + memoryOperandForPlace(q.arg2)))
                return false;
        } else if (q.op == "THEN" || q.op == "DO") {
            const std::string jumpText = jumpMnemonicFromQuadCode(q.arg2);
            if (!emitInstruction(jumpText + " " + sanitizeLabel(q.arg1)))
                return false;
            invalidateAxCache();
        } else if (q.op == "JMP") {
            if (!emitInstruction("jmp " + sanitizeLabel(q.arg1)))
                return false;
            invalidateAxCache();
        } else if (q.op == "CALL") {
            const std::string procNameUpper = toUpperCopy(q.arg1);
            if (procNameUpper == "READINTEGER") {
                if (!emitReadIntegerCall(q.arg2))
                    return false;
            } else if (procNameUpper == "PRINT") {
                if (!emitPrintCall(q.arg2))
                    return false;
            } else {
                if (!emitInstruction("call " + sanitizeLabel(q.arg1)))
                    return false;
                invalidateAxCache();
            }
        } else if (isLabelQuad(q)) {
            invalidateAxCache();
            if (!emitLabel(q.op))
                return false;
        } else {
            reportCodeGeneratorError("unsupported quad op: " + q.op);
            return false;
        }
    }

    return true;
}

static bool emitRuntimeRoutines() {
    if (g_needsPrintRuntime || g_needsReadIntegerRuntime || g_needsArrayBoundsRuntime) {
        if (!emitInstruction("mov eax, sys_exit"))
            return false;
        if (!emitInstruction("xor ebx, ebx"))
            return false;
        if (!emitInstruction("int 80h"))
            return false;
    }

    if (g_needsArrayBoundsRuntime) {
        if (!emitLabel("ArrayBoundsError"))
            return false;
        if (g_needsPrintRuntime || g_needsReadIntegerRuntime) {
            if (!emitInstruction("mov eax, sys_write"))
                return false;
            if (!emitInstruction("mov ebx, stdout"))
                return false;
            if (!emitInstruction("mov ecx, BoundsMsg"))
                return false;
            if (!emitInstruction("mov edx, lenBoundsMsg"))
                return false;
            if (!emitInstruction("int 80h"))
                return false;
        }
        if (!emitInstruction("mov eax, sys_exit"))
            return false;
        if (!emitInstruction("mov ebx, 1"))
            return false;
        if (!emitInstruction("int 80h"))
            return false;
    }

    if (g_needsReadIntegerRuntime) {
        if (!emitLabel("PrintEnterMessage"))
            return false;
        if (!emitInstruction("mov eax, sys_write"))
            return false;
        if (!emitInstruction("mov ebx, stdout"))
            return false;
        if (!emitInstruction("mov ecx, userMsg"))
            return false;
        if (!emitInstruction("mov edx, lenUserMsg"))
            return false;
        if (!emitInstruction("int 80h"))
            return false;
        if (!emitInstruction("ret"))
            return false;

        if (!emitLabel("GetAnInteger"))
            return false;
        if (!emitInstruction("call PrintEnterMessage"))
            return false;
        if (!emitInstruction("mov eax, sys_read"))
            return false;
        if (!emitInstruction("mov ebx, stdin"))
            return false;
        if (!emitInstruction("mov ecx, ReadBuffer"))
            return false;
        if (!emitInstruction("mov edx, 8"))
            return false;
        if (!emitInstruction("int 80h"))
            return false;
        if (!emitInstruction("mov word [ReadInt], 0"))
            return false;
        if (!emitInstruction("lea rsi, [rel ReadBuffer]"))
            return false;
        if (!emitLabel("ReadLoop"))
            return false;
        if (!emitInstruction("xor bx, bx"))
            return false;
        if (!emitInstruction("mov bl, byte [rsi]"))
            return false;
        if (!emitInstruction("cmp bl, 10"))
            return false;
        if (!emitInstruction("je ReadDone"))
            return false;
        if (!emitInstruction("cmp bl, 13"))
            return false;
        if (!emitInstruction("je ReadDone"))
            return false;
        if (!emitInstruction("cmp bl, ' '"))
            return false;
        if (!emitInstruction("je ReadDone"))
            return false;
        if (!emitInstruction("sub bl, '0'"))
            return false;
        if (!emitInstruction("mov ax, word [ReadInt]"))
            return false;
        if (!emitInstruction("mov dx, word [Ten]"))
            return false;
        if (!emitInstruction("mul dx"))
            return false;
        if (!emitInstruction("add ax, bx"))
            return false;
        if (!emitInstruction("mov word [ReadInt], ax"))
            return false;
        if (!emitInstruction("inc rsi"))
            return false;
        if (!emitInstruction("jmp ReadLoop"))
            return false;
        if (!emitLabel("ReadDone"))
            return false;
        if (!emitInstruction("ret"))
            return false;
    }

    if (g_needsPrintRuntime) {
        if (!emitLabel("ConvertIntegerToString"))
            return false;
        if (!emitInstruction("lea rdi, [rel ResultValue + 4]"))
            return false;
        if (!emitInstruction("lea rsi, [rel ResultValue]"))
            return false;
        if (!emitLabel("ConvertLoop"))
            return false;
        if (!emitInstruction("xor dx, dx"))
            return false;
        if (!emitInstruction("mov cx, 10"))
            return false;
        if (!emitInstruction("div cx"))
            return false;
        if (!emitInstruction("add dl, '0'"))
            return false;
        if (!emitInstruction("mov [rdi], dl"))
            return false;
        if (!emitInstruction("dec rdi"))
            return false;
        if (!emitInstruction("cmp rdi, rsi"))
            return false;
        if (!emitInstruction("jge ConvertLoop"))
            return false;
        if (!emitInstruction("ret"))
            return false;

        if (!emitLabel("PrintResult"))
            return false;
        if (!emitInstruction("mov eax, sys_write"))
            return false;
        if (!emitInstruction("mov ebx, stdout"))
            return false;
        if (!emitInstruction("mov ecx, Result"))
            return false;
        if (!emitInstruction("mov edx, ResultEnd"))
            return false;
        if (!emitInstruction("int 80h"))
            return false;
        if (!emitInstruction("ret"))
            return false;
    }

    if (!g_needsPrintRuntime && !g_needsReadIntegerRuntime && !g_needsArrayBoundsRuntime) {
        if (!emitInstruction("mov eax, sys_exit"))
            return false;
        if (!emitInstruction("xor ebx, ebx"))
            return false;
        if (!emitInstruction("int 80h"))
            return false;
    }

    return true;
}

static bool writeAssemblyFile(const std::filesystem::path& asmOutputPath) {
    std::ofstream out(asmOutputPath);
    if (!out.is_open()) {
        reportCodeGeneratorError("failed to open assembly output file: " +
                                 asmOutputPath.string());
        return false;
    }

    out << "; Behold my upper crust!!\n";
    out << "BITS 64\n";
    out << "DEFAULT REL\n\n";

    out << "global _start\n\n";

    out << "sys_exit equ 1\n";
    out << "sys_read equ 3\n";
    out << "sys_write equ 4\n";
    out << "stdin equ 0\n";
    out << "stdout equ 1\n\n";

    out << "section .data\n";
    for (int i = 0; i < g_symbolCount; i++) {
        const SymbolEntry& entry = g_symbols[i];
        if (entry.classification == SYM_PROGRAM_NAME) {
            continue;
        }

        if (entry.classification == SYM_CONST || entry.classification == SYM_NUM_LITERAL) {
            out << "    " << symbolToAsmLabel(entry.symbol) << " dw " << entry.value
                << "    ; DS offset " << entry.address << "\n";
        }
    }

    // Small numbers good, big numbers bad!!
    if (g_needsReadIntegerRuntime || g_needsPrintRuntime) {
        out << "    userMsg db 'Enter an integer(less than 32,765): '\n";
        out << "    lenUserMsg equ $-userMsg\n";
        out << "    Ten dw 10\n";
    }

    if (g_needsPrintRuntime) {
        out << "    Result db 'Ans = '\n";
        out << "    ResultValue db '00000'\n";
        out << "    db 0xA\n";
        out << "    ResultEnd equ $-Result\n";
    }

    if (g_needsArrayBoundsRuntime) {
        out << "    BoundsMsg db 'Array bounds error', 0xA\n";
        out << "    lenBoundsMsg equ $-BoundsMsg\n";
    }

    out << "\nsection .bss\n";
    for (int i = 0; i < g_symbolCount; i++) {
        const SymbolEntry& entry = g_symbols[i];
        if (entry.classification == SYM_VAR || entry.classification == SYM_TEMP) {
            out << "    " << symbolToAsmLabel(entry.symbol) << " resw 1"
                << "    ; DS offset " << entry.address << "\n";
        } else if (entry.classification == SYM_ARRAY) {
            out << "    " << symbolToAsmLabel(entry.symbol) << " resw " << entry.value
                << "    ; DS offset " << entry.address << ", length " << entry.value << "\n";
        }
    }

    if (g_needsReadIntegerRuntime) {
        out << "    ReadBuffer resb 8\n";
        out << "    ReadInt resw 1\n";
    }

    out << "\nsection .text\n";
    out << "_start:\n";
    out << "    jmp START\n";

    for (int i = 0; i < g_codeLineCount; i++) {
        if (g_codeLines[i].isLabel) {
            out << g_codeLines[i].text << "\n";
        } else {
            out << "    " << g_codeLines[i].text << "\n";
        }
    }

    return true;
}

static bool writeNumericListing(const std::filesystem::path& numericCodeOutputPath) {
    std::ofstream out(numericCodeOutputPath);
    if (!out.is_open()) {
        reportCodeGeneratorError("failed to open numeric code output file: " +
                                 numericCodeOutputPath.string());
        return false;
    }

    out << "Address\tCode\n";
    out << 0 << "\t_start:\n";
    out << 0 << "\tjmp START\n";

    for (int i = 0; i < g_codeLineCount; i++) {
        out << g_codeLines[i].address << '\t' << g_codeLines[i].text << '\n';
    }

    return true;
}

// Code Generator?! I hardly know her!!
bool generateAssemblyFromQuadAndSymbolFiles(
    const std::filesystem::path& quadInputPath, const std::filesystem::path& symbolInputPath,
    const std::filesystem::path& asmOutputPath,
    const std::filesystem::path& numericCodeOutputPath) {
    resetCodeGeneratorState();

    if (!loadSymbols(symbolInputPath)) {
        return false;
    }

    if (!loadQuads(quadInputPath)) {
        return false;
    }

    if (!generateUserCodeFromQuads()) {
        return false;
    }

    if (!emitRuntimeRoutines()) {
        return false;
    }

    if (!writeAssemblyFile(asmOutputPath)) {
        return false;
    }

    if (!writeNumericListing(numericCodeOutputPath)) {
        return false;
    }

    return (g_codeGeneratorErrorCount == 0);
}
