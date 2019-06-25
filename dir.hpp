//////////////////////////////////////////////////////////////////////
// dir.hpp -- Directory Operations
// Date: Wed Mar 29 20:48:36 2017   (C) Warren Gay VE3WWG
///////////////////////////////////////////////////////////////////////

#ifndef DIRENT_HPP
#define DIRENT_HPP

#include <sys/types.h>
#include <dirent.h>

#include <string>

class Dir {
public: enum FileType {
		Any = 300,              // Any file type
		Directory,              // Directory entries only
		File,                   // File entries only
		Symlink                 // Symlinks only
	};
protected:
	std::string     dirname;        // Opened directory
	DIR             *dir;           // Opened directory
	dirent          *result;        // Ref to last returned dir entry
	dirent          dentry;         // Current directory entry
	
	int read(std::string& name);

public: Dir();
	~Dir();
	
	int open(const char *pathname);
	void close();
	
	inline bool is_open() { return dir != 0; }
	
	int read(std::string& name,const char *wildpattern,FileType ftype);

	static std::string basename(const std::string& path);
	static std::string basename(const char *path);
};

#endif // DIRENT_HPP

// End dir.hpp
