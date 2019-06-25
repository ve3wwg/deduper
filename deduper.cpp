//////////////////////////////////////////////////////////////////////
// deduper.cpp -- Main deduper program.
// Date: Tue Jun 25 10:33:23 2019   (C) datablocks.net
///////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>

#include "system.hpp"
#include "dir.hpp"

#pragma GCC diagnostic ignored "-Wunused-variable"

#include <thread>
#include <vector>

static const char *version = "0.1";

int opt_verbose = 0;
int opt_version = 0;
std::vector<std::string> opt_rootvec;
int exit_code = 0;

Uid uid_pool;

static Files *files = nullptr;
std::vector<Files*> filevec;
std::vector<std::thread> thvec;

static void
dive(const std::string directory,unsigned thno) {
	Files *files = filevec[thno];
	Dir dir;
	std::string path;
	struct stat sbuf;
	Fileno_t fileno;
	int rc;

	rc = dir.open(directory.c_str());
	if ( rc ) {
		fprintf(stderr,"%s: opening directory %s\n",
			strerror(rc),
			directory.c_str());
		exit_code |= 2;
		return;
	}
	tracef(2,"Examining dir %s\n",directory.c_str());

	while ( (rc = dir.read(path,"*",Dir::Any)) == 0 ) {
		rc = ::stat(path.c_str(),&sbuf);
		if ( rc != 0 ) {
			if ( errno == ENOENT ) {
				::lstat(path.c_str(),&sbuf);
				if ( S_ISLNK(sbuf.st_mode) ) {
					fprintf(stderr,"Ignoring symlink %s\n",path.c_str());
					continue;
				}
			}

			fprintf(stderr,"%s: stat(2) on '%s'\n",strerror(errno),path.c_str());
			continue;
		}

		if ( S_ISREG(sbuf.st_mode) ) {
			fileno = files->add(path.c_str());
			tracef(3,"%ld: file %s\n",long(fileno),path.c_str());
		} else if ( S_ISDIR(sbuf.st_mode) ) {
			dive(path,thno);
		} else	{
			tracef(2,"Ignoring %s\n",path.c_str());
		}
	}
	dir.close();

	if ( rc != ENOENT ) {
		fprintf(stderr,"%s: Reading directory %s\n",strerror(rc),path.c_str());
		exit_code |= 2;
	}
}

static void
thread_dive(const std::string directory,Files *files) {
	Dir dir;
	std::string path;
	struct stat sbuf;
	Fileno_t fileno;
	int rc;

	rc = dir.open(directory.c_str());
	if ( rc ) {
		fprintf(stderr,"%s: opening directory %s\n",
			strerror(rc),
			directory.c_str());
		exit_code |= 2;
		return;
	}
	tracef(1,"Examining dir %s\n",directory.c_str());

	while ( (rc = dir.read(path,"*",Dir::Any)) == 0 ) {
		rc = ::stat(path.c_str(),&sbuf);
		if ( rc != 0 ) {
			if ( errno == ENOENT ) {
				::lstat(path.c_str(),&sbuf);
				if ( S_ISLNK(sbuf.st_mode) ) {
					fprintf(stderr,"Ignoring symlink %s\n",path.c_str());
					continue;
				}
			}

			fprintf(stderr,"%s: stat(2) on '%s'\n",strerror(errno),path.c_str());
			continue;
		}

		if ( S_ISREG(sbuf.st_mode) ) {
			fileno = files->add(path.c_str());
			tracef(3,"%ld: file %s\n",long(fileno),path.c_str());
		} else if ( S_ISDIR(sbuf.st_mode) ) {
			if ( thvec.size() < 32 ) {
				tracef(1,"New thread %u for dir %s\n",unsigned(thvec.size()),path.c_str());
				filevec.push_back(new Files);
				thvec.emplace_back(std::thread(dive,path,filevec.size()-1));
			} else	{
				tracef(1,"Dive dir %s\n",path.c_str());
				dive(path,0);
			}
		} else	{
			tracef(2,"Ignoring %s\n",path.c_str());
		}
	}
	dir.close();

	if ( rc != ENOENT ) {
		fprintf(stderr,"%s: Reading directory %s\n",strerror(rc),path.c_str());
		exit_code |= 2;
	}
}

int
main(int argc,char **argv) {
	extern char *optarg;
	extern int optind, opterr, optopt;
	static struct option long_options[] = {
		{"verbose",  	no_argument,		nullptr,	'v' },	// 0
		{"version",	no_argument,		&opt_version,	1 },	// 1
		{0,         	0,             		nullptr,	0 },	// End
	};
	int option_index = 0;
	int ch;
	
	for (;;) {
		ch = getopt_long(argc,argv,"vVr:",long_options,&option_index);
		if ( ch == -1 )
			break;
	
		switch (ch) {
		case 0:
		case 'v':
			++opt_verbose;
			break;
		case 1:
		case 'V':
			break;		
		default:
			printf("Unknown option: -%c\n",ch);
			exit(1);
		}
	}

	if ( opt_version ) {
		printf("Version: %s\n",version);
		exit(0);
	}

	if ( optind < argc ) {
		while ( optind < argc )
			opt_rootvec.push_back(argv[optind++]);
	}

	if ( opt_rootvec.empty() )
		opt_rootvec.push_back(".");

	if ( opt_verbose > 0 ) {
		printf("Processing the following directories:\n");
		for ( auto& dir : opt_rootvec )
			printf("  %s\n",dir.c_str());
	}

	{
		bool fail = false;

		// Check that these are directories (or symlinks to one)
		for ( auto& dir : opt_rootvec ) {
			struct stat sbuf;

			if ( stat(dir.c_str(),&sbuf) == -1 ) {
				fail = true;
				fprintf(stderr,"%s: directory %s\n",strerror(errno),dir.c_str());
			} else if ( !S_ISDIR(sbuf.st_mode) ) {
				fprintf(stderr,"Not a directory: %s\n",dir.c_str());
				fail = true;
			}
		}
		if ( fail )
			exit(1);
	}

	if ( opt_rootvec.size() == 1 ) {
		files = new Files;
		std::thread thmain(thread_dive,opt_rootvec.front(),files);

		thmain.join();
	} else if ( opt_rootvec.size() > 1 ) {
		for ( auto& dir : opt_rootvec ) {
			tracef(1,"Thread %u for %s\n",unsigned(filevec.size()),dir.c_str());
			filevec.push_back(new Files);
			thvec.emplace_back(std::thread(dive,dir,filevec.size()-1));
		}
	}

	unsigned thno = 0, fx = 0;

	for ( auto& thentry : thvec ) {
		thentry.join();
		tracef(1,"Joined thread %u\n",thno++);
	}
	thvec.clear();

	if ( !files ) {
		files = filevec.front();	// Use first as map master
		fx = 1;				// Merge filesvec[1+]
	} else	fx = 0;				// Merge filesvec[0+] with files

	for ( ; fx < filevec.size(); ++fx ) {
		files->merge(*filevec[fx]);
		delete filevec[fx];
	}

	return exit_code;
}

// End deduper.cpp
