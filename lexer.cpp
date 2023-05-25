
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

static std::string identifier_string; // Filled in if tok_identifier
static double num_val;                // Filled in if tok_number

static int gettok()
{
    static int last_char = ' ';
    // Skips any whitespace
    while (isspace(last_char))
        last_char = getchar();

    if (isalpha(last_char)) {
        identifier_string = last_char;
        while (isalnum((last_char = getchar())))
            identifier_string += last_char;

        if (identifier_string == "def")
            return tok_def;
        if (identifier_string == "extern")
            return tok_extern;

        return tok_identifier;
    }
}
