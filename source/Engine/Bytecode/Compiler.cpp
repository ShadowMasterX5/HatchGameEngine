#if INTERFACE
#include <Engine/Bytecode/Types.h>
#include <Engine/Bytecode/CompilerEnums.h>

class Compiler {
public:
    static Parser               parser;
    static Scanner              scanner;
    static ParseRule*           Rules;
    static vector<ObjFunction*> Functions;
    static HashMap<Token>*      TokenMap;
    static const char*          Magic;
    static bool                 PrettyPrint;

    class Compiler* Enclosing = NULL;
    ObjFunction*    Function = NULL;
    int             Type = 0;
    Local           Locals[0x100];
    int             LocalCount = 0;
    int             ScopeDepth = 0;
    int             WithDepth = 0;
    vector<Uint32>  ClassHashList;
    vector<Uint32>  ClassExtendedList;
};
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <Engine/Diagnostics/Log.h>
#include <Engine/Bytecode/Compiler.h>
#include <Engine/Bytecode/GarbageCollector.h>
#include <Engine/IO/FileStream.h>

#include <Engine/Application.h>

Parser               Compiler::parser;
Scanner              Compiler::scanner;
ParseRule*           Compiler::Rules = NULL;
vector<ObjFunction*> Compiler::Functions;
HashMap<Token>*      Compiler::TokenMap = NULL;
const char*          Compiler::Magic = "HTVM";
bool                 Compiler::PrettyPrint = true;
vector<ObjString*>   Strings;

Enum            Enums[0x100];
int             EnumsCount = 0;

#define Panic(returnMe) if (parser.PanicMode) { SynchronizeToken(); return returnMe; }

// Order these by C/C++ precedence operators
enum TokenTYPE {
    // Other
    TOKEN_LEFT_BRACE,
    TOKEN_RIGHT_BRACE,
    // Precedence 2
    TOKEN_DECREMENT,
    TOKEN_INCREMENT,
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_SQUARE_BRACE,
    TOKEN_RIGHT_SQUARE_BRACE,
    TOKEN_DOT,
    // Precedence 3
    TOKEN_LOGICAL_NOT, // (!)
    TOKEN_BITWISE_NOT, // (~)
    // Precedence 5
    TOKEN_MULTIPLY,
    TOKEN_DIVISION,
    TOKEN_MODULO,
    // Precedence 6
    TOKEN_PLUS,
    TOKEN_MINUS,
    // Precedence 7
    TOKEN_BITWISE_LEFT,
    TOKEN_BITWISE_RIGHT,
    // Precedence 8
    // Precedence 9
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,
    // Precedence 10
    TOKEN_EQUALS,
    TOKEN_NOT_EQUALS,
    // Precedence 11
    TOKEN_BITWISE_AND,
    TOKEN_BITWISE_XOR,
    TOKEN_BITWISE_OR,
    // Precedence 14
    TOKEN_LOGICAL_AND,
    TOKEN_LOGICAL_OR,
    // Precedence 16
    TOKEN_TERNARY,
    TOKEN_COLON,
    // Assignments
    TOKEN_ASSIGNMENT,
    TOKEN_ASSIGNMENT_MULTIPLY,
    TOKEN_ASSIGNMENT_DIVISION,
    TOKEN_ASSIGNMENT_MODULO,
    TOKEN_ASSIGNMENT_PLUS,
    TOKEN_ASSIGNMENT_MINUS,
    TOKEN_ASSIGNMENT_BITWISE_LEFT,
    TOKEN_ASSIGNMENT_BITWISE_RIGHT,
    TOKEN_ASSIGNMENT_BITWISE_AND,
    TOKEN_ASSIGNMENT_BITWISE_XOR,
    TOKEN_ASSIGNMENT_BITWISE_OR,
    // Precedence 17
    TOKEN_COMMA,
    TOKEN_SEMICOLON,

    // Others
    TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER, TOKEN_DECIMAL,

    // Literals.
    TOKEN_FALSE,
    TOKEN_TRUE,
    TOKEN_NULL,

    // Script Keywords.
    TOKEN_EVENT,
    TOKEN_VAR,
    TOKEN_REF,

    // Keywords.
    TOKEN_DO,
    TOKEN_IF,
    TOKEN_OR,
    TOKEN_AND,
    TOKEN_FOR,
    TOKEN_CASE,
    TOKEN_ELSE,
    TOKEN_ENUM,
    TOKEN_THIS,
    TOKEN_WITH,
    TOKEN_SUPER,
    TOKEN_BREAK,
    TOKEN_CLASS,
    TOKEN_WHILE,
    TOKEN_REPEAT,
    TOKEN_RETURN,
    TOKEN_SWITCH,
    TOKEN_DEFAULT,
    TOKEN_CONTINUE,

    // HeaderReader token types.
    // TOKEN_STRUCT, TOKEN_PUBLIC, TOKEN_PRIVATE, TOKEN_PROTECTED, TOKEN_LEFT_HOOK, TOKEN_RIGHT_HOOK, TOKEN_UNION,

    TOKEN_PRINT,

    TOKEN_ERROR,
    TOKEN_EOF
};
enum FunctionType {
    TYPE_TOP_LEVEL,
    TYPE_FUNCTION,
    TYPE_CONSTRUCTOR,
    TYPE_METHOD,
    TYPE_WITH,
};

PUBLIC Token         Compiler::MakeToken(int type) {
    Token token;
    token.Type = type;
    token.Start = scanner.Start;
    token.Length = (int)(scanner.Current - scanner.Start);
    token.Line = scanner.Line;
    token.Pos = (scanner.Start - scanner.LinePos) + 1;

    return token;
}
PUBLIC Token         Compiler::MakeTokenRaw(int type, const char* message) {
    Token token;
    token.Type = type;
    token.Start = (char*)message;
    token.Length = (int)strlen(message);
    token.Line = 0;
    token.Pos = scanner.Current - scanner.LinePos;

    return token;
}
PUBLIC Token         Compiler::ErrorToken(const char* message) {
    Token token;
    token.Type = TOKEN_ERROR;
    token.Start = (char*)message;
    token.Length = (int)strlen(message);
    token.Line = scanner.Line;
    token.Pos = scanner.Current - scanner.LinePos;

    return token;
}

PUBLIC bool          Compiler::IsEOF() {
    return *scanner.Current == 0;
}
PUBLIC bool          Compiler::IsDigit(char c) {
    return c >= '0' && c <= '9';
}
PUBLIC bool          Compiler::IsHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}
PUBLIC bool          Compiler::IsAlpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
PUBLIC bool          Compiler::IsIdentifierStart(char c) {
    return IsAlpha(c) || c == '$' || c == '_';
}
PUBLIC bool          Compiler::IsIdentifierBody(char c) {
    return IsIdentifierStart(c) || IsDigit(c);
}

PUBLIC bool          Compiler::MatchChar(char expected) {
    if (IsEOF()) return false;
    if (*scanner.Current != expected) return false;

    scanner.Current++;
    return true;
}
PUBLIC char          Compiler::AdvanceChar() {
    return *scanner.Current++;
    // scanner.Current++;
    // return *(scanner.Current - 1);
}
PUBLIC char          Compiler::PrevChar() {
    return *(scanner.Current - 1);
}
PUBLIC char          Compiler::PeekChar() {
    return *scanner.Current;
}
PUBLIC char          Compiler::PeekNextChar() {
    if (IsEOF()) return 0;
    return *(scanner.Current + 1);
}

PUBLIC VIRTUAL void  Compiler::SkipWhitespace() {
    while (true) {
        char c = PeekChar();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                AdvanceChar(); // char in 'c'
                break;

            case '\n':
                scanner.Line++;
                AdvanceChar(); // char in 'c'
                scanner.LinePos = scanner.Current;
                break;

            case '/':
                if (PeekNextChar() == '/') {
                    AdvanceChar(); // char in 'c'
                    AdvanceChar(); // '/'
                    while (PeekChar() != '\n' && !IsEOF()) AdvanceChar();
                }
                else if (PeekNextChar() == '*') {
                    AdvanceChar(); // char in 'c'
                    AdvanceChar(); // '*'
                    do {
                        if (PeekChar() == '\n') {
                            scanner.Line++;
                            AdvanceChar();
                            scanner.LinePos = scanner.Current;
                        }
                        else if (PeekChar() == '*') {
                            AdvanceChar(); // '*'
                            if (PeekChar() == '/') {
                                break;
                            }
                        }
                        else {
                            AdvanceChar();
                        }
                    }
                    while (!IsEOF());

                    if (!IsEOF()) AdvanceChar();
                }
                else
                    return;
                break;

            default:
                return;
        }
    }
}

// Token functions
PUBLIC int           Compiler::CheckKeyword(int start, int length, const char* rest, int type) {
    if (scanner.Current - scanner.Start == start + length &&
        (!rest || memcmp(scanner.Start + start, rest, length) == 0))
        return type;

    return TOKEN_IDENTIFIER;
}
PUBLIC VIRTUAL int   Compiler::GetKeywordType() {
    switch (*scanner.Start) {
        case 'a':
            return CheckKeyword(1, 2, "nd", TOKEN_AND);
        case 'b':
            return CheckKeyword(1, 4, "reak", TOKEN_BREAK);
        case 'c':
            if (scanner.Current - scanner.Start > 1) {
                switch (*(scanner.Start + 1)) {
                    case 'a': return CheckKeyword(2, 2, "se", TOKEN_CASE);
                    case 'l': return CheckKeyword(2, 3, "ass", TOKEN_CLASS);
                    case 'o': return CheckKeyword(2, 6, "ntinue", TOKEN_CONTINUE);
                }
            }
            break;
        case 'd':
            if (scanner.Current - scanner.Start > 1) {
                switch (*(scanner.Start + 1)) {
                    case 'e': return CheckKeyword(2, 5, "fault", TOKEN_DEFAULT);
                    case 'o': return CheckKeyword(2, 0, NULL, TOKEN_DO);
                }
            }
            break;
        case 'e':
            if (scanner.Current - scanner.Start > 1) {
                switch (*(scanner.Start + 1)) {
                    case 'l': return CheckKeyword(2, 2, "se", TOKEN_ELSE);
                    case 'n': return CheckKeyword(2, 2, "um", TOKEN_ENUM);
                    case 'v': return CheckKeyword(2, 3, "ent", TOKEN_EVENT);
                }
            }
            break;
        case 'f':
            if (scanner.Current - scanner.Start > 1) {
                switch (*(scanner.Start + 1)) {
                    case 'a': return CheckKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o': return CheckKeyword(2, 1, "r", TOKEN_FOR);
                    // case 'u': return CheckKeyword(2, 1, "n", TOKEN_FUN);
                }
            }
            break;
        case 'i':
            return CheckKeyword(1, 1, "f", TOKEN_IF);
        case 'n':
            return CheckKeyword(1, 3, "ull", TOKEN_NULL);
        case 'o':
            if (scanner.Current - scanner.Start > 1) {
                switch (*(scanner.Start + 1)) {
                    case 'r': return CheckKeyword(2, 0, NULL, TOKEN_OR);
                    // case 'b': return CheckKeyword(2, 4, "ject", TOKEN_OBJECT);
                }
            }
            break;
        case 'p':
            return CheckKeyword(1, 4, "rint", TOKEN_PRINT);
            // if (scanner.Current - scanner.Start > 1) {
            //     switch (*(scanner.Start + 1)) {
            //         case 'r':
            //             if (scanner.Current - scanner.Start > 2) {
            //                 switch (*(scanner.Start + 2)) {
            //                     case 'i': return CheckKeyword(3, 4, "vate", TOKEN_PRIVATE);
            //                     case 'o': return CheckKeyword(3, 6, "tected", TOKEN_PROTECTED);
            //                 }
            //             }
            //             break;
            //         case 'u': return CheckKeyword(2, 4, "blic", TOKEN_PUBLIC);
            //     }
            // }
            break;
        case 'r':
            if (scanner.Current - scanner.Start > 1) {
                switch (*(scanner.Start + 1)) {
                    case 'e':
                        if (scanner.Current - scanner.Start > 2) {
                            switch (*(scanner.Start + 2)) {
                                case 't': return CheckKeyword(3, 3, "urn", TOKEN_RETURN);
                                case 'p': return CheckKeyword(3, 3, "eat", TOKEN_REPEAT);
                            }
                        }
                        break;
                }
            }
            break;
        case 's':
            if (scanner.Current - scanner.Start > 1) {
                switch (*(scanner.Start + 1)) {
                    // case 't': return CheckKeyword(2, 4, "ruct", TOKEN_STRUCT);
                    case 'u':
                        if (scanner.Current - scanner.Start > 2) {
                            switch (*(scanner.Start + 2)) {
                                // case 'b': return CheckKeyword(3, 5, "class", TOKEN_SUBCLASS);
                                case 'p': return CheckKeyword(3, 2, "er", TOKEN_SUPER);
                            }
                        }
                        break;
                    case 'w': return CheckKeyword(2, 4, "itch", TOKEN_SWITCH);
                }
            }
            break;
        case 't':
            if (scanner.Current - scanner.Start > 1) {
                switch (*(scanner.Start + 1)) {
                    case 'h': return CheckKeyword(2, 2, "is", TOKEN_THIS);
                    case 'r': return CheckKeyword(2, 2, "ue", TOKEN_TRUE);
                }
            }
            break;
        case 'v':
            return CheckKeyword(1, 2, "ar", TOKEN_VAR);
        case 'w':
            if (scanner.Current - scanner.Start > 1) {
                switch (*(scanner.Start + 1)) {
                    case 'i': return CheckKeyword(2, 2, "th", TOKEN_WITH);
                    case 'h': return CheckKeyword(2, 3, "ile", TOKEN_WHILE);
                }
            }
            break;
    }

    return TOKEN_IDENTIFIER;
}

