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

extern Names names_pool;

Name_t
Names::name_register(const char *name) {
	const std::string find_name(name);
	std::lock_guard<std::mutex> lock(mutex);

	auto it = names.find(find_name);
	if ( it == names.end() ) {
		Name_t id = allocate();
		names.insert(std::pair<std::string,Name_t>(find_name,id));
		auto i2 = names.find(find_name);
		rev_names.insert(std::pair<Name_t,const char *>(id,i2->first.c_str()));
		return id;
	} else	{
		return it->second;
	}
}

std::string
Names::lookup(Name_t name_id) {
	std::lock_guard<std::mutex> lock(mutex);

	auto it = rev_names.find(name_id);
	if ( it != rev_names.end() )
		return std::string(it->second);
	return "";
}

std::string
GlobalFiles::abspath(const char *filename) {
	char buf[PATH_MAX+1];

	realpath(filename,buf);
	return buf;
}

std::list<std::string>
GlobalFiles::pathparse(const char *pathname) {
	std::list<std::string> list;
	char buf[strlen(pathname)+1];
	char *ep;

	strncpy(buf,pathname,sizeof buf);
	buf[sizeof buf-1] = 0;

	for ( char *cp = buf; cp && *cp; cp = ep + 1 ) {
		if ( (ep = strchr(cp,'/')) != nullptr )
			*ep = 0;
		if ( !*cp )
			continue;
		list.push_back(cp);		
		if ( !ep )
			return list;
	}
	return list;
}

NameStr_t
Names::add_names(std::list<std::string>& list_path) {
	NameStr_t names_path;
	Name_t id;

	for ( auto& component : list_path ) {
		id = name_pool.name_register(component.c_str());
		names_path += id;
	}
	return names_path;
}

Fileno_t
GlobalFiles::add(const char *path) {
	std::list<std::string> list = pathparse(abspath(path).c_str());
	std::lock_guard<std::recursive_mutex> lock(mutex);
	struct stat sinfo;
	int rc;
	extern Names name_pool;

	rc = stat(path,&sinfo);
	assert(!rc);

	auto& devmap = rmap[sinfo.st_dev];
	auto it = devmap.find(sinfo.st_ino);
	if ( it == devmap.end() ) {
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

		fent.path = name_pool.add_names(list);

		// Track files by size
		by_size[fent.st_size].insert(fileno);
		return fent.fileno;
	}
	
	return it->second;
}

#if 0
void
Files::merge(const Files& other) {
	
	for ( const auto& pair : other.fmap ) {
		const Fileno_t fileno = pair.first;
		const s_file_ent& fent = pair.second;

		fmap[fileno] = fent;
		rmap[fent.st_dev][fent.st_ino] = fileno;
		by_size[fent.st_size].insert(fileno);
	}
}
#endif

s_file_ent&
GlobalFiles::lookup(dev_t dev,ino_t ino) {
	std::lock_guard<std::recursive_mutex> lock(mutex);

	auto it = rmap.find(dev);
	assert(it != rmap.end());
	auto r2map = it->second;
	auto i2 = r2map.find(ino);
	assert(i2 != r2map.end());
	return fmap.at(i2->second);
}

s_file_ent&
GlobalFiles::lookup(Fileno_t fileno) {
	std::lock_guard<std::recursive_mutex> lock(mutex);

	auto it = fmap.find(fileno);
	assert(it != fmap.end());
	return it->second;
}

std::unordered_map<off_t,std::unordered_set<Fileno_t>>
GlobalFiles::dup_candidates() {
	std::unordered_map<off_t,std::unordered_set<Fileno_t>> candidates;

	for ( auto& pair : by_size ) {
		auto& uset = pair.second;

		if ( uset.size() > 1 )
			candidates.insert(pair);
	}
	return candidates;
}

std::string
GlobalFiles::namestr_pathname(const NameStr_t& path) {
	std::stringstream ss;

	for ( auto name_id : path ) {
		const std::string& sname = name_pool.lookup(name_id);
		assert(!sname.empty());
		ss << '/' << sname;
	}
	std::string r(ss.str());
	return r;
}

std::string
GlobalFiles::pathname(Fileno_t file) {

	auto it = fmap.find(file);
	if ( it == fmap.end() )
		return "";
	const auto& fent = it->second;

	return namestr_pathname(fent.path);
}

Compare
GlobalFiles::compare_equal(Fileno_t f1,Fileno_t f2) {
	std::string path1 = pathname(f1);
	std::string path2 = pathname(f2);

	if ( path1.empty() || path2.empty() )
		return Compare::Error;

	File_Guard fg1(path1.c_str());
	File_Guard fg2(path2.c_str());
	off_t offset = 0;
	char buf1[4096], buf2[sizeof buf1];
	int rc1, rc2;

	if ( fg1.fd < 0 || fg2.fd < 0 )
		return Compare::Error;

	for ( offset=0; ; offset += sizeof buf1 ) {
		rc1 = fg1.read(buf1,sizeof buf1,offset);
		rc2 = fg2.read(buf2,sizeof buf2,offset);
		if ( rc1 == -1 || rc2 == -1 )
			return Compare::Error;
		else if ( rc1 != rc2 )
			return Compare::NotEqual;
		if ( memcmp(buf1,buf2,rc1) != 0 )
			return Compare::NotEqual;
		if ( rc1 == 0 )
			break;
	}
	return Compare::Equal;	
}

void
vtracef(int level,const char *format,va_list ap) {
	extern int opt_verbose;

	if ( opt_verbose < level )
		return;

	vprintf(format,ap);
	fflush(stdout);
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
	fflush(stdout);
}

// End system.cpp
