#ifndef PROCESS_H
#define PROCESS_H

    #include <sys/types.h>
    #include "parser.h"

    typedef struct job *Job;
    typedef struct jobl      *Jobl;
    typedef struct jobl_tail *Jobl_tail;

    struct job {
        pid_t pid;
        int   jid;
        Command cmd;
        int   status;
        int   is_valid;
        int   is_foreground;
    };

    struct jobl_tail {
        Jobl_tail next;
        Job       item;
    };

    struct jobl {
        Jobl_tail head;
        int       size;
        int       jid_count;
    };

    Jobl create_jobl();
    Jobl add_job(Jobl, Job);
    void invalidate_job(Job);
    void free_jobl(Jobl);

#endif
