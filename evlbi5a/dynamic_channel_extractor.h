// generate C code that will compile to a channel extractor
// Copyright (C) 2007-2008 Harro Verkouter
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
// 
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE.  See the GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
// 
// Author:  Harro Verkouter - verkouter@jive.nl
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#ifndef DYNAMIC_CHANNEL_EXTRACTOR_H
#define DYNAMIC_CHANNEL_EXTRACTOR_H

#include <string>
#include <vector>
#include <iostream>
#include <ezexcept.h>

// Exceptions of this type may be thrown.
// Derived from std::exception
// If your codebase does not have 'ezexcept.h'
// you can delete the #include<> and this line.
// Edit the top of dynamic_channel_extractor.cc
// to fit your project's needs
DECLARE_EZEXCEPT(dce_error)


// Generate code for a dynamic channel extractor, named <functionname>
// take a string of the format
//   int > [int,...][int,...] ...
//   <#ofbitsperinputword> '>' <list-of-bitlists>
//   <bitlist> = [int,int, ...] the bit numbers from the
//                              source word that make up
//                              that section
//

typedef std::vector<unsigned int>        channelbitlist_type;
typedef std::vector<channelbitlist_type> channellist_type;

struct extractorconfig_type {
    extractorconfig_type(unsigned int bitsperinput, const channellist_type& chlist);

    const unsigned int     bitsperinputword;
    const unsigned int     bitsperchannel;
    const channellist_type channels;
};

std::ostream& operator<<(std::ostream& os, const extractorconfig_type& config);

// Parse the config string (see above). On error it will throw
// a dce_error exception explaining what you dun wrong
extractorconfig_type parse_dynamic_channel_extractor( const std::string& configstr );

// The algorithm is based upon that used in SFXC although I modified it
// a bit, it is now slightly more generic.
// The name of the generated function will be <functionname> [should be 
// able to "dlsym()" that name after compile, link and load
std::string          generate_dynamic_channel_extractor(const extractorconfig_type& config,
                                                        const std::string& functionname);


#endif
