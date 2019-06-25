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

#pragma GCC diagnostic ignored "-Wunused-variable"

#include <vector>

static const char *version = "0.1";

int opt_verbose = 0;
int opt_version = 0;
std::vector<std::string> opt_rootvec;

Uid uid_pool;

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

	return 0;
}

// End deduper.cpp
