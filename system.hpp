//////////////////////////////////////////////////////////////////////
// system.hpp -- System Header File
// Date: Sat Nov 26 02:56:33 2016   (C) Warren Gay ve3wwg
///////////////////////////////////////////////////////////////////////

#ifndef SYSTEM_HPP
#define SYSTEM_HPP

#include "config.hpp"

#include <stdarg.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#include <unordered_map>

typedef uint64_t Fileno_t;

struct s_file_ent {
	Fileno_t	fileno;		// Internal file number
	dev_t		st_dev;		// Device number
	ino_t		st_ino;		// Inode number
	off_t		st_size;	// File size in bytes
	nlink_t		st_nlink;	// # of hard links
	timespec	st_mtimespec;	// Time of last modification
};

class Uid {
	uint64_t	next_uid;

public:	Uid() : next_uid(1) {};
	uint64_t allocate() {
		return next_uid++;
	}
};

extern Uid uid_pool;

class Files {
public:
	std::unordered_map<Fileno_t,s_file_ent> fmap;
	std::unordered_map<dev_t,std::unordered_map<ino_t,Fileno_t>> rmap;

	Fileno_t add(struct stat& sinfo);

public:	Files() {};
	Fileno_t add(const char *path);
	s_file_ent& lookup(dev_t dev,ino_t ino);
	s_file_ent& lookup(Fileno_t fileno);
};

void vtracef(int level,const char *format,va_list ap);
void tracef(int level,const char *format,...) __attribute__((format(printf,2,3)));

#endif // SYSTEM_HPP

// End system.hpp
