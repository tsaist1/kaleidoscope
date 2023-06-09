#include <cctype>
#include <cstdio>
#include <string>
#include <memory>
#include <vector>
#include <map>

/*---------------------------------------------------------------------------------
 * Parser
 *-------------------------------------------------------------------------------*/
// The lexer returns tokens [0-255] if it is an unknown character, otherwise one of
// these for known things.
enum Token
{
    tok_eof = -1,

    // commands
    tok_def = -2,
    tok_extern = -3,

    // primary
    tok_identifier = -4,
    tok_number = -5,
};

static std::string identifierString; // Filled in if tok_identifier
static double NumVal;                // Filled in if tok_number

/// gettok - Return the next token from standard input.
static int gettok()
{
    static int last_char = ' ';
    // Skips any whitespace
    while (isspace(last_char)) // identifier: [a-zA-Z][a-zA-Z0-9]*
        last_char = getchar();

    if (isalpha(last_char)) {
        identifierString = last_char;
        while (isalnum((last_char = getchar())))
            identifierString += last_char;

        if (identifierString == "def")
            return tok_def;
        if (identifierString == "extern")
            return tok_extern;

        return tok_identifier;
    }

    if (isdigit(last_char) || last_char == '.') {  // Number: [0-9.]+
        std::string num_str;
        do {
            num_str += last_char;
            last_char = getchar();
        } while (isdigit(last_char) || last_char == '.');
        NumVal = std::strtod(num_str.c_str(), 0);
        return tok_number;
    }

    if (last_char == '#') {
        // Comment until end of line.
        do
            last_char = getchar();
        while (last_char != EOF && last_char != '\n' && last_char != '\r');

        if (last_char != EOF)
            return gettok();
    }
    // Check for end of file.  Don't eat the EOF.
    if (last_char == EOF)
        return tok_eof;
    
    // Otherwise, just return the character as its ascii value.
    int this_char = last_char;
    last_char = getchar();
    return this_char;
}
/*-------------------------------------------------------------------------------
 * AST -- Abstract Syntax Tree (Parse Tree)
 *------------------------------------------------------------------------------*/
namespace
{
/// ExprAST - Base class for all expression nodes.
    class ExprAST 
    {
    public:
        virtual ~ExprAST() = default;
        virtual Value *codegen() = 0;
    };

/// NumberExprAST - Expression class for numeric literals like "1.0".
    class NumberExprAST : public ExprAST 
    {
        double Val;
    public:
        NumberExprAST(double val) : Val(val) {}
        Value *codegen() override;
    };

/// VariableExprAST - Expression class for referencing a variable, like "a".
    class VariableExprAST : public ExprAST 
    {
        std::string Name;
    public:
        VariableExprAST(const std::string &name) : Name(name) {}
    };

/// BinaryExprAST - Expression class for a binary operator.
    class BinaryExprAST : public ExprAST 
    {
        char Op;
        std::unique_ptr<ExprAST> LHS, RHS;
    public:
        BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                      std::unique_ptr<ExprAST> RHS)
                : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
    };

/// CallExprAST - Expression class for function calls.
    class CallExprAST : public ExprAST 
    {
        std::string Callee;
        std::vector<std::unique_ptr<ExprAST>> Args;
    public:
        CallExprAST(const std::string &Callee,
                    std::vector<std::unique_ptr<ExprAST>> Args)
                : Callee(Callee), Args(std::move(Args)) {}
    };

/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes).
    class PrototypeAST 
    {
        std::string Name;
        std::vector<std::string> Args;
    public:
        PrototypeAST(const std::string &Name, std::vector<std::string> Args)
                : Name(Name), Args(std::move(Args)) {}

        const std::string &getName() { return Name; }
    };

/// FunctionAST - This class represents a function definition itself.
    class FunctionAST 
    {
        std::unique_ptr<PrototypeAST> Proto;
        std::unique_ptr<ExprAST> Body;

    public:
        FunctionAST(std::unique_ptr<PrototypeAST> Proto,
                      std::unique_ptr<ExprAST> Body)
                : Proto(std::move(Proto)), Body(std::move(Body)) {}
    };
} // end anonymous namespace
/*--------------------------------------------------------------------------------
 * Parser
 *------------------------------------------------------------------------------*/
/// CurTok/getNextToken - Provide a simple token buffer.  CurTok is the current
/// token the parser is looking at.  getNextToken reads another token from the
/// lexer and updates CurTok with its results.
static int CurTok;

static int getNextToken() 
{
    return CurTok = gettok();
}

std::unique_ptr<ExprAST> LogError(const char *Str)
{
    fprintf(stderr, "Error, %s\n", Str);
    return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) 
{
    LogError(Str);
    return nullptr;
}

static std:: unique_ptr<ExprAST> ParseExpression();

/// numberexp ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr()
{
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    getNextToken();
    return std::move(Result);
}


/// parenexpr ::= '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr() 
{
    getNextToken();
    auto V = ParseExpression();
    if (!V)
        return nullptr;

    if (CurTok != ')')
        return LogError("expected ')");
    getNextToken();
    return V;
}

