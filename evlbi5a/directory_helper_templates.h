// function templates for mapping or filtering directory entries
//
// Note: all templated functions throw an int (errno) on error
// in a system call
//
//
//  ===========================================================
//  direntries_type dir_filter(DIR/string, <predicate>)
//  ===========================================================
//
//  use the dir_filter function template to filter directory
//  entries satisfying <predicate> (the template parameter).
//  An example predicate is found below.
//
//  <predicate> is a unary boolean function:
//          bool <predicate>(std::string const&)
//
//  Returns a std::set<std::string> [direntries_type] with matching
//  directory entries.
//
//  Usage:
//  struct my_predicate {
//      bool operator()(std::string const& nm) {
//          return nm[0]!=".";
//  };
//
//  ...
//  direntries_type  f = dir_filter("/path/to/dir", my_predicate())
//
//
//  ===========================================================
//  struct dir_mapper<fun> { ... };
//  ===========================================================
//
//  Use the templated struct dir_mapper<> to apply a function/functor
//  to each entry in the directory. The results are returned as
//
//      map [STRING] => <operator>::value_type
//
//  where the keys are the directory entry names and the mapped
//  values are the result of calling the <fun> with that 
//  directory entry
//
//  The struct overloads the function-call operator twice:
//      operator()( DIR* );
//      operator()( string const& dirname );
//
//  Example:
//      // Simple fileSize functor - just to test dirMapper construction
//      struct fileSize {
//          // the result of calling this functor will be an 'off_t'
//          typedef off_t   value_type;
//
//          value_type operator()(std::string const& fnm) const {
//              int    fd;
//              off_t  fs; 
//
//              if( (fd=::open(fnm.c_str(), O_RDONLY))<0 )
//                  return (off_t)-1;
//              fs = ::lseek(fd, 0, SEEK_END);
//              ::close(fd);
//              return fs; 
//          }
//      };
//
//      ...
//
//      typedef dir_mapper<fileSize>    filesizer_type;
//
//      filesizer_type::value_type  filesizes = filesizer_type()("/path/to/directory")
//
//      It is possible to pass an instance of the functor, in case you want
//      the functor to use some dynamic value:
//
//      struct newerThan {
//          // the result of calling this functor will be a boolean - wether
//          // or not the entry is newer than some time
//          typedef bool   value_type;
//
//          newerThan(time_t ts):
//              __m_timestamp(ts)
//          {}
//
//          value_type operator()(std::string const& fnm) const {
//              struct stat status;
//
//              ::lstat(....);
//              return status.st_mtime>__m_timestamp;
//          }
//
//          time_t  __m_timestamp;
//      };
//
//      typedef dir_mapper<newerThan>   changedsince_type;
//
//      // newer than one minute ago:
//      newerThan                      oneMinuteAgo( time_t(0) - 60 );
//      changedsince_type::value_type  entries = changedsince_type(oneMinuteAgo)( "/path/to/directory" )
//
//      // this sucks about c++ ... [c++11 is better, with the 'auto' keyword]
//      for(changedsince_type::value_type::const_iterator ptr=entries.begin(); ptr!=entries.end(); ptr++)
//          // newerThan returned true for the entry if it was newer ...
//          if( ptr->second )
//              cout << ptr->first << " is younger than one minute" << endl;
//
#ifndef EVLBI5A_DIRECTORY_HELPER_TEMPLATES_H
#define EVLBI5A_DIRECTORY_HELPER_TEMPLATES_H

#include <auto_array.h>
#include <threadutil.h>

#include <set>
#include <map>
#include <string>
#include <cstddef>
#include <cerrno>
#include <cstring>

#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>


///////////////////////////////////////////////
// VBS_FS/libvbs are going to have to quite often
// grovel over directories, sometimes filtering
// entries and sometimes not.
// This set of function templates allow for flexible
// filtering (or not) of directory entries.
//
// Works on already opened DIR* or std::string (==path)
// The template parameter is the predicate.
// IF the predicate(d_entry) is true, then d_entry
// will be added to the set of entries.
//
///////////////////////////////////////////////
typedef std::set<std::string> direntries_type;

// This version will call the predicate with "entry->d_name"
// (i.e. just the name of the entry in the directory) as
// parameter because we do not know the path leading to DIR*
template <typename Predicate>
direntries_type dir_filter(DIR* dirp, Predicate const& pred) {
    // Memory for the "dirent" struct may or may not include
    // space for the "d_name[]" field. POSIX sais that it's
    // almost impossible to pre-allocate the correct amount of memory
    // but this is currently one of the better approximations
    int                       eno;
    const size_t              entryLen = offsetof(struct dirent, d_name)+::pathconf("/", _PC_NAME_MAX) + 1;
    struct dirent*            entryPtr;
    direntries_type           rv;
    auto_array<unsigned char> dirEntry( new unsigned char[entryLen] );

    // Make sure it's rewound
    ::rewinddir( dirp );

    // Check each entry
    //  ::readdir_r(3) returns 0 on success, entryPtr==NULL if end-of-directory reached
    while( (eno=::readdir_r(dirp, (struct dirent*)&dirEntry[0], &entryPtr))==0 ) {
        if( entryPtr==0 )
            break;
        // If predicate returns true, add current entry to result
        if( pred(std::string(entryPtr->d_name)) )
            if( rv.insert(entryPtr->d_name).second==false )
                std::cerr << "dir_filter[DIR*]/duplicate insert - " << entryPtr->d_name << std::endl;
    }
    if( eno!=0 ) {
        std::cerr << "*** dir_filter: " << evlbi5a::strerror(eno) << std::endl;
        throw eno;
    }
    return rv;
}

