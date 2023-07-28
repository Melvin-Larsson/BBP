#ifndef INCLUDE_STRING_H
#define INCLUDE_STRING_H

int strlen(const char* str);
char* strcpy(char* destination, const char* source);
void strReadInt(int x, char* output);
char* strAppend(char *destination, const char* source);
char* strAppendFrom(char *destination, const char* source, int start);
void sprintf(char *str, const char *format, ...);

#endif
