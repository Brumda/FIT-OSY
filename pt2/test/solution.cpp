#ifndef __PROGTEST__
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <stdexcept>
using namespace std;

constexpr int                          SECTOR_SIZE                             =             512;
constexpr int                          MAX_RAID_DEVICES                        =              16;
constexpr int                          MAX_DEVICE_SECTORS                      = 1024 * 1024 * 2;
constexpr int                          MIN_DEVICE_SECTORS                      =    1 * 1024 * 2;

constexpr int                          RAID_STOPPED                            = 0;
constexpr int                          RAID_OK                                 = 1;
constexpr int                          RAID_DEGRADED                           = 2;
constexpr int                          RAID_FAILED                             = 3;

struct TBlkDev
{
  int                                  m_Devices;
  int                                  m_Sectors;
  int                               (* m_Read )  ( int, int, void *, int );
  int                               (* m_Write ) ( int, int, const void *, int );
};
#endif /* __PROGTEST__ */

class CRaidVolume
{
  public:
    static bool                        create                                  ( const TBlkDev   & dev );
    int                                start                                   ( const TBlkDev   & dev );
    int                                stop                                    ();
    int                                resync                                  ();
    int                                status                                  () const;
    int                                size                                    () const;
    bool                               read                                    ( int               secNr,
                                                                                 void            * data,
                                                                                 int               secCnt );
    bool                               write                                   ( int               secNr,
                                                                                 const void      * data,
                                                                                 int               secCnt );
  protected:
    // todo
};

#ifndef __PROGTEST__
#include "tests.inc"
#endif /* __PROGTEST__ */
