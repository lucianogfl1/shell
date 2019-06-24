#include <stdlib.h>
#include "process_control.h"

Jobl create_jobl() {
    Jobl list = (Jobl) malloc(sizeof(struct jobl));

    list->head = NULL;
    list->size = 0;
    list->jid_count = 0;
    return list;
}

Jobl add_job(Jobl list, Job job) {
    Jobl_tail temp = list->head;

    list->head = (Jobl_tail) malloc(sizeof(struct jobl_tail));
    job->is_valid = VALID;
    list->head->item = job;
    list->head->next = temp;
    list->jid_count++;
    list->size++;
}

void invalidate_job(Job job) {
    job->is_valid = INVALID;
}

void free_jobl(Jobl list) {
    Jobl_tail head = list->head;

    while(head != NULL) {
        Jobl_tail temp = head;

        free_cmd(&(head->item->cmd));
        free(head->item);
        head = head->next;
        free(temp);
    }
    free(list);
}
