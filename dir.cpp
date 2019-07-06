//////////////////////////////////////////////////////////////////////
// dir.cpp -- Directory Operations
// Date: Wed Mar 29 20:51:09 2017   (C) Warren Gay VE3WWG
///////////////////////////////////////////////////////////////////////

#include <string.h>
#include <assert.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "dir.hpp"

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#ifndef NAME_MAX
# ifdef _DARWIN_FEATURE_64_BIT_INODE
#  define NAME_MAX	1023
# else
#  define NAME_MAX	255
# endif
#endif

Dir::Dir() {
        dir = 0;
        result = 0;
}

Dir::~Dir() {
        if ( dir  )
                close();
}

int
Dir::open(const char *pathname) {
        dirname = pathname;
        close();
        dir = opendir(pathname);
        return dir ? 0 : errno;
}

void
Dir::close() {
        if ( dir ) {
                closedir(dir);
                dir = 0;
        }
        result = 0;
}

int
Dir::read(std::string& name) {
	int rc;
	
	name.clear();
	if ( !dir )
		return EBADF;          	// Not open
	
	rc = readdir_r(dir,&dentry,&result);
	
	if ( !rc && !result )
		rc = ENOENT;            // No more entries
	
	if ( !rc ) {
		char dname[NAME_MAX+1];
		size_t slen = strlen(dentry.d_name);

		if ( slen > NAME_MAX )
			slen = NAME_MAX;
		memcpy(dname,dentry.d_name,slen);
		dname[slen] = 0;
		name = dname;
	}
	
	return rc;
}

int
Dir::read(std::string& name,const char *wildpattern,FileType ftype) {
        std::string objname;
        int rc;

        name.clear();
        do      {
                rc = read(objname);
                if ( !rc ) {
                        if ( fnmatch(wildpattern,objname.c_str(),FNM_PERIOD) != 0 )
                                continue;

                        name = dirname;
                        name += "/";
                        name += objname;
                        if ( ftype == Any )
                                break;
                        else if ( ftype == Directory && dentry.d_type == DT_DIR ) {
                                if ( objname != "." && objname != ".." )
                                        break;
                        } else if ( ftype == File && dentry.d_type == DT_REG )
                                break;
                        else if ( ftype == Symlink && dentry.d_type == DT_LNK )
                                break;
                }
        } while ( !rc );

        return rc;
}

std::string
Dir::basename(const std::string& path) {
	return basename(path.c_str());
}

std::string
Dir::basename(const char *path) {
	const char *r = strrchr(path,'/');

	if ( !r )
		return path;
	if ( !r[1] )
		return ".";
	else	return r+1;
}

// End dir.cpp
