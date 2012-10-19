#ifndef MDFN_FILE_H
#define MDFN_FILE_H

#include <string>

class MDFNFILE
{
 public:

 MDFNFILE();
 MDFNFILE(const char *path);

 ~MDFNFILE();

 bool Open(const char *path);
 bool Close(void);
 uint8 *f_data;
 int64 f_size;
 char *f_ext;
};

// These functions should be used for data like save states and non-volatile backup memory.
// Until(if, even) we add LoadFromFile functions, for reading the files these functions generate, just use gzopen(), gzread(), etc.

bool MDFN_DumpToFile(const char *filename, int compress, const void *data, const uint64 length);

#endif
