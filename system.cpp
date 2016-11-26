//////////////////////////////////////////////////////////////////////
// system.cpp -- System Objects and Types
// Date: Sat Nov 26 03:08:16 2016  (C) Warren W. Gay VE3WWG 
///////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include "system.hpp"

Fileno_t
Files::add(struct stat& sinfo) {

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
	fent.st_mtimespec = sinfo.st_mtimespec;
	return fileno;
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

#if UNIT_TEST

Uid uid_pool;
Files files;

int
main(int argc,char **argv) {
	struct stat sbuf1, sbuf2;
	Fileno_t fileno1, fileno2;
	int rc;

	rc = stat("system.cpp",&sbuf1);
	assert(!rc);
	fileno1 = files.add(sbuf1);
	printf("%ld: system.cpp\n",long(fileno1));

	rc = stat("system.hpp",&sbuf2);
	assert(!rc);
	fileno2 = files.add(sbuf2);
	printf("%ld: system.hpp\n",long(fileno2));

	{
		s_file_ent& fent = files.lookup(fileno1);
		assert(fent.fileno == fileno1);
		assert(fent.st_dev = sbuf1.st_dev);
		assert(fent.st_ino == sbuf1.st_ino);
		assert(fent.st_size == sbuf1.st_size);
		assert(fent.st_nlink == sbuf1.st_nlink);
	}
	{
		s_file_ent& fent = files.lookup(fileno2);
		assert(fent.fileno == fileno2);
		assert(fent.st_dev = sbuf2.st_dev);
		assert(fent.st_ino == sbuf2.st_ino);
		assert(fent.st_size == sbuf2.st_size);
		assert(fent.st_nlink == sbuf2.st_nlink);
	}

	return 0;
}

#endif

// End system.cpp
