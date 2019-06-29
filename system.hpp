//////////////////////////////////////////////////////////////////////
// system.hpp -- System Header File
// Date: Sat Nov 26 02:56:33 2016   (C) Warren Gay ve3wwg
///////////////////////////////////////////////////////////////////////

#ifndef SYSTEM_HPP
#define SYSTEM_HPP

#include "config.hpp"

#include <stdarg.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <string>
#include <atomic>
#include <queue>

typedef uint32_t crc32_t;
typedef uint64_t Fileno_t;
typedef uint64_t Name_t;
typedef std::basic_string<Name_t> NameStr_t;
typedef uint32_t dup_t;

struct s_file_ent {
	Fileno_t	fileno;		// Internal file number
	dev_t		st_dev;		// Device number
	ino_t		st_ino;		// Inode number
	off_t		st_size;	// File size in bytes
	nlink_t		st_nlink;	// # of hard links
	timespec	st_mtimespec;	// Time of last modification
	NameStr_t	path;		// Path to the file
	uint32_t	crc32=0;	// CTC32 of first 1-k
	int		error=0;	// Non-zero if open fails
	dup_t		duplicate=0;	// Non-zero when duplicate ID
};

template<typename T>
class Uid {
	std::atomic<T>	next_uid;

public:	Uid() : next_uid(1) { }
	uint64_t allocate() {
		return ++next_uid;
	}
};

class Names : Uid<Name_t> {
	std::mutex				mutex;
	std::unordered_map<std::string,Name_t>	names;
	std::unordered_map<Name_t,const char *>	rev_names;

public:	Names() {}
	Name_t name_register(const char *name);
	std::string lookup(Name_t name_id);
	size_t size() { return names.size(); }
};

template<typename T>
class Queue : public std::queue<T> {
	std::mutex	mutex;

public:	Queue() {}
	void push(const T& s) {
		std::lock_guard<std::mutex> lock(mutex);
		std::queue<T>::push(s);
	}
	bool pop(T& s) {
		std::lock_guard<std::mutex> lock(mutex);

		if ( this->empty() )
			return false;
		s = std::queue<T>::front();
		std::queue<T>::pop();
		return true;
	}
};

extern Uid<Fileno_t>	uid_pool;
extern Names		name_pool;

class File_Guard {
public:
	int		fd = -1;
	int		error = 0;

	File_Guard(const char *path=nullptr) {
		if ( path )
			open(path);
	};
	~File_Guard() {
		if ( fd >= 0 ) {
			::close(fd);
			fd = -1;
		}
	}
	int open(const char *pathname) {
		fd = ::open(pathname,O_RDONLY);
		if ( fd == -1 )
			error = errno;
		else	error = 0;
		return fd;
	}
	int read(void *buf,size_t bytes,off_t offset) {
		int rc;
		do	{
			rc = pread(fd,buf,bytes,offset);
		} while ( rc == -1 && errno == EINTR );
		if ( rc == -1 )
			error = errno;
		return rc;
	}
	void close() {
		if ( fd >= 0 ) {
			::close(fd);
			fd = -1;
		}
	}
};

enum class Compare {
	Equal,
	NotEqual,
	Error
};

class Files {
	std::unordered_map<Fileno_t,s_file_ent> 	fmap;
	std::unordered_map<dev_t,std::unordered_map<ino_t,Fileno_t>> rmap;
	std::unordered_map<off_t,std::unordered_set<Fileno_t>> by_size;

	NameStr_t add_names(std::list<std::string>& list_path);

public:	Files() {};
	Fileno_t add(const char *path);
	void merge(const Files& other);
	s_file_ent& lookup(dev_t dev,ino_t ino);
	s_file_ent& lookup(Fileno_t fileno);
	std::unordered_map<off_t,std::unordered_set<Fileno_t>> dup_candidates();

	size_t size() { return fmap.size(); }
	std::string namestr_pathname(const NameStr_t& path);
	std::string pathname(Fileno_t file);
	Compare compare_equal(Fileno_t f1,Fileno_t f2);

	static std::string abspath(const char *filename);
	static std::list<std::string> pathparse(const char *pathname);
	static void crc32(uint32_t& crc32,const void *buf,size_t buflen);
};

void vtracef(int level,const char *format,va_list ap);
void tracef(int level,const char *format,...) __attribute__((format(printf,2,3)));

#endif // SYSTEM_HPP

// End system.hpp
