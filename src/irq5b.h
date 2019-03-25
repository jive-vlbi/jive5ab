/* irq5b.h 
 * HV: Copied from JAB 's Mark5A/dimino source code 'irq.h'
 *     and deleted some includes
 * Mostly copied from KAD's irq.c, the interrupt test program 
 * Revised:  2005 December 31, JAB */ 
#include <sys/types.h>
#include <time.h> /* For struct timeval */
/* start of stuff for driver module */
/* Need for _IOR macro */
/* Note: on Solaris, these definitions do not live in sys/ioctl.h ... */
#if defined(__sun__)
#  include <inttypes.h>
#  include <sys/ioccom.h>
#else
#  include <sys/ioctl.h> 
#endif


struct mk5b_intr_info {
   short soi;           /* status of interrupt (value in mk5b reg 0x13) */
   struct timeval toi;  /* time of interrupt occurence                  */
   size_t coi;          /* count of interrupts (since module load)      */

   // Make sure everything is zeroed initially
   mk5b_intr_info() :
       soi( 0 ), coi( 0 )
    { toi.tv_sec = 0; toi.tv_usec = 0; }

  };
#define MK5B_IOC_MAGIC     '5'

/* Below is the ioctl cmd argument for blocking until next interrupt */
#define MK5B_IOCWAITONIRQ  _IOR(MK5B_IOC_MAGIC, 0, struct mk5b_intr_info)

/* end of stuff for driver module */

