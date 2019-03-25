#include <boost/mpl/insert_range.hpp>
#include <boost/mpl/begin_end.hpp>
#include <boost/mpl/push_back.hpp>
#include <boost/mpl/fold.hpp>
#include <boost/mpl/vector.hpp>
#include <boost/mpl/pair.hpp>
#include <boost/mpl/at.hpp>
#include <boost/mpl/insert.hpp>
#include <boost/mpl/map.hpp>
#include <boost/mpl/string.hpp>
#include <boost/mpl/int.hpp>
#include <boost/mpl/bool.hpp>

namespace jive5ab_mpl_stay_away_here_be_dragons {

// first define helper functions,
// the actual definitions are at the end of the file

template <typename StringA, typename StringB> struct concat {
    typedef typename boost::mpl::insert_range<
        typename StringA::type, 
        typename boost::mpl::end<typename StringA::type>::type,
        typename StringB::type>::type type;
};

// the Mark5A/Mark5B layout has 4 "options", which define the layout
// (1) 1024/65536 scans (for 5A/5B respectively)
// (2) 8/16 Disks (used for serial number caching)
// (3) SDK8/SDK9 the struct defining the disk changed
// (4) BankB yes or no, at some point Haystack started storing the companion disk vsn

// the idea is to make a vector of vectors of pairs,
// where the first element of such a pair is a string,
// a short name for the second element
// the second element defines a choice of one of the options described above

// the inner vector will be of size 4, one choice for each "option level"
// the outer vector will be of size 16, for each 2^4 possible option choices

template<typename To_Push> struct Pusher {
    template<typename State, typename Element> struct apply {
        typedef typename boost::mpl::push_back<
            State,
            typename boost::mpl::push_back<Element, To_Push>::type
            >::type type;
    };
};

template<typename Selection_Vectors, typename New_Selection> struct Insert_To_All {
    typedef typename boost::mpl::fold<
        Selection_Vectors,
        boost::mpl::vector<>,
        Pusher<New_Selection>
        >::type type;
};

struct Add_Option_Level_Helper {
    template <typename OldNewState, typename ToAdd> struct apply {
        // return the adapted state, which is again a pair of old and new state
        typedef boost::mpl::pair<
            typename OldNewState::first, // don't change the old state
            typename boost::mpl::insert_range<
                typename OldNewState::second,
                typename boost::mpl::end<typename OldNewState::second>::type,
                typename Insert_To_All<typename OldNewState::first, ToAdd>::type
                >::type // insert to add to all vectors in old state
            > type;
    };
};

typedef boost::mpl::vector< > Empty_Options;

template<typename State, typename Level_Options> struct Add_Option_Level {
    typedef typename boost::mpl::fold< 
        Level_Options,
        boost::mpl::pair<State, Empty_Options>,
        Add_Option_Level_Helper
        >::type::second::type type;
    
};

// the above functions help make a vector of vectors of pairs,
// now we want to turn this into a mapping where the inner vectors are combined,
// to a key, value pair of string to Mark5ABLayout and these pairs are
// stored in a map

template<typename V> struct Make_Layout_Pair {
    typedef boost::mpl::pair<
        typename concat<
            typename boost::mpl::at_c<V, 0>::type::first, 
            concat<typename boost::mpl::at_c<V, 1>::type::first, 
                   concat<typename boost::mpl::at_c<V, 2>::type::first, 
                          typename boost::mpl::at_c<V, 3>::type::first>
                   >
            >::type,
        Mark5ABLayout<
            boost::mpl::at_c<V, 0>::type::second::type::value,
            boost::mpl::at_c<V, 1>::type::second::type::value,
            typename boost::mpl::at_c<V, 2>::type::second,
            boost::mpl::at_c<V, 3>::type::second::type::value
            >
        > type;
};

struct Add_To_Map {
    template<typename Map, typename Option_Vector> struct apply {
        typedef typename boost::mpl::insert<
            Map, 
            typename Make_Layout_Pair<Option_Vector>::type
            >::type type;
    };
};

template<typename Option_Vectors> struct Make_Map {
    typedef typename boost::mpl::fold<
        Option_Vectors,
        boost::mpl::map<>,
        Add_To_Map
        >::type type;
};

/////////////////////////////////////////////
// here, the actual type definitions start //
/////////////////////////////////////////////

// the words that will be combined to make the strings mapping to the actual layout
typedef boost::mpl::string<'M','a','r','k','5','A'>         S_Mark5A;
typedef boost::mpl::string<'M','a','r','k','5','B'>         S_Mark5B;
typedef boost::mpl::string<'8','D','i','s','k','s'>         S_8Disks;
typedef boost::mpl::string<'1','6','D','i','s','k','s'>     S_16Disks;
typedef boost::mpl::string<'S','D','K','8'>                 S_SDK8;
typedef boost::mpl::string<'S','D','K','9'>                 S_SDK9;
typedef boost::mpl::string<'S','D','K','9','x'>             S_SDK9X;
typedef boost::mpl::string<>                                S_NoBankB;
typedef boost::mpl::string<'B','a','n','k','B'>             S_BankB;

typedef boost::mpl::string<'O','r','i','g','i','n','a','l'> S_Original;
typedef boost::mpl::string<'L','a','y','o','u','t'>         S_Layout;
typedef boost::mpl::string<'M','a','r','k','5','C'>         S_Mark5C;

// the Mark5ABLayout has 4 "levels" of options
typedef boost::mpl::pair< S_Mark5A, boost::mpl::int_<1024> > L1_A;
typedef boost::mpl::pair< S_Mark5B, boost::mpl::int_<65536> > L1_B;
typedef boost::mpl::vector< L1_A, L1_B > L1_Options;

typedef boost::mpl::pair< S_8Disks, boost::mpl::int_<8> > L2_8;
typedef boost::mpl::pair< S_16Disks, boost::mpl::int_<16> > L2_16;
typedef boost::mpl::vector< L2_8, L2_16 > L2_Options;

typedef boost::mpl::pair< S_SDK8, SDK8_DRIVEINFO > L3_8;
typedef boost::mpl::pair< S_SDK9, SDK9_DRIVEINFO > L3_9;
typedef boost::mpl::pair< S_SDK9X, SDK9_DRIVEINFO_wrong > L3_10;
typedef boost::mpl::vector< L3_8, L3_9, L3_10 > L3_Options;

typedef boost::mpl::pair< S_NoBankB, boost::mpl::bool_<false> > L4_false;
typedef boost::mpl::pair< S_BankB, boost::mpl::bool_<true> > L4_true;
typedef boost::mpl::vector< L4_false, L4_true > L4_Options;

// combine these options to make a map of 16 (2^4) layouts
typedef boost::mpl::vector< boost::mpl::vector<> > Initial_Options;
typedef Add_Option_Level< Initial_Options, L1_Options >::type L1;
typedef Add_Option_Level< L1, L2_Options >::type L2;
typedef Add_Option_Level< L2, L3_Options >::type L3;
typedef Add_Option_Level< L3, L4_Options >::type L4;

typedef Make_Map<L4>::type Mark5ABLayout_Map;

// add the Original and Mark5C layout manually
typedef boost::mpl::insert< 
    Mark5ABLayout_Map,
    boost::mpl::pair<
        concat<S_Original, S_Layout>::type,
        OriginalLayout
        >
    >::type Map_Original_Added;
                                
typedef boost::mpl::insert< 
    Map_Original_Added,
    boost::mpl::pair<
        concat<S_Mark5C, S_Layout>::type,
        EnhancedLayout
        >
    >::type Map_Mark5C_Added;

typedef Map_Mark5C_Added Layout_Map;
}

typedef jive5ab_mpl_stay_away_here_be_dragons::Layout_Map Layout_Map;
