//////////////////////////////////////////////////////////////////////
// system.cpp -- System Objects and Types
// Date: Sat Nov 26 03:08:16 2016  (C) Warren W. Gay VE3WWG 
///////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <sstream>

#include "system.hpp"
#include "dir.hpp"

std::string
Files::abspath(const char *filename) {
	char buf[PATH_MAX+1];

	realpath(filename,buf);
	return buf;
}

std::list<std::string>
Files::pathparse(const char *pathname) {
	std::list<std::string> list;
	char buf[strlen(pathname)+1];
	char *ep;

	strncpy(buf,pathname,sizeof buf);
	buf[sizeof buf-1] = 0;

	for ( char *cp = buf; cp && *cp; cp = ep + 1 ) {
		if ( (ep = strchr(cp,'/')) != nullptr )
			*ep = 0;
		list.push_back(cp);		
		if ( !ep )
			return list;
	}
	return list;
}

NameStr_t
Files::add_names(std::list<std::string>& list_path) {
	NameStr_t names_path;
	Name_t id;

	for ( auto& component : list_path ) {
		auto it = names.find(component);

		if ( it == names.end() ) {
			id = name_pool.allocate();
			names.insert(std::pair<std::string,Name_t>(component,id));
			rev_names.insert(std::pair<Name_t,std::string>(id,component));
		} else	id = it->second;
		names_path += id;
	}
	return names_path;
}

Fileno_t
Files::add(const char *path) {
	struct stat sinfo;
	std::list<std::string> list = Files::pathparse(Files::abspath(path).c_str());
	int rc;

	rc = stat(path,&sinfo);
	assert(!rc);

	// Make sure there is no duplicate
	auto it = rmap.find(sinfo.st_dev);
	if ( it != rmap.end() ) {
		auto& r2map = it->second;
		auto i2 = r2map.find(sinfo.st_ino);
		assert(i2 == r2map.end());
	}

	Fileno_t fileno = uid_pool.allocate();
	s_file_ent& fent = fmap[fileno];
	fent.fileno = fileno;
	fent.st_dev = sinfo.st_dev;
	fent.st_ino = sinfo.st_ino;
	fent.st_size = sinfo.st_size;
	fent.st_nlink = sinfo.st_nlink;
#if ST_MTIMESPEC
	fent.st_mtimespec = sinfo.st_mtimespec;
#else
	fent.st_mtimespec.tv_sec = sinfo.st_mtime;
	fent.st_mtimespec.tv_nsec = 0;
#endif
	fent.path = Files::add_names(list);

	// Track files by size
	by_size[fent.st_size].insert(fileno);
	return fileno;
}

void
Files::merge(const Files& other) {
	
	for ( auto& pair : other.names ) {
		auto it = names.find(pair.first);
		if ( it != names.end() ) {
			names.insert(pair);
			rev_names.insert(std::pair<Name_t,std::string>(pair.second,pair.first));
		}
	}
	for ( const auto& pair : other.fmap ) {
		const Fileno_t fileno = pair.first;
		const s_file_ent& fent = pair.second;

		fmap[fileno] = fent;
		rmap[fent.st_dev][fent.st_ino] = fileno;
		by_size[fent.st_size].insert(fileno);
	}
}

s_file_ent&
Files::lookup(dev_t dev,ino_t ino) {
	auto it = rmap.find(dev);
	assert(it != rmap.end());
	auto r2map = it->second;
	auto i2 = r2map.find(ino);
	assert(i2 != r2map.end());
	return fmap.at(i2->second);
}

s_file_ent&
Files::lookup(Fileno_t fileno) {
	auto it = fmap.find(fileno);
	assert(it != fmap.end());
	return it->second;
}

void
vtracef(int level,const char *format,va_list ap) {
	extern int opt_verbose;

	if ( opt_verbose < level )
		return;

	vprintf(format,ap);
}

void
tracef(int level,const char *format,...) {
	extern int opt_verbose;

	if ( opt_verbose < level )
		return;

	va_list ap;
	va_start(ap,format);
	vprintf(format,ap);
	va_end(ap);
}

#if UNIT_TEST

Uid uid_pool;
Files files;

static void
dive(const char *dirpath) {
	Dir dir;
	std::string path;
	struct stat sbuf;
	Fileno_t fileno;
	int rc;

	rc = dir.open(dirpath);
	if ( rc ) {
		fprintf(stderr,"%s: opening directory %s",
			strerror(rc),
			dirpath);
		return;
	}

	while ( (rc = dir.read(path,"*",Dir::Any)) == 0 ) {
		std::string full_path = Files::abspath(path);

		rc = ::stat(full_path.c_str(),&sbuf);
		if ( rc != 0 ) {
			if ( errno == ENOENT ) {
				::lstat(full_path.c_str(),&sbuf);
				if ( S_ISLNK(sbuf.st_mode) ) {
					fprintf(stderr,"Ignoring symlink %s\n",full_path.c_str());
					continue;
				}
			}

			fprintf(stderr,"%s: stat(2) on '%s'\n",strerror(errno),full_path.c_str());
			continue;
		}

		if ( S_ISREG(sbuf.st_mode) ) {
			fileno = files.add(full_path.c_str());
			printf("%ld: file %s\n",long(fileno),full_path.c_str());
		} else if ( S_ISDIR(sbuf.st_mode) ) {
			dive(full_path.c_str());
		} else	{
			fprintf(stderr,"Ignoring %s\n",full_path.c_str());
		}
	}
	dir.close();

	if ( rc != ENOENT )
		fprintf(stderr,"%s: Reading directory %s\n",strerror(rc),full_path.c_str());
}

int
main(int argc,char **argv) {

	if ( !argv[1] ) {
		dive(".");
	} else	{
		for ( int x=1; x<argc; ++x )
			dive(argv[x]);
	}

#if 0
	for ( auto& pair0 : files.rmap ) {
		const dev_t device = pair0.first;
		const std::unordered_map<ino_t,Fileno_t>& inomap = pair0.second;

		for ( auto& pair1 : inomap ) {
			const ino_t inode = pair1.first;
			const Fileno_t fileno = pair1.second;

			

		}
	}
#endif

	return 0;
}

#endif

// End system.cpp
