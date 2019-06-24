#include "parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

struct command {
    char  *ptr[CMD_MAX_SIZE + 1];
    int   len;
};

// Shifts one to left (i.e. \_123\n -> _123\n\n)
void shift2left(char *str, const char EOL) {
    int i;

    for (i = 0; str[i] != EOL; i++)
        str[i] = str[i + 1];
}

char* get_token(char* str, const char delim, const char EOL) {
    static char* my_str = NULL;
    char* token;
    int i;

    // Check if it is a new call
    if (str) {
        my_str = str;

        // Fills any required delimiter with '\0'
        for (i = 0; my_str[i] != EOL; i++) {
            // Delimiter
            if (my_str[i] == delim) my_str[i] = 0;
            // Skip character
            else if(my_str[i] == '\\') shift2left(&my_str[i], EOL);
            // Single quotes
            else if(my_str[i] == '\"') {
                shift2left(&my_str[i], EOL);

                // Look for the next quotes, replaces it with '\0' and
                // skip everything in between
                for(; my_str[i] != '\"' && my_str[i] != EOL; i++);
                if (my_str[i] == '\"') my_str[i] = 0;
                else i--;
            }
        }
    }

    // Suppress '\0'
    for(i = 0; my_str[i] == 0; i++);

    // Case we've reached an end
    if(my_str[i] == EOL) return NULL;

    // Copies the token
    token = strdup(&my_str[i]);

    // Advance to the next token
    for(; my_str[i] != 0 && my_str[i] != EOL; i++);
    my_str = &my_str[i];

    // Suppress any remaining EOL
    for(i = 0; token[i] != 0; i++) {
        if (token[i] == EOL) {
            token[i] = 0;
            break;
        }
    }

    return token;
}

Command parse(char *cmd_str) {
    if (cmd_str) {
        char    *token = get_token(cmd_str, ' ', '\n');
        Command cmd = (Command) malloc(sizeof(struct command));

        cmd->len = 0;

        // Process token per token
        while(token != NULL) {
            cmd->ptr[cmd->len++] = token;

            // Get next token
            token = get_token(NULL, ' ', '\n');
        }

        if (cmd->len == 0) {
            free_cmd(&cmd);
            return NULL;
        }

        // Handles an empty input
        if (!strcmp(cmd->ptr[0], "")) {
            free_cmd(&cmd);
            return NULL;
        }

        // Put a NULL value at the end of the args list
        cmd->ptr[cmd->len] = NULL;
        return cmd;
    }
    return NULL;
}

char *get_cmd_name(Command cmd) {
    return cmd->len > 0 ? cmd->ptr[0] : NULL;
}

char **get_cmd_args(Command cmd) {
    return cmd->len > 0 ? cmd->ptr : NULL;
}

void print_cmd(Command cmd) {
    int i;

    printf("%s (", cmd->ptr[0]);
    for (i = 1; i < cmd->len; i++) {
        printf("%s", cmd->ptr[i]);
        if (i < cmd->len - 1)
            printf(", ");
    }
    printf(")");
}

void free_cmd(Command *cmd){
    int i;

    for (i = 0; i < (*cmd)->len; i++) {
        free((*cmd)->ptr[i]);
    }
    free(*cmd);
    cmd = NULL;
}

int is_foreground(Command cmd) {
    return !(strcmp("&", cmd->ptr[cmd->len - 1]) == 0);
}

int get_cmd_argc(Command cmd) {
    return cmd->len;
}

struct redirection_t* extract_redirection (char** cmd, const char* check,
                                           int type) {
    int i, nops = 0, error = 0, found_idx = -1, last_idx;
    struct redirection_t* r = (struct redirection_t*)
                               malloc(sizeof(struct redirection_t));
    char *temp;

    r->file = NULL;
    for (i = 0; cmd[i]; i++) {
        if (!strcmp(cmd[i], check)) {
            r->type = type;
            if (cmd[i + 1]) {
                r->file = cmd[i + 1];
            }
            else {
                error = 1;
            }
            found_idx = i;
            nops++;
        }
    }

    if (error || found_idx < 0 || nops > 1) {
        free(r);
        return NULL;
    }

    last_idx = found_idx;
    temp = cmd[found_idx];
    for(; cmd[found_idx]; found_idx++) {
        cmd[found_idx] = cmd[found_idx + 1];
    }
    free(temp);
    found_idx = last_idx;
    for(; cmd[found_idx]; found_idx++) {
        cmd[found_idx] = cmd[found_idx + 1];
    }

    return r;
}

int count_pipes(Command cmd) {
    int i, count = 0;

    if (!strcmp(cmd->ptr[cmd->len - 1], "|")) {
        return 0;
    }

    for (i = 0; i < cmd->len; i++) {
        if (!strcmp(cmd->ptr[i], "|")) {
            count++;
        }
    }
    return count;
}

Command* break_into_commands(Command cmd, int count) {
    int i, j = 0;
    Command* cmds;
    Command new_cmd;

    cmds = (Command *) malloc((count + 1) * sizeof(Command *));

    if (!count) {
        cmds[0] = cmd;
        return cmds;
    }
    new_cmd = (Command) malloc(sizeof(struct command));
    new_cmd->len = 0;
    cmd->ptr[cmd->len] = "|";

    for (i = 0; i <= cmd->len; i++) {
        if (!strcmp(cmd->ptr[i], "|")) {
            new_cmd->ptr[new_cmd->len] = 0;
            cmds[j++] = new_cmd;
            new_cmd = (Command) malloc(sizeof(struct command));
            new_cmd->len = 0;
        }
        else {
            new_cmd->ptr[new_cmd->len++] = cmd->ptr[i];
        }
    }
    free(cmd);
    return cmds;
}
