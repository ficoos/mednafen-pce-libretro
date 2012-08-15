#ifndef MDFN_FILE_H
#define MDFN_FILE_H

#include <string>

#define MDFNFILE_EC_NOTFOUND	1
#define MDFNFILE_EC_OTHER	2

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
 private:
 int error_code;
 int local_errno;
 int64 location;
 bool MakeMemWrapAndClose(FILE *tz);
};

class PtrLengthPair
{
 public:

 inline PtrLengthPair(const void *new_data, const uint64 new_length)
 {
  data = new_data;
  length = new_length;
 }

 ~PtrLengthPair() { } 
 const void *data;
 uint64 length;
};

// These functions should be used for data like save states and non-volatile backup memory.
// Until(if, even) we add LoadFromFile functions, for reading the files these functions generate, just use gzopen(), gzread(), etc.

bool MDFN_DumpToFile(const char *filename, int compress, const void *data, const uint64 length);

#endif
