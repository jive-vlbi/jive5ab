/* HV: change the includeguard. This will ensure that we get a complaining
 * compiler in case we happen to include both the official xlrtypes.h and
 * this one (which should, preferably, never happen)
 */
#ifndef JIVE5A_XLRDBCOMMON_H
#define JIVE5A_XLRDBCOMMON_H
/*******************************************************************
 *
 *    DESCRIPTION:  Common API constants that are unique to AMAZON 
 *    daughter boards.
 *                  
 *    IMPORTANT:  This file should contain ONLY constants.
 *       Please adhere to these ranges when selecting values:
 *           1 to 999:  Use for FPDP constants. 
 *       1000 to 1999:  Use for LVDS16 constants.
 *       2000 to 2999:  Use for SFPDP constants.
 *******************************************************************/

//--------------------------------------------------------------
// Generic daughter board temperature sensor information
#define SS_DBPARAM_TEMPLOCAL        0
#define SS_DBPARAM_TEMPREMOTE       1
#define SS_DBPARAM_TEMPSTATUS       2
#define SS_DBPARAM_TEMPCONFIG       3
#define SS_DBPARAM_TEMPUNITS        4
#define SS_DBPARAM_TEMPLOCALMAX     5
#define SS_DBPARAM_TEMPLOCALMIN     6
#define SS_DBPARAM_TEMPREMOTEMAX    7
#define SS_DBPARAM_TEMPSPARE        8
#define SS_DBPARAM_TEMPREMOTEMIN    9
#define SS_DBPARAM_TEMPDEVID        10
#define SS_DBPARAM_TEMPDIEREV       11

// FPDP settings for the XLRSetPortClock() command.
#define SS_PORTCLOCK_6MHZ           0
#define SS_PORTCLOCK_8MHZ           1
#define SS_PORTCLOCK_10MHZ          2
#define SS_PORTCLOCK_11MHZ          3  // 11.4 MHz
#define SS_PORTCLOCK_13MHZ          4  // 13.33 MHz
#define SS_PORTCLOCK_16MHZ          5
#define SS_PORTCLOCK_20MHZ          6
#define SS_PORTCLOCK_25MHZ          7  // 26.56 MHz
#define SS_PORTCLOCK_26MHZ          8  // 26.66 MHz
#define SS_PORTCLOCK_32MHZ          9
#define SS_PORTCLOCK_40MHZ          10
#define SS_PORTCLOCK_50MHZ          11
#define SS_PORTCLOCK_51MHZ          12 // 50.04 MHz
#ifdef TARGET
  // see xlrdiag.h for Hidden port clock settings 13-15
  #define SS_PORTCLOCK_55MHZ        13
  #define SS_PORTCLOCK_60MHZ        14
  #define SS_PORTCLOCK_65MHZ        15
  #define SS_PORTCLOCK_62_5MHZ      16  
  #define SS_PORTCLOCK_TOTAL        17
#endif //TARGET

// LVDS16 settings for XLRSetPortClock() command.
#define SS_LVDSCLOCK_20MHZ          100
#define SS_LVDSCLOCK_31_25MHZ       101
#define SS_LVDSCLOCK_62_5MHZ        102
#define SS_LVDSCLOCK_95MHZ          103
#define SS_LVDSCLOCK_100MHZ         104
#define SS_LVDSCLOCK_125MHZ         105
#define SS_LVDSCLOCK_150MHZ         106
#define SS_LVDSCLOCK_160MHZ         107
#define SS_LVDSCLOCK_190MHZ         108
#define SS_LVDSCLOCK_200MHZ         109
#define SS_LVDSCLOCK_TOTAL          10

//--------------------------------------------------------------
// FPDP operation modes
#define SS_FPDPMODE_DEFAULT         (20)
#define SS_FPDPMODE_FIRST           (SS_FPDPMODE_DEFAULT)       // Used to check for valid value
#define SS_FPDPMODE_RECVM           (21)   // Terminated
#define SS_FPDPMODE_RECV            (22)   // Non terminated
#define SS_FPDPMODE_XMIT            (23)   // Does not drive clock
#define SS_FPDPMODE_XMITM           (24)   // Normal xmit
#define SS_FPDPMODE_RECVM_CLOCKS    (25)   // RECVM but drives clocks.
#define SS_FPDPMODE_LAST            (SS_FPDPMODE_RECVM_CLOCKS)  // Used to check for valid value
 