// This variation, which works on a given path will call the predicate with
// the FULL path to the entry, not just the name from the directory listing!
// So it is "pred( dir + "/" + entry->d_name )"
template <typename Predicate>
direntries_type dir_filter(std::string const& dir, Predicate const& pred) {
    // Memory for the "dirent" struct may or may not include
    // space for the "d_name[]" field. POSIX sais that it's
    // almost impossible to pre-allocate the correct amount of memory
    // but this is currently one of the better approximations
    int                       eno;
    DIR*                      dirp;
    const size_t              entryLen = offsetof(struct dirent, d_name)+::pathconf("/", _PC_NAME_MAX) + 1;
    struct dirent*            entryPtr;
    direntries_type           rv;
    auto_array<unsigned char> dirEntry( new unsigned char[entryLen] );

    // This is a systemcall so can use ASSERT*() which will 
    // capture the error message from errno 
    if( (dirp=::opendir(dir.c_str()))==0 ) {
        std::cerr << "*** dir_filter/failed to open " << dir << " - " << evlbi5a::strerror(errno) << std::endl;
        throw errno;
    }

    // Check each entry
    //  ::readdir_r(3) returns 0 on success, entryPtr==NULL if end-of-directory reached
    while( (eno=::readdir_r(dirp, (struct dirent*)&dirEntry[0], &entryPtr))==0 ) {
        if( entryPtr==0 )
            break;
        // If predicate returns true, add current entry to result
        if( pred(dir+"/"+entryPtr->d_name) )
            if( rv.insert(dir+"/"+entryPtr->d_name).second==false )
                std::cerr << "dir_filter[" << dir << "]/duplicate insert - " << entryPtr->d_name << std::endl;
    }
    // Force succesfull loop ending
    int oeno = eno;

    ::closedir(dirp);

    if( oeno!=0 ) {
        std::cerr << "*** dir_filter[" << dir << "] - " << evlbi5a::strerror(oeno) << std::endl;
        throw oeno;
    }
    return rv;
}

#if 0
// A no-filter predicate - gets all entries in a directory
// this isn't a template and therefore it's left in comment
struct NoFilter {
    bool operator()(std::string const&) const {
        return true;
    }
};

direntries_type vbs_readdir(DIR* dirp) {
    return vbs_readdir(dirp, NoFilter());
}

direntries_type vbs_readdir(std::string const& dir) {
    return vbs_readdir(dir, NoFilter());
}
#endif

///////////////////////////////////////////////
//
//          The dir_mapper
//
///////////////////////////////////////////////

template <typename Callback>
struct dir_mapper {
    typedef std::map<std::string, typename Callback::value_type> value_type;

    dir_mapper():
        __m_cb( Callback() )
    {}
    dir_mapper( Callback const& cb ):
        __m_cb( cb )
    {}

    value_type operator()(std::string const& dir) const {
        int                       eno;
        DIR*                      dirp;
        value_type                rv;
        const size_t              entryLen = offsetof(struct dirent, d_name)+::pathconf("/", _PC_NAME_MAX) + 1;
        struct dirent*            entryPtr;
        auto_array<unsigned char> dirEntry( new unsigned char[entryLen] );

        // This is a systemcall so can use ASSERT*() which will 
        // capture the error message from errno 
        if( (dirp=::opendir(dir.c_str()))==0 ) {
            std::cerr << "*** dir_mapper[" << dir << "] failed to opendir - " << evlbi5a::strerror(errno) << std::endl;
            throw errno;
        }

        // Check each entry
        //  ::readdir_r(3) returns 0 on success, entryPtr==NULL if end-of-directory reached
        while( (eno=::readdir_r(dirp, (struct dirent*)&dirEntry[0], &entryPtr))==0 ) {
            if( entryPtr==0 )
                break;
            // If predicate returns true, add current entry to result
            const std::string fnm = dir + "/" + entryPtr->d_name;
            if( rv.insert ( make_pair(fnm, __m_cb(fnm))).second==false )
                std::cerr << "dir_mapper[" << dir << "]/duplicate insert - " << fnm << std::endl;
        }
        // Force succesfull loop ending
        int oeno = eno;

        ::closedir(dirp);

        if( oeno!=0 ) {
            std::cerr << "*** dir_mapper[" << dir << "] " << evlbi5a::strerror(errno) << std::endl;
            throw oeno;
        }
        return rv;
    }
    value_type operator()(DIR* dirp) const {
        int                       eno;
        value_type                rv;
        const size_t              entryLen = offsetof(struct dirent, d_name)+::pathconf("/", _PC_NAME_MAX) + 1;
        struct dirent*            entryPtr;
        auto_array<unsigned char> dirEntry( new unsigned char[entryLen] );

        ::rewinddir(dirp);
        // Check each entry
        //  ::readdir_r(3) returns 0 on success, entryPtr==NULL if end-of-directory reached
        while( (eno=::readdir_r(dirp, (struct dirent*)&dirEntry[0], &entryPtr))==0 ) {
            if( entryPtr==0 )
                break;
            // If predicate returns true, add current entry to result
            const std::string fnm = entryPtr->d_name;
            if( rv.insert ( make_pair(fnm, __m_cb(fnm))).second==false )
                std::cerr << "dir_mapper[DIR*]/duplicate insert - " << fnm << std::endl;
        }
        if( eno!=0 ) {
            std::cerr << "*** dir_mapper[DIR*] " << evlbi5a::strerror(errno) << std::endl;
            throw eno;
        }
        return rv;
    }

    Callback  __m_cb;
};

#endif // DIRECTORY_HELPER_TEMPLATES_H
