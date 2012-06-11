// TODO/WIP

class Stream
{
 public:

 Stream();
 virtual ~Stream();

 virtual uint64 read(void *data, uint64 count, bool error_on_eos = true) = 0;
 virtual void write(const void *data, uint64 count) = 0;
 virtual void seek(int64 offset, int whence) = 0;
 virtual int64 tell(void) = 0;
 virtual int64 size(void) = 0;
 virtual void close(void) = 0;	// Flushes(in the case of writeable streams) and closes the stream.
				// Necessary since this operation can fail(running out of disk space, for instance),
				// and throw an exception in the destructor would be a Bad Idea(TM).
				//
				// Manually calling this function isn't strictly necessary, but recommended when the
				// stream is writeable; it will be called automatically from the destructor, with any
				// exceptions thrown caught and logged.

 //
 // Utility functions:
 //
 int scanf(const char *format, ...) MDFN_FORMATSTR(scanf, 2, 3);
 void printf(const char *format, ...) MDFN_FORMATSTR(printf, 2, 3);
 uint8 get_u8(void);
 void put_u8(uint8 c);
 void put_string(const char *str);
 void put_string(const std::string &str);
 bool get_line(std::string &str);

 unsigned int line_read_skip;
};

// StreamFilter takes ownership of the Stream pointer passed, and will delete it in its destructor.
class StreamFilter : public Stream
{
 public:

 StreamFilter();
 StreamFilter(Stream *target_arg);
 virtual ~StreamFilter();

 virtual uint64 read(void *data, uint64 count, bool error_on_eos = true) = 0;
 virtual void write(const void *data, uint64 count) = 0;
 virtual void seek(int64 offset, int whence) = 0;
 virtual int64 tell(void) = 0;
 virtual int64 size(void) = 0;
 virtual void close(void) = 0;

 virtual Stream *steal(void);

 private:
 Stream *target_stream;
};