//--------------------------------------------------------------
// FPDPII operation options
#define SS_DBOPT_FPDPSTROB          (0x100)// Enable data strobe clock 
                                           // (TTL strobe signals).  Default
                                           // is pstrobe clock (PECL strobe 
                                           // signals).
#define SS_DBOPT_FPDP2DISABLE       (0x200)// Disables FPDP2 mode
#define SS_DBOPT_FPDPNRASSERT       (0x400)// Rcvr asserts NRDY to hold bus
                                           // at startup.
#define SS_DBOPT_FPDPALL            (SS_DBOPT_FPDPSTROB|SS_DBOPT_FPDP2DISABLE| \
                                     SS_DBOPT_FPDPNRASSERT)

//--------------------------------------------------------------
// LVDS16 operation modes
#define SS_LVDS16MODE_DEFAULT       (1000)
#define SS_LVDS16MODE_FIRST         (SS_LVDS16MODE_DEFAULT)   // Used to check for valid value
#define SS_LVDS16MODE_RECV          (1001)  // Non terminated
#define SS_LVDS16MODE_XMIT          (1002)  // Does not drive clock
#define SS_LVDS16MODE_LAST          (SS_LVDS16MODE_XMIT)      // Used to check for valid value

//--------------------------------------------------------------
// LVDS16 operation options
#define B_OPT_LVDS_DATAVALID_GLOBAL    0
#define B_OPT_LVDS_DATAVALID_RS422     1
#define B_OPT_LVDS_FLOWCONTROL         31

#define SS_DBOPT_LVDS16_DATAVALID_GLOBAL  0x1
#define SS_DBOPT_LVDS16_DATAVALID_RS422   0x2
#define SS_DBOPT_LVDS16_FLOWCONTROL       0x80000000

#define SS_DBOPT_LVDS16ALL          (SS_DBOPT_LVDS16_DATAVALID_GLOBAL|SS_DBOPT_LVDS16_DATAVALID_RS422| \
                                     SS_DBOPT_LVDS16_FLOWCONTROL)

//--------------------------------------------------------------
// LVDS16 PLL values
#define SS_LVDS_RECORD_CLOCK_0      0
#define SS_LVDS_RECORD_CLOCK_1      1
#define SS_LVDS_RECORD_CLOCK_2      2
#define SS_LVDS_RECORD_CLOCK_3      3

//--------------------------------------------------------------
// Serial FPDP (SFSDP) operation modes
#define SS_SFPDPMODE_DEFAULT        (2000)
#define SS_SFPDPMODE_FIRST          (SS_SFPDPMODE_DEFAULT) // Used to check for valid value
#define SS_SFPDPMODE_NORMAL         (2001)   // Only valid mode for Serial FPDP from API
#define SS_SFPDPMODE_LAST           (SS_SFPDPMODE_NORMAL)   // Used to check for valid value
//--------------------------------------------------------------
// Serial FPDP (SFPDP) operation options - these should be defined with unique bits
#define SS_DBOPT_SFPDPNRASSERT      (0x1) // Rcvr asserts NRDY to hold bus at startup.
#define SS_DBOPT_SFPDP_CRC_ENABLE   (0x2) // CRC enable for all SFPDP channels
#define SS_DBOPT_SFPDP_CRC_DISABLE  (0x4) // CRC disable for all SFPDP channels
#define SS_DBOPT_SFPDP_FLOWCTL_ENABLE  (0x8) // Flow Control enable for all SFPDP channels -default
#define SS_DBOPT_SFPDP_FLOWCTL_DISABLE (0x10) // Flow Control disable for all SFPDP channels- Could be dangerous to disable flow control 

#define SS_DBOPT_SFPDPALL           (SS_DBOPT_SFPDPNRASSERT|SS_DBOPT_SFPDP_CRC_ENABLE| \
                                     SS_DBOPT_SFPDP_CRC_DISABLE|SS_DBOPT_SFPDP_FLOWCTL_ENABLE| \
                                     SS_DBOPT_SFPDP_FLOWCTL_DISABLE)   
