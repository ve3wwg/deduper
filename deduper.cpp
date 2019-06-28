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
#include <fcntl.h>
#include <getopt.h>

#include "system.hpp"
#include "dir.hpp"

#pragma GCC diagnostic ignored "-Wunused-variable"

#include <thread>
#include <vector>
#include <set>

static const char *version = "0.1";

int opt_verbose = 0;
std::vector<std::string> opt_rootvec;
int exit_code = 0;
static int opt_version = 0;
static int opt_help = 0;
static int opt_threads = 0;

Uid<Fileno_t> uid_pool;
Names name_pool;

static Files *files = nullptr;
static Queue<std::string> dir_queue;

std::vector<Files*> filevec;
std::vector<std::thread> thvec;
std::atomic<time_t> dive_depth(0);

static void
dive_dir(const std::string& directory,Files *files) {
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
		--dive_depth;
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
			++dive_depth;
			dir_queue.push(path);
		} else	{
			tracef(2,"Ignoring %s\n",path.c_str());
		}
	}
	dir.close();

	if ( rc != ENOENT ) {
		fprintf(stderr,"%s: Reading directory %s\n",strerror(rc),path.c_str());
		exit_code |= 2;
	}
	--dive_depth;
}

static void
dive(Files *files) {
	std::string dir;

	for (;;) {
		if ( !dir_queue.pop(dir) ) {
			if ( !dive_depth.load() )
				break;
			usleep(1000);
			continue;
		}
		dive_dir(dir,files);
	}
}

static void
usage(const char *argv0) {
	char cmd[strlen(argv0)+1];
	char *cp = strrchr(cmd,'/');

	if ( cp )
		argv0 = cp+1;
	printf("Usage: %s [options] directories..\n\n"
		"\t-h, --help\tHelp info.\n"
		"\t-v, --verbose\n"
		"\t--version\n"
		"\t--threads n\tDefaults to 4\n",
		argv0);
	exit(0);
}

int
main(int argc,char **argv) {
	extern char *optarg;
	extern int optind, opterr, optopt;
	static struct option long_options[] = {
		{"verbose",  	no_argument,		nullptr,	'v' },	// 0
		{"version",	no_argument,		&opt_version,	1 },	// 1
		{"threads",	required_argument,	nullptr,	2 },	// 2
		{"help",	no_argument,		&opt_help,	'h' },	// 3
		{0,         	0,             		nullptr,	0 },	// End
	};
	int option_index = 0;
	int ch;
	
	for (;;) {
		ch = getopt_long(argc,argv,"vVr:h",long_options,&option_index);
		if ( ch == -1 )
			break;
	
		switch (ch) {
		case 0:			// -v, --verbose
		case 'v':
			++opt_verbose;
			break;
		case 1:			// --version
		case 'V':
			break;		
		case 2:			// --threads
			opt_threads = atoi(optarg);
			break;
		case 'h':
			opt_help = 1;
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

	if ( opt_threads <= 0 )
		opt_threads = 4;

	if ( opt_help )
		usage(argv[0]);

	{
		std::set<std::string> file_set;

		if ( optind < argc ) {
			while ( optind < argc )
				file_set.insert(Files::abspath(argv[optind++]));
		}

		if ( file_set.empty() )
			file_set.insert(Files::abspath("."));

		for ( auto& file : file_set )
			opt_rootvec.push_back(file);
	}

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
			dir_queue.push(dir);
		}
		if ( fail )
			exit(1);
	}

	dive_depth.store(dir_queue.size());

	//////////////////////////////////////////////////////////////
	// Start worker threads
	//////////////////////////////////////////////////////////////

	for ( int thx=0; thx<opt_threads; ++thx ) {
		filevec.push_back(new Files);
		thvec.emplace_back(std::thread(dive,filevec.back()));
	}

	for ( auto& thread : thvec )
		thread.join();
	thvec.clear();

	//////////////////////////////////////////////////////////////
	// Merge file info
	//////////////////////////////////////////////////////////////

	tracef(1,"Merging %ld maps..\n",long(filevec.size()));

	files = filevec.front();

	for ( unsigned fx=1; fx < filevec.size(); ++fx ) {
		files->merge(*filevec[fx]);
		delete filevec[fx];
	}
	filevec.clear();

	tracef(1,"%ld files registered, + %ld name ids\n",
		long(files->size()),
		long(name_pool.size()));

	auto candidates = files->dup_candidates();

	tracef(1,"There are %ld duplicate candidates\n",long(candidates.size()));

	{
		typedef uint32_t crc32_t;
		std::unordered_map<crc32_t,std::unordered_set<Fileno_t>> candidates_crc32;
		struct s_size_qent {
			Fileno_t	fileno;
			size_t		size;
			
		};
		struct s_crc32 : public s_size_qent {
			crc32_t		crc32;
		};
		Queue<s_size_qent>	inq;

		auto crc32 = [](Fileno_t fileno,size_t size,bool& ok) -> crc32_t {
			s_file_ent& fent = files->lookup(fileno);
			const std::string path(files->pathname(fileno));
			char buf[size];
			int fd = ::open(path.c_str(),O_RDONLY);
			int rc;

			if ( fd == -1 ) {
				fprintf(stderr,"%s: opening %s for CRC32\n",strerror(errno),path.c_str());
				fent.crc32 = 0;
				ok = false;
				return 0;
			}
			do	{
				rc = ::read(fd,buf,sizeof buf);
			} while ( rc == -1 && errno == EINTR );

			if ( rc != int(sizeof buf) ) {
				ok = false;
				::close(fd);
				return 0;
			}
			::close(fd);

			uint32_t crc = 0;
			Files::crc32(crc,buf,sizeof buf);
			fent.crc32 = crc;
			ok = true;
			return crc;
		};

		// Queue up CRC32 work:
		for ( auto& pair : candidates ) {
			s_size_qent qent;
			qent.size = pair.first;
			const auto& fileset = pair.second;
			crc32_t crc;
			bool ok;

			printf("SIZE: %ld bytes\n",long(qent.size));

			for ( auto fileno: fileset ) {
				qent.fileno = fileno;
				inq.push(qent);
			}
		}		

		auto crc32_func = [&]() {
			s_size_qent qent;
			bool ok;

			while ( inq.pop(qent) ) {
				crc32(qent.fileno,qent.size,ok);
			}
		};

		thvec.clear();
		for ( int thx=0; thx < opt_threads; ++thx ) {
			thvec.emplace_back(std::thread(crc32_func));
		}

		for ( auto& thread : thvec )
			thread.join();

		puts("Done CRC32");
		fflush(stdout);

#if 0
		for ( auto& pair : candidates ) {
			const off_t size = pair.first;
			const auto& fileset = pair.second;
			crc32_t crc;
			bool ok;

			printf("SIZE: %ld bytes\n",long(size));

			for ( auto fileno: fileset ) {
				const std::string path(files->pathname(fileno));
				crc = crc32(path,size,ok);

				if ( ok )
					printf("  path %s CRC32 0x%08X\n",path.c_str(),unsigned(crc));
				else	printf("  path %s CRC32 error!\n",path.c_str());
			}
		}
#endif
	}

	return exit_code;
}

// End deduper.cpp