PUBLIC Token         Compiler::StringToken() {
    while (PeekChar() != '"' && !IsEOF()) {
        bool lineBreak = false;
        switch (PeekChar()) {
            // Line Break
            case '\n':
                lineBreak = true;
                break;
            // Escaped
            case '\\':
                AdvanceChar();
                break;
        }

        AdvanceChar();

        if (lineBreak) {
            scanner.Line++;
            scanner.LinePos = scanner.Current;
        }
    }

    if (IsEOF()) return ErrorToken("Unterminated string.");

    // The closing double-quote.
    AdvanceChar();
    return MakeToken(TOKEN_STRING);
}
PUBLIC Token         Compiler::NumberToken() {
    if (*scanner.Start == '0' && (PeekChar() == 'x' || PeekChar() == 'X')) {
        AdvanceChar(); // x
        while (IsHexDigit(PeekChar()))
            AdvanceChar();
        return MakeToken(TOKEN_NUMBER);
    }

    while (IsDigit(PeekChar()))
        AdvanceChar();

    // Look for a fractional part.
    if (PeekChar() == '.' && IsDigit(PeekNextChar())) {
        // Consume the "."
        AdvanceChar();

        while (IsDigit(PeekChar()))
            AdvanceChar();

        return MakeToken(TOKEN_DECIMAL);
    }

    return MakeToken(TOKEN_NUMBER);
}
PUBLIC Token         Compiler::IdentifierToken() {
    while (IsIdentifierBody(PeekChar()))
        AdvanceChar();

    return MakeToken(GetKeywordType());
}
PUBLIC VIRTUAL Token Compiler::ScanToken() {
    SkipWhitespace();

    scanner.Start = scanner.Current;

    if (IsEOF()) return MakeToken(TOKEN_EOF);

    char c = AdvanceChar();

    if (IsDigit(c)) return NumberToken();
    if (IsIdentifierStart(c)) return IdentifierToken();

    switch (c) {
        case '(': return MakeToken(TOKEN_LEFT_PAREN);
        case ')': return MakeToken(TOKEN_RIGHT_PAREN);
        case '{': return MakeToken(TOKEN_LEFT_BRACE);
        case '}': return MakeToken(TOKEN_RIGHT_BRACE);
        case '[': return MakeToken(TOKEN_LEFT_SQUARE_BRACE);
        case ']': return MakeToken(TOKEN_RIGHT_SQUARE_BRACE);
        case ';': return MakeToken(TOKEN_SEMICOLON);
        case ',': return MakeToken(TOKEN_COMMA);
        case '.': return MakeToken(TOKEN_DOT);
        case ':': return MakeToken(TOKEN_COLON);
        case '?': return MakeToken(TOKEN_TERNARY);
        case '~': return MakeToken(TOKEN_BITWISE_NOT);
        // case '#': return MakeToken(TOKEN_HASH);
        // Two-char punctuations
        case '!': return MakeToken(MatchChar('=') ? TOKEN_NOT_EQUALS : TOKEN_LOGICAL_NOT);
        case '=': return MakeToken(MatchChar('=') ? TOKEN_EQUALS : TOKEN_ASSIGNMENT);

        case '*': return MakeToken(MatchChar('=') ? TOKEN_ASSIGNMENT_MULTIPLY : TOKEN_MULTIPLY);
        case '/': return MakeToken(MatchChar('=') ? TOKEN_ASSIGNMENT_DIVISION : TOKEN_DIVISION);
        case '%': return MakeToken(MatchChar('=') ? TOKEN_ASSIGNMENT_MODULO : TOKEN_MODULO);
        case '+': return MakeToken(MatchChar('=') ? TOKEN_ASSIGNMENT_PLUS : MatchChar('+') ? TOKEN_INCREMENT : TOKEN_PLUS);
        case '-': return MakeToken(MatchChar('=') ? TOKEN_ASSIGNMENT_MINUS : MatchChar('-') ? TOKEN_DECREMENT : TOKEN_MINUS);
        case '<': return MakeToken(MatchChar('<') ? MatchChar('=') ? TOKEN_ASSIGNMENT_BITWISE_LEFT : TOKEN_BITWISE_LEFT : MatchChar('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>': return MakeToken(MatchChar('>') ? MatchChar('=') ? TOKEN_ASSIGNMENT_BITWISE_RIGHT : TOKEN_BITWISE_RIGHT : MatchChar('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '&': return MakeToken(MatchChar('=') ? TOKEN_ASSIGNMENT_BITWISE_AND : MatchChar('&') ? TOKEN_LOGICAL_AND : TOKEN_BITWISE_AND);
        case '|': return MakeToken(MatchChar('=') ? TOKEN_ASSIGNMENT_BITWISE_OR : MatchChar('|') ? TOKEN_LOGICAL_OR  : TOKEN_BITWISE_OR);
        case '^': return MakeToken(MatchChar('=') ? TOKEN_ASSIGNMENT_BITWISE_XOR : TOKEN_BITWISE_XOR);
        // String
        case '"': return StringToken();
    }

    return ErrorToken("Unexpected character.");
}

PUBLIC void          Compiler::AdvanceToken() {
    parser.Previous = parser.Current;

    while (true) {
        parser.Current = ScanToken();
        if (parser.Current.Type != TOKEN_ERROR)
            break;

        ErrorAtCurrent(parser.Current.Start);
    }
}
PUBLIC Token         Compiler::NextToken() {
    AdvanceToken();
    return parser.Previous;
}
PUBLIC Token         Compiler::PeekToken() {
    return parser.Current;
}
PUBLIC Token         Compiler::PrevToken() {
    return parser.Previous;
}
PUBLIC bool          Compiler::MatchToken(int expectedType) {
    if (!CheckToken(expectedType)) return false;
    AdvanceToken();
    return true;
}
PUBLIC bool          Compiler::MatchAssignmentToken() {
    switch (parser.Current.Type) {
        case TOKEN_ASSIGNMENT:
        case TOKEN_ASSIGNMENT_MULTIPLY:
        case TOKEN_ASSIGNMENT_DIVISION:
        case TOKEN_ASSIGNMENT_MODULO:
        case TOKEN_ASSIGNMENT_PLUS:
        case TOKEN_ASSIGNMENT_MINUS:
        case TOKEN_ASSIGNMENT_BITWISE_LEFT:
        case TOKEN_ASSIGNMENT_BITWISE_RIGHT:
        case TOKEN_ASSIGNMENT_BITWISE_AND:
        case TOKEN_ASSIGNMENT_BITWISE_XOR:
        case TOKEN_ASSIGNMENT_BITWISE_OR:
            AdvanceToken();
            return true;

        case TOKEN_INCREMENT:
        case TOKEN_DECREMENT:
            AdvanceToken();
            return true;

        default:
            break;
    }
    return false;
}
PUBLIC bool          Compiler::CheckToken(int expectedType) {
    return parser.Current.Type == expectedType;
}
PUBLIC void          Compiler::ConsumeToken(int type, const char* message) {
    if (parser.Current.Type == type) {
        AdvanceToken();
        return;
    }

    ErrorAtCurrent(message);
}

PUBLIC void          Compiler::SynchronizeToken() {
    parser.PanicMode = false;

    while (PeekToken().Type != TOKEN_EOF) {
        if (PrevToken().Type == TOKEN_SEMICOLON) return;

        switch (PeekToken().Type) {
            case TOKEN_ENUM:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_DO:
            case TOKEN_FOR:
            case TOKEN_SWITCH:
            case TOKEN_CASE:
            case TOKEN_DEFAULT:
            case TOKEN_RETURN:
            case TOKEN_BREAK:
            case TOKEN_CONTINUE:
            case TOKEN_VAR:
            case TOKEN_EVENT:
            case TOKEN_PRINT:
                return;
            default:
                break;
        }

        AdvanceToken();
    }
}

// Error handling
PUBLIC bool          Compiler::ReportError(int line, const char* string, ...) {
    char message[1024];
    memset(message, 0, 1024);

    va_list args;
    va_start(args, string);
    vsprintf(message, string, args);
    va_end(args);

    if (line > 0)
        Log::Print(Log::LOG_ERROR, "in file '%s' on line %d:\n    %s\n\n", scanner.SourceFilename, line, message);
    else
        Log::Print(Log::LOG_ERROR, "in file '%s' on line %d:\n    %s\n\n", scanner.SourceFilename, line, message);

    assert(false);
    return false;
}
PUBLIC bool          Compiler::ReportErrorPos(int line, int pos, const char* string, ...) {
    char message[1024];
    memset(message, 0, 1024);

    va_list args;
    va_start(args, string);
    vsprintf(message, string, args);
    va_end(args);

	char* textBuffer = (char*)malloc(512);

	PrintBuffer buffer;
	buffer.Buffer = &textBuffer;
	buffer.WriteIndex = 0;
	buffer.BufferSize = 512;

    if (line > 0)
		buffer_printf(&buffer, "In file '%s' on line %d, position %d:\n    %s\n\n", scanner.SourceFilename, line, pos, message);
    else
		buffer_printf(&buffer, "In file '%s' on line %d, position %d:\n    %s\n\n", scanner.SourceFilename, line, pos, message);

	bool fatal = true;

	Log::Print(Log::LOG_ERROR, textBuffer);

	const SDL_MessageBoxButtonData buttonsError[] = {
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "Exit Game" },
		{ 0                                      , 2, "Ignore All" },
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Continue" },
	};
	const SDL_MessageBoxButtonData buttonsFatal[] = {
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "Exit Game" },
	};

	const SDL_MessageBoxData messageBoxData = {
		SDL_MESSAGEBOX_ERROR, NULL,
		"Syntax Error",
		textBuffer,
		(int)(fatal ? SDL_arraysize(buttonsFatal) : SDL_arraysize(buttonsError)),
		fatal ? buttonsFatal : buttonsError,
		NULL,
	};

	int buttonClicked;
	if (SDL_ShowMessageBox(&messageBoxData, &buttonClicked) < 0) {
		buttonClicked = 2;
	}

	free(textBuffer);

	switch (buttonClicked) {
		// Exit Game
		case 1:
			exit(-1);
			// NOTE: This is for later, this doesn't actually execute.
			return ERROR_RES_EXIT;
			// Ignore All
		case 2:
			// VMThread::InstructionIgnoreMap[000000000] = true;
			return ERROR_RES_CONTINUE;
	}

    return false;
}
PUBLIC void          Compiler::ErrorAt(Token* token, const char* message) {
    // SynchronizeToken();
    if (parser.PanicMode) return;
    parser.PanicMode = true;

    if (token->Type == TOKEN_EOF)
        ReportError(token->Line, " at end: %s", message);
    else if (token->Type == TOKEN_ERROR)
        ReportErrorPos(token->Line, (int)token->Pos, "%s", message);
    else
        ReportErrorPos(token->Line, (int)token->Pos, " at '%.*s': %s", token->Length, token->Start, message);

    parser.HadError = true;
}
PUBLIC void          Compiler::Error(const char* message) {
    ErrorAt(&parser.Previous, message);
}
PUBLIC void          Compiler::ErrorAtCurrent(const char* message) {
    ErrorAt(&parser.Current, message);
}

PUBLIC bool          Compiler::IsValueType(char* str) {
    return
        // Integer types
        !strcmp(str, "bool") ||
        !strcmp(str, "char") ||
        !strcmp(str, "uchar") ||
        !strcmp(str, "short") ||
        !strcmp(str, "ushort") ||
        !strcmp(str, "int") ||
        !strcmp(str, "uint") ||
        !strcmp(str, "long") ||
        !strcmp(str, "ulong") ||
        // Floating-point types
        !strcmp(str, "float") ||
        !strcmp(str, "double");
}
PUBLIC bool          Compiler::IsReferenceType(char* str) {
    return !IsValueType(str);
}

PUBLIC void  Compiler::ParseVariable(const char* errorMessage) {
    ConsumeToken(TOKEN_IDENTIFIER, errorMessage);

    DeclareVariable();
    if (ScopeDepth > 0) return;

    // return IdentifierConstant(&parser.Previous);
}
PUBLIC Uint8 Compiler::IdentifierConstant(Token* name) {
    return 0;
    // return MakeConstant(OBJECT_VAL(CopyString(name->Start, name->Length)));
}
PUBLIC bool  Compiler::IdentifiersEqual(Token* a, Token* b) {
    if (a->Length != b->Length) return false;
    return memcmp(a->Start, b->Start, a->Length) == 0;
}
PUBLIC void  Compiler::MarkInitialized() {
    if (ScopeDepth == 0) return;
    Locals[LocalCount - 1].Depth = ScopeDepth;
}
PUBLIC void  Compiler::DefineVariableToken(Token global) {
    if (ScopeDepth > 0) {
        MarkInitialized();
        return;
    }
    EmitByte(OP_DEFINE_GLOBAL);
    EmitStringHash(global);
}
PUBLIC void  Compiler::DeclareVariable() {
    if (ScopeDepth == 0) return;

    Token* name = &parser.Previous;
    for (int i = LocalCount - 1; i >= 0; i--) {
        Local* local = &Locals[i];

        if (local->Depth != -1 && local->Depth < ScopeDepth)
            break;

        if (IdentifiersEqual(name, &local->Name))
            Error("Variable with this name already declared in this scope.");
    }

    AddLocal(*name);
}

PUBLIC void  Compiler::EmitSetOperation(Uint8 setOp, int arg, Token name) {
    switch (setOp) {
        case OP_SET_GLOBAL:
        case OP_SET_PROPERTY:
            EmitByte(setOp);
            EmitStringHash(name);
            break;
        case OP_SET_LOCAL:
            EmitBytes(setOp, (Uint8)arg);
            break;
        case OP_SET_ELEMENT:
            EmitByte(setOp);
            break;
        default:
            break;
    }
}
PUBLIC void  Compiler::EmitGetOperation(Uint8 getOp, int arg, Token name) {
    switch (getOp) {
        case OP_GET_GLOBAL:
        case OP_GET_PROPERTY:
            EmitByte(getOp);
            EmitStringHash(name);
            break;
        case OP_GET_LOCAL:
            EmitBytes(getOp, (Uint8)arg);
            break;
        case OP_GET_ELEMENT:
            EmitByte(getOp);
            break;
        default:
            break;
    }
}
PUBLIC void  Compiler::EmitAssignmentToken(Token assignmentToken) {
    switch (assignmentToken.Type) {
        case TOKEN_ASSIGNMENT_PLUS:
            EmitByte(OP_ADD);
            break;
        case TOKEN_ASSIGNMENT_MINUS:
            EmitByte(OP_SUBTRACT);
            break;
        case TOKEN_ASSIGNMENT_MODULO:
            EmitByte(OP_MODULO);
            break;
        case TOKEN_ASSIGNMENT_DIVISION:
            EmitByte(OP_DIVIDE);
            break;
        case TOKEN_ASSIGNMENT_MULTIPLY:
            EmitByte(OP_MULTIPLY);
            break;
        case TOKEN_ASSIGNMENT_BITWISE_OR:
            EmitByte(OP_BW_OR);
            break;
        case TOKEN_ASSIGNMENT_BITWISE_AND:
            EmitByte(OP_BW_AND);
            break;
        case TOKEN_ASSIGNMENT_BITWISE_XOR:
            EmitByte(OP_BW_XOR);
            break;
        case TOKEN_ASSIGNMENT_BITWISE_LEFT:
            EmitByte(OP_BITSHIFT_LEFT);
            break;
        case TOKEN_ASSIGNMENT_BITWISE_RIGHT:
            EmitByte(OP_BITSHIFT_RIGHT);
            break;

        case TOKEN_INCREMENT:
            EmitByte(OP_INCREMENT);
            break;
        case TOKEN_DECREMENT:
            EmitByte(OP_DECREMENT);
            break;
        default:
            break;
    }
}
PUBLIC void  Compiler::EmitCopy(Uint8 count) {
    EmitByte(OP_COPY);
    EmitByte(count);
}

// Uint8 NEXTsetOp;
// int   NEXTarg;
// Token NEXTname;
PUBLIC void  Compiler::NamedVariable(Token name, bool canAssign) {
    Uint8 getOp, setOp;
    int arg = ResolveLocal(&name);

    // Determine whether local or global
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    }
    else {
        // arg = IdentifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if (canAssign && MatchAssignmentToken()) {
        Token assignmentToken = parser.Previous;
        if (assignmentToken.Type == TOKEN_INCREMENT ||
            assignmentToken.Type == TOKEN_DECREMENT) {
            EmitGetOperation(getOp, arg, name);

            EmitCopy(1);
            EmitByte(OP_SAVE_VALUE); // Save value. (value)
            EmitAssignmentToken(assignmentToken); // OP_DECREMENT (value - 1, this)

            EmitSetOperation(setOp, arg, name);
            EmitByte(OP_POP);

            EmitByte(OP_LOAD_VALUE); // Load value. (value)
        }
        else {
            if (assignmentToken.Type != TOKEN_ASSIGNMENT)
                EmitGetOperation(getOp, arg, name);

            GetExpression();

            EmitAssignmentToken(assignmentToken);
            EmitSetOperation(setOp, arg, name);
        }
    }
    else {
        EmitGetOperation(getOp, arg, name);
    }
}
PUBLIC void  Compiler::ScopeBegin() {
    ScopeDepth++;
}
PUBLIC void  Compiler::ScopeEnd() {
    ScopeDepth--;
    ClearToScope(ScopeDepth);
}
PUBLIC void  Compiler::ClearToScope(int depth) {
    while (LocalCount > 0 && Locals[LocalCount - 1].Depth > depth) {
        EmitByte(OP_POP); // pop locals

        LocalCount--;
    }
}
PUBLIC void  Compiler::PopToScope(int depth) {
    int lcl = LocalCount;
    while (lcl > 0 && Locals[lcl - 1].Depth > depth) {
        EmitByte(OP_POP); // pop locals
        lcl--;
    }
}
PUBLIC void  Compiler::AddLocal(Token name) {
    if (LocalCount == 0xFF) {
        Error("Too many local variables in function.");
        return;
    }
    Local* local = &Locals[LocalCount++];
    local->Name = name;
    // local->Depth = ScopeDepth;
    local->Depth = -1;
}
PUBLIC int   Compiler::ResolveLocal(Token* name) {
    for (int i = LocalCount - 1; i >= 0; i--) {
        Local* local = &Locals[i];
        if (IdentifiersEqual(name, &local->Name)) {
            if (local->Depth == -1) {
                Error("Cannot read local variable in its own initializer.");
            }
            return i;
        }
    }
    return -1;
}
PUBLIC Uint8 Compiler::GetArgumentList() {
    Uint8 argumentCount = 0;
    if (!CheckToken(TOKEN_RIGHT_PAREN)) {
        do {
            GetExpression();
            if (argumentCount >= 255) {
                Error("Cannot have more than 255 arguments.");
            }
            argumentCount++;
        }
        while (MatchToken(TOKEN_COMMA));
    }

    ConsumeToken(TOKEN_RIGHT_PAREN, "Expected \")\" after arguments.");
    return argumentCount;
}

// Enums
PUBLIC void  Compiler::AddEnum(Token name) {
    if (EnumsCount == 0xFF) {
        Error("Too many local variables in function.");
        return;
    }
    Enum* enume = &Enums[EnumsCount++];
    enume->Name = name;
    enume->Constant = -1;
}
PUBLIC int   Compiler::ResolveEnum(Token* name) {
    for (int i = EnumsCount - 1; i >= 0; i--) {
        Enum* enume = &Enums[i];
        if (IdentifiersEqual(name, &enume->Name)) {
            return i;
        }
    }
    return -1;
}
PUBLIC void  Compiler::DeclareEnum() {
    Token* name = &parser.Previous;
    for (int i = LocalCount - 1; i >= 0; i--) {
        Enum* local = &Enums[i];
        if (IdentifiersEqual(name, &local->Name))
            Error("Enumeration with this name already declared.");
    }

    AddEnum(*name);
}

Token InstanceToken = Token { 0, NULL, 0, 0, 0 };
PUBLIC void  Compiler::GetThis(bool canAssign) {
    InstanceToken = parser.Previous;
    // if (currentClass == NULL) {
    //     Error("Cannot use 'this' outside of a class.");
    // }
    // else {
        GetVariable(false);
    // }
}
PUBLIC void  Compiler::GetSuper(bool canAssign) {
    InstanceToken = parser.Previous;
    EmitBytes(OP_GET_LOCAL, 0);
}
PUBLIC void  Compiler::GetDot(bool canAssign) {
    Token instanceToken = InstanceToken;
    InstanceToken.Type = -1;

    ConsumeToken(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    // uint8_t name = IdentifierConstant(&parser.Previous);
    Token nameToken = parser.Previous;

    if (canAssign && MatchAssignmentToken()) {
        Token assignmentToken = parser.Previous;
        if (assignmentToken.Type == TOKEN_INCREMENT ||
            assignmentToken.Type == TOKEN_DECREMENT) {
            // (this)
            EmitCopy(1); // Copy property holder. (this, this)
            EmitGetOperation(OP_GET_PROPERTY, -1, nameToken); // Pops a property holder. (value, this)

            EmitCopy(1); // Copy value. (value, value, this)
            EmitByte(OP_SAVE_VALUE); // Save value. (value, this)
            EmitAssignmentToken(assignmentToken); // OP_DECREMENT (value - 1, this)

            EmitSetOperation(OP_SET_PROPERTY, -1, nameToken);
            // Pops the value and then pops the instance, pushes the value (value - 1)

            EmitByte(OP_POP); // ()
            EmitByte(OP_LOAD_VALUE); // Load value. (value)
        }
        else {
            if (assignmentToken.Type != TOKEN_ASSIGNMENT) {
                EmitCopy(1);
                EmitGetOperation(OP_GET_PROPERTY, -1, nameToken);
            }

            GetExpression();

            EmitAssignmentToken(assignmentToken);
            EmitSetOperation(OP_SET_PROPERTY, -1, nameToken);
        }
    }
    else if (MatchToken(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = GetArgumentList();
        EmitBytes(OP_INVOKE, argCount);

        // EmitByte(name);
        EmitStringHash(nameToken);
        // For supers
        EmitByte((instanceToken.Type == TOKEN_SUPER));
    }
    else {
        // EmitBytes(OP_GET_PROPERTY, name);
        EmitGetOperation(OP_GET_PROPERTY, -1, nameToken);
    }
}
PUBLIC void  Compiler::GetElement(bool canAssign) {
    Token blank;
    memset(&blank, 0, sizeof(blank));
    GetExpression();
    ConsumeToken(TOKEN_RIGHT_SQUARE_BRACE, "Expected matching ']'.");

    if (canAssign && MatchAssignmentToken()) {
        Token assignmentToken = parser.Previous;
        if (assignmentToken.Type == TOKEN_INCREMENT ||
            assignmentToken.Type == TOKEN_DECREMENT) {
            // (index, array)
            EmitCopy(2); // Copy array & index.
            EmitGetOperation(OP_GET_ELEMENT, -1, blank); // Pops a array and index. (value)

            EmitCopy(1); // Copy value. (value, value, index)
            EmitByte(OP_SAVE_VALUE); // Save value. (value, index)
            EmitAssignmentToken(assignmentToken); // OP_DECREMENT (value - 1, index)

            EmitSetOperation(OP_SET_ELEMENT, -1, blank);
            // Pops the value and then pops the instance, pushes the value (value - 1)

            EmitByte(OP_POP); // ()
            EmitByte(OP_LOAD_VALUE); // Load value. (value)
        }
        else {
            if (assignmentToken.Type != TOKEN_ASSIGNMENT) {
                EmitCopy(2);
                EmitGetOperation(OP_GET_ELEMENT, -1, blank);
            }

            // Get right-hand side
            GetExpression();

            EmitAssignmentToken(assignmentToken);
            EmitSetOperation(OP_SET_ELEMENT, -1, blank);
        }
    }
    else if (MatchToken(TOKEN_LEFT_PAREN)) {
        EmitGetOperation(OP_GET_ELEMENT, -1, blank);
        // uint8_t argCount = GetArgumentList();
        // EmitBytes(OP_INVOKE, argCount);
        // EmitStringHash(nameToken);
    }
    else {
        EmitGetOperation(OP_GET_ELEMENT, -1, blank);
    }
}

// Reading expressions
bool negateConstant = false;
PUBLIC void Compiler::GetGrouping(bool canAssign) {
    GetExpression();
    ConsumeToken(TOKEN_RIGHT_PAREN, "Expected \")\" after expression.");
}
PUBLIC void Compiler::GetLiteral(bool canAssign) {
    switch (parser.Previous.Type) {
        case TOKEN_NULL:  EmitByte(OP_NULL); break;
        case TOKEN_TRUE:  EmitByte(OP_TRUE); break;
        case TOKEN_FALSE: EmitByte(OP_FALSE); break;
    default:
        return; // Unreachable.
    }
}
PUBLIC void Compiler::GetInteger(bool canAssign) {
    int value = 0;
    char* start = parser.Previous.Start;
    if (start[0] == '0' && (start[1] == 'x' || start[1] == 'X'))
        value = (int)strtol(start + 2, NULL, 16);
    else
        value = (int)atof(start);

    if (negateConstant)
        value = -value;
    negateConstant = false;

    EmitConstant(INTEGER_VAL(value));
}
PUBLIC void Compiler::GetDecimal(bool canAssign) {
    float value = 0;
    value = (float)atof(parser.Previous.Start);

    if (negateConstant)
        value = -value;
    negateConstant = false;

    EmitConstant(DECIMAL_VAL(value));
}
PUBLIC void Compiler::GetString(bool canAssign) {
    ObjString* string = CopyString(parser.Previous.Start + 1, parser.Previous.Length - 2);

    // Escape the string
    char* dst = string->Chars;
    string->Length = 0;

    for (char* src = parser.Previous.Start + 1; src < parser.Previous.Start + parser.Previous.Length - 1; src++) {
        if (*src == '\\') {
            src++;
            switch (*src) {
                case 'n':
                    *dst++ = '\n';
                    break;
                case '"':
                    *dst++ = '"';
                    break;
                case '\'':
                    *dst++ = '\'';
                    break;
                case '\\':
                    *dst++ = '\\';
                    break;
                default:
                    Error("Unknown escape character");
                    break;
            }
            string->Length++;
        }
        else {
            *dst++ = *src;
            string->Length++;
        }
    }
    *dst++ = 0;

    EmitConstant(OBJECT_VAL(string));
    Strings.push_back(string);
}
PUBLIC void Compiler::GetArray(bool canAssign) {
    Uint32 count = 0;

    while (!MatchToken(TOKEN_RIGHT_SQUARE_BRACE)) {
        GetExpression();
        count++;

        if (!MatchToken(TOKEN_COMMA)) {
            ConsumeToken(TOKEN_RIGHT_SQUARE_BRACE, "Expected \"]\" at end of array.");
            break;
        }
    }

    EmitByte(OP_NEW_ARRAY);
    EmitUint32(count);
}
PUBLIC void Compiler::GetMap(bool canAssign) {
    Uint32 count = 0;

    while (!MatchToken(TOKEN_RIGHT_BRACE)) {
        AdvanceToken();
        GetString(false);

        ConsumeToken(TOKEN_COLON, "Expected \":\" after key string.");
        GetExpression();
        count++;

        if (!MatchToken(TOKEN_COMMA)) {
            ConsumeToken(TOKEN_RIGHT_BRACE, "Expected \"}\" after map.");
            break;
        }
    }

    EmitByte(OP_NEW_MAP);
    EmitUint32(count);
}
PUBLIC void Compiler::GetConstant(bool canAssign) {
    switch (NextToken().Type) {
        case TOKEN_NULL:
        case TOKEN_TRUE:
        case TOKEN_FALSE:
            GetLiteral(canAssign);
            break;
        case TOKEN_STRING:
            GetString(canAssign);
            break;
        case TOKEN_NUMBER:
            GetInteger(canAssign);
            break;
        case TOKEN_DECIMAL:
            GetDecimal(canAssign);
            break;
        case TOKEN_MINUS: {
            negateConstant = true;
            switch (NextToken().Type) {
                case TOKEN_NUMBER:
                    GetInteger(canAssign);
                    break;
                case TOKEN_DECIMAL:
                    GetDecimal(canAssign);
                    break;
                default:
                    Error("Invalid value after negative sign!");
                    break;
            }
            break;
        }
        default:
            Error("Invalid value!");
            break;
    }
}
PUBLIC void Compiler::GetVariable(bool canAssign) {
    NamedVariable(parser.Previous, canAssign);
}
PUBLIC void Compiler::GetLogicalAND(bool canAssign) {
    int endJump = EmitJump(OP_JUMP_IF_FALSE);

    EmitByte(OP_POP);
    ParsePrecedence(PREC_AND);

    PatchJump(endJump);
}
PUBLIC void Compiler::GetLogicalOR(bool canAssign) {
    int elseJump = EmitJump(OP_JUMP_IF_FALSE);
    int endJump = EmitJump(OP_JUMP);

    PatchJump(elseJump);
    EmitByte(OP_POP);

    ParsePrecedence(PREC_OR);
    PatchJump(endJump);
}
PUBLIC void Compiler::GetConditional(bool canAssign) {
    int thenJump = EmitJump(OP_JUMP_IF_FALSE);
    EmitByte(OP_POP);
    ParsePrecedence(PREC_TERNARY);

    int elseJump = EmitJump(OP_JUMP);
    ConsumeToken(TOKEN_COLON, "Expected \":\" after conditional condition.");

    PatchJump(thenJump);
    EmitByte(OP_POP);
    ParsePrecedence(PREC_TERNARY);
    PatchJump(elseJump);
}
PUBLIC void Compiler::GetUnary(bool canAssign) {
    int operatorType = parser.Previous.Type;

    ParsePrecedence(PREC_UNARY);

    switch (operatorType) {
        case TOKEN_MINUS:       EmitByte(OP_NEGATE); break;
        case TOKEN_BITWISE_NOT: EmitByte(OP_BW_NOT); break;
        case TOKEN_LOGICAL_NOT: EmitByte(OP_LG_NOT); break;

        // HACK: replace these with prefix version of OP
        // case TOKEN_INCREMENT:   EmitByte(OP_INCREMENT); break;
        // case TOKEN_DECREMENT:   EmitByte(OP_DECREMENT); break;
        default:
            return; // Unreachable.
    }
}
PUBLIC void Compiler::GetBinary(bool canAssign) {
    // printf("GetBinary()\n");
    Token operato = parser.Previous;
    int operatorType = operato.Type;

    ParseRule* rule = GetRule(operatorType);
    ParsePrecedence((Precedence)(rule->Precedence + 1));

    switch (operatorType) {
        // Numeric Operations
        case TOKEN_PLUS:                EmitByte(OP_ADD); break;
        case TOKEN_MINUS:               EmitByte(OP_SUBTRACT); break;
        case TOKEN_MULTIPLY:            EmitByte(OP_MULTIPLY); break;
        case TOKEN_DIVISION:            EmitByte(OP_DIVIDE); break;
        case TOKEN_MODULO:              EmitByte(OP_MODULO); break;
        // Bitwise Operations
        case TOKEN_BITWISE_LEFT:        EmitByte(OP_BITSHIFT_LEFT); break;
        case TOKEN_BITWISE_RIGHT:       EmitByte(OP_BITSHIFT_RIGHT); break;
        case TOKEN_BITWISE_OR:          EmitByte(OP_BW_OR); break;
        case TOKEN_BITWISE_AND:         EmitByte(OP_BW_AND); break;
        case TOKEN_BITWISE_XOR:         EmitByte(OP_BW_XOR); break;
        // Logical Operations
        case TOKEN_LOGICAL_AND:         EmitByte(OP_LG_AND); break;
        case TOKEN_LOGICAL_OR:          EmitByte(OP_LG_OR); break;
        // Equality and Comparison Operators
        case TOKEN_NOT_EQUALS:          EmitByte(OP_EQUAL_NOT); break;
        case TOKEN_EQUALS:              EmitByte(OP_EQUAL); break;
        case TOKEN_GREATER:             EmitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL:       EmitByte(OP_GREATER_EQUAL); break;
        case TOKEN_LESS:                EmitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:          EmitByte(OP_LESS_EQUAL); break;
        default:
            ErrorAt(&operato, "Unknown binary operator.");
            return; // Unreachable.
    }
}
PUBLIC void Compiler::GetSuffix(bool canAssign) {

}
PUBLIC void Compiler::GetCall(bool canAssign) {
    Uint8 argCount = GetArgumentList();
    EmitByte(OP_CALL);
    EmitByte(argCount);
}
PUBLIC void Compiler::GetExpression() {
    ParsePrecedence(PREC_ASSIGNMENT);
}
// Reading statements
struct switch_case {
    int position;
    int constant_index;
    int patch_ptr;
};
stack<vector<int>*> BreakJumpListStack;
stack<vector<int>*> ContinueJumpListStack;
stack<vector<switch_case>*> SwitchJumpListStack;
stack<int> BreakScopeStack;
stack<int> ContinueScopeStack;
stack<int> SwitchScopeStack;
PUBLIC void Compiler::GetPrintStatement() {
    GetExpression();
    ConsumeToken(TOKEN_SEMICOLON, "Expected \";\" after value.");
    EmitByte(OP_PRINT);
}
PUBLIC void Compiler::GetExpressionStatement() {
    GetExpression();
    EmitByte(OP_POP);
    ConsumeToken(TOKEN_SEMICOLON, "Expected \";\" after expression.");
}
PUBLIC void Compiler::GetContinueStatement() {
    if (ContinueJumpListStack.size() == 0) {
        Error("Can't continue outside of loop.");
    }

    PopToScope(ContinueScopeStack.top());

    int jump = EmitJump(OP_JUMP);
    ContinueJumpListStack.top()->push_back(jump);

    ConsumeToken(TOKEN_SEMICOLON, "Expect ';' after continue.");
}
PUBLIC void Compiler::GetDoWhileStatement() {
    // Set the start of the loop to before the condition
    int loopStart = CodePointer();

    // Push new jump list on break stack
    StartBreakJumpList();

    // Push new jump list on continue stack
    StartContinueJumpList();

    // Execute code block
    GetStatement();

    // Pop jump list off continue stack, patch all continue to this code point
    EndContinueJumpList();

    // Evaluate the condition
    ConsumeToken(TOKEN_WHILE, "Expected 'while' at end of 'do' block.");
    ConsumeToken(TOKEN_LEFT_PAREN, "Expected '(' after 'while'.");
    GetExpression();
    ConsumeToken(TOKEN_RIGHT_PAREN, "Expected ')' after condition.");
    ConsumeToken(TOKEN_SEMICOLON, "Expected ';' after ')'.");

    // Jump if false (or 0)
    int exitJump = EmitJump(OP_JUMP_IF_FALSE);

    // Pop while expression value off the stack.
    EmitByte(OP_POP);

    // After block, return to evaluation of while expression.
    EmitLoop(loopStart);

    // Set the exit jump to this point
    PatchJump(exitJump);

    // Pop value since OP_JUMP_IF_FALSE doesn't pop off expression value
    EmitByte(OP_POP);

    // Pop jump list off break stack, patch all breaks to this code point
    EndBreakJumpList();
}
PUBLIC void Compiler::GetReturnStatement() {
    if (Type == TYPE_TOP_LEVEL) {
        Error("Cannot return from top-level code.");
    }

    // int lclCnt = LocalCount;
    // while (lclCnt > 0 && Locals[lclCnt - 1].Depth > ScopeDepth - 1) {
    //     EmitByte(OP_POP);
    //     lclCnt--;
    // }

    if (MatchToken(TOKEN_SEMICOLON)) {
        EmitReturn();
    }
    else {
        if (Type == TYPE_CONSTRUCTOR) {
            Error("Cannot return a value from an initializer.");
        }

        GetExpression();
        ConsumeToken(TOKEN_SEMICOLON, "Expect ';' after return value.");
        EmitByte(OP_RETURN);
    }
}
PUBLIC void Compiler::GetRepeatStatement() {
    ConsumeToken(TOKEN_LEFT_PAREN, "Expect '(' after 'repeat'.");
    GetExpression();
    ConsumeToken(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int loopStart = CurrentChunk()->Count;

    int exitJump = EmitJump(OP_JUMP_IF_FALSE);

    StartBreakJumpList();

    EmitByte(OP_DECREMENT);

    // Repeat Code Body
    GetStatement();

    EmitLoop(loopStart);

    PatchJump(exitJump);
    EmitByte(OP_POP);

    EndBreakJumpList();
}
// TODO: Implement
PUBLIC void Compiler::GetSwitchStatement() {
    Chunk* chunk = CurrentChunk();

    StartBreakJumpList();

    // Evaluate the condition
    ConsumeToken(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    GetExpression();
    ConsumeToken(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    ConsumeToken(TOKEN_LEFT_BRACE, "Expected \"{\" before statements.");

    int code_block_start = CodePointer();
    int code_block_length = code_block_start;
    Uint8* code_block_copy = NULL;
    int*   line_block_copy = NULL;

    StartSwitchJumpList();
    ScopeBegin();
    GetBlockStatement();
    ScopeEnd();

    code_block_length = CodePointer() - code_block_length;

    // Copy code block
    code_block_copy = (Uint8*)malloc(code_block_length * sizeof(Uint8));
    memcpy(code_block_copy, &chunk->Code[code_block_start], code_block_length * sizeof(Uint8));

    // Copy line info block
    line_block_copy = (int*)malloc(code_block_length * sizeof(int));
    memcpy(line_block_copy, &chunk->Lines[code_block_start], code_block_length * sizeof(int));

    chunk->Count -= code_block_length;

    // printf("code_block_start: %d\n", code_block_start);
    // printf("code_block_length: %d\n", code_block_length);

    int integer_min = 0x7FFFFFFF;
    int integer_max = 0x80000000;
    bool all_constants_are_integer = true;
    vector<switch_case> cases = *SwitchJumpListStack.top();
    for (size_t i = 0; i < cases.size(); i++) {
        // int position = cases[i].position;
        int constant_index = cases[i].constant_index;
        if (constant_index > -1) {
            VMValue value = chunk->Constants->at(constant_index);

            all_constants_are_integer &= (value.Type == VAL_INTEGER);
            if (all_constants_are_integer) {
                int val = AS_INTEGER(value);
                if (integer_min > val)
                    integer_min = val;
                if (integer_max < val)
                    integer_max = val;
            }
        }
        else {
            // printf("default");
        }
        // printf(": %d\n", position - code_block_start);
    }

    if (all_constants_are_integer && false) {
        //
    }
    else {
        // Stack:
        //  0 : switch value

        // EmitByte(OP_PRINT_STACK);

        EmitByte(OP_SWITCH_TABLE);
        EmitUint16(cases.size());
        for (size_t i = 0; i < cases.size(); i++) {
            int position = cases[i].position - code_block_start;
            int constant_index = cases[i].constant_index;
            EmitByte(constant_index);
            EmitUint16(position);
        }

        int exitJump = EmitJump(OP_JUMP);

        int new_block_pos = CodePointer();
        // We do this here so that if an allocation is needed, it happens.
        for (int i = 0; i < code_block_length; i++) {
            ChunkWrite(chunk, code_block_copy[i], line_block_copy[i]);
        }
        free(code_block_copy);
        free(line_block_copy);

        PatchJump(exitJump);

        // Set the old break opcode positions to the newly placed ones
        vector<int>* top = BreakJumpListStack.top();
        for (size_t i = 0; i < top->size(); i++) {
            (*top)[i] += -code_block_start + new_block_pos;
            // printf("break at: %d (%d)\n", (*top)[i], chunk->Code[(*top)[i] - 1]);
        }
    }

    EndSwitchJumpList();

    // vector<switch_case>* sdf = SwitchJumpListStack.top();

    // // Pop value since OP_JUMP_IF_FALSE doesn't pop off expression value
    // EmitByte(OP_POP);

    // Pop jump list off break stack, patch all breaks to this code point
    EndBreakJumpList();
}
PUBLIC void Compiler::GetCaseStatement() {
    if (SwitchJumpListStack.size() == 0) {
        Error("Cannot use case label outside of switch statement.");
    }

    int position, constant_index;
    position = CodePointer();

    // int enum_index;
    // Token t = PeekToken();
    // if ((enum_index = ResolveEnum(&t)) != -1) {
    //     if (Enums[enum_index].Constant != -1) {
    //         AdvanceToken();
    //         ConsumeToken(TOKEN_COLON, "Expected \":\" after \"case\".");
    //         CurrentChunk()->Count = position;
    //
    //         SwitchJumpListStack.top()->push_back(switch_case { position, Enums[enum_index].Constant, 0 });
    //         return;
    //     }
    // }

    GetConstant(false);
    ConsumeToken(TOKEN_COLON, "Expected \":\" after \"case\".");

    constant_index = CurrentChunk()->Code[position + 1];
    CurrentChunk()->Count = position;

    SwitchJumpListStack.top()->push_back(switch_case { position, constant_index, 0 });
}
PUBLIC void Compiler::GetDefaultStatement() {
    if (SwitchJumpListStack.size() == 0) {
        Error("Cannot use default label outside of switch statement.");
    }

    ConsumeToken(TOKEN_COLON, "Expected \":\" after \"default\".");

    int position;
    position = CodePointer();

    SwitchJumpListStack.top()->push_back(switch_case { position, -1, 0 });
}
PUBLIC void Compiler::GetWhileStatement() {
    // Set the start of the loop to before the condition
    int loopStart = CodePointer();

    // Evaluate the condition
    ConsumeToken(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    GetExpression();
    ConsumeToken(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    // Jump if false (or 0)
    int exitJump = EmitJump(OP_JUMP_IF_FALSE);

    // Pop while expression value off the stack.
    EmitByte(OP_POP);

    // Push new jump list on break stack
    StartBreakJumpList();

    // Push new jump list on continue stack
    StartContinueJumpList();

    // Execute code block
    GetStatement();

    // Pop jump list off continue stack, patch all continue to this code point
    EndContinueJumpList();

    // After block, return to evaluation of while expression.
    EmitLoop(loopStart);

    // Set the exit jump to this point
    PatchJump(exitJump);

    // Pop value since OP_JUMP_IF_FALSE doesn't pop off expression value
    EmitByte(OP_POP);

    // Pop jump list off break stack, patch all breaks to this code point
    EndBreakJumpList();
}
PUBLIC void Compiler::GetBreakStatement() {
    if (BreakJumpListStack.size() == 0) {
        Error("Cannot break outside of loop or switch statement.");
    }

    PopToScope(BreakScopeStack.top());

    int jump = EmitJump(OP_JUMP);
    BreakJumpListStack.top()->push_back(jump);

    ConsumeToken(TOKEN_SEMICOLON, "Expect ';' after break.");
}
PUBLIC void Compiler::GetBlockStatement() {
    // printf("GetBlockStatement()\n");
    while (!CheckToken(TOKEN_RIGHT_BRACE) && !CheckToken(TOKEN_EOF)) {
        GetDeclaration();
    }

    ConsumeToken(TOKEN_RIGHT_BRACE, "Expected \"}\" after block.");
}
PUBLIC void Compiler::GetWithStatement() {
    enum {
        WITH_STATE_INIT,
        WITH_STATE_ITERATE,
        WITH_STATE_FINISH,
    };

    // With "expression"
    ConsumeToken(TOKEN_LEFT_PAREN, "Expect '(' after 'with'.");
    GetExpression();
    ConsumeToken(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    EmitByte(OP_WITH);
    EmitByte(WITH_STATE_INIT);
    EmitByte(0xFF);
    EmitByte(0xFF);

    int loopStart = CurrentChunk()->Count;

    // Push new jump list on break stack
    StartBreakJumpList();

    // Push new jump list on continue stack
    StartContinueJumpList();

    WithDepth++;

    // Execute code block
    GetStatement();

    WithDepth--;

    // Pop jump list off continue stack, patch all continue to this code point
    EndContinueJumpList();

    // Loop back?
    EmitByte(OP_WITH);
    EmitByte(WITH_STATE_ITERATE);

    int offset = CurrentChunk()->Count - loopStart + 2;
    if (offset > UINT16_MAX) Error("Loop body too large.");

    EmitByte(offset & 0xFF);
    EmitByte((offset >> 8) & 0xFF);

    // Pop jump list off break stack, patch all breaks to this code point
    EndBreakJumpList();

    // End
    EmitByte(OP_WITH);
    EmitByte(WITH_STATE_FINISH);
    EmitByte(0xFF);
    EmitByte(0xFF);

    int jump = CurrentChunk()->Count - loopStart;
    CurrentChunk()->Code[loopStart - 2] = jump & 0xFF;
    CurrentChunk()->Code[loopStart - 1] = (jump >> 8) & 0xFF;
}
PUBLIC void Compiler::GetForStatement() {
    // Start new scope
    ScopeBegin();

    ConsumeToken(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    // Initializer (happens only once)
    if (MatchToken(TOKEN_VAR)) {
        GetVariableDeclaration();
    }
    else if (MatchToken(TOKEN_SEMICOLON)) {
        // No initializer.
    }
    else {
        GetExpressionStatement();
    }

    int exitJump = -1;
    int loopStart = CurrentChunk()->Count;

    // Conditional
    if (!MatchToken(TOKEN_SEMICOLON)) {
        GetExpression();
        ConsumeToken(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        // Jump out of the loop if the condition is false.
        exitJump = EmitJump(OP_JUMP_IF_FALSE);
        EmitByte(OP_POP); // Condition.
    }

    // Incremental
    if (!MatchToken(TOKEN_RIGHT_PAREN)) {
        int bodyJump = EmitJump(OP_JUMP);

        int incrementStart = CurrentChunk()->Count;
        GetExpression();
        EmitByte(OP_POP);
        ConsumeToken(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        EmitLoop(loopStart);
        loopStart = incrementStart;
        PatchJump(bodyJump);
    }

    // Push new jump list on break stack
    StartBreakJumpList();

    // Push new jump list on continue stack
    StartContinueJumpList();

    // Execute code block
    GetStatement();

    // Pop jump list off continue stack, patch all continue to this code point
    EndContinueJumpList();

    // After block, return to evaluation of condition.
    EmitLoop(loopStart);

    //
    if (exitJump != -1) {
        PatchJump(exitJump);
        EmitByte(OP_POP); // Condition.
    }

    // Pop jump list off break stack, patch all break to this code point
    EndBreakJumpList();

    // End new scope
    ScopeEnd();

    // EmitByte(OP_PRINT_STACK);
}
PUBLIC void Compiler::GetIfStatement() {
    ConsumeToken(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    GetExpression();
    ConsumeToken(TOKEN_RIGHT_PAREN, "Expect ')' after condition."); // [paren]

    int thenJump = EmitJump(OP_JUMP_IF_FALSE);
    EmitByte(OP_POP);
    GetStatement();

    int elseJump = EmitJump(OP_JUMP);

    PatchJump(thenJump);
    EmitByte(OP_POP); // Only Pop if OP_JUMP_IF_FALSE, as it doesn't pop

    if (MatchToken(TOKEN_ELSE)) GetStatement();

    PatchJump(elseJump);
}
PUBLIC void Compiler::GetStatement() {
    if (MatchToken(TOKEN_PRINT)) {
        GetPrintStatement();
    }
    else if (MatchToken(TOKEN_CONTINUE)) {
        GetContinueStatement();
    }
    else if (MatchToken(TOKEN_DEFAULT)) {
        GetDefaultStatement();
    }
    else if (MatchToken(TOKEN_RETURN)) {
        GetReturnStatement();
    }
    else if (MatchToken(TOKEN_REPEAT)) {
        GetRepeatStatement();
    }
    else if (MatchToken(TOKEN_SWITCH)) {
        GetSwitchStatement();
    }
    else if (MatchToken(TOKEN_WHILE)) {
        GetWhileStatement();
    }
    else if (MatchToken(TOKEN_BREAK)) {
        GetBreakStatement();
    }
    else if (MatchToken(TOKEN_CASE)) {
        GetCaseStatement();
    }
    else if (MatchToken(TOKEN_WITH)) {
        GetWithStatement();
    }
    else if (MatchToken(TOKEN_FOR)) {
        GetForStatement();
    }
    else if (MatchToken(TOKEN_DO)) {
        GetDoWhileStatement();
    }
    else if (MatchToken(TOKEN_IF)) {
        GetIfStatement();
    }
    else if (MatchToken(TOKEN_LEFT_BRACE)) {
        ScopeBegin();
        GetBlockStatement();
        ScopeEnd();
    }
    else {
        GetExpressionStatement();
    }
}
// Reading declarations
PUBLIC int  Compiler::GetFunction(int type) {
    int index = (int)Compiler::Functions.size();

    Compiler* compiler = new Compiler;
    compiler->Initialize(this, 1, type);

    // compiler->EmitByte(OP_FAILSAFE);
    //
    // int failsafe = compiler->CodePointer();
    // compiler->EmitUint16(0xFFFFU);

    if (type == TYPE_WITH) {
        // With statements shouldn't need brackets
        compiler->GetStatement();
    }
    else {
        // Compile the parameter list.
        compiler->ConsumeToken(TOKEN_LEFT_PAREN, "Expect '(' after function name.");

        if (!compiler->CheckToken(TOKEN_RIGHT_PAREN)) {
            do {
                compiler->ParseVariable("Expect parameter name.");
                compiler->DefineVariableToken(parser.Previous);

                compiler->Function->Arity++;
                if (compiler->Function->Arity > 255) {
                    compiler->Error("Cannot have more than 255 parameters.");
                }
            }
            while (compiler->MatchToken(TOKEN_COMMA));
        }

        compiler->ConsumeToken(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");

        // The body.
        compiler->ConsumeToken(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
        compiler->GetBlockStatement();
    }

    // Create the function object.
    // ObjFunction* function =
        compiler->FinishCompiler();

    return index;
}
PUBLIC void Compiler::GetMethod() {
    ConsumeToken(TOKEN_IDENTIFIER, "Expect method name.");
    // Uint8 constant = IdentifierConstant(&parser.Previous);
    Token constantToken = parser.Previous;

    // If the method is named "init", it's an initializer.
    int type = TYPE_METHOD;
    if (parser.Previous.Length == 4 && memcmp(parser.Previous.Start, "init", 4) == 0) {
        type = TYPE_CONSTRUCTOR;
    }

    int index = GetFunction(type);

    // EmitBytes(OP_METHOD, constant);
    EmitByte(OP_METHOD);
    EmitByte(index);
    EmitStringHash(constantToken);
}
PUBLIC void Compiler::GetVariableDeclaration() {
    if (SwitchScopeStack.size() != 0) {
        if (SwitchScopeStack.top() == ScopeDepth)
            Error("Cannot initialize variable inside switch statement.");
    }

    do {
        // Uint8 global =
            ParseVariable("Expected variable name.");
        Token token = parser.Previous;

        if (MatchToken(TOKEN_ASSIGNMENT)) {
            GetExpression();
        }
        else {
            EmitByte(OP_NULL);
        }

        // DefineVariable(global);
        DefineVariableToken(token);
    }
    while (MatchToken(TOKEN_COMMA));

    ConsumeToken(TOKEN_SEMICOLON, "Expected \";\" after variable declaration.");
}
PUBLIC void Compiler::GetClassDeclaration() {
    ConsumeToken(TOKEN_IDENTIFIER, "Expect class name.");

    Token className = parser.Previous;
    // Uint8 nameConstant = IdentifierConstant(&parser.Previous);
    DeclareVariable();

    // EmitBytes(OP_CLASS, nameConstant);
    // DefineVariable(nameConstant);
    EmitByte(OP_CLASS);
    EmitStringHash(className);

    ClassHashList.push_back(GetHash(className));
    // Check for class extension
    if (MatchToken(TOKEN_PLUS)) {
        EmitByte(true);
        ClassExtendedList.push_back(1);
    }
    else {
        EmitByte(false);
        ClassExtendedList.push_back(0);
    }

    // ClassCompiler classCompiler;
    // classCompiler.name = parser.Previous;
    // classCompiler.hasSuperclass = false;
    // classCompiler.enclosing = currentClass;
    // currentClass = &classCompiler;

    if (MatchToken(TOKEN_LESS)) {
        ConsumeToken(TOKEN_IDENTIFIER, "Expect base class name.");
        Token superName = parser.Previous;

        EmitByte(OP_INHERIT);
        EmitStringHash(superName);
        // printf("super: %.*s (0x%08X)\n", superName.Length, superName.Start, GetHash(superName));

        //
        // if (IdentifiersEqual(&className, &parser.Previous)) {
        //     Error("A class cannot inherit from itself.");
        // }
        //
        // // classCompiler.hasSuperclass = true;
        //
        // ScopeBegin();
        //
        // // Store the superclass in a local variable named "super".
        // GetVariable(false);
        // AddLocal(syntheticToken("super"));
        // DefineVariable(0);
        //
        // NamedVariable(className, false);
    }

    DefineVariableToken(className);

    ConsumeToken(TOKEN_LEFT_BRACE, "Expect '{' before class body.");

    while (!CheckToken(TOKEN_RIGHT_BRACE) && !CheckToken(TOKEN_EOF)) {
        if (MatchToken(TOKEN_EVENT)) {
            NamedVariable(className, false);
            GetMethod();
        }
        else {
            NamedVariable(className, false);
            GetMethod();
        }
    }

    ConsumeToken(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");

    // if (classCompiler.hasSuperclass) {
    //     ScopeEnd();
    // }
    // currentClass = currentClass->enclosing;
}
PUBLIC void Compiler::GetEventDeclaration() {
    ConsumeToken(TOKEN_IDENTIFIER, "Expected event name.");
    // Uint8 constant = IdentifierConstant(&parser.Previous);
    Token constantToken = parser.Previous;

    // If the method is named "init", it's an initializer.
    int type = TYPE_FUNCTION;

    int index = GetFunction(type);

    EmitByte(OP_EVENT);
    EmitByte(index);

    EmitByte(OP_DEFINE_GLOBAL);
    EmitStringHash(constantToken);
}
PUBLIC void Compiler::GetEnumDeclaration() {
    do {
        // Uint8 global =
        ConsumeToken(TOKEN_IDENTIFIER, "Expected enumeration name.");
        DeclareEnum();

        Token t = parser.Previous;

        ConsumeToken(TOKEN_ASSIGNMENT, "Expected \"=\" after enumeration name.");

        // Get Constant Value
        int position, constant_index;
        position = CodePointer();
        GetConstant(false);
        constant_index = CurrentChunk()->Code[position + 1];
        CurrentChunk()->Count = position;

        int enum_index;
        if ((enum_index = ResolveEnum(&t)) != -1) {
            Enums[enum_index].Constant = constant_index;
            // printf("enum (%.*s) = %d\n", t.Length, t.Start, constant_index);
        }
    }
    while (MatchToken(TOKEN_COMMA));

    ConsumeToken(TOKEN_SEMICOLON, "Expected \";\" after enum declaration.");
}
PUBLIC void Compiler::GetDeclaration() {
    if (MatchToken(TOKEN_CLASS))
        GetClassDeclaration();
    else if (MatchToken(TOKEN_VAR))
        GetVariableDeclaration();
    else if (MatchToken(TOKEN_ENUM))
        GetEnumDeclaration();
    else if (MatchToken(TOKEN_EVENT))
        GetEventDeclaration();
    else
        GetStatement();

    if (parser.PanicMode) SynchronizeToken();
}

PUBLIC STATIC void   Compiler::MakeRules() {
    Rules = (ParseRule*)Memory::TrackedCalloc("Compiler::Rules", TOKEN_EOF + 1, sizeof(ParseRule));
    // Single-character tokens.
    Rules[TOKEN_LEFT_PAREN] = ParseRule { &Compiler::GetGrouping, &Compiler::GetCall, NULL, PREC_CALL };
    Rules[TOKEN_RIGHT_PAREN] = ParseRule { NULL, NULL, NULL, PREC_NONE };
    Rules[TOKEN_LEFT_BRACE] = ParseRule { &Compiler::GetMap, NULL, NULL, PREC_CALL };
    Rules[TOKEN_RIGHT_BRACE] = ParseRule { NULL, NULL, NULL, PREC_NONE };
    Rules[TOKEN_LEFT_SQUARE_BRACE] = ParseRule { &Compiler::GetArray, &Compiler::GetElement, NULL, PREC_CALL };
    Rules[TOKEN_RIGHT_SQUARE_BRACE] = ParseRule { NULL, NULL, NULL, PREC_NONE };
    Rules[TOKEN_COMMA] = ParseRule { NULL, NULL, NULL, PREC_NONE };
    Rules[TOKEN_DOT] = ParseRule { NULL, &Compiler::GetDot, NULL, PREC_CALL };
    Rules[TOKEN_SEMICOLON] = ParseRule { NULL, NULL, NULL, PREC_NONE };
    // Operators
    Rules[TOKEN_MINUS] = ParseRule { &Compiler::GetUnary, &Compiler::GetBinary, NULL, PREC_TERM };
    Rules[TOKEN_PLUS] = ParseRule { NULL, &Compiler::GetBinary, NULL, PREC_TERM };
    Rules[TOKEN_DECREMENT] = ParseRule { &Compiler::GetUnary, NULL, NULL, PREC_CALL }; // &Compiler::GetSuffix
    Rules[TOKEN_INCREMENT] = ParseRule { &Compiler::GetUnary, NULL, NULL, PREC_CALL }; // &Compiler::GetSuffix
    Rules[TOKEN_DIVISION] = ParseRule { NULL, &Compiler::GetBinary, NULL, PREC_FACTOR };
    Rules[TOKEN_MULTIPLY] = ParseRule { NULL, &Compiler::GetBinary, NULL, PREC_FACTOR };
    Rules[TOKEN_MODULO] = ParseRule { NULL, &Compiler::GetBinary, NULL, PREC_FACTOR };
    Rules[TOKEN_BITWISE_XOR] = ParseRule { NULL, &Compiler::GetBinary, NULL, PREC_BITWISE_XOR };
    Rules[TOKEN_BITWISE_AND] = ParseRule { NULL, &Compiler::GetBinary, NULL, PREC_BITWISE_AND };
    Rules[TOKEN_BITWISE_OR] = ParseRule { NULL, &Compiler::GetBinary, NULL, PREC_BITWISE_OR };
    Rules[TOKEN_BITWISE_LEFT] = ParseRule { NULL, &Compiler::GetBinary, NULL, PREC_BITWISE_SHIFT };
    Rules[TOKEN_BITWISE_RIGHT] = ParseRule { NULL, &Compiler::GetBinary, NULL, PREC_BITWISE_SHIFT };
    Rules[TOKEN_BITWISE_NOT] = ParseRule { &Compiler::GetUnary, NULL, NULL, PREC_UNARY };
    Rules[TOKEN_TERNARY] = ParseRule { NULL, &Compiler::GetConditional, NULL, PREC_TERNARY };
    Rules[TOKEN_COLON] = ParseRule { NULL, NULL, NULL, PREC_NONE };
    Rules[TOKEN_LOGICAL_AND] = ParseRule { NULL, &Compiler::GetLogicalAND, NULL, PREC_AND };
    Rules[TOKEN_LOGICAL_OR] = ParseRule { NULL, &Compiler::GetLogicalOR, NULL, PREC_OR };
    Rules[TOKEN_LOGICAL_NOT] = ParseRule { &Compiler::GetUnary, NULL, NULL, PREC_UNARY };
    Rules[TOKEN_NOT_EQUALS] = ParseRule { NULL, &Compiler::GetBinary, NULL, PREC_EQUALITY };
    Rules[TOKEN_EQUALS] = ParseRule { NULL, &Compiler::GetBinary, NULL, PREC_EQUALITY };
    Rules[TOKEN_GREATER] = ParseRule { NULL, &Compiler::GetBinary, NULL, PREC_COMPARISON };
    Rules[TOKEN_GREATER_EQUAL] = ParseRule { NULL, &Compiler::GetBinary, NULL, PREC_COMPARISON };
    Rules[TOKEN_LESS] = ParseRule { NULL, &Compiler::GetBinary, NULL, PREC_COMPARISON };
    Rules[TOKEN_LESS_EQUAL] = ParseRule { NULL, &Compiler::GetBinary, NULL, PREC_COMPARISON };
    //
    Rules[TOKEN_ASSIGNMENT] = ParseRule { NULL, NULL, NULL, PREC_NONE };
    Rules[TOKEN_ASSIGNMENT_MULTIPLY] = ParseRule { NULL, NULL, NULL, PREC_NONE };
    Rules[TOKEN_ASSIGNMENT_DIVISION] = ParseRule { NULL, NULL, NULL, PREC_NONE };
    Rules[TOKEN_ASSIGNMENT_MODULO] = ParseRule { NULL, NULL, NULL, PREC_NONE };
    Rules[TOKEN_ASSIGNMENT_PLUS] = ParseRule { NULL, NULL, NULL, PREC_NONE };
    Rules[TOKEN_ASSIGNMENT_MINUS] = ParseRule { NULL, NULL, NULL, PREC_NONE };
    Rules[TOKEN_ASSIGNMENT_BITWISE_LEFT] = ParseRule { NULL, NULL, NULL, PREC_NONE };
    Rules[TOKEN_ASSIGNMENT_BITWISE_RIGHT] = ParseRule { NULL, NULL, NULL, PREC_NONE };
    Rules[TOKEN_ASSIGNMENT_BITWISE_AND] = ParseRule { NULL, NULL, NULL, PREC_NONE };
    Rules[TOKEN_ASSIGNMENT_BITWISE_XOR] = ParseRule { NULL, NULL, NULL, PREC_NONE };
    Rules[TOKEN_ASSIGNMENT_BITWISE_OR] = ParseRule { NULL, NULL, NULL, PREC_NONE };
    // Keywords
    Rules[TOKEN_THIS] = ParseRule { &Compiler::GetThis, NULL, NULL, PREC_NONE };
    Rules[TOKEN_SUPER] = ParseRule { &Compiler::GetSuper, NULL, NULL, PREC_NONE };
    // Constants or whatever
    Rules[TOKEN_NULL] = ParseRule { &Compiler::GetLiteral, NULL, NULL, PREC_NONE };
    Rules[TOKEN_TRUE] = ParseRule { &Compiler::GetLiteral, NULL, NULL, PREC_NONE };
    Rules[TOKEN_FALSE] = ParseRule { &Compiler::GetLiteral, NULL, NULL, PREC_NONE };
    Rules[TOKEN_STRING] = ParseRule { &Compiler::GetString, NULL, NULL, PREC_NONE };
    Rules[TOKEN_NUMBER] = ParseRule { &Compiler::GetInteger, NULL, NULL, PREC_NONE };
    Rules[TOKEN_DECIMAL] = ParseRule { &Compiler::GetDecimal, NULL, NULL, PREC_NONE };
    Rules[TOKEN_IDENTIFIER] = ParseRule { &Compiler::GetVariable, NULL, NULL, PREC_NONE };
}
PUBLIC ParseRule*    Compiler::GetRule(int type) {
    return &Compiler::Rules[(int)type];
}

PUBLIC void          Compiler::ParsePrecedence(Precedence precedence) {
    AdvanceToken();
    ParseFn prefixRule = GetRule(parser.Previous.Type)->Prefix;
    if (prefixRule == NULL) {
        Error("Expected expression.");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    (this->*prefixRule)(canAssign);

    while (precedence <= GetRule(parser.Current.Type)->Precedence) {
        AdvanceToken();
        ParseFn infixRule = GetRule(parser.Previous.Type)->Infix;
        if (infixRule)
            (this->*infixRule)(canAssign);
    }

    if (canAssign && MatchAssignmentToken()) {
        Error("Invalid assignment target.");
        GetExpression();
    }
}
PUBLIC Uint32        Compiler::GetHash(char* string) {
    return Murmur::EncryptString(string);
}
PUBLIC Uint32        Compiler::GetHash(Token token) {
    return Murmur::EncryptData(token.Start, token.Length);
}

PUBLIC Chunk*        Compiler::CurrentChunk() {
    return &Function->Chunk;
}
PUBLIC int           Compiler::CodePointer() {
    return CurrentChunk()->Count;
}
PUBLIC void          Compiler::EmitByte(Uint8 byte) {
    // ChunkWrite(CurrentChunk(), byte, parser.Previous.Line);
    ChunkWrite(CurrentChunk(), byte, (int)((parser.Previous.Pos & 0xFFFF) << 16 | (parser.Previous.Line & 0xFFFF)));
}
PUBLIC void          Compiler::EmitBytes(Uint8 byte1, Uint8 byte2) {
    EmitByte(byte1);
    EmitByte(byte2);
}
PUBLIC void          Compiler::EmitUint16(Uint16 value) {
    EmitByte(value & 0xFF);
    EmitByte(value >> 8 & 0xFF);
}
PUBLIC void          Compiler::EmitUint32(Uint32 value) {
    EmitByte(value & 0xFF);
    EmitByte(value >> 8 & 0xFF);
    EmitByte(value >> 16 & 0xFF);
    EmitByte(value >> 24 & 0xFF);
}
PUBLIC void          Compiler::EmitConstant(VMValue value) {
    int index = FindConstant(value);
    if (index < 0)
        index = MakeConstant(value);

    EmitByte(OP_CONSTANT);
    EmitUint32(index);
}
PUBLIC void          Compiler::EmitLoop(int loopStart) {
    EmitByte(OP_JUMP_BACK);

    int offset = CurrentChunk()->Count - loopStart + 2;
    if (offset > UINT16_MAX) Error("Loop body too large.");

    EmitByte(offset & 0xFF);
    EmitByte((offset >> 8) & 0xFF);
}
PUBLIC int           Compiler::GetJump(int offset) {
    int jump = CurrentChunk()->Count - (offset + 2);
    if (jump > UINT16_MAX) {
        Error("Too much code to jump over.");
    }

    return jump;
}
PUBLIC int           Compiler::GetPosition() {
    return CurrentChunk()->Count;
}
PUBLIC int           Compiler::EmitJump(Uint8 instruction) {
    EmitByte(instruction);
    EmitByte(0xFF);
    EmitByte(0xFF);
    return CurrentChunk()->Count - 2;
}
PUBLIC int           Compiler::EmitJump(Uint8 instruction, int jump) {
    EmitByte(instruction);
    EmitUint16(jump);
    return CurrentChunk()->Count - 2;
}
PUBLIC void          Compiler::PatchJump(int offset) {
    int jump = GetJump(offset);

    CurrentChunk()->Code[offset]     = jump & 0xFF;
    CurrentChunk()->Code[offset + 1] = (jump >> 8) & 0xFF;
}
PUBLIC void          Compiler::EmitStringHash(char* string) {
    EmitUint32(GetHash(string));
}
PUBLIC void          Compiler::EmitStringHash(Token token) {
    if (!TokenMap->Exists(GetHash(token)))
        TokenMap->Put(GetHash(token), token);
    EmitUint32(GetHash(token));
}
PUBLIC void          Compiler::EmitReturn() {
    if (Type == TYPE_CONSTRUCTOR) {
        EmitBytes(OP_GET_LOCAL, 0); // return the new instance built from the constructor
    }
    else {
        EmitByte(OP_NULL);
    }
    EmitByte(OP_RETURN);
}

// Advanced Jumping
PUBLIC void          Compiler::StartBreakJumpList() {
    BreakJumpListStack.push(new vector<int>());
    BreakScopeStack.push(ScopeDepth);
}
PUBLIC void          Compiler::EndBreakJumpList() {
    vector<int>* top = BreakJumpListStack.top();
    for (size_t i = 0; i < top->size(); i++) {
        int offset = (*top)[i];
        PatchJump(offset);
    }
    delete top;
    BreakJumpListStack.pop();
    BreakScopeStack.pop();
}
PUBLIC void          Compiler::StartContinueJumpList() {
    ContinueJumpListStack.push(new vector<int>());
    ContinueScopeStack.push(ScopeDepth);
}
PUBLIC void          Compiler::EndContinueJumpList() {
    vector<int>* top = ContinueJumpListStack.top();
    for (size_t i = 0; i < top->size(); i++) {
        int offset = (*top)[i];
        PatchJump(offset);
    }
    delete top;
    ContinueJumpListStack.pop();
    ContinueScopeStack.pop();
}
PUBLIC void          Compiler::StartSwitchJumpList() {
    SwitchJumpListStack.push(new vector<switch_case>());
    SwitchScopeStack.push(ScopeDepth + 1);
}
PUBLIC void          Compiler::EndSwitchJumpList() {
    vector<switch_case>* top = SwitchJumpListStack.top();
    for (size_t i = 0; i < top->size(); i++) {
        // switch_case offset = (*top)[i];
        // PatchJump(offset);
    }
    delete top;
    SwitchJumpListStack.pop();
    SwitchScopeStack.pop();
}

PUBLIC int           Compiler::FindConstant(VMValue value) {
    for (size_t i = 0; i < CurrentChunk()->Constants->size(); i++) {
        if (ValuesEqual(value, (*CurrentChunk()->Constants)[i]))
            return (int)i;
    }
    return -1;
}
PUBLIC int           Compiler::MakeConstant(VMValue value) {
    int constant = ChunkAddConstant(CurrentChunk(), value);
    // if (constant > UINT8_MAX) {
    //     Error("Too many constants in one chunk.");
    //     return 0;
    // }
    return constant;
}

int  justin_print(char** buffer, int* buf_start, const char *format, ...) {
    va_list args;
    va_list argsCopy;
    va_start(args, format);
    va_copy(argsCopy, args);

    if (!buffer) {
        vprintf(format, args);
        return 0;
    }

    int count = vsnprintf(NULL, 0, format, argsCopy);

    // printf("pos %04d | adding %04d | size %04d\n", buf_start[0], count, buf_start[1]);

    while (buf_start[0] + count >= buf_start[1]) {
        buf_start[1] *= 2;

        // printf("\x1b[1;93m#%d buffer increased from %d -> %d (pos: %d + %d = %d)\x1b[m\n", i++, buf_start[1] / 2, buf_start[1], buf_start[0], count, buf_start[0] + count);
        *buffer = (char*)realloc(*buffer, buf_start[1]);
        if (!*buffer) {
            Log::Print(Log::LOG_ERROR, "Could not realloc for justin_print!");
            exit(-1);
        }
    }

    buf_start[0] += vsnprintf(*buffer + buf_start[0], buf_start[1] - buf_start[0], format, args);
    va_end(args);
    va_end(argsCopy);
    return 0;
}
PUBLIC STATIC void   Compiler::PrintValue(VMValue value) {
    Compiler::PrintValue(NULL, NULL, value);
}
PUBLIC STATIC void   Compiler::PrintValue(char** buffer, int* buf_start, VMValue value) {
    Compiler::PrintValue(buffer, buf_start, value, 0);
}
PUBLIC STATIC void   Compiler::PrintValue(char** buffer, int* buf_start, VMValue value, int indent) {
    switch (value.Type) {
        case VAL_NULL:
            justin_print(buffer, buf_start, "null");
            break;
        case VAL_INTEGER:
        case VAL_LINKED_INTEGER:
            justin_print(buffer, buf_start, "%d", AS_INTEGER(value));
            break;
        case VAL_DECIMAL:
        case VAL_LINKED_DECIMAL:
            justin_print(buffer, buf_start, "%f", AS_DECIMAL(value));
            break;
        case VAL_OBJECT:
            PrintObject(buffer, buf_start, value, indent);
            break;
        default:
            justin_print(buffer, buf_start, "UNKNOWN VALUE TYPE");
    }
}
PUBLIC STATIC void   Compiler::PrintObject(char** buffer, int* buf_start, VMValue value, int indent) {
    switch (OBJECT_TYPE(value)) {
        case OBJ_CLASS:
            justin_print(buffer, buf_start, "<class %s>", AS_CLASS(value)->Name ? AS_CLASS(value)->Name->Chars : "(null)");
            break;
        case OBJ_BOUND_METHOD:
            justin_print(buffer, buf_start, "<bound method %s>", AS_BOUND_METHOD(value)->Method->Name ? AS_BOUND_METHOD(value)->Method->Name->Chars : "(null)");
            break;
        case OBJ_CLOSURE:
            justin_print(buffer, buf_start, "<clsr %s>", AS_CLOSURE(value)->Function->Name ? AS_CLOSURE(value)->Function->Name->Chars : "(null)");
            break;
        case OBJ_FUNCTION:
            justin_print(buffer, buf_start, "<fn %s>", AS_FUNCTION(value)->Name ? AS_FUNCTION(value)->Name->Chars : "(null)");
            break;
        case OBJ_INSTANCE:
            justin_print(buffer, buf_start, "<class %s> instance", AS_INSTANCE(value)->Class->Name ? AS_INSTANCE(value)->Class->Name->Chars : "(null)");
            break;
        case OBJ_NATIVE:
            justin_print(buffer, buf_start, "<native fn>");
            break;
        case OBJ_STRING:
            justin_print(buffer, buf_start, "\"%s\"", AS_CSTRING(value));
            break;
        case OBJ_UPVALUE:
            justin_print(buffer, buf_start, "<upvalue>");
            break;
        case OBJ_ARRAY: {
            ObjArray* array = (ObjArray*)AS_OBJECT(value);

            justin_print(buffer, buf_start, "[", (int)array->Values->size());
            if (PrettyPrint)
                justin_print(buffer, buf_start, "\n");
            for (size_t i = 0; i < array->Values->size(); i++) {
                if (i > 0) {
                    justin_print(buffer, buf_start, ",");
                    if (PrettyPrint)
                        justin_print(buffer, buf_start, "\n");
                }

                for (int k = 0; k < indent + 1 && PrettyPrint; k++)
                    justin_print(buffer, buf_start, "    ");

                PrintValue(buffer, buf_start, (*array->Values)[i], indent + 1);
            }

            if (PrettyPrint)
                justin_print(buffer, buf_start, "\n");

            for (int i = 0; i < indent && PrettyPrint; i++)
                justin_print(buffer, buf_start, "    ");

            justin_print(buffer, buf_start, "]");
            break;
        }
        case OBJ_MAP: {
            ObjMap* map = (ObjMap*)AS_OBJECT(value);

            Uint32 hash;
            VMValue value;
            justin_print(buffer, buf_start, "{");
            if (PrettyPrint)
                justin_print(buffer, buf_start, "\n");

            bool first = false;
            for (int i = 0; i < map->Values->Capacity; i++) {
                if (map->Values->Data[i].Used) {
                    if (!first) {
                        first = true;
                    }
                    else {
                        justin_print(buffer, buf_start, ",");
                        if (PrettyPrint)
                            justin_print(buffer, buf_start, "\n");
                    }

                    for (int k = 0; k < indent + 1 && PrettyPrint; k++)
                        justin_print(buffer, buf_start, "    ");

                    hash = map->Values->Data[i].Key;
                    value = map->Values->Data[i].Data;
                    if (map->Keys && map->Keys->Exists(hash))
                        justin_print(buffer, buf_start, "\"%s\": ", map->Keys->Get(hash));
                    else
                        justin_print(buffer, buf_start, "0x%08X: ", hash);
                    PrintValue(buffer, buf_start, value, indent + 1);
                }
            }
            if (PrettyPrint)
                justin_print(buffer, buf_start, "\n");
            for (int k = 0; k < indent && PrettyPrint; k++)
                justin_print(buffer, buf_start, "    ");

            justin_print(buffer, buf_start, "}");
            break;
        }
        default:
            justin_print(buffer, buf_start, "UNKNOWN OBJECT TYPE %d", OBJECT_TYPE(value));
    }
}

// Debugging functions
PUBLIC STATIC int    Compiler::HashInstruction(const char* name, Chunk* chunk, int offset) {
    uint32_t hash = *(uint32_t*)&chunk->Code[offset + 1];
    printf("%-16s #%08X", name, hash);
    if (TokenMap->Exists(hash)) {
        Token t = TokenMap->Get(hash);
        printf(" (%.*s)", (int)t.Length, t.Start);
    }
    printf("\n");
    return offset + 5;
}
PUBLIC STATIC int    Compiler::ConstantInstruction(const char* name, Chunk* chunk, int offset) {
    int constant = *(int*)&chunk->Code[offset + 1];
    printf("%-16s %9d '", name, constant);
    PrintValue(NULL, NULL, (*chunk->Constants)[constant]);
    printf("'\n");
    return offset + 5;
}
PUBLIC STATIC int    Compiler::ConstantInstructionN(const char* name, int n, Chunk* chunk, int offset) {
    int constant = *(int*)&chunk->Code[offset + 1];
    printf("%s_%-*d %9d '", name, 15 - (int)strlen(name), n, constant);
    PrintValue(NULL, NULL, (*chunk->Constants)[constant]);
    printf("'\n");
    return offset + 5;
}
PUBLIC STATIC int    Compiler::SimpleInstruction(const char* name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}
PUBLIC STATIC int    Compiler::SimpleInstructionN(const char* name, int n, int offset) {
    printf("%s_%d\n", name, n);
    return offset + 1;
}
PUBLIC STATIC int    Compiler::ByteInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t slot = chunk->Code[offset + 1];
    printf("%-16s %9d\n", name, slot);
    return offset + 2; // [debug]
}
PUBLIC STATIC int    Compiler::LocalInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t slot = chunk->Code[offset + 1];
    if (slot > 0)
        printf("%-16s %9d\n", name, slot);
    else
        printf("%-16s %9d 'this'\n", name, slot);
    return offset + 2; // [debug]
}
PUBLIC STATIC int    Compiler::InvokeInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t slot = chunk->Code[offset + 1];
    uint32_t hash = *(uint32_t*)&chunk->Code[offset + 2];
    printf("%-13s %2d", name, slot);
    // PrintValue(NULL, NULL, (*chunk->Constants)[constant]);
    printf(" #%08X", hash);
    if (TokenMap->Exists(hash)) {
        Token t = TokenMap->Get(hash);
        printf(" (%.*s)", (int)t.Length, t.Start);
    }
    printf("\n");
    return offset + 6; // [debug]
}
PUBLIC STATIC int    Compiler::JumpInstruction(const char* name, int sign, Chunk* chunk, int offset) {
    uint16_t jump = (uint16_t)(chunk->Code[offset + 1]);
    jump |= chunk->Code[offset + 2] << 8;
    printf("%-16s %9d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}
PUBLIC STATIC int    Compiler::WithInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t slot = chunk->Code[offset + 1];
    if (slot == 0) {
        printf("%-16s %9d\n", name, slot);
        return offset + 2; // [debug]
    }
    uint16_t jump = (uint16_t)(chunk->Code[offset + 2]);
    jump |= chunk->Code[offset + 3] << 8;
    printf("%-16s %9d -> %d\n", name, slot, jump);
    return offset + 4; // [debug]
}
PUBLIC STATIC int    Compiler::DebugInstruction(Chunk* chunk, int offset) {
    printf("%04d ", offset);
    if (offset > 0 && (chunk->Lines[offset] & 0xFFFF) == (chunk->Lines[offset - 1] & 0xFFFF)) {
        printf("   | ");
    }
    else {
        printf("%4d ", chunk->Lines[offset] & 0xFFFF);
    }

    uint8_t instruction = chunk->Code[offset];
    switch (instruction) {
        case OP_CONSTANT:
            return ConstantInstruction("OP_CONSTANT", chunk, offset);
        case OP_NULL:
            return SimpleInstruction("OP_NULL", offset);
        case OP_TRUE:
            return SimpleInstruction("OP_TRUE", offset);
        case OP_FALSE:
            return SimpleInstruction("OP_FALSE", offset);
        case OP_POP:
            return SimpleInstruction("OP_POP", offset);
        case OP_COPY:
            return SimpleInstruction("OP_COPY", offset);
        case OP_GET_LOCAL:
            return LocalInstruction("OP_GET_LOCAL", chunk, offset);
        case OP_SET_LOCAL:
            return LocalInstruction("OP_SET_LOCAL", chunk, offset);
        case OP_GET_GLOBAL:
            return HashInstruction("OP_GET_GLOBAL", chunk, offset);
        case OP_DEFINE_GLOBAL:
            return HashInstruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL:
            return HashInstruction("OP_SET_GLOBAL", chunk, offset);
        case OP_GET_PROPERTY:
            return HashInstruction("OP_GET_PROPERTY", chunk, offset);
        case OP_SET_PROPERTY:
            return HashInstruction("OP_SET_PROPERTY", chunk, offset);

        case OP_INCREMENT:
            return SimpleInstruction("OP_INCREMENT", offset);
        case OP_DECREMENT:
            return SimpleInstruction("OP_DECREMENT", offset);

        case OP_BITSHIFT_LEFT:
            return SimpleInstruction("OP_BITSHIFT_LEFT", offset);
        case OP_BITSHIFT_RIGHT:
            return SimpleInstruction("OP_BITSHIFT_RIGHT", offset);

        case OP_EQUAL:
            return SimpleInstruction("OP_EQUAL", offset);
        case OP_EQUAL_NOT:
            return SimpleInstruction("OP_EQUAL_NOT", offset);
        case OP_LESS:
            return SimpleInstruction("OP_LESS", offset);
        case OP_LESS_EQUAL:
            return SimpleInstruction("OP_LESS_EQUAL", offset);
        case OP_GREATER:
            return SimpleInstruction("OP_GREATER", offset);
        case OP_GREATER_EQUAL:
            return SimpleInstruction("OP_GREATER_EQUAL", offset);

        case OP_ADD:
            return SimpleInstruction("OP_ADD", offset);
        case OP_SUBTRACT:
            return SimpleInstruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY:
            return SimpleInstruction("OP_MULTIPLY", offset);
        case OP_MODULO:
            return SimpleInstruction("OP_MODULO", offset);
        case OP_DIVIDE:
            return SimpleInstruction("OP_DIVIDE", offset);

        case OP_BW_NOT:
            return SimpleInstruction("OP_BW_NOT", offset);
        case OP_BW_AND:
            return SimpleInstruction("OP_BW_AND", offset);
        case OP_BW_OR:
            return SimpleInstruction("OP_BW_OR", offset);
        case OP_BW_XOR:
            return SimpleInstruction("OP_BW_XOR", offset);

        case OP_LG_NOT:
            return SimpleInstruction("OP_LG_NOT", offset);
        case OP_LG_AND:
            return SimpleInstruction("OP_LG_AND", offset);
        case OP_LG_OR:
            return SimpleInstruction("OP_LG_OR", offset);

        case OP_GET_ELEMENT:
            return SimpleInstruction("OP_GET_ELEMENT", offset);
        case OP_SET_ELEMENT:
            return SimpleInstruction("OP_SET_ELEMENT", offset);
        case OP_NEW_ARRAY:
            return SimpleInstruction("OP_NEW_ARRAY", offset) + 4;
        case OP_NEW_MAP:
            return SimpleInstruction("OP_NEW_MAP", offset) + 4;

        case OP_NEGATE:
            return SimpleInstruction("OP_NEGATE", offset);
        case OP_PRINT:
            return SimpleInstruction("OP_PRINT", offset);
        case OP_JUMP:
            return JumpInstruction("OP_JUMP", 1, chunk, offset);
        case OP_JUMP_IF_FALSE:
            return JumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_JUMP_BACK:
            return JumpInstruction("OP_JUMP_BACK", -1, chunk, offset);
        case OP_CALL:
            return ByteInstruction("OP_CALL", chunk, offset);
        case OP_INVOKE:
            return InvokeInstruction("OP_INVOKE", chunk, offset);

        case OP_PRINT_STACK: {
            offset++;
            uint8_t constant = chunk->Code[offset++];
            printf("%-16s %4d ", "OP_PRINT_STACK", constant);
            PrintValue(NULL, NULL, (*chunk->Constants)[constant]);
            printf("\n");

            ObjFunction* function = AS_FUNCTION((*chunk->Constants)[constant]);
            for (int j = 0; j < function->UpvalueCount; j++) {
                int isLocal = chunk->Code[offset++];
                int index = chunk->Code[offset++];
                printf("%04d   |                     %s %d\n", offset - 2, isLocal ? "local" : "upvalue", index);
            }

            return offset;
        }

        case OP_RETURN:
            return SimpleInstruction("OP_RETURN", offset);
        case OP_WITH:
            return WithInstruction("OP_WITH", chunk, offset);
        case OP_CLASS:
            return HashInstruction("OP_CLASS", chunk, offset);
        case OP_INHERIT:
            return SimpleInstruction("OP_INHERIT", offset);
        case OP_METHOD:
            return InvokeInstruction("OP_METHOD", chunk, offset);
        default:
            printf("\x1b[1;93mUnknown opcode %d\x1b[m\n", instruction);
            return offset + 1;
    }
}
PUBLIC STATIC void   Compiler::DebugChunk(Chunk* chunk, const char* name, int arity) {
    printf("== %s (argCount: %d) ==\n", name, arity);
    printf("byte   ln\n");
    for (int offset = 0; offset < chunk->Count;) {
        offset = DebugInstruction(chunk, offset);
    }

    printf("\nConstants: (%d count)\n", (int)(*chunk->Constants).size());
    for (size_t i = 0; i < (*chunk->Constants).size(); i++) {
        printf(" %2d '", (int)i);
        PrintValue(NULL, NULL, (*chunk->Constants)[i]);
        printf("'\n");
    }
}

PUBLIC STATIC void   Compiler::HTML5ConvertChunk(Chunk* chunk, const char* name, int arity) {
    printf("== %s (argCount: %d) ==\n", name, arity);
    printf("byte   ln\n");
    for (int offset = 0; offset < chunk->Count;) {
        offset = DebugInstruction(chunk, offset);
    }
}

// Compiling
PUBLIC STATIC void   Compiler::Init() {
    Compiler::MakeRules();

    if (Compiler::TokenMap == NULL) {
        Compiler::TokenMap = new HashMap<Token>(NULL, 8);
    }
}
PUBLIC void          Compiler::Initialize(Compiler* enclosing, int scope, int type) {
    Type = type;
    LocalCount = 0;
    ScopeDepth = scope;
    Enclosing = enclosing;
    Function = NewFunction();
    Compiler::Functions.push_back(Function);

    switch (type) {
        case TYPE_CONSTRUCTOR:
        case TYPE_METHOD:
        case TYPE_FUNCTION:
            Function->Name = CopyString(parser.Previous.Start, parser.Previous.Length);
            break;
        case TYPE_WITH:
            Function->Name = CopyString("<anonymous-fn>", 14);
            break;
        case TYPE_TOP_LEVEL:
            Function->Name = CopyString("main", 4);
            break;
    }

    Local* local = &Locals[LocalCount++];
    local->Depth = ScopeDepth;

    if (type != TYPE_FUNCTION) {
        // In a method, it holds the receiver, "this".
        local->Name.Start = (char*)"this";
        local->Name.Length = 4;

        // local = &Locals[LocalCount++];
        // local->Depth = ScopeDepth;
        // local->Name.Start = (char*)"super";
        // local->Name.Length = 5;
    }
    else {
        // In a function, it holds the function, but cannot be referenced,
        // so has no name.
        local->Name.Start = (char*)"";
        local->Name.Length = 0;
    }
}
PUBLIC bool          Compiler::Compile(const char* filename, const char* source, const char* output) {
    scanner.Line = 1;
    scanner.Start = (char*)source;
    scanner.Current = (char*)source;
    scanner.LinePos = (char*)source;
    scanner.SourceFilename = (char*)filename;

    parser.HadError = false;
    parser.PanicMode = false;

    EnumsCount = 0;

    Compiler::Functions.clear();
    Initialize(NULL, 0, TYPE_TOP_LEVEL);

    AdvanceToken();
    while (!MatchToken(TOKEN_EOF)) {
        // if (PeekToken().Type == TOKEN_EOF) break;
        // AdvanceToken();
        // GetExpression();
        GetDeclaration();
    }

    ConsumeToken(TOKEN_EOF, "Expected end of file.");
    FinishCompiler();

    bool debugCompiler = false;
    Application::Settings->GetBool("dev", "debugCompiler", &debugCompiler);
    if (debugCompiler) {
        for (size_t c = 0; c < Compiler::Functions.size(); c++) {
            Chunk* chunk = &Compiler::Functions[c]->Chunk;
            DebugChunk(chunk, Compiler::Functions[c]->Name->Chars, Compiler::Functions[c]->Arity);
            printf("\n");
        }
    }

    // return false;

    Stream* stream = FileStream::New(output, FileStream::WRITE_ACCESS);
    if (!stream) return false;

    bool doLineNumbers = false;
    // #ifdef DEBUG
    doLineNumbers = true;
    // #endif

    stream->WriteBytes((char*)Compiler::Magic, 4);
    stream->WriteByte(0x00);
    stream->WriteByte(doLineNumbers);
    stream->WriteByte(0x00);
    stream->WriteByte(0x00);

    int chunkCount = (int)Compiler::Functions.size();

    stream->WriteUInt32(chunkCount);
    for (int c = 0; c < chunkCount; c++) {
        int    arity = Compiler::Functions[c]->Arity;
        Chunk* chunk = &Compiler::Functions[c]->Chunk;

        stream->WriteUInt32(chunk->Count);
        stream->WriteUInt32(arity);
        stream->WriteUInt32(Murmur::EncryptString(Compiler::Functions[c]->Name->Chars));

        stream->WriteBytes(chunk->Code, chunk->Count);
        if (doLineNumbers) {
            stream->WriteBytes(chunk->Lines, chunk->Count * sizeof(int));
        }

        int constSize = (int)chunk->Constants->size();
        stream->WriteUInt32(constSize); // fwrite(&constSize, 1, sizeof(constSize), f);
        for (int i = 0; i < constSize; i++) {
            VMValue constt = (*chunk->Constants)[i];
            Uint8 type = (Uint8)constt.Type;
            stream->WriteByte(type); // fwrite(&type, 1, sizeof(type), f);

            switch (type) {
                case VAL_INTEGER:
                    stream->WriteBytes(&AS_INTEGER(constt), sizeof(int));
                    break;
                case VAL_DECIMAL:
                    stream->WriteBytes(&AS_DECIMAL(constt), sizeof(float));
                    break;
                case VAL_OBJECT:
                    if (OBJECT_TYPE(constt) == OBJ_STRING) {
                        ObjString* str = AS_STRING(constt);
                        stream->WriteBytes(str->Chars, str->Length + 1);
                    }
                    else {
                        printf("Unsupported object type...Chief.\n");
                    }
                    break;
            }
            // printf(" %2d '", i);
            // PrintValue(NULL, NULL, (*chunk->Constants)[i]);
            // printf("'\n");
        }
    }

    // Output HTML5 here
    bool html5output = false;
    Application::Settings->GetBool("dev", "html5output", &html5output);
    html5output = false;
    if (html5output) {
        Log::Print(Log::LOG_IMPORTANT, "Filename: %s", filename);
        // for (size_t c = 0; c < Compiler::Functions.size(); c++) {
        int c = 1;
            Chunk* chunk = &Compiler::Functions[c]->Chunk;
            HTML5ConvertChunk(chunk, Compiler::Functions[c]->Name->Chars, Compiler::Functions[c]->Arity);
        //     printf("\n");
        // }
        Log::Print(Log::LOG_ERROR, "Could not make html5output!");
        exit(-1);
    }

    // Add tokens
    if (doLineNumbers && TokenMap) {
        stream->WriteUInt32(TokenMap->Count);
        TokenMap->WithAll([stream](Uint32, Token t) -> void {
            stream->WriteBytes(t.Start, t.Length);
            stream->WriteByte(0); // NULL terminate
        });
        TokenMap->Clear();
    }

    stream->Close();

    for (size_t s = 0; s < Strings.size(); s++) {
        Memory::Free(Strings[s]->Chars);
        Memory::Free(Strings[s]);
    }
    Strings.clear();

    return !parser.HadError;
}
PUBLIC ObjFunction*  Compiler::FinishCompiler() {
    EmitReturn();

    return Function;
}

PUBLIC VIRTUAL       Compiler::~Compiler() {

}
PUBLIC STATIC void   Compiler::Dispose(bool freeTokens) {
    for (size_t c = 0; c < Compiler::Functions.size(); c++) {
        ChunkFree(&Compiler::Functions[c]->Chunk);
        Memory::Free(Compiler::Functions[c]->Name->Chars);
        Memory::Free(Compiler::Functions[c]->Name);
        Memory::Free(Compiler::Functions[c]);
        if (GarbageCollector::GarbageSize >= sizeof(ObjFunction))
            GarbageCollector::GarbageSize -= sizeof(ObjFunction);
    }
    Memory::Free(Rules);

    if (TokenMap && freeTokens) {
        delete TokenMap;
        TokenMap = NULL;
    }
}
