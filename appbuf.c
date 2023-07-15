#include <string.h>
struct AppendBuffer {
    char * b;
    int len;
};
#define AB_INIT {NULL,0}

void AppendAB(struct AppendBuffer * ab,char * s,int len) {
    char * new=realloc(ab->b,ab->len+len);

    if (new==NULL) return;
    memcpy(&new[ab->len],s,len);
    ab->b=new;
    ab->len+=len;
}
void FreeAB(struct AppendBuffer * ab) {
    free(ab->b);
}