//--------------------------------------------------------------
// Serial FPDP (SFSDP) operation modes
#define SS_CAMLINKMODE_DEFAULT        (2000)
#define SS_CAMLINKMODE_FIRST          (SS_CAMLINKMODE_DEFAULT) // Used to check for valid value
#define SS_CAMLINKMODE_NORMAL         (2001)   // Only valid mode for Serial FPDP from API
#define SS_CAMLINKMODE_LAST           (SS_CAMLINKMODE_NORMAL)   // Used to check for valid value
//--------------------------------------------------------------
// Serial FPDP (CAMLINK) operation options - these should be defined with unique bits
#define SS_DBOPT_CAMLINKNRASSERT      (0x1) // Rcvr asserts NRDY to hold bus at startup.
#define SS_DBOPT_CAMLINK_CRC_ENABLE   (0x2) // CRC enable for all CAMLINK channels
#define SS_DBOPT_CAMLINK_CRC_DISABLE  (0x4) // CRC disable for all CAMLINK channels
#define SS_DBOPT_CAMLINK_FLOWCTL_ENABLE  (0x8) // Flow Control enable for all CAMLINK channels -default
#define SS_DBOPT_CAMLINK_FLOWCTL_DISABLE (0x10) // Flow Control disable for all CAMLINK channels- Could be dangerous to disable flow control 

#define SS_DBOPT_CAMLINKALL           (SS_DBOPT_CAMLINKNRASSERT|SS_DBOPT_CAMLINK_CRC_ENABLE| \
                                     SS_DBOPT_CAMLINK_CRC_DISABLE|SS_DBOPT_CAMLINK_FLOWCTL_ENABLE| \
                                     SS_DBOPT_CAMLINK_FLOWCTL_DISABLE)   

#define SS_DBOPT_ALL                (SS_DBOPT_FPDPALL|SS_DBOPT_LVDS16ALL|SS_DBOPT_SFPDPALL|SS_DBOPT_CAMLINKALL)

//-------------------------------------------------------------
// Camera Link Daughter Board Register Indexes
//   -- Use with XLRWriteDBReg32 and XLRReadDBReg32
//
#define  SSREG_DB_CAMLINK_CTL                0
#define  SSREG_DB_CAMLINK_RCRDSTATUS         1
#define  SSREG_DB_CAMLINK_WRAPREG            2
#define  SSREG_DB_CAMLINK_SDRAMWRAPREG       3
#define  SSREG_DB_CAMLINK_UART_CNTL_A        4
#define  SSREG_DB_CAMLINK_UART_STATUS_DATA_A 5
#define  SSREG_DB_CAMLINK_UART_CNTL_B        6
#define  SSREG_DB_CAMLINK_UART_STATUS_DATA_B 7
#define  SSREG_DB_CAMLINK_X_OFFSET           8
#define  SSREG_DB_CAMLINK_X_ACTIVE           9
#define  SSREG_DB_CAMLINK_Y_OFFSET           10
#define  SSREG_DB_CAMLINK_Y_ACTIVE           11
#define  SSREG_DB_CAMLINK_ULMARKER           12
#define  SSREG_DB_CAMLINK_ULSIZE             13
#define  SSREG_DB_CAMLINK_YEARMONTH_RD       14
#define  SSREG_DB_CAMLINK_DAYHOUR_RD         15
#define  SSREG_DB_CAMLINK_MINSECD_RD         16
#define  SSREG_DB_CAMLINK_MILLISECD_RD       17
#define  SSREG_DB_CAMLINK_YEARMONTH_WRT      18
#define  SSREG_DB_CAMLINK_DAYHOUR_WRT        19
#define  SSREG_DB_CAMLINK_MINSECD_WRT        20
#define  SSREG_DB_CAMLINK_MILLMICROSECD_WRT  21
#define  SSREG_DB_CAMLINK_MAX                22 // For bounds checking

//-------------------------------------------------------------
// SFPDP Daughter Board Register Indexes
//   -- Use with XLRWriteDBReg32 and XLRReadDBReg32
//
#define SSREG_DB_SFPDP_START_ON_SYNC  13
#define SS_SYNC_CLEAR      0  
#define SS_SYNCSET_PORT_1  1
#define SS_SYNCSET_PORT_2  2 
#define SS_SYNCSET_PORT_3  4 
#define SS_SYNCSET_PORT_4  8


#endif //XLRDBCOMMON_H
