//////////////////////////////////////////////////////////////////////
// config.hpp -- System configuration
// Date: Tue Jun 25 10:27:27 2019   (C) datablocks.net
///////////////////////////////////////////////////////////////////////

#ifndef CONFIG_HPP
#define CONFIG_HPP

#if defined(__apple__)
#define ST_MTIMESPEC	1
#else
#define ST_MTIMESPEC	0
#endif

#endif // CONFIG_HPP

// End config.hpp
