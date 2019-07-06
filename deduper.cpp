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
#include <map>

static const char *version = "0.1";

int opt_verbose = 0;
std::vector<std::string> opt_rootvec;
int exit_code = 0;
static int opt_version = 0;
static int opt_help = 0;
static int opt_threads = 0;
static uint64_t opt_size = 0;

Uid<Fileno_t> uid_pool;
Names name_pool;
Uid<dup_t> dup_pool;

static GlobalFiles global_files(uid_pool);
static Queue<std::string> dir_queue;

std::vector<std::thread> thvec;
std::atomic<time_t> dive_depth(0);

static void
dive_dir(const std::string& directory) {
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
		rc = ::lstat(path.c_str(),&sbuf);
		if ( rc != 0 ) {
			if ( errno == ENOENT ) {
				::lstat(path.c_str(),&sbuf);
				if ( S_ISLNK(sbuf.st_mode) ) {
					tracef(2,"Ignoring symlink %s\n",path.c_str());
					continue;
				}
			}

			fprintf(stderr,"%s: stat(2) on '%s'\n",strerror(errno),path.c_str());
			continue;
		}

		if ( S_ISREG(sbuf.st_mode) ) {
			if ( opt_size == 0 || off_t(opt_size) <= sbuf.st_size ) {
				fileno = global_files.add(path.c_str());
				tracef(3,"%ld: file %s\n",long(fileno),path.c_str());
			}
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
dive() {
	std::string dir;

	for (;;) {
		if ( !dir_queue.pop(dir) ) {
			if ( !dive_depth.load() )
				break;
			usleep(1000);
			continue;
		}
		dive_dir(dir);
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
		"\t--threads n\tDefaults to 4\n"
		"\t-s, --size n\tSize >= n bytes\n",
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
		{"size",	required_argument,	nullptr,	4 },	// 4
		{0,         	0,             		nullptr,	0 },	// End
	};
	int option_index = 0;
	int ch;
	
	for (;;) {
		ch = getopt_long(argc,argv,"vVr:sh",long_options,&option_index);
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
		case 's':
		case 4:
			opt_size = strtoull(optarg,nullptr,10);
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
				file_set.insert(global_files.abspath(argv[optind++]));
		}

		if ( file_set.empty() )
			file_set.insert(global_files.abspath("."));

		for ( auto& file : file_set )
			opt_rootvec.push_back(file);
	}

	if ( opt_verbose > 0 ) {
		tracef(1,"Processing the following directories:\n");
		for ( auto& dir : opt_rootvec )
			tracef(1,"  %s\n",dir.c_str());
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
		thvec.emplace_back(std::thread(dive));
	}

	for ( auto& thread : thvec )
		thread.join();
	thvec.clear();

	tracef(2,"%ld files registered, + %ld name ids\n",
		long(global_files.size()),
		long(name_pool.size()));

	auto candidates = global_files.dup_candidates();
	std::map<size_t,std::unordered_map<crc32_t,std::set<Fileno_t>>> candidates2;

	{
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
			s_file_ent& fent = global_files.lookup(fileno);
			const std::string path(global_files.pathname(fileno));
			char buf[size];
			int fd = ::open(path.c_str(),O_RDONLY);
			int rc;

			if ( fd == -1 ) {
				fent.error = errno;
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
				fent.error = errno;
				::close(fd);
				return 0;
			}
			::close(fd);

			uint32_t crc = 0;

			global_files.crc32(crc,buf,sizeof buf);
			fent.crc32 = crc;
			fent.error = 0;
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

			for ( auto fileno: fileset ) {
				qent.fileno = fileno;
				inq.push(qent);
			}
		}		

		auto crc32_func = [&]() {
			s_size_qent qent;
			bool ok;

			while ( inq.pop(qent) ) {
				crc32(qent.fileno,qent.size>1024?1024:qent.size,ok);
			}
		};

		tracef(1,"Performing first 1k CRC32 calcs on %ld files..\n",long(inq.size()));

		std::vector<std::thread> tvec;
		for ( int thx=0; thx < opt_threads; ++thx )
			tvec.emplace_back(std::thread(crc32_func));

		for ( auto& thread : tvec )
			thread.join();
		tvec.clear();

		for ( auto& pair : candidates ) {
			const off_t size = pair.first;
			const auto& fileset = pair.second;
			crc32_t crc;
			bool ok;

			for ( auto fileno: fileset ) {
				s_file_ent& fent = global_files.lookup(fileno);

				if ( fent.error != 0 )
					continue;

				candidates2[size][fent.crc32].insert(fileno);
				std::string path(global_files.namestr_pathname(fent.path));
				tracef(2,"File crc32 %08X size %9ld file %ld %s\n",
					fent.crc32,long(size),size_t(fileno),path.c_str());
			}
		}
		candidates.clear();
	}

	unsigned cancount = 0;

	for ( auto& pair : candidates2 ) {
		const size_t size = pair.first;
		auto& crc32map = pair.second;

		for ( auto& pair2 : crc32map ) {
			const crc32_t crc = pair2.first;
			auto& fileset = pair2.second;
				
			if ( fileset.size() >= 2 )
				cancount += fileset.size();
		}
	}

	tracef(2,"CRC32 Dup Candidates: %u\n",cancount);

	std::map<size_t,std::unordered_map<crc32_t,std::set<Fileno_t>>> final_candidates;

	for ( auto& pair : candidates2 ) {
		const size_t size = pair.first;
		auto& crc32map = pair.second;

		for ( auto& pair2 : crc32map ) {
			const crc32_t crc32 = pair2.first;
			auto& fileset = pair2.second;
				
			if ( fileset.size() <= 1 )
				continue;

			for ( auto file : fileset ) {
				s_file_ent& fent = global_files.lookup(file);
				std::string path(global_files.namestr_pathname(fent.path));
					
				final_candidates[size][crc32].insert(file);
			}
		}
	}

	std::map<size_t,std::map<dup_t,std::set<Fileno_t>>> dups;

	tracef(2,"Final file comparisons:\n");

	for ( auto& pair : final_candidates ) {
		const size_t size = pair.first;
		auto& crc32map = pair.second;
		bool sizef = false;

		for ( auto& pair2 : crc32map ) {
			const crc32_t crc32 = pair2.first;
			auto& fileset = pair2.second;
			bool crcf = false;
				
			for ( auto file1 : fileset ) {
				s_file_ent& fent1 = global_files.lookup(file1);
				std::string path1(global_files.namestr_pathname(fent1.path));
				const char *match = "?";

				if ( !sizef ) {
					tracef(2,"SIZE: %ld bytes\n",long(size));
					sizef = true;
				}
				if ( !crcf ) {
					tracef(2,"  CRC32 %08X:\n",unsigned(crc32));
					crcf = true;
				}

				for ( auto file2 : fileset ) {
					if ( file2 == file1 )
						continue;

					s_file_ent& fent2 = global_files.lookup(file2);
					std::string path2(global_files.namestr_pathname(fent2.path));

					if ( fent1.duplicate != 0 && fent2.duplicate != 0 )
						continue;	// Already evaluated

					auto cmpf = global_files.compare_equal(file1,file2);

					switch ( cmpf ) {
					case Compare::Equal:
						match = "Equal";
						break;
					case Compare::NotEqual:
						match = "Not Equal";
						break;
					case Compare::Error:
						match = "ERROR";
						break;
					}

					if ( cmpf == Compare::Equal ) {
						if ( !fent1.duplicate && !fent2.duplicate ) {
							fent1.duplicate = fent2.duplicate = dup_pool.allocate();
							dups[size][fent1.duplicate].insert(file1);
							dups[size][fent2.duplicate].insert(file2);
						} else if ( !fent1.duplicate ) {
							fent1.duplicate = fent2.duplicate;
							dups[size][fent1.duplicate].insert(file1);
						} else	{
							// !fent2.duplicate
							fent2.duplicate = fent1.duplicate;
							dups[size][fent2.duplicate].insert(file2);
						}
					}

					tracef(2,"    %s (%ld) vs %s (%ld) : %s\n",
						path1.c_str(),long(fent1.fileno),
						path2.c_str(),long(fent2.fileno),
						match);
				}
			}
		}
	}

	printf("LIST OF DUPLICATE FILES:\n");

	for ( auto pair : dups ) {
		const size_t size = pair.first;
		auto& dupmap = pair.second;

		for ( auto pair2 : dupmap ) {
			const dup_t dup_id = pair2.first;
			const auto& fileset = pair2.second;

			printf("  Duplicate set %ld, %ld bytes:\n",long(dup_id),long(size));
			for ( auto fileno : fileset ) {
				auto& fent = global_files.lookup(fileno);
				const std::string path(global_files.namestr_pathname(fent.path));

				printf("    File %s\n",path.c_str());
				for ( auto& nstr : fent.links ) {
					const std::string lpath(global_files.namestr_pathname(nstr));

					printf("      ln %s\n",lpath.c_str());
				}
			}
		}
	}

	tracef(1,"Exit.\n");

	return exit_code;
}

// End deduper.cpp
