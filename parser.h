#ifndef PARSER_H
#define PARSER_H

    #define CMD_MAX_SIZE 50
    #define VALID        1
    #define INVALID      0

    // Redirection types
    #define ROUT        1
    #define ROUT_APPEND 2
    #define RERR        3
    #define RIN         4

    struct redirection_t {
        int type;
        char *file;
    };

    typedef struct command *Command;

    Command parse(char *);
    char *get_cmd_name(Command);
    char **get_cmd_args(Command);
    char* get_token(char* str, const char delim, const char EOL);
    int is_foreground(Command);
    int get_cmd_argc(Command);
    void free_cmd(Command *);
    void print_cmd(Command);
    struct redirection_t* extract_redirection (char **, const char* check,
                                               int type);
    Command* break_into_commands(Command, int);
    int count_pipes(Command);

#endif