/// identifierexpr
///   ::= identifier
///   ::= identifier '(' expression* ')'
static std::unique_ptr<ExprAST> ParseIdentifierExpr() 
{
    std::string IdName = identifierString;
    getNextToken();    // eat identifier

    if (CurTok != '(')     // Simple variable ref
        return std::make_unique<VariableExprAST>(IdName);


    // Call
    getNextToken();    // eat (
    std::vector<std::unique_ptr<ExprAST>> Args;
    if (CurTok != ')') {
        while (true) {
            if (auto Arg = ParseExpression())
                Args.push_back(std::move(Arg));
            else
                return nullptr;

            if (CurTok == ')')
                return LogError("Expected ')' or ',' in argument list");
            getNextToken();
        }
    }
    // Eat the ')'.
    getNextToken();
    return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

/// primary
///   ::= identifierexpr
///   ::= numberexpr
///   ::= parenexpr
static std::unique_ptr<ExprAST> ParsePrimary()
{
    switch (CurTok) {
        default:
            return LogError("unknown token when expecting an expression");
        case tok_identifier:
            return ParseIdentifierExpr();
        case tok_number:
            return ParseNumberExpr();
        case '(':
            return ParseParenExpr();
    }
}

/// BinopPrecedence - This holds the precedence for each binary operator that is
/// defined.
static std::map<char, int> BinopPrecedence;

/// GetTokPrecedence - Get the precedence of the pending binary operator token.
static int GetTokPrecedence()
{
    if (!isascii(CurTok))
        return -1;

    int TokPrec = BinopPrecedence[CurTok];
    if (TokPrec <= 0) return -1;
    return TokPrec;
}

static std::unique_ptr<ExprAST> ParseBinOpRHS(int, std::unique_ptr<ExprAST>);
/// expression
///   ::= primary binoprhs
///
static std::unique_ptr<ExprAST> ParseExpression() 
{
  auto LHS = ParsePrimary();
  if (!LHS)
    return nullptr;

  return ParseBinOpRHS(0, std::move(LHS));
}
/// binoprhs
///   ::= ('+' primary)*
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                              std::unique_ptr<ExprAST> LHS) 
{
    // If this is a binop, find its precedence.
    while (true) {
        int TokPrec = GetTokPrecedence();
        
        // If this is a binop that binds at least as tightly as the current binop,
        // consume it, otherwise we are done.
        if (TokPrec < ExprPrec)
            return LHS;
    }
}

static std::unique_ptr<PrototypeAST> ParsePrototype() 
{
    if (CurTok != tok_identifier)
    return LogErrorP("Expected function name in prototype");

    std::string FnName = identifierString;
    getNextToken();

    if (CurTok != '(')
        return LogErrorP("Expected '(' in prototype");
    
    // Read the list of argument names.
    std::vector<std::string> ArgNames;
    while (getNextToken() == tok_identifier)
        ArgNames.push_back(identifierString);
    if (CurTok != ')')
        return LogErrorP("Expected ')' in prototype");
    
    // success.
    getNextToken();     // eat ')'.

    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

/// definition ::= 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseDefinition()
{
    getNextToken();     // eat def.
    auto Proto = ParsePrototype();
    if (!Proto) return nullptr;

    if (auto E = ParseExpression())
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    return nullptr;
}

/// external ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() 
{
    getNextToken;   // eat extern.
    return ParsePrototype();
}

/// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr()
{
    if (auto E = ParseExpression()) {
        // Make an anonymous proto.
        auto Proto = std::make_unique<PrototypeAST>("", std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}
/*--------------------------------------------------------------------------------
 * Top-Level parsing
 *------------------------------------------------------------------------------*/
static void HandleDefinition()
{
    if (ParseDefinition()) {
        fprintf(stderr, "Parsed a function definition.\n");
    } else {
        // Skip token for error recovery.
        getNextToken();
    }
}

static void HandleExtern()
{
    if (ParseExtern()) {
        fprintf(stderr, "Parsed an extern.\n");
    } else {
        // Skip for error recovery.
        getNextToken();
    }
}

static void HandleTopLevelExpression()
{
    // Evaluate a top-level expression into an anonymous function.
    if (ParseTopLevelExpr()) {
        fprintf(stderr, "Parsed a top-level expr\n");
    } else {
        // Skip for error recovery
        getNextToken();
    }
}
/// top ::= definition | external | expression | ';'
static void MainLoop()
{
    while (true) {
        fprintf(stderr, "ready> ");
        switch (CurTok) {
            case tok_eof:
                return;
            case ';':   // ignore top-level semicolons.
                getNextToken();
                break;
            case tok_def:
                HandleDefinition();
                break;
            case tok_extern:
                HandleExtern();
                break;
            default:
                HandleTopLevelExpression();
                break;
        }        
    }
}



int main() {
    // Install standard binary operators.
    // 1 is lowest precedence.
    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 30;
    BinopPrecedence['*'] = 40;	// highest.
    // TODO: binary operators
    return 0;
}